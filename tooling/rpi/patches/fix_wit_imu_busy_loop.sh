#!/usr/bin/env bash
# Patch the robot's IMU driver (Pi ~/robot_ws/src/wit_ros2_imu, not a git repo) so its read loop blocks
# for the next byte instead of spinning when the serial buffer is empty: it held one Pi core at 100%,
# which starved rosbridge/DDS. After: ~22% CPU, /imu/data_raw still 100 Hz. Applied 2026-10-19.
# Run from the PC:  tooling/rpi/patches/fix_wit_imu_busy_loop.sh [ssh-host]   (default: rpi)
set -euo pipefail
HOST="${1:-rpi}"
ssh "$HOST" 'bash -s' <<'EOF'
set -e
f=~/robot_ws/src/wit_ros2_imu/wit_ros2_imu/wit_ros2_imu.py
grep -q "instead of spinning" "$f" && { echo "이미 적용됨"; exit 0; }
cp "$f" ~/robot_link/wit_ros2_imu.py.bak-$(date +%Y%m%d%H%M)
python3 - "$f" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """            else:
                if buff_count > 0:
                    buff_data = wt_imu.read(buff_count)
                    for i in range(0, buff_count):
                        tag = handle_serial_data(buff_data[i])
                        if tag:
                            self.imu_data()
"""
new = """            else:
                # Nothing buffered: block for one byte (serial timeout 0.5 s) instead of spinning,
                # which kept this thread at 100% of a CPU core.
                buff_data = wt_imu.read(buff_count if buff_count > 0 else 1)
                for i in range(0, len(buff_data)):
                    tag = handle_serial_data(buff_data[i])
                    if tag:
                        self.imu_data()
"""
assert s.count(old) == 1, "pattern not found"
open(p, "w", encoding="utf-8").write(s.replace(old, new))
PY
python3 -m py_compile "$f"
sudo systemctl restart robot-bringup
echo "적용 완료"
EOF
