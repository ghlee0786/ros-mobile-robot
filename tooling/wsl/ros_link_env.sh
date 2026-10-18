# ROS 2 environment for talking to the robot over DDS from WSL (source this file).
# Requires WSL mirrored networking (~/.wslconfig: networkingMode=mirrored).
source /opt/ros/jazzy/setup.bash

ROBOT_ETH="${ROBOT_ETH:-192.168.1.200}"
ROBOT_WLAN="${ROBOT_WLAN:-192.168.1.201}"
export ROS_DOMAIN_ID=7                       # same domain as the Pi (~/.bashrc there)
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp # the Pi runs CycloneDDS too
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET

# Bind DDS to whichever mirrored interface routes to the robot (the Wi-Fi NIC on this PC).
DDS_IF="$(ip route get "$ROBOT_ETH" 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p')"
export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${DDS_IF}\"/></Interfaces><AllowMulticast>spdp</AllowMulticast></General><Internal><SocketReceiveBufferSize min=\"8MB\"/></Internal><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>64</MaxAutoParticipantIndex><Peers><Peer Address=\"${ROBOT_ETH}\"/><Peer Address=\"${ROBOT_WLAN}\"/></Peers></Discovery></Domain></CycloneDDS>"
