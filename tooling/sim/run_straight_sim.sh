#!/usr/bin/env bash
# Offline check of straight_drive_node against diffdrive_sim, isolated from the robot's domain 7.
#   wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/sim/run_straight_sim.sh [distance] [sim params...]
cd "$(dirname "$0")/../.."
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=42 ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
DIST="${1:-2.0}"; shift || true
LOGDIR=$(mktemp -d)
python3 tooling/sim/diffdrive_sim.py --ros-args "$@" >"$LOGDIR/sim.log" 2>&1 &
SIM=$!
python3 tooling/rpi/robot_link/straight_drive_node.py --ros-args -p log_dir:="$LOGDIR" >"$LOGDIR/node.log" 2>&1 &
NODE=$!
trap 'kill $SIM $NODE 2>/dev/null' EXIT
python3 tooling/sim/straight_drive_check.py "$DIST"
echo "[로그] $(ls "$LOGDIR"/straight_*.csv 2>/dev/null)"
grep -iE "error|traceback" "$LOGDIR/node.log" "$LOGDIR/sim.log" | head -5
