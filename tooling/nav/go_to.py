#!/usr/bin/env python3
"""Send the robot to a map position with Nav2 and report progress (PC, WSL, Jazzy).

    source tooling/wsl/ros_local_env.sh && python3 tooling/nav/go_to.py X Y [YAW_DEG]
Coordinates are in the map frame (metres). Ctrl+C cancels the goal.
"""
import math
import sys

import rclpy
from geometry_msgs.msg import PoseStamped
from nav2_simple_commander.robot_navigator import BasicNavigator, TaskResult


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    x, y = float(sys.argv[1]), float(sys.argv[2])
    yaw = math.radians(float(sys.argv[3])) if len(sys.argv) > 3 else 0.0
    rclpy.init()
    nav = BasicNavigator()
    goal = PoseStamped()
    goal.header.frame_id = 'map'
    goal.header.stamp = nav.get_clock().now().to_msg()
    goal.pose.position.x, goal.pose.position.y = x, y
    goal.pose.orientation.z, goal.pose.orientation.w = math.sin(yaw / 2), math.cos(yaw / 2)
    print(f'[목표] map ({x:.2f}, {y:.2f}) {math.degrees(yaw):.0f}°')
    nav.goToPose(goal)
    try:
        while not nav.isTaskComplete():
            fb = nav.getFeedback()
            if fb:
                print(f'\r  남은 거리 {fb.distance_remaining:.2f} m | 경과 {fb.navigation_time.sec} s'
                      f' | 복구 {fb.number_of_recoveries}회   ', end='')
    except KeyboardInterrupt:
        nav.cancelTask()
        print('\n[취소] 목표를 취소했습니다.')
        return 1
    result = nav.getResult()
    print('\n[결과]', {TaskResult.SUCCEEDED: '도착', TaskResult.CANCELED: '취소됨',
                     TaskResult.FAILED: '실패'}.get(result, str(result)))
    rclpy.shutdown()
    return 0 if result == TaskResult.SUCCEEDED else 1


if __name__ == '__main__':
    sys.exit(main())
