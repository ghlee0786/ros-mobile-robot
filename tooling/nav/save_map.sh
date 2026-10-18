#!/usr/bin/env bash
# Save the map being built by slam_toolbox to maps/<name>.{yaml,pgm} (+ a posegraph to resume later).
#   save_map.sh [name]     (default: map_YYYYmmdd_HHMM)
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
NAME="${1:-map_$(date +%Y%m%d_%H%M)}"
mkdir -p maps
ros2 run nav2_map_server map_saver_cli -f "maps/$NAME" --ros-args -p save_map_timeout:=10.0 \
  && echo "[저장] maps/$NAME.yaml, maps/$NAME.pgm"
ros2 service call /slam_toolbox/serialize_map slam_toolbox/srv/SerializePoseGraph "{filename: '$PWD/maps/$NAME'}" >/dev/null \
  && echo "[저장] maps/$NAME.posegraph (지도 이어 그리기용)"
