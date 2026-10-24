#!/usr/bin/env python3
"""Cancel every Nav2 goal when the dashboard stops or e-stops (PC nav domain).

The dashboard only talks to the Pi's rosbridge, so it cannot reach Nav2's action services in the PC-local
domain. It publishes /dashboard/nav_cancel ('정지' mode, cancel) and /teleop/estop (e-stop); split_relay
brings both here and this node cancels all goals, so Nav2 stops sending velocity instead of resuming
once the Pi watchdog's e-stop hold ends.
"""
import rclpy
from action_msgs.srv import CancelGoal
from std_msgs.msg import Empty

SERVICES = ['/navigate_to_pose/_action/cancel_goal', '/navigate_through_poses/_action/cancel_goal',
            '/follow_waypoints/_action/cancel_goal']


def main():
    rclpy.init()
    node = rclpy.create_node('nav_cancel_bridge')
    clients = [node.create_client(CancelGoal, s) for s in SERVICES]

    def cancel_all(why):
        for c in clients:
            if c.service_is_ready():
                c.call_async(CancelGoal.Request())   # zero goal id + zero stamp = cancel all goals
        node.get_logger().info(f'{why}: cancelled all Nav2 goals')

    node.create_subscription(Empty, '/dashboard/nav_cancel', lambda _m: cancel_all('dashboard stop'), 10)
    node.create_subscription(Empty, '/teleop/estop', lambda _m: cancel_all('e-stop'), 10)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
