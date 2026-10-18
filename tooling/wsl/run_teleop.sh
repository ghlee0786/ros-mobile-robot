#!/usr/bin/env bash
# Keyboard teleop over DDS:  wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/wsl/run_teleop.sh
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
exec python3 tooling/wsl/teleop_keyboard.py
