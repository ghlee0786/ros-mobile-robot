#!/usr/bin/env bash
# Run the DDS ping-pong test from WSL:  wsl -d Ubuntu-ROS -- bash tooling/wsl/run_dds_test.sh [count]
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
echo "DDS interface: ${DDS_IF:-auto}  domain: $ROS_DOMAIN_ID  rmw: $RMW_IMPLEMENTATION"
timeout 90 python3 tooling/wsl/dds_pingpong_test.py "${1:-10}"
for t in /odom /scan /imu/data_raw /sync/odom /sync/scan; do
  rate=$(timeout 15 ros2 topic hz "$t" 2>/dev/null | grep -m1 "average rate" | awk '{print $3}')
  if [ -n "$rate" ]; then echo "[센서] $t: ${rate} Hz"; else echo "[센서] $t: 수신 없음"; fi
done
