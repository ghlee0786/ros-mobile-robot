# ROS 2 environment for the PC-local navigation stack (source this file).
# Domain 8, localhost only, FastDDS (shared memory): SLAM, Nav2, RViz and helpers talk here, and
# tooling/nav/run_bridge.sh relays the robot's topics in from domain 7 (CycloneDDS over Wi-Fi).
# Why not CycloneDDS here too: on WSL it lost large messages between local processes (14 KB scans to
# best-effort readers such as SLAM and the costmaps), and when ~20 Nav2/SLAM/RViz nodes talked to the
# Foxy robot directly, their reliable traffic to the Pi failed ("ddsi_udp_conn_write ... failed").
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=8
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset CYCLONEDDS_URI
