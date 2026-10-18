#!/usr/bin/env python3
"""Record commanded vs. measured speed (/cmd_vel vs. /odom twist) to CSV for speed-response analysis.

    python3 speed_probe.py [seconds] [csv_path]
"""
import csv
import os
import sys
import time

import rclpy
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry

DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 180.0
PATH = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser(time.strftime('~/robot_link/logs/speed_%Y%m%d_%H%M%S.csv'))


def main():
    rclpy.init()
    n = rclpy.create_node('speed_probe')
    state = {'cmd': Twist(), 'odom': None}
    n.create_subscription(Twist, '/cmd_vel', lambda m: state.__setitem__('cmd', m), 10)
    n.create_subscription(Odometry, '/odom', lambda m: state.__setitem__('odom', m), 10)
    os.makedirs(os.path.dirname(PATH), exist_ok=True)
    with open(PATH, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['t', 'cmd_v', 'cmd_w', 'odom_v', 'odom_w', 'x', 'y'])
        t0 = time.monotonic()
        nxt = t0
        while time.monotonic() - t0 < DURATION:
            rclpy.spin_once(n, timeout_sec=0.01)
            if time.monotonic() >= nxt and state['odom'] is not None:
                o = state['odom']
                w.writerow([round(time.monotonic() - t0, 2), state['cmd'].linear.x, state['cmd'].angular.z,
                            round(o.twist.twist.linear.x, 4), round(o.twist.twist.angular.z, 4),
                            round(o.pose.pose.position.x, 4), round(o.pose.pose.position.y, 4)])
                nxt += 0.1
    print(PATH)
    rclpy.shutdown()


if __name__ == '__main__':
    main()
