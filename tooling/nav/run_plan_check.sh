#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_local_env.sh   # nav stack lives in PC-local domain 8
timeout 60 python3 tooling/nav/plan_check.py "${1:-1.0}"
