#!/usr/bin/env python3
"""Check the Pi's cmd_watchdog over the real link with a harmless command (default 0.001 m/s).

Feeds /teleop/cmd_vel for 2 s, then goes silent and reports what reached /cmd_vel on the robot.
    source tooling/wsl/ros_link_env.sh && python3 tooling/wsl/watchdog_link_check.py [speed]
"""
import sys
import time

import rclpy
from geometry_msgs.msg import Twist

SPEED = float(sys.argv[1]) if len(sys.argv) > 1 else 0.001


def main():
    rclpy.init()
    n = rclpy.create_node('pc_watchdog_check')
    got = []
    n.create_subscription(Twist, '/cmd_vel', lambda m: got.append((time.monotonic(), m.linear.x)), 10)
    pub = n.create_publisher(Twist, '/teleop/cmd_vel', 10)
    end = time.monotonic() + 15
    while time.monotonic() < end and (pub.get_subscription_count() == 0 or n.count_publishers('/cmd_vel') == 0):
        rclpy.spin_once(n, timeout_sec=0.1)
    print(f'[연결] /teleop/cmd_vel 구독자 {pub.get_subscription_count()}개, /cmd_vel 발행자 {n.count_publishers("/cmd_vel")}개')

    def spin(sec, feed=None):
        stop, nxt = time.monotonic() + sec, 0.0
        while time.monotonic() < stop:
            if feed is not None and time.monotonic() >= nxt:
                t = Twist()
                t.linear.x = feed
                pub.publish(t)
                nxt = time.monotonic() + 0.1
            rclpy.spin_once(n, timeout_sec=0.01)

    spin(1.0)
    t0 = time.monotonic()
    spin(2.0, feed=SPEED)
    t_cut = time.monotonic()
    spin(2.0)
    during = [v for t, v in got if t0 < t < t_cut and v > 0]
    after = [(t - t_cut, v) for t, v in got if t > t_cut]
    first_zero = next((d for d, v in after if v == 0), None)
    print(f'[중계] 조종 중 로봇 /cmd_vel {len(during)}건 ({SPEED} m/s)')
    print(f'[끊김] 정지 명령 {first_zero:.2f}s 후' if first_zero is not None else '[끊김] 정지 명령 없음!')
    print(f'[대기] 1초 이후 추가 명령 {len([1 for d, v in after if d > 1.0])}건')
    rclpy.shutdown()


if __name__ == '__main__':
    main()
