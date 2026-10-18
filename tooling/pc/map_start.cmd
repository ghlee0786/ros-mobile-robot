@echo off
rem Start mapping (SLAM + Nav2 + RViz). Drive with teleop.cmd, then run map_save.cmd.
title Robot SLAM
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/nav/run_nav_logged.sh slam
pause
