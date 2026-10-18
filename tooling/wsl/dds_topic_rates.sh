#!/usr/bin/env bash
# Measure robot topic rates over DDS from WSL, in the order given (default: a mixed order).
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
topics=("$@"); [ ${#topics[@]} -eq 0 ] && topics=(/sync/odom /odom /scan /sync/scan /imu/data_raw)
for t in "${topics[@]}"; do
  rate=$(timeout 15 ros2 topic hz "$t" 2>/dev/null | grep -m1 "average rate" | awk '{print $3}')
  if [ -n "$rate" ]; then echo "[센서] $t: ${rate} Hz"; else echo "[센서] $t: 수신 없음"; fi
done
