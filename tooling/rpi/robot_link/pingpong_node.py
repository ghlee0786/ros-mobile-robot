#!/usr/bin/env python3
"""Link check responder: echoes /link/ping to /link/pong and publishes /link/heartbeat every second."""
import socket
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class PingPong(Node):
    def __init__(self):
        super().__init__('link_pingpong')
        self.host = socket.gethostname()
        self.count = 0
        self.pong_pub = self.create_publisher(String, '/link/pong', 10)
        self.beat_pub = self.create_publisher(String, '/link/heartbeat', 10)
        self.create_subscription(String, '/link/ping', self.on_ping, 10)
        self.create_timer(1.0, self.on_beat)
        self.get_logger().info('ready: /link/ping -> /link/pong, /link/heartbeat @1Hz')

    def on_ping(self, msg):
        # Echo the payload untouched so the sender can match it and compute round-trip time.
        self.pong_pub.publish(String(data=f'{msg.data}|pi={self.host}|pi_time={time.time():.6f}'))

    def on_beat(self):
        self.count += 1
        self.beat_pub.publish(String(data=f'{self.host} seq={self.count} time={time.time():.3f}'))


def main():
    rclpy.init()
    node = PingPong()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():   # SIGINT may already have shut the context down
            rclpy.shutdown()


if __name__ == '__main__':
    main()
