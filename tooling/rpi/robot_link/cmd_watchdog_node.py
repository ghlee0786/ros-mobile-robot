#!/usr/bin/env python3
"""Safety relay for remote driving: /teleop/cmd_vel -> /cmd_vel with a dead-man timeout.

control_node and the Pico firmware keep the last velocity forever, so a dropped Wi-Fi link or a
crashed PC teleop would leave the robot driving. Remote teleop therefore publishes to
/teleop/cmd_vel continuously (10 Hz, including zeros) and this node, running on the Pi, relays it:
  * fresh non-zero command      -> relayed to /cmd_vel
  * command becomes zero        -> zero relayed a few times, then the node goes quiet
  * no message for `timeout` s  -> zero published a few times (link lost), then quiet
Going quiet while idle keeps it from fighting other /cmd_vel sources such as straight_drive.
"""
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node


def is_zero(t):
    return abs(t.linear.x) < 1e-6 and abs(t.angular.z) < 1e-6


class CmdWatchdog(Node):
    def __init__(self):
        super().__init__('cmd_watchdog')
        self.timeout = self.declare_parameter('timeout', 0.5).value
        self.pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.create_subscription(Twist, '/teleop/cmd_vel', self.on_cmd, 10)
        self.create_timer(0.05, self.step)
        self.last = Twist()
        self.last_t = 0.0
        self.zeros_left = 0          # zero messages still to send after a stop / timeout
        self.moving = False

    def on_cmd(self, msg):
        self.last, self.last_t = msg, time.monotonic()

    def step(self):
        fresh = time.monotonic() - self.last_t < self.timeout
        if fresh and not is_zero(self.last):
            self.pub.publish(self.last)
            self.moving, self.zeros_left = True, 5
            return
        if self.moving and not fresh:
            self.get_logger().warn(f'/teleop/cmd_vel silent for {self.timeout}s: stopping robot')
        self.moving = False
        if self.zeros_left > 0:
            self.pub.publish(Twist())
            self.zeros_left -= 1


def main():
    rclpy.init()
    node = CmdWatchdog()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.pub.publish(Twist())
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
