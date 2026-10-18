#!/usr/bin/env bash
# Offline check of cmd_watchdog_node on an isolated domain: relay while fed, stop when input stops.
cd "$(dirname "$0")/../.."
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=42 ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}
python3 -m py_compile tooling/wsl/teleop_keyboard.py tooling/rpi/robot_link/cmd_watchdog_node.py && echo "[문법] OK"
python3 tooling/rpi/robot_link/cmd_watchdog_node.py >/tmp/wd.log 2>&1 &
WD=$!
trap 'kill $WD 2>/dev/null' EXIT
python3 - <<'EOF'
import time, rclpy
from geometry_msgs.msg import Twist
rclpy.init(); n = rclpy.create_node('wd_check')
got = []
n.create_subscription(Twist, '/cmd_vel', lambda m: got.append((time.monotonic(), m.linear.x)), 10)
pub = n.create_publisher(Twist, '/teleop/cmd_vel', 10)
def spin(sec, feed=None):
    end = time.monotonic() + sec
    nxt = 0
    while time.monotonic() < end:
        if feed is not None and time.monotonic() >= nxt:
            t = Twist(); t.linear.x = feed; pub.publish(t); nxt = time.monotonic() + 0.1
        rclpy.spin_once(n, timeout_sec=0.01)
end = time.monotonic() + 15
while time.monotonic() < end and (pub.get_subscription_count() == 0 or n.count_publishers("/cmd_vel") == 0):
    rclpy.spin_once(n, timeout_sec=0.1)
print(f"[연결] 워치독 구독 {pub.get_subscription_count()}개")
spin(1)
t_feed = time.monotonic(); spin(2.0, feed=0.05)   # 2 s of teleop at 0.05 m/s
t_cut = time.monotonic(); spin(2.0)               # teleop dies (no messages)
moving = [v for t, v in got if t_feed < t < t_cut and v > 0]
after = [(t - t_cut, v) for t, v in got if t > t_cut]
nonzero_after = [d for d, v in after if v != 0]
first_zero = next((d for d, v in after if v == 0), None)
print(f"[중계] 조종 중 /cmd_vel {len(moving)}건 (0.05 m/s)")
print(f"[끊김] 마지막 비0 명령 {max(nonzero_after) if nonzero_after else 0:.2f}s 후, 정지 명령 {first_zero:.2f}s 후" if first_zero else "[끊김] 정지 명령 없음!")
print(f"[대기] 이후 계속 0 을 보내는가: {len([1 for d, v in after if d > 1.0])}건 (0 이어야 다른 노드와 안 싸움)")
EOF
grep -i warn /tmp/wd.log | head -2
