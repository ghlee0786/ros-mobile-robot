#!/usr/bin/env python3
"""Keyboard teleop for the robot (run in a terminal on the PC, WSL ROS 2 Jazzy over DDS).

Publishes the latched command to /teleop/cmd_vel at 10 Hz; the Pi's cmd_watchdog relays it to
/cmd_vel and stops the robot if this program or the link goes away. Keys follow the old
integrated_robot_slam teleop_keyboard layout (i , j l u o m . k).
"""
import select
import sys
import termios
import time
import tty

import rclpy
from geometry_msgs.msg import Twist, Vector3
from nav_msgs.msg import Odometry
from rclpy.node import Node
from std_msgs.msg import Empty, String

V_STEP, W_STEP = 0.02, 0.05
V_MAX, W_MAX = 0.10, 0.25          # Pico firmware clamps to these anyway
STRAIGHT_DIST = 1.0

HELP = f"""
──────────────── 로봇 키보드 조종 ────────────────
        u    i    o          i / ,  : 전진 / 후진 속도 ±{V_STEP} m/s
        j    k    l          j / l  : 좌회전 / 우회전 ±{W_STEP} rad/s
        m    ,    .          u o m . : 대각선(전진+회전)
                             k 또는 스페이스 : 정지
  g : 직진 주행 {STRAIGHT_DIST} m (보정 제어)   b : 직진 후진 {STRAIGHT_DIST} m
  x : 직진 주행 취소            q : 정지 후 종료
  최대 속도: 전진 {V_MAX} m/s, 회전 {W_MAX} rad/s (펌웨어 제한)
  안전: 이 창을 닫거나 연결이 끊기면 0.5초 안에 로봇이 멈춥니다.
──────────────────────────────────────────────────
"""

MOVES = {   # key: (linear step sign, angular step sign) applied to the target
    'i': (1, 0), ',': (-1, 0), 'j': (0, 1), 'l': (0, -1),
    'u': (1, 1), 'o': (1, -1), 'm': (-1, -1), '.': (-1, 1),
}


def clamp(x, m):
    return max(-m, min(m, x))


class Teleop(Node):
    def __init__(self):
        super().__init__('pc_teleop_keyboard')
        self.pub = self.create_publisher(Twist, '/teleop/cmd_vel', 10)
        self.goal_pub = self.create_publisher(Vector3, '/straight_drive/goal', 10)
        self.cancel_pub = self.create_publisher(Empty, '/straight_drive/cancel', 10)
        self.create_subscription(Odometry, '/odom', self.on_odom, 10)
        self.create_subscription(String, '/straight_drive/status', self.on_status, 10)
        self.v = self.w = 0.0
        self.odom = None
        self.status = ''

    def on_odom(self, m):
        self.odom = m

    def on_status(self, m):
        self.status = m.data[:90]

    def send(self):
        t = Twist()
        t.linear.x, t.angular.z = self.v, self.w
        self.pub.publish(t)

    def stop(self):
        self.v = self.w = 0.0
        for _ in range(3):
            self.send()


def main():
    rclpy.init()
    node = Teleop()
    old = termios.tcgetattr(sys.stdin)
    print(HELP)
    try:
        tty.setcbreak(sys.stdin.fileno())
        next_pub = 0.0
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.0)
            if select.select([sys.stdin], [], [], 0.02)[0]:
                key = sys.stdin.read(1)
                if key in MOVES:
                    dv, dw = MOVES[key]
                    node.v = round(clamp(node.v + dv * V_STEP, V_MAX), 3)
                    node.w = round(clamp(node.w + dw * W_STEP, W_MAX), 3)
                elif key in ('k', ' '):
                    node.stop()
                elif key in ('g', 'b'):
                    node.stop()
                    node.goal_pub.publish(Vector3(x=STRAIGHT_DIST if key == 'g' else -STRAIGHT_DIST, y=0.08))
                elif key == 'x':
                    node.stop()
                    node.cancel_pub.publish(Empty())
                elif key in ('q', '\x03'):
                    break
            now = time.monotonic()
            if now >= next_pub:                       # 10 Hz heartbeat, zeros included
                node.send()
                next_pub = now + 0.1
                pos = ''
                if node.odom:
                    p = node.odom.pose.pose.position
                    pos = f' | odom x={p.x:+.3f} y={p.y:+.3f} v={node.odom.twist.twist.linear.x:+.3f}'
                sys.stdout.write(f'\r명령 v={node.v:+.2f} m/s  w={node.w:+.2f} rad/s{pos}   {node.status:<40.40}')
                sys.stdout.flush()
    finally:
        node.stop()
        node.cancel_pub.publish(Empty())
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
        print('\n정지 명령을 보내고 종료했습니다.')
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
