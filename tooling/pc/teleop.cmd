@echo off
rem Robot keyboard teleop window (double-click to run). Help text inside is Korean.
chcp 65001 >nul
title Robot Teleop
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/wsl/run_teleop.sh
pause
