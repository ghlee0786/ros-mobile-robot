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
  * speed limits                 -> 명령을 먼저 max_forward · max_reverse · max_angular 로 자른 뒤 가감속 · 장애물 정지를
                                   거친다(speed_limits.py). 범위(0 ~ 펌웨어 한계) 밖 set_parameters 는 거부하고, 바뀐 값은
                                   ~/robot_link/limits.yaml 에 써서 재시작 · 재부팅 뒤에도 남긴다(대시보드 '속도 한계').
Going quiet while idle keeps it from fighting other /cmd_vel sources such as straight_drive.
Parameters can be changed while running (ros2 param set /cmd_watchdog ramp_enabled false, or the
dashboard's 가감속 switch through /cmd_watchdog/set_parameters).
"""
import math
import time

import rclpy
from geometry_msgs.msg import Twist
from rcl_interfaces.msg import SetParametersResult
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan
from std_msgs.msg import Empty

import speed_limits
from obstacle_guard import allowed_speed, nearest_in_sector

STEP = 0.02   # s, output period while moving (50 Hz: the Pico reads at 33 Hz, so a stop reaches it within one cycle)
ZEROS = 12    # zero messages after a stop (~0.25 s)


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
        self.declare_parameter('guard_latency', 0.25)    # s, measured: scan age up to 0.15 + loop 0.05 + Pico 0.03
        self.declare_parameter('guard_half_width_deg', 35.0)
        self.declare_parameter('scan_timeout', 0.5)      # s
        self.declare_parameter('blind_speed', 0.10)      # m/s cap while the scan is missing
        self.limits_path = self.declare_parameter('limits_file', speed_limits.DEFAULT_PATH).value
        self.limits = speed_limits.load(self.limits_path)
        for name, val in self.limits.items():
            self.declare_parameter(name, val)
        self.add_on_set_parameters_callback(self.on_set_params)
        self.limit_warn_t = 0.0
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

    def on_set_params(self, params):
        changed = {}
        for prm in params:
            if prm.name in speed_limits.FIRMWARE:
                why = speed_limits.check(prm.name, prm.value)
                if why:
                    return SetParametersResult(successful=False, reason=why)
                changed[prm.name] = float(prm.value)
        if changed:
            self.limits.update(changed)
            try:
                speed_limits.save(self.limits, self.limits_path)
            except OSError as e:   # 저장에 실패해도 지금 실행에는 적용한다(재시작하면 앞 값으로 돌아간다)
                self.get_logger().error(f'cannot save {self.limits_path}: {e}')
            self.get_logger().info('speed limits ' + ', '.join(f'{k}={v:.2f}' for k, v in self.limits.items()))
        return SetParametersResult(successful=True)

    def limit_speed(self, v, w):
        lv, lw = speed_limits.clamp(v, w, self.limits)
        if (lv, lw) != (v, w) and time.monotonic() - self.limit_warn_t > 2.0:
            self.limit_warn_t = time.monotonic()
            self.get_logger().info(f'speed limit: linear {v:.2f} -> {lv:.2f} m/s, angular {w:.2f} -> {lw:.2f} rad/s')
        return lv, lw

    def on_scan(self, msg):
        # nearest returns ahead / behind once per scan (12 Hz), not on every 50 Hz step
        hw = math.radians(self.get_parameter('guard_half_width_deg').value)
        self.scan = tuple(nearest_in_sector(msg.ranges, msg.angle_min, msg.angle_increment, c, hw) for c in (0.0, math.pi))
        self.scan_t = time.monotonic()

    def limit_for_obstacles(self, v):
        p = self.get_parameter
        if not p('obstacle_guard').value or abs(v) < 1e-6:
            return v
        if self.scan is None or time.monotonic() - self.scan_t > p('scan_timeout').value:
            lim = p('blind_speed').value
            return max(-lim, min(lim, v))
        near = self.scan[0 if v > 0 else 1]
        vmax = allowed_speed(near, p('stop_dist').value, max(p('lin_dec').value, 0.1), p('guard_latency').value)
        out = math.copysign(min(abs(v), vmax), v)
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
        self.moving, self.zeros_left = False, ZEROS

    def step(self):
        fresh = time.monotonic() - self.last_t < self.timeout
        tv, tw = (self.target.linear.x, self.target.angular.z) if fresh else (0.0, 0.0)
        mw = self.get_parameter('min_turn_w').value
        if mw > 0 and abs(tv) < 0.02 and 0.01 <= abs(tw) < mw:
            tw = mw if tw > 0 else -mw
        tv, tw = self.limit_speed(tv, tw)   # min_turn_w 뒤에: 회전 한계가 그보다 작아도 한계를 넘지 않게
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
            self.moving, self.zeros_left = True, ZEROS
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
