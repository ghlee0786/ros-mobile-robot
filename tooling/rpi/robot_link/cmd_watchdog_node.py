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
Going quiet while idle keeps it from fighting other /cmd_vel sources such as straight_drive.
Parameters can be changed while running (ros2 param set /cmd_watchdog ramp_enabled false, or the
dashboard's 가감속 switch through /cmd_watchdog/set_parameters).
"""
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from std_msgs.msg import Empty

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
        self.declare_parameter('lin_acc', 0.30)    # m/s^2   (0 -> 0.25 m/s in 0.83 s)
        self.declare_parameter('lin_dec', 0.50)    # m/s^2   (0.25 -> 0 in 0.5 s)
        self.declare_parameter('ang_acc', 1.00)    # rad/s^2 (0 -> 0.6 rad/s in 0.6 s)
        self.declare_parameter('ang_dec', 1.50)    # rad/s^2
        self.declare_parameter('estop_hold', 1.0)  # s
        self.declare_parameter('min_turn_w', 0.20)  # rad/s, smallest in-place turn the wheels follow (0 = off)
        self.pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.create_subscription(Twist, '/teleop/cmd_vel', self.on_cmd, 10)
        self.create_subscription(Empty, '/teleop/estop', self.on_estop, 10)
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
