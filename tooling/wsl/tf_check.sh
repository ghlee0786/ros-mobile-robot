#!/usr/bin/env bash
# Check what WSL sees over DDS: robot topics and the odom->base_link TF used by RViz.
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
echo "== /odom 수신 =="; timeout 10 ros2 topic echo --once /odom nav_msgs/msg/Odometry 2>&1 | grep -m2 -E "frame_id"
echo "== TF odom->base_link =="; timeout 10 ros2 run tf2_ros tf2_echo odom base_link 2>&1 | grep -m3 -E "Translation|Rotation|Invalid|exist"
echo "== TF base_link->laser =="; timeout 10 ros2 run tf2_ros tf2_echo base_link laser 2>&1 | grep -m2 -E "Translation|Invalid|exist"
