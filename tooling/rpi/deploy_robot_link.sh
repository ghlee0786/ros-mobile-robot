#!/usr/bin/env bash
# Deploy robot_link (ROS env, ping-pong responder, straight-drive controller, systemd units,
# eth0 link hook) from this repo to the Pi. Does not touch ~/robot_ws.
# Usage: tooling/rpi/deploy_robot_link.sh [ssh-host]   (default: rpi)
set -euo pipefail
HOST="${1:-rpi}"
SRC="$(cd "$(dirname "$0")/robot_link" && pwd)"
UNITS="robot-rosbridge robot-pingpong robot-cmd-watchdog robot-odom-tf robot-straight-drive robot-bringup"
FILES="ros_env.sh pingpong_node.py cmd_watchdog_node.py odom_tf_node.py speed_probe.py straight_drive_node.py 50-robot-link-relink"

ssh "$HOST" 'mkdir -p ~/robot_link/logs'
for f in $FILES; do tr -d '\r' < "$SRC/$f" | ssh "$HOST" "cat > ~/robot_link/$f"; done
for u in $UNITS; do tr -d '\r' < "$SRC/$u.service" | ssh "$HOST" "cat > ~/robot_link/$u.service"; done
ssh "$HOST" "UNITS='$UNITS' bash -s" <<'EOF'
set -e
chmod +x ~/robot_link/*.py ~/robot_link/50-robot-link-relink
for u in $UNITS; do sudo install -m 644 ~/robot_link/$u.service /etc/systemd/system/$u.service; done
# Rebind DDS when the cable is plugged in (routable) or pulled (no-carrier / off).
for d in routable.d no-carrier.d off.d; do
  sudo install -D -m 755 ~/robot_link/50-robot-link-relink /etc/networkd-dispatcher/$d/50-robot-link-relink
done
sudo systemctl daemon-reload
sudo systemctl enable $UNITS
sudo systemctl restart $UNITS
sleep 8
systemctl --no-pager --lines=0 status $UNITS | grep -E "●|Active:"
systemctl is-active networkd-dispatcher
EOF
