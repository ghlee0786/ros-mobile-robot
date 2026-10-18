@echo off
rem Navigate on the newest saved map (AMCL + Nav2 + RViz). Set the pose with 2D Pose Estimate first.
title Robot Navigation
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/nav/run_nav_logged.sh localization %1
pause
