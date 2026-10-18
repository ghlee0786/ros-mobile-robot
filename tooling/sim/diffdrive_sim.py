#!/usr/bin/env python3
"""Minimal stand-in for the robot (Pico + control_node + IMU) to test drive logic without motors.

Models what makes the real robot curve: the Pico's near-P-only wheel PID leaves a different
steady-state speed on each wheel (left_gain / right_gain), and the encoders see a slightly wrong
wheel radius (encoder_scale_*), so encoder odometry and the true pose disagree. The IMU reports
the true yaw plus a fixed offset and noise. Same limits as the firmware: |v|<=0.1, |w|<=0.25.

Publishes /odom (encoder estimate, 33 Hz), /imu/data_raw (100 Hz), /sim/true_pose (Pose2D).
"""
import math
import random

import rclpy
from geometry_msgs.msg import Pose2D, Quaternion, Twist
from nav_msgs.msg import Odometry
from rclpy.node import Node
from sensor_msgs.msg import Imu

R, L = 0.05, 0.34          # wheel radius, wheel separation (firmware config.h)


def quat(yaw):
    return Quaternion(x=0.0, y=0.0, z=math.sin(yaw / 2), w=math.cos(yaw / 2))


class Sim(Node):
    def __init__(self):
        super().__init__('diffdrive_sim')
        p = self.declare_parameter
        self.left_gain = p('left_gain', 0.95).value      # left wheel reaches 95% of its setpoint
        self.right_gain = p('right_gain', 1.0).value
        self.enc_l = p('encoder_scale_left', 1.0).value  # encoder-perceived / true wheel travel
        self.enc_r = p('encoder_scale_right', 1.01).value
        self.imu_offset = p('imu_yaw_offset', 1.0).value
        self.imu_noise = p('imu_noise', 0.002).value
        self.cmd = Twist()
        self.true = [0.0, 0.0, 0.0]
        self.est = [0.0, 0.0, 0.0]
        self.create_subscription(Twist, '/cmd_vel', self.on_cmd, 10)
        self.odom_pub = self.create_publisher(Odometry, '/odom', 10)
        self.imu_pub = self.create_publisher(Imu, '/imu/data_raw', 50)
        self.true_pub = self.create_publisher(Pose2D, '/sim/true_pose', 10)
        self.dt = 0.01
        self.create_timer(self.dt, self.physics)
        self.create_timer(0.03, self.publish_odom)
        self.tick = 0

    def on_cmd(self, msg):
        self.cmd = msg

    def physics(self):
        v = max(-0.1, min(0.1, self.cmd.linear.x))
        w = max(-0.25, min(0.25, self.cmd.angular.z))
        wl = (2 * v - w * L) / (2 * R) * self.left_gain     # true wheel angular speeds
        wr = (2 * v + w * L) / (2 * R) * self.right_gain
        for pose, sl, sr in ((self.true, 1.0, 1.0), (self.est, self.enc_l, self.enc_r)):
            vl, vr = wl * R * sl, wr * R * sr
            vv, ww = (vl + vr) / 2, (vr - vl) / L
            pose[0] += vv * math.cos(pose[2]) * self.dt
            pose[1] += vv * math.sin(pose[2]) * self.dt
            pose[2] += ww * self.dt
        self.tick += 1
        imu = Imu()
        imu.header.stamp = self.get_clock().now().to_msg()
        imu.header.frame_id = 'base_link'
        imu.orientation = quat(self.true[2] + self.imu_offset + random.gauss(0, self.imu_noise))
        self.imu_pub.publish(imu)

    def publish_odom(self):
        o = Odometry()
        o.header.stamp = self.get_clock().now().to_msg()
        o.header.frame_id, o.child_frame_id = 'odom', 'base_link'
        o.pose.pose.position.x, o.pose.pose.position.y = self.est[0], self.est[1]
        o.pose.pose.orientation = quat(self.est[2])
        self.odom_pub.publish(o)
        self.true_pub.publish(Pose2D(x=self.true[0], y=self.true[1], theta=self.true[2]))


def main():
    rclpy.init()
    rclpy.spin(Sim())


if __name__ == '__main__':
    main()
