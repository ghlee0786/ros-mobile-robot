#!/usr/bin/env python3
"""Safety relay for remote driving: /teleop/cmd_vel -> /cmd_vel with a dead-man timeout and an
acceleration profile.

control_node and the Pico firmware keep the last velocity forever, so a dropped Wi-Fi link or a
crashed PC teleop would leave the robot driving. Remote teleop therefore publishes to
/teleop/cmd_vel continuously (10 Hz, including zeros) and this node, running on the Pi, relays it:
  * fresh command               -> the output moves toward it at most by the acceleration /
                                   deceleration limits per step (ramp), so a key press does not jolt
                                   or tip the robot; with ramp_enabled=false it is relayed as is
  * command becomes zero        -> the output decelerates to zero, a few zeros, then the node goes quiet
  * no message for `timeout` s  -> treated as zero (link lost): decelerate and stop
  * slow turn in place           -> |angular| raised to min_turn_w when |linear| < 0.02 m/s: below about
                                   0.12 rad/s a wheel stalls (Pico P-only PID + static friction), so Nav2's
                                   final heading alignment (it asks ~0.10 rad/s) never turned the robot
  * /teleop/estop (any message) -> output set to zero at once, ignoring the ramp; commands are
                                   ignored for `estop_hold` s
  * obstacle in the way (/scan)  -> linear speed limited to what can still stop `stop_dist` m from the
                                   nearest return in the travel direction (front or rear sector), so
                                   keyboard, MCP, straight_drive and Nav2 alike cannot drive into a wall;
                                   turning in place is not limited. Scan older than `scan_timeout` s ->
                                   at most `blind_speed` (obstacle_guard.py)
Going quiet while idle keeps it from fighting other /cmd_vel sources such as straight_drive.
Parameters can be changed while running (ros2 param set /cmd_watchdog ramp_enabled false, or the
dashboard's 가감속 switch through /cmd_watchdog/set_parameters).
"""
import math
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan
from std_msgs.msg import Empty

from obstacle_guard import guard

STEP = 0.05   # s, output period while moving


def approach(cur, target, acc, dec, dt):
    """Move cur toward target: speeding up (same sign, larger magnitude) is limited by acc, slowing
    down or reversing by dec. A limit <= 0 means no limit."""
    if cur * target >= 0 and abs(target) > abs(cur):
        lim = acc
    else:
        lim = dec
    if lim <= 0:
        return target
    d = target - cur
    step = lim * dt
    return target if abs(d) <= step else cur + (step if d > 0 else -step)


class CmdWatchdog(Node):
    def __init__(self):
        super().__init__('cmd_watchdog')
        self.timeout = self.declare_parameter('timeout', 0.5).value
        self.declare_parameter('ramp_enabled', True)
        self.declare_parameter('lin_acc', 0.35)    # m/s^2   (0 -> 0.35 m/s in 1 s)
        self.declare_parameter('lin_dec', 0.60)    # m/s^2   (0.35 -> 0 in 0.58 s)
        self.declare_parameter('ang_acc', 1.00)    # rad/s^2 (0 -> 0.6 rad/s in 0.6 s)
        self.declare_parameter('ang_dec', 1.50)    # rad/s^2
        self.declare_parameter('estop_hold', 1.0)  # s
        self.declare_parameter('min_turn_w', 0.20)  # rad/s, smallest in-place turn the wheels follow (0 = off)
        self.declare_parameter('obstacle_guard', True)
        self.declare_parameter('stop_dist', 0.35)        # m from the LiDAR (base_link) to stop at, ~0.1 m past the bumper
        self.declare_parameter('guard_latency', 0.2)     # s, scan age + loop + Pico reaction
        self.declare_parameter('guard_half_width_deg', 35.0)
        self.declare_parameter('scan_timeout', 0.5)      # s
        self.declare_parameter('blind_speed', 0.10)      # m/s cap while the scan is missing
        self.pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.create_subscription(Twist, '/teleop/cmd_vel', self.on_cmd, 10)
        self.create_subscription(Empty, '/teleop/estop', self.on_estop, 10)
        self.create_subscription(LaserScan, '/scan', self.on_scan, qos_profile_sensor_data)
        self.scan, self.scan_t, self.guard_warn_t = None, 0.0, 0.0
        self.create_timer(STEP, self.step)
        self.target = Twist()
        self.last_t = 0.0
        self.out_v = 0.0
        self.out_w = 0.0
        self.estop_until = 0.0
        self.zeros_left = 0          # zero messages still to send after a stop / timeout
        self.moving = False

    def on_cmd(self, msg):
        if time.monotonic() < self.estop_until:
            return
        self.target, self.last_t = msg, time.monotonic()

    def on_scan(self, msg):
        self.scan, self.scan_t = (msg.ranges, msg.angle_min, msg.angle_increment), time.monotonic()

    def limit_for_obstacles(self, v):
        p = self.get_parameter
        if not p('obstacle_guard').value or abs(v) < 1e-6:
            return v
        if self.scan is None or time.monotonic() - self.scan_t > p('scan_timeout').value:
            lim = p('blind_speed').value
            return max(-lim, min(lim, v))
        out, near = guard(v, self.scan, p('stop_dist').value, max(p('lin_dec').value, 0.1),
                          p('guard_latency').value, math.radians(p('guard_half_width_deg').value))
        if abs(out) < abs(v) - 1e-3 and time.monotonic() - self.guard_warn_t > 2.0:
            self.guard_warn_t = time.monotonic()
            self.get_logger().warn(f'obstacle {near:.2f} m {"ahead" if v > 0 else "behind"}: '
                                   f'linear {v:.2f} -> {out:.2f} m/s')
        return out

    def on_estop(self, _msg):
        self.estop_until = time.monotonic() + self.get_parameter('estop_hold').value
        self.target, self.out_v, self.out_w = Twist(), 0.0, 0.0
        self.pub.publish(Twist())
        if self.moving:
            self.get_logger().warn('estop: robot stopped at once')
        self.moving, self.zeros_left = False, 5

    def step(self):
        fresh = time.monotonic() - self.last_t < self.timeout
        tv, tw = (self.target.linear.x, self.target.angular.z) if fresh else (0.0, 0.0)
        mw = self.get_parameter('min_turn_w').value
        if mw > 0 and abs(tv) < 0.02 and 0.01 <= abs(tw) < mw:
            tw = mw if tw > 0 else -mw
        if self.moving and not fresh and (self.out_v or self.out_w):
            if not getattr(self, '_warned', False):
                self.get_logger().warn(f'/teleop/cmd_vel silent for {self.timeout}s: stopping robot')
                self._warned = True
        if fresh:
            self._warned = False
        p = self.get_parameter
        if p('ramp_enabled').value:
            self.out_v = approach(self.out_v, tv, p('lin_acc').value, p('lin_dec').value, STEP)
            self.out_w = approach(self.out_w, tw, p('ang_acc').value, p('ang_dec').value, STEP)
        else:
            self.out_v, self.out_w = tv, tw
        self.out_v = self.limit_for_obstacles(self.out_v)
        if abs(self.out_v) > 1e-6 or abs(self.out_w) > 1e-6:
            t = Twist()
            t.linear.x, t.angular.z = self.out_v, self.out_w
            self.pub.publish(t)
            self.moving, self.zeros_left = True, 5
            return
        self.out_v = self.out_w = 0.0
        self.moving = False
        if self.zeros_left > 0:
            self.pub.publish(Twist())
            self.zeros_left -= 1


def main():
    rclpy.init()
    node = CmdWatchdog()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.pub.publish(Twist())
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
