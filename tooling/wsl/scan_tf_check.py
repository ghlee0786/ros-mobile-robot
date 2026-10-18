#!/usr/bin/env python3
"""Check that the odom<-base_link TF is available at each /scan timestamp (what SLAM needs)."""
import time

import rclpy
from rclpy.duration import Duration
from rclpy.qos import qos_profile_sensor_data
from rclpy.time import Time
from sensor_msgs.msg import LaserScan
from tf2_ros import Buffer, TransformListener


def main():
    rclpy.init()
    n = rclpy.create_node('pc_scan_tf_check')
    buf = Buffer()
    TransformListener(buf, n)
    scans = []
    n.create_subscription(LaserScan, '/scan', scans.append, qos_profile_sensor_data)
    end = time.time() + 6
    while time.time() < end:
        rclpy.spin_once(n, timeout_sec=0.05)
    ok = fail = 0
    reasons = {}
    for s in scans[-20:]:
        try:
            buf.lookup_transform('odom', s.header.frame_id, Time.from_msg(s.header.stamp), Duration(seconds=0.2))
            ok += 1
        except Exception as e:  # tf2 raises several exception types
            fail += 1
            reasons[type(e).__name__] = str(e)[:160]
    print(f'scan {len(scans)}개 수신, frame={scans[-1].header.frame_id if scans else "-"}, TF 조회 성공 {ok} / 실패 {fail}')
    for k, v in reasons.items():
        print(f'  {k}: {v}')
    print('TF 프레임 목록:', buf.all_frames_as_string().replace('\n', ' | ')[:300])
    rclpy.shutdown()


if __name__ == '__main__':
    main()
