#!/usr/bin/env bash
# Show the latest Nav2 decisions (goal, planner, controller, behaviors) from the session log.
grep -E "bt_navigator|controller_server|planner_server|behavior_server|collision_monitor" "/tmp/robot_nav_${1:-slam}.log" \
  | grep -vE "udp_conn_write|Message Filter|Timed out waiting for transform|lifecycle node launched|Waiting on external|design.ros2|^\[[a-z_0-9-]+\] *$" \
  | tail -"${2:-20}" | cut -c1-200
