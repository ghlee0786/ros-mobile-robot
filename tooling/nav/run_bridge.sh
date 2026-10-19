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

# robot -> PC: sensors, TF, dashboard click goals and initial pose. Both sides list the same topics in
# the same order (the frame carries the index), so a topic can arrive under another name: the dashboard
# sends its initial pose on /dashboard/initialpose because the Pi's initialpose_node publishes
# /initialpose = (0, 0, 0) every 5 s, which would keep resetting AMCL if it were relayed.
TO_PC=(/scan:sensor_msgs/msg/LaserScan:best_effort
       /odom:nav_msgs/msg/Odometry
       /tf:tf2_msgs/msg/TFMessage
       /tf_static:tf2_msgs/msg/TFMessage:transient_local
       /goal_pose:geometry_msgs/msg/PoseStamped)
TO_PC_ROBOT_SIDE=("${TO_PC[@]}" /dashboard/initialpose:geometry_msgs/msg/PoseWithCovarianceStamped)
TO_PC_PC_SIDE=("${TO_PC[@]}" /initialpose:geometry_msgs/msg/PoseWithCovarianceStamped)
# PC -> robot: Nav2 velocity (the Pi's cmd_watchdog relays it); map, plan, localization and the local
# costmap for the dashboard
TO_ROBOT=(/teleop/cmd_vel:geometry_msgs/msg/Twist
          /map:nav_msgs/msg/OccupancyGrid:transient_local
          /plan:nav_msgs/msg/Path
          /amcl_pose:geometry_msgs/msg/PoseWithCovarianceStamped:transient_local
          /local_costmap/costmap:nav_msgs/msg/OccupancyGrid
          /local_costmap/published_footprint:geometry_msgs/msg/PolygonStamped)

start local_ in  --port 7701 "${TO_PC_PC_SIDE[@]}"
start robot  out --port 7701 "${TO_PC_ROBOT_SIDE[@]}"
start robot  in  --port 7702 "${TO_ROBOT[@]}"
start local_ out --port 7702 "${TO_ROBOT[@]}"
wait
