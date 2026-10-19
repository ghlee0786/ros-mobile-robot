#!/usr/bin/env bash
# Robot side of the PC navigation link, run on the Pi (Foxy) by robot-nav-relay.service: one
# split_relay.py per direction, each waiting for the PC on TCP. The PC side (tooling/nav/run_bridge.sh)
# runs only in the PC-local domain and connects here, so no Jazzy participant joins the robot's domain.
cd "$(dirname "$0")"
source ./ros_env.sh >/dev/null
source ./nav_relay_topics.sh
pids=()
trap 'kill "${pids[@]}" 2>/dev/null' EXIT
python3 split_relay.py out --listen 0.0.0.0 --port $NAV_RELAY_TO_PC_PORT "${NAV_RELAY_TO_PC_ROBOT_SIDE[@]}" & pids+=($!)
python3 split_relay.py in  --listen 0.0.0.0 --port $NAV_RELAY_TO_ROBOT_PORT "${NAV_RELAY_TO_ROBOT[@]}" & pids+=($!)
wait -n
exit 1   # one side died: let systemd restart both
