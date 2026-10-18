#!/usr/bin/env bash
# Two-process large-message test on a PC-local ROS domain (default 8, loopback, CycloneDDS):
# a LaserScan-sized message (1800 ranges, ~14 KB) at 10 Hz for 8 s.  Expect ~70+ received.
cd "$(dirname "$0")/../.."
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-8} RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
export CYCLONEDDS_URI='<CycloneDDS><Domain Id="any"><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Internal><SocketReceiveBufferSize min="8MB"/></Internal><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>80</MaxAutoParticipantIndex><Peers><Peer Address="127.0.0.1"/></Peers></Discovery></Domain></CycloneDDS>'
python3 - <<'PY' &
import time, rclpy
from sensor_msgs.msg import LaserScan
rclpy.init(); n = rclpy.create_node('bigpub'); p = n.create_publisher(LaserScan, '/bigmsg_test', 10)
m = LaserScan(); m.ranges = [1.0] * 1800; m.intensities = [1.0] * 1800
end = time.time() + 11
while time.time() < end:
    p.publish(m); time.sleep(0.1)
PY
P=$!
python3 - <<'PY'
import time, rclpy
from sensor_msgs.msg import LaserScan
rclpy.init(); n = rclpy.create_node('bigsub'); got = []
n.create_subscription(LaserScan, '/bigmsg_test', got.append, 10)
end = time.time() + 10
while time.time() < end:
    rclpy.spin_once(n, timeout_sec=0.1)
print(f"다른 프로세스에서 받은 14 KB 메시지: {len(got)}개 (10 초, 10 Hz 발행)")
PY
wait $P
