#!/usr/bin/env bash
# Open RViz2 on the PC (WSLg window) showing the robot's /scan and /odom over DDS.
# The odom->base_link TF comes from the Pi (robot-odom-tf service).
#   wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/wsl/run_rviz.sh
cd "$(dirname "$0")/../.."
source tooling/wsl/ros_link_env.sh
# WSLg on this hybrid-GPU laptop renders RViz garbled on the default adapter.
# RVIZ_GL=nvidia (default) pins Mesa's D3D12 driver to the NVIDIA GPU; RVIZ_GL=software uses llvmpipe.
case "${RVIZ_GL:-nvidia}" in
  nvidia)   export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA ;;
  software) export LIBGL_ALWAYS_SOFTWARE=1 ;;
esac
# Striped/garbled windows on WSLg come from Qt's X11 shared-memory path; disable it.
export QT_X11_NO_MITSHM=1 QT_QPA_PLATFORM=xcb
rviz2 -d tooling/wsl/robot_view.rviz
