#!/usr/bin/env bash
# Harmless end-to-end check of the Pi's teleop watchdog over DDS.
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
timeout 40 python3 tooling/wsl/watchdog_link_check.py "${1:-0.001}"
