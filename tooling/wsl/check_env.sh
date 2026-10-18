#!/usr/bin/env bash
# Quick health check of the Ubuntu-ROS distro (ROS 2, Gazebo, bridges, Gazebo MCP).
source /opt/ros/jazzy/setup.bash
echo "ROS_DISTRO=$ROS_DISTRO"
echo "gz sim: $(gz sim --versions 2>/dev/null)"
for p in desktop ros-gz rosbridge-suite foxglove-bridge; do
  dpkg -s "ros-jazzy-$p" >/dev/null 2>&1 && echo "ros-jazzy-$p: OK" || echo "ros-jazzy-$p: MISSING"
done
cd ~/mcp/gazebo-mcp && echo "gazebo-mcp: $(git log --oneline -1)" && ls src
