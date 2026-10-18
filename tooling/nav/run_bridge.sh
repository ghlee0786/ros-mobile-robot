#!/usr/bin/env bash
# Link the robot (domain 7: CycloneDDS over Wi-Fi) with the PC-local navigation stack
# (domain 8: FastDDS, localhost). Each direction is a pair of split_relay.py processes, one per
# domain/RMW, joined by localhost TCP (see split_relay.py for why). Runs until killed.
cd "$(dirname "$0")/../.."
R=tooling/nav/split_relay.py
pids=()
start() { "$@" & pids+=($!); }
trap 'kill "${pids[@]}" 2>/dev/null' EXIT

robot() { ( source tooling/wsl/ros_link_env.sh >/dev/null; exec python3 $R "$@" ); }
local_() { ( source tooling/wsl/ros_local_env.sh >/dev/null; exec python3 $R "$@" ); }

# robot -> PC: sensors, TF, dashboard click goals
TO_PC=(/scan:sensor_msgs/msg/LaserScan:best_effort
       /odom:nav_msgs/msg/Odometry
       /tf:tf2_msgs/msg/TFMessage
       /tf_static:tf2_msgs/msg/TFMessage:transient_local
       /goal_pose:geometry_msgs/msg/PoseStamped)
# PC -> robot: Nav2 velocity (the Pi's cmd_watchdog relays it), map and plan for the dashboard
TO_ROBOT=(/teleop/cmd_vel:geometry_msgs/msg/Twist
          /map:nav_msgs/msg/OccupancyGrid:transient_local
          /plan:nav_msgs/msg/Path)

start local_ in  --port 7701 "${TO_PC[@]}"
start robot  out --port 7701 "${TO_PC[@]}"
start robot  in  --port 7702 "${TO_ROBOT[@]}"
start local_ out --port 7702 "${TO_ROBOT[@]}"
wait
