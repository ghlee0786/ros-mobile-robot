#!/usr/bin/env bash
# Show which robot nodes/topics WSL discovers over DDS and whether sensor data arrives.
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
ros2 daemon stop >/dev/null 2>&1
echo "== nodes (after ${1:-10}s) =="
sleep "${1:-10}"
ros2 node list --no-daemon 2>/dev/null
echo "== /odom echo (40s max) =="
timeout 40 ros2 topic echo --once /odom nav_msgs/msg/Odometry 2>&1 | grep -m3 -E "frame_id|x:" || echo "no /odom"
