@echo off
rem Save the current SLAM map to maps\ (name optional).
title Save Map
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/nav/save_map.sh %1
pause
