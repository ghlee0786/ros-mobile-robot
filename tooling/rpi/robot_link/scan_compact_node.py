#!/usr/bin/env python3
"""Republish /scan as /scan_compact (std_msgs/UInt16MultiArray, ranges in mm) for the dashboard.

The Pi's rosbridge (Python) tops out at one core, and /scan was ~90% of what it sent: each float32 range
becomes a 17-digit JSON number (~37 KB per scan at 12 Hz), which pushed /odom down from 33 to 23 Hz.
Ranges in mm as integers make the same scan ~4x smaller. 0 = no return (inf/NaN/out of range).
layout.dim[0].label carries the geometry: "frame_id angle_min angle_increment range_min range_max stamp_sec stamp_nsec".
"""
import math

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import LaserScan
from std_msgs.msg import MultiArrayDimension, UInt16MultiArray


def main():
    rclpy.init()
    node = rclpy.create_node('scan_compact')
    pub = node.create_publisher(UInt16MultiArray, '/scan_compact', 5)

    def on_scan(s):
        if pub.get_subscription_count() == 0:
            return
        out = UInt16MultiArray()
        label = (f'{s.header.frame_id} {s.angle_min:.7f} {s.angle_increment:.9f} {s.range_min:.3f} {s.range_max:.3f} '
                 f'{s.header.stamp.sec} {s.header.stamp.nanosec}')
        out.layout.dim = [MultiArrayDimension(label=label, size=len(s.ranges), stride=len(s.ranges))]
        out.data = [int(r * 1000) if math.isfinite(r) and s.range_min <= r <= s.range_max and r < 65.5 else 0
                    for r in s.ranges]
        pub.publish(out)

    node.create_subscription(LaserScan, '/scan', on_scan, qos_profile_sensor_data)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
