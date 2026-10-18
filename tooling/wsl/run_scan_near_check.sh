#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
python3 tooling/wsl/scan_near_check.py
