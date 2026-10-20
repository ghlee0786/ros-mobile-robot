#!/usr/bin/env bash
# Start SLAM or map-based navigation on the PC (WSL). The dashboard shows the map, costmaps, plan and takes
# goals, so RViz is optional: NAV_RVIZ=1 run_nav.sh ... also opens RViz.
#   run_nav.sh slam                     # build a new map (drive with teleop, or click Nav2 goals)
#   run_nav.sh localization <map.yaml>  # navigate on a saved map (default: newest map in maps/)
# The stack runs in the PC-local domain 8 (FastDDS); run_bridge.sh relays the robot's topics.
cd "$(dirname "$0")/../.."
REPO="$PWD"
MODE="${1:-slam}"
MAP="${2:-}"
if [ "$MODE" = "localization" ] && [ -z "$MAP" ]; then
  MAP=$(ls -t "$REPO"/maps/*.yaml 2>/dev/null | head -1)
  [ -n "$MAP" ] || { echo "저장된 지도가 없습니다 (maps/*.yaml). 먼저 slam 모드로 지도를 만드세요."; exit 1; }
fi
pkill -f 'split_relay.py|map_odom_pub.py|nav_cancel_bridge.py' 2>/dev/null
bash tooling/nav/run_bridge.sh > /tmp/robot_bridge.log 2>&1 &
BRIDGE=$!
trap 'kill $BRIDGE 2>/dev/null; pkill -f "split_relay.py|map_odom_pub.py|nav_cancel_bridge.py"' EXIT
source tooling/wsl/ros_local_env.sh
export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA QT_X11_NO_MITSHM=1 QT_QPA_PLATFORM=xcb   # WSLg RViz fixes
echo "[모드] $MODE ${MAP:+지도=$MAP} | RViz ${NAV_RVIZ:-0} | 로봇 중계 로그 /tmp/robot_bridge.log"
RVIZ=$([ "${NAV_RVIZ:-0}" = "1" ] && echo true || echo false)
ros2 launch tooling/nav/robot_nav.launch.py mode:="$MODE" rviz:="$RVIZ" ${MAP:+map:="$MAP"}
