# ROS 2 environment shared by the robot_link systemd services (sourced, not executed).
# Mirrors the interactive ~/.bashrc setup so services see the same graph as manual launches.
source /opt/ros/foxy/setup.bash
source /home/ubuntu/robot_ws/install/local_setup.bash

export ROS_DOMAIN_ID=7
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export PYTHONUNBUFFERED=1
export RCUTILS_CONSOLE_OUTPUT_FORMAT='[{severity}] [{name}]: {message}'

# CycloneDDS 0.7 (Foxy) binds a single interface: prefer wired, fall back to Wi-Fi.
if [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" = "1" ]; then DDS_IF=eth0; else DDS_IF=wlan0; fi
# AllowMulticast=spdp: discovery may use multicast, but user data always goes unicast. With
# multicast data, topics that also have local readers (e.g. /odom) never reached the PC over Wi-Fi.
# ParticipantIndex=auto puts every node on a well-known unicast port, so the PC (which lists this
# Pi as a peer) can discover all nodes directly instead of relying on lossy Wi-Fi multicast.
export CYCLONEDDS_URI="<CycloneDDS><Domain><General><NetworkInterfaceAddress>${DDS_IF}</NetworkInterfaceAddress><AllowMulticast>spdp</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>40</MaxAutoParticipantIndex></Discovery></Domain></CycloneDDS>"
