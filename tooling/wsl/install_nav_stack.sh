#!/usr/bin/env bash
# Install SLAM + Nav2 for the PC side (WSL, ROS 2 Jazzy).  Run as root inside Ubuntu-ROS.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ros-jazzy-slam-toolbox ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
  ros-jazzy-nav2-regulated-pure-pursuit-controller ros-jazzy-nav2-rviz-plugins
printf "net.core.rmem_max=16777216
net.core.rmem_default=4194304
" > /etc/sysctl.d/60-ros-dds.conf   # DDS large-message buffers
sysctl --system >/dev/null
echo "==> done"
