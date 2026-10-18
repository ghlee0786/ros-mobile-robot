#!/usr/bin/env bash
# Start SLAM or map-based navigation on the PC (WSL), with RViz.
#   run_nav.sh slam                     # build a new map (drive with teleop, or click Nav2 goals)
#   run_nav.sh localization <map.yaml>  # navigate on a saved map (default: newest map in maps/)
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA QT_X11_NO_MITSHM=1 QT_QPA_PLATFORM=xcb   # WSLg RViz fixes
MODE="${1:-slam}"
MAP="${2:-}"
if [ "$MODE" = "localization" ] && [ -z "$MAP" ]; then
  MAP=$(ls -t "$PWD"/maps/*.yaml 2>/dev/null | head -1)
  [ -n "$MAP" ] || { echo "저장된 지도가 없습니다 (maps/*.yaml). 먼저 slam 모드로 지도를 만드세요."; exit 1; }
fi
echo "[모드] $MODE ${MAP:+지도=$MAP}"
exec ros2 launch tooling/nav/robot_nav.launch.py mode:="$MODE" ${MAP:+map:="$MAP"}
