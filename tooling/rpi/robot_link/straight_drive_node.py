#!/usr/bin/env python3
"""Straight-line driving controller (heading hold + cross-track correction) for the diff-drive robot.

Idle until a goal arrives; only then it publishes /cmd_vel, so manual teleop is not overridden.

  goal   : /straight_drive/goal   geometry_msgs/Vector3  x = distance [m] (negative = reverse),
                                                         y = speed [m/s] (0 -> default_speed)
  cancel : /straight_drive/cancel std_msgs/Empty
  status : /straight_drive/status std_msgs/String (JSON, 10 Hz while active, once on finish)

Heading  = alpha * (IMU yaw change) + (1 - alpha) * (encoder odom theta change), both relative
           to the goal start, so the IMU's absolute yaw offset/drift does not matter.
Steering = w = k_heading * heading_error - k_cross * cross_track_error * sign(v), clamped.
Inputs   : /odom (Pico encoder odometry via control_node), /imu/data_raw (orientation).
Each run is logged to ~/robot_link/logs/straight_<time>.csv for tuning.
"""
import csv
import json
import math
import os
import time

import rclpy
from geometry_msgs.msg import Twist, Vector3
from nav_msgs.msg import Odometry
from rclpy.node import Node
from sensor_msgs.msg import Imu
from std_msgs.msg import Empty, String


def yaw_of(q):
    return math.atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z))


def wrap(a):
    return math.atan2(math.sin(a), math.cos(a))


class StraightDrive(Node):
    def __init__(self):
        super().__init__('straight_drive')
        p = self.declare_parameter
        self.rate_hz = p('rate_hz', 20.0).value
        self.default_speed = p('default_speed', 0.08).value
        self.max_speed = p('max_speed', 0.1).value          # Pico firmware clamps |v| <= 0.1 m/s
        self.max_angular = p('max_angular', 0.25).value     # Pico firmware clamps |w| <= 0.25 rad/s
        self.accel = p('accel', 0.1).value                  # m/s^2 ramp up/down
        self.alpha = p('imu_weight', 0.7).value             # heading fusion weight of the IMU
        self.k_heading = p('k_heading', 1.5).value          # rad/s per rad
        self.k_cross = p('k_cross', 2.0).value              # rad/s per m
        self.stop_tolerance = p('stop_tolerance', 0.01).value
        # control_node can block up to 1 s on a serial timeout, so short /odom gaps happen:
        # pause (v = w = 0) after sensor_pause, abort only after sensor_timeout.
        self.sensor_pause = p('sensor_pause', 0.3).value
        self.sensor_timeout = p('sensor_timeout', 1.5).value
        self.use_imu = p('use_imu', True).value
        self.log_dir = os.path.expanduser(p('log_dir', '~/robot_link/logs').value)

        self.cmd_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.status_pub = self.create_publisher(String, '/straight_drive/status', 10)
        self.create_subscription(Odometry, '/odom', self.on_odom, 10)
        # /imu/data_raw (100 Hz) is only subscribed while a goal runs: idle, it cost ~35% of a Pi core.
        self.imu_sub = None
        self.create_subscription(Vector3, '/straight_drive/goal', self.on_goal, 10)
        self.create_subscription(Empty, '/straight_drive/cancel', lambda _: self.finish('canceled'), 10)
        self.create_timer(1.0 / self.rate_hz, self.step)

        self.odom = None
        self.odom_t = 0.0
        self.imu_yaw = None
        self.imu_t = 0.0
        self.goal = None
        self.log = None
        self.log_path = None
        self.steps = 0
        self.get_logger().info('ready: publish geometry_msgs/Vector3 {x: distance, y: speed} to /straight_drive/goal')

    # ---- inputs -------------------------------------------------------------------------
    def on_odom(self, msg):
        p = msg.pose.pose
        self.odom = (p.position.x, p.position.y, yaw_of(p.orientation))
        self.odom_t = time.monotonic()

    def on_imu(self, msg):
        self.imu_yaw = yaw_of(msg.orientation)
        self.imu_t = time.monotonic()

    def on_goal(self, msg):
        if self.odom is None or time.monotonic() - self.odom_t > self.sensor_timeout:
            self.report({'state': 'rejected', 'reason': 'no /odom'})
            return
        speed = min(abs(msg.y) or self.default_speed, self.max_speed)
        x0, y0, th0 = self.odom
        if self.use_imu and self.imu_sub is None:
            self.imu_yaw = None
            self.imu_sub = self.create_subscription(Imu, '/imu/data_raw', self.on_imu, 50)
        self.goal = {
            'distance': abs(msg.x), 'dir': 1.0 if msg.x >= 0 else -1.0, 'speed': speed,
            'x0': x0, 'y0': y0, 'th0': th0, 'imu0': None,
            # Wait briefly for the first IMU sample (the subscription was just created).
            'imu_wait_until': time.monotonic() + 0.6 if self.use_imu else None,
            't0': time.monotonic(), 'v': 0.0,
            'px': x0, 'py': y0, 'along': 0.0, 'cross': 0.0,
            'timeout': abs(msg.x) / speed * 2.0 + 5.0,
        }
        self.open_log()
        self.report({'state': 'started', 'distance': msg.x, 'speed': speed, 'imu': self.use_imu})

    # ---- control loop -------------------------------------------------------------------
    def heading_error(self, g):
        """Return (heading error [rad], odom part, imu part) relative to the start heading."""
        d_odom = wrap(self.odom[2] - g['th0'])
        imu_fresh = time.monotonic() - self.imu_t < self.sensor_pause
        if g['imu0'] is None or not imu_fresh:    # no IMU, or IMU stalled: encoder heading only
            return -d_odom, d_odom, None
        d_imu = wrap(self.imu_yaw - g['imu0'])
        return -(self.alpha * d_imu + (1.0 - self.alpha) * d_odom), d_odom, d_imu

    def step(self):
        g = self.goal
        if g is None:
            return
        now = time.monotonic()
        if g['imu_wait_until'] is not None:
            if self.imu_yaw is not None and now - self.imu_t < self.sensor_pause:
                g['imu0'], g['imu_wait_until'] = self.imu_yaw, None
                x0, y0, th0 = self.odom            # start the run from this instant
                g.update(x0=x0, y0=y0, th0=th0, px=x0, py=y0, t0=now)
            elif now < g['imu_wait_until']:
                self.cmd_pub.publish(Twist())
                return
            else:
                g['imu_wait_until'] = None         # no IMU: encoder heading only
        if now - self.odom_t > self.sensor_timeout:
            return self.finish('odom_timeout')
        if now - g['t0'] > g['timeout']:
            return self.finish('time_limit')
        if now - self.odom_t > self.sensor_pause:
            g['v'] = 0.0                     # hold still until fresh data arrives, then ramp up again
            self.cmd_pub.publish(Twist())
            return

        # Dead-reckon in the start frame: travelled distance from the encoders, direction from the
        # fused heading. Encoder odometry's own heading (and so its x/y) inherits wheel-radius
        # errors, which would bias the cross-track error if we used odom x/y directly.
        x, y, _ = self.odom
        e_head, d_odom, d_imu = self.heading_error(g)
        ds = math.hypot(x - g['px'], y - g['py'])
        g['px'], g['py'] = x, y
        g['along'] += ds * math.cos(-e_head)                    # progress along the start line
        g['cross'] += ds * math.sin(-e_head) * g['dir']         # left of the line is positive
        along, cross = g['along'], g['cross']
        remaining = g['distance'] - along
        if remaining <= self.stop_tolerance:
            return self.finish('reached', along, cross)

        # Trapezoidal speed: ramp up with accel, slow down so we can stop at the target.
        dt = 1.0 / self.rate_hz
        v_stop = math.sqrt(max(0.0, 2.0 * self.accel * remaining))
        v = min(g['speed'], g['v'] + self.accel * dt, max(v_stop, 0.02))
        g['v'] = v

        w =self.k_heading * e_head - self.k_cross * cross * g['dir']
        w = max(-self.max_angular, min(self.max_angular, w))

        cmd = Twist()
        cmd.linear.x = v * g['dir']
        cmd.angular.z = w
        self.cmd_pub.publish(cmd)

        row = [round(now - g['t0'], 3), x, y, self.odom[2], d_odom, d_imu, along, cross, e_head, cmd.linear.x, w]
        if self.log:
            self.log_writer.writerow(row)
        self.steps += 1
        if self.steps % max(1, int(self.rate_hz / 10)) == 0:   # ~10 Hz status
            self.report({'state': 'running', 'along': round(along, 3), 'cross': round(cross, 4),
                         'heading_err_deg': round(math.degrees(e_head), 2), 'v': round(v, 3), 'w': round(w, 3)})

    # ---- helpers ------------------------------------------------------------------------
    def stop_robot(self):
        for _ in range(3):
            self.cmd_pub.publish(Twist())

    def finish(self, reason, along=None, cross=None):
        if self.goal is None:
            return
        self.stop_robot()
        g, self.goal = self.goal, None
        if along is None:
            along, cross = g['along'], g['cross']
        e_head = self.heading_error(g)[0] if self.odom is not None else None
        if self.imu_sub is not None:
            self.destroy_subscription(self.imu_sub)
            self.imu_sub = None
        self.close_log()
        self.report({'state': 'finished', 'reason': reason, 'along': round(along or 0.0, 3),
                     'cross': round(cross or 0.0, 4),
                     'heading_err_deg': round(math.degrees(e_head), 2) if e_head is not None else None,
                     'duration': round(time.monotonic() - g['t0'], 2), 'log': self.log_path})

    def report(self, data):
        self.status_pub.publish(String(data=json.dumps(data)))
        if data.get('state') != 'running':
            self.get_logger().info(json.dumps(data))

    def open_log(self):
        os.makedirs(self.log_dir, exist_ok=True)
        self.log_path = os.path.join(self.log_dir, time.strftime('straight_%Y%m%d_%H%M%S.csv'))
        self.log = open(self.log_path, 'w', newline='')
        self.log_writer = csv.writer(self.log)
        self.log_writer.writerow(['t', 'x', 'y', 'theta', 'd_odom', 'd_imu', 'along', 'cross',
                                  'heading_err', 'v_cmd', 'w_cmd'])

    def close_log(self):
        if getattr(self, 'log', None):
            self.log.close()
        self.log = None

    def destroy_node(self):
        if self.goal is not None:
            self.stop_robot()
        super().destroy_node()


def main():
    rclpy.init()
    node = StraightDrive()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():   # SIGINT may already have shut the context down
            rclpy.shutdown()


if __name__ == '__main__':
    main()
