#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
timeout 60 python3 tooling/nav/plan_check.py "${1:-1.0}"
