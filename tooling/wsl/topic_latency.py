#!/usr/bin/env python3
"""Measure arrival delay (receive time - header stamp) of robot topics on the PC.

    source tooling/wsl/ros_link_env.sh && python3 tooling/wsl/topic_latency.py [seconds]
Needs the PC and Pi clocks in sync (NTP); delays of hundreds of ms mean the link is congested.
"""
import statistics
import sys
import time

import rclpy
from nav_msgs.msg import Odometry
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan
from tf2_msgs.msg import TFMessage

SEC = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0


def main():
    rclpy.init()
    n = rclpy.create_node('pc_topic_latency')
    delays = {'/scan': [], '/odom': [], '/tf': []}

    def rec(name, stamp):
        now = n.get_clock().now().nanoseconds * 1e-9
        delays[name].append(now - (stamp.sec + stamp.nanosec * 1e-9))

    n.create_subscription(LaserScan, '/scan', lambda m: rec('/scan', m.header.stamp), qos_profile_sensor_data)
    n.create_subscription(Odometry, '/odom', lambda m: rec('/odom', m.header.stamp), 10)
    n.create_subscription(TFMessage, '/tf', lambda m: m.transforms and rec('/tf', m.transforms[0].header.stamp), 10)
    end = time.time() + SEC
    while time.time() < end:
        rclpy.spin_once(n, timeout_sec=0.05)
    for name, d in delays.items():
        if d:
            d.sort()
            print(f'{name:6} {len(d) / SEC:5.1f} Hz | 지연 중앙값 {statistics.median(d) * 1000:7.0f} ms'
                  f' | 90% {d[int(len(d) * 0.9)] * 1000:7.0f} ms | 최대 {d[-1] * 1000:7.0f} ms')
        else:
            print(f'{name:6} 수신 없음')
    rclpy.shutdown()


if __name__ == '__main__':
    main()
