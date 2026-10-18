#!/usr/bin/env bash
# Report whether /map is being published (latched) and the robot's pose in the map frame.
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
timeout 15 ros2 topic echo --once --qos-durability transient_local --qos-reliability reliable /map nav_msgs/msg/OccupancyGrid --field info 2>/dev/null | grep -E "resolution|width|height" | tr '\n' ' '; echo
timeout 10 ros2 run tf2_ros tf2_echo map base_link 2>&1 | grep -m2 -E "Translation|Rotation"
