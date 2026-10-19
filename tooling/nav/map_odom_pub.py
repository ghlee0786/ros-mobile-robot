#!/usr/bin/env python3
"""Publish map -> odom (from SLAM or AMCL) on /map_odom (tf2_msgs/TFMessage) at 5 Hz in the PC nav domain.

run_bridge.sh relays /map_odom to the robot so the dashboard (which only talks to the Pi's rosbridge)
can place the map and send goals in the map frame. The whole /tf cannot be relayed back: the robot's own
/tf arrives here through the same relay and would loop.
"""
import rclpy
from rclpy.duration import Duration
from rclpy.time import Time
from tf2_msgs.msg import TFMessage
from tf2_ros import Buffer, TransformException, TransformListener


def main():
    rclpy.init()
    node = rclpy.create_node('map_odom_pub')
    buf = Buffer()
    TransformListener(buf, node)
    pub = node.create_publisher(TFMessage, '/map_odom', 5)

    def tick():
        try:
            t = buf.lookup_transform('map', 'odom', Time(), timeout=Duration(seconds=0))
        except TransformException:
            return
        pub.publish(TFMessage(transforms=[t]))

    node.create_timer(0.2, tick)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
