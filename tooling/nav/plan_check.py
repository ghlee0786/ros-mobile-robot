#!/usr/bin/env python3
"""Ask Nav2's planner for a path without driving (ComputePathToPose only).

    source tooling/wsl/ros_local_env.sh && python3 tooling/nav/plan_check.py [ahead_m]
Plans from the robot's current map pose to a point `ahead_m` metres straight ahead.
"""
import math
import sys

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_simple_commander.robot_navigator import BasicNavigator
from rclpy.duration import Duration
from rclpy.time import Time
from tf2_ros import Buffer, TransformListener

AHEAD = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0


def main():
    rclpy.init()
    nav = BasicNavigator()
    buf = Buffer()
    TransformListener(buf, nav)
    end = nav.get_clock().now() + Duration(seconds=10)
    tf = None
    while nav.get_clock().now() < end and tf is None:
        rclpy.spin_once(nav, timeout_sec=0.1)
        try:
            tf = buf.lookup_transform('map', 'base_link', Time())
        except Exception:
            pass
    if tf is None:
        print('map->base_link TF 없음')
        return 1
    t, q = tf.transform.translation, tf.transform.rotation
    yaw = math.atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))
    start = PoseStamped()
    start.header.frame_id = 'map'
    start.pose.position.x, start.pose.position.y = t.x, t.y
    start.pose.orientation = q
    goal = PoseStamped()
    goal.header.frame_id = 'map'
    goal.pose.position.x = t.x + AHEAD * math.cos(yaw)
    goal.pose.position.y = t.y + AHEAD * math.sin(yaw)
    goal.pose.orientation = q
    print(f'[시작] ({t.x:.2f}, {t.y:.2f}) {math.degrees(yaw):.0f}°  [목표] {AHEAD} m 앞 '
          f'({goal.pose.position.x:.2f}, {goal.pose.position.y:.2f})')
    path = nav.getPath(start, goal, use_start=True)
    if path and path.poses:
        length = sum(math.dist((a.pose.position.x, a.pose.position.y), (b.pose.position.x, b.pose.position.y))
                     for a, b in zip(path.poses, path.poses[1:]))
        print(f'[계획 성공] 경로 점 {len(path.poses)}개, 길이 {length:.2f} m (로봇은 움직이지 않음)')
        return 0
    print('[계획 실패]')
    return 1


if __name__ == '__main__':
    sys.exit(main())
