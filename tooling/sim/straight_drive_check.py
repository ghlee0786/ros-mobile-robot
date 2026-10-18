#!/usr/bin/env python3
"""Compare open-loop driving vs. straight_drive_node against diffdrive_sim (no hardware).

Run with the sim and straight_drive_node up on an isolated domain (see run_straight_sim.sh).
Prints the true lateral error and heading error after driving DISTANCE metres both ways.
"""
import json
import math
import sys
import time

import rclpy
from geometry_msgs.msg import Pose2D, Twist, Vector3
from rclpy.node import Node
from std_msgs.msg import String

DISTANCE = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
SPEED = 0.08


class Check(Node):
    def __init__(self):
        super().__init__('straight_drive_check')
        self.cmd = self.create_publisher(Twist, '/cmd_vel', 10)
        self.goal = self.create_publisher(Vector3, '/straight_drive/goal', 10)
        self.pose = None
        self.done = None
        self.create_subscription(Pose2D, '/sim/true_pose', lambda m: setattr(self, 'pose', m), 10)
        self.create_subscription(String, '/straight_drive/status', self.on_status, 10)

    def on_status(self, msg):
        d = json.loads(msg.data)
        if d.get('state') == 'finished':
            self.done = d

    def spin_for(self, sec, cond=lambda: False):
        end = time.time() + sec
        while time.time() < end and not cond():
            rclpy.spin_once(self, timeout_sec=0.02)

    def snapshot(self):
        return self.pose.x, self.pose.y, self.pose.theta


def report(name, start, end):
    x0, y0, th0 = start
    x1, y1, th1 = end
    along = (x1 - x0) * math.cos(th0) + (y1 - y0) * math.sin(th0)
    cross = -(x1 - x0) * math.sin(th0) + (y1 - y0) * math.cos(th0)
    head = math.degrees(math.atan2(math.sin(th1 - th0), math.cos(th1 - th0)))
    print(f'[{name}] 전진 {along:.3f} m | 옆으로 벗어남 {cross * 100:+.1f} cm | 방향 오차 {head:+.2f}°')


def main():
    rclpy.init()
    n = Check()
    n.spin_for(10, lambda: n.pose is not None and n.goal.get_subscription_count() > 0)
    if n.pose is None:
        print('sim not running'); return 1

    # 1) open loop: what the robot does today with a plain forward command
    start = n.snapshot()
    end_t = time.time() + DISTANCE / SPEED
    while time.time() < end_t:
        n.cmd.publish(Twist(linear=Vector3(x=SPEED)))
        n.spin_for(0.05)
    n.cmd.publish(Twist())
    n.spin_for(1.0)
    report('기존(보정 없음)', start, n.snapshot())

    # 2) closed loop via straight_drive_node
    start = n.snapshot()
    n.done = None
    n.goal.publish(Vector3(x=DISTANCE, y=SPEED))
    n.spin_for(DISTANCE / SPEED * 2 + 10, lambda: n.done is not None)
    n.spin_for(1.0)
    report('직진 제어', start, n.snapshot())
    print('[노드 보고]', json.dumps(n.done, ensure_ascii=False))
    rclpy.shutdown()
    return 0


if __name__ == '__main__':
    sys.exit(main())
