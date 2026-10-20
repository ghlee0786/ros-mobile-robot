#!/usr/bin/env bash
# Link the PC-local navigation stack (domain 8: FastDDS, localhost) with the robot. The robot side of
# each split_relay.py pair runs on the Pi itself (robot-nav-relay.service, Foxy) and waits on TCP; this
# side only joins domain 8 and connects to it. Before, the robot side ran here as a Jazzy process in the
# robot's domain 7, and Foxy cannot parse Jazzy discovery data: the Pi's node graph got corrupted,
# rosapi died (UnicodeDecodeError in get_node_names) and rosbridge stopped delivering to the dashboard.
#   ROBOT_HOST=192.168.1.200 tooling/nav/run_bridge.sh   (default: Wi-Fi 192.168.1.201). Runs until killed.
cd "$(dirname "$0")/../.."
R=tooling/nav/split_relay.py
HOST="${ROBOT_HOST:-192.168.1.201}"
source tooling/rpi/robot_link/nav_relay_topics.sh
pids=()
start() { "$@" & pids+=($!); }
trap 'kill "${pids[@]}" 2>/dev/null' EXIT

local_() { ( source tooling/wsl/ros_local_env.sh >/dev/null; exec python3 $R "$@" ); }

start local_ in  --connect "$HOST" --port $NAV_RELAY_TO_PC_PORT "${NAV_RELAY_TO_PC_PC_SIDE[@]}"
start local_ out --connect "$HOST" --port $NAV_RELAY_TO_ROBOT_PORT "${NAV_RELAY_TO_ROBOT[@]}"
start bash -c 'source tooling/wsl/ros_local_env.sh >/dev/null; exec python3 tooling/nav/map_odom_pub.py'
start bash -c 'source tooling/wsl/ros_local_env.sh >/dev/null; exec python3 tooling/nav/nav_cancel_bridge.py'
wait
