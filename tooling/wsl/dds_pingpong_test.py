#!/usr/bin/env python3
"""PC (WSL, ROS 2 Jazzy) -> Pi (ROS 2 Foxy) link test over native DDS.

Waits for /link/heartbeat from the Pi, then publishes /link/ping and times the /link/pong echo.
    source tooling/wsl/ros_link_env.sh && python3 tooling/wsl/dds_pingpong_test.py [count]
"""
import statistics
import sys
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 10


class Pinger(Node):
    def __init__(self):
        super().__init__('pc_link_pinger')
        self.pub = self.create_publisher(String, '/link/ping', 10)
        self.pongs = {}
        self.heartbeat = None
        self.create_subscription(String, '/link/pong', self.on_pong, 10)
        self.create_subscription(String, '/link/heartbeat', self.on_beat, 10)

    def on_pong(self, msg):
        token = msg.data.split('|', 1)[0]
        self.pongs.setdefault(token, (time.perf_counter(), msg.data))

    def on_beat(self, msg):
        self.heartbeat = msg.data


def spin_until(node, cond, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline and not cond():
        rclpy.spin_once(node, timeout_sec=0.05)
    return cond()


def main():
    rclpy.init()
    node = Pinger()
    if spin_until(node, lambda: node.heartbeat is not None, 15.0):
        print(f'[하트비트] {node.heartbeat}')
    else:
        print('[하트비트] 15초 동안 없음 (DDS 탐색 실패)')
    spin_until(node, lambda: node.pub.get_subscription_count() > 0, 10.0)
    print(f'[매칭] /link/ping 구독자 {node.pub.get_subscription_count()}개')

    rtts = []
    for i in range(COUNT):
        token = f'pc-dds-{i}-{time.time():.6f}'
        t0 = time.perf_counter()
        node.pub.publish(String(data=token))
        if spin_until(node, lambda: token in node.pongs, 2.0):
            t1, data = node.pongs[token]
            rtts.append((t1 - t0) * 1000)
            print(f'  ping {i}: pong {rtts[-1]:.1f} ms  <- {data.split("|", 1)[1]}')
        else:
            print(f'  ping {i}: 응답 없음')
        spin_until(node, lambda: False, 0.2)
    print(f'[핑퐁] {len(rtts)}/{COUNT} 성공' + (f', 평균 {statistics.mean(rtts):.1f} ms, 최대 {max(rtts):.1f} ms' if rtts else ''))
    node.destroy_node()
    rclpy.shutdown()
    return 0 if rtts else 1


if __name__ == '__main__':
    sys.exit(main())
