#!/usr/bin/env bash
# Topic rates/delays as seen inside the PC-local domain 8 (i.e. after the domain bridge).
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_local_env.sh
python3 tooling/wsl/topic_latency.py "${1:-8}"
