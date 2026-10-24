@echo off
rem Start mapping (SLAM + Nav2). Watch and send goals in the dashboard; map_start.cmd rviz also opens RViz. Then map_save.cmd.
title Robot SLAM
set NAV_RVIZ=0
if /i "%1"=="rviz" set NAV_RVIZ=1
set WSLENV=NAV_RVIZ
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/nav/run_nav_logged.sh slam
pause
