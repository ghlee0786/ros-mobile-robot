@echo off
rem Navigate on the newest saved map (AMCL + Nav2). Set the pose with the dashboard initial pose tool first. Add "rviz" as the last argument to open RViz.
title Robot Navigation
set NAV_RVIZ=0
if /i "%~2"=="rviz" set NAV_RVIZ=1
if /i "%~1"=="rviz" set NAV_RVIZ=1
set WSLENV=NAV_RVIZ
set MAPARG=%~1
if /i "%MAPARG%"=="rviz" set MAPARG=
wsl -d Ubuntu-ROS -- bash /mnt/e/workspace/cpp/Ros/tooling/nav/run_nav_logged.sh localization %MAPARG%
pause
