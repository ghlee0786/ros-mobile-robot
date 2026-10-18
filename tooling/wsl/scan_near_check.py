#!/usr/bin/env python3
"""Show how many /scan returns fall close to the robot (self-hits from the robot's own body)."""
import math
import time

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan


def main():
    rclpy.init()
    n = rclpy.create_node('pc_scan_near_check')
    got = []
    n.create_subscription(LaserScan, '/scan', got.append, qos_profile_sensor_data)
    end = time.time() + 5
    while time.time() < end and len(got) < 10:
        rclpy.spin_once(n, timeout_sec=0.1)
    if not got:
        print('/scan 수신 없음')
        return
    s = got[-1]
    print(f'점 {len(s.ranges)}개, range_min={s.range_min:.2f} m, frame={s.header.frame_id}')
    for lim in (0.15, 0.25, 0.35, 0.45, 0.6, 0.8, 1.0):
        print(f'  {lim:.2f} m 이내: {sum(1 for r in s.ranges if math.isfinite(r) and r < lim)}개')
    near = [(math.degrees(s.angle_min + i * s.angle_increment), r) for i, r in enumerate(s.ranges)
            if math.isfinite(r) and r < 0.35]
    mid = [(math.degrees(s.angle_min + i * s.angle_increment), r) for i, r in enumerate(s.ranges)
           if math.isfinite(r) and 0.3 <= r < 1.0]
    if mid:
        print('  0.3~1.0 m 점:', ', '.join(f'{a:.0f}°:{r:.2f}m' for a, r in mid[::max(1, len(mid) // 16)]))
    if near:
        print('  가까운 점의 각도 범위:', ', '.join(f'{a:.0f}°:{r:.2f}m' for a, r in near[::max(1, len(near) // 12)]))
    rclpy.shutdown()


if __name__ == '__main__':
    main()
