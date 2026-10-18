#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
source /opt/ros/jazzy/setup.bash
pkill -f odom_tf_bridge.py && echo "PC 임시 TF 보조 노드 종료"
python3 tooling/nav/make_nav2_params.py
f=tooling/nav/nav2_params_robot.yaml
grep -nE "cmd_vel_out_topic|plugin: nav2_regulated|desired_linear_vel|robot_radius|max_velocity|base_frame_id|robot_base_frame" "$f" | head -20
