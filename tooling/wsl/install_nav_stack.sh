#!/usr/bin/env bash
# Install SLAM + Nav2 for the PC side (WSL, ROS 2 Jazzy).  Run as root inside Ubuntu-ROS.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ros-jazzy-slam-toolbox ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
  ros-jazzy-nav2-regulated-pure-pursuit-controller ros-jazzy-nav2-rviz-plugins
echo "==> done"
