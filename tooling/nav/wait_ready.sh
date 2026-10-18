#!/usr/bin/env bash
# Wait until the PC nav session (slam mode) is ready and summarise its health from the log.
LOG=/tmp/robot_nav_${1:-slam}.log
for i in $(seq 1 30); do
  grep -q "Managed nodes are active" "$LOG" 2>/dev/null && { echo "Nav2 활성화: 약 $((i*4))초"; break; }
  sleep 4
done
echo "센서 등록: $(grep -c 'Registering sensor' "$LOG")"
echo "odom TF 없음 경고: $(grep -c 'Invalid frame ID "odom"' "$LOG")"
echo "큐 가득참으로 버린 메시지: $(grep -c 'queue is full' "$LOG")"
