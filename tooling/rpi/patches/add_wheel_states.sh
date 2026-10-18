#!/usr/bin/env bash
# Patch the robot's control_node (Pi ~/robot_ws, branch ver1.3) to publish wheel speeds:
#   /wheel_states  sensor_msgs/JointState  measured left/right wheel speed from the Pico [rad/s]
#   /wheel_cmd     sensor_msgs/JointState  setpoint from cmd_vel with the firmware's clamps [rad/s]
# The Pico already sends both wheel speeds in every state frame; control_node just never published them.
# Run from the PC:  tooling/rpi/patches/add_wheel_states.sh [ssh-host]   (default: rpi)
# Backs the file up to ~/robot_link/, commits on the Pi's current branch, restarts robot-bringup.
set -euo pipefail
HOST="${1:-rpi}"
ssh "$HOST" 'bash -s' <<'EOF'
set -e
cd ~/robot_ws/src/integrated_robot_control
if grep -q "wheel_state_publisher" integrated_robot_control/control_node.py; then echo "이미 적용됨"; exit 0; fi
git status --porcelain | grep -q . && { echo "작업 트리에 변경이 있어 중단합니다"; exit 1; }
cp integrated_robot_control/control_node.py ~/robot_link/control_node.py.bak-$(date +%Y%m%d%H%M)
python3 - <<'PY'
p = "integrated_robot_control/control_node.py"
s = open(p, encoding="utf-8").read()
def rep(old, new):
    global s
    assert s.count(old) == 1, f"not unique: {old[:40]!r}"
    s = s.replace(old, new)
rep("from nav_msgs.msg import Odometry, Path\n",
    "from nav_msgs.msg import Odometry, Path\nfrom sensor_msgs.msg import JointState\n")
rep("Linear = 0.8\nAngular = 0.2\n",
    "Linear = 0.8\nAngular = 0.2\n\n"
    "# Pico firmware (pwm-pico include/config.h): wheel geometry and command limits it clamps to.\n"
    "WHEEL_RADIUS = 0.050\nWHEEL_SEPARATION = 0.34\nMAX_LINEAR = 0.1\nMAX_ANGULAR = 0.25\n"
    "WHEEL_NAMES = ['left_wheel', 'right_wheel']\n")
rep("        self.odom_publisher = self.create_publisher(Odometry, '/odom', qos_profile)\n",
    "        self.odom_publisher = self.create_publisher(Odometry, '/odom', qos_profile)\n"
    "        # Wheel speeds for monitoring: measured (from the Pico) and the setpoint it is tracking, rad/s.\n"
    "        self.wheel_state_publisher = self.create_publisher(JointState, '/wheel_states', qos_profile)\n"
    "        self.wheel_cmd_publisher = self.create_publisher(JointState, '/wheel_cmd', qos_profile)\n")
rep("        self.odom_publisher.publish(odom_msg)\n",
    "        self.odom_publisher.publish(odom_msg)\n"
    "        self.publish_wheels(timestamp, robot_state)\n")
rep("    def receive_message(self) -> SerialStatus:\n",
    "    def publish_wheels(self, stamp, robot_state):\n"
    "        measured = JointState()\n"
    "        measured.header.stamp = stamp\n"
    "        measured.name = WHEEL_NAMES\n"
    "        measured.velocity = [float(robot_state.left_speed), float(robot_state.right_speed)]\n"
    "        self.wheel_state_publisher.publish(measured)\n"
    "        # Same unicycle -> wheel conversion and clamps as the firmware (robot.cpp setUnicycle).\n"
    "        v = max(-MAX_LINEAR, min(MAX_LINEAR, self.twist.linear.x))\n"
    "        w = max(-MAX_ANGULAR, min(MAX_ANGULAR, self.twist.angular.z))\n"
    "        target = JointState()\n"
    "        target.header.stamp = stamp\n"
    "        target.name = WHEEL_NAMES\n"
    "        target.velocity = [(2 * v - w * WHEEL_SEPARATION) / (2 * WHEEL_RADIUS),\n"
    "                           (2 * v + w * WHEEL_SEPARATION) / (2 * WHEEL_RADIUS)]\n"
    "        self.wheel_cmd_publisher.publish(target)\n\n"
    "    def receive_message(self) -> SerialStatus:\n")
open(p, "w", encoding="utf-8").write(s)
PY
python3 -m py_compile integrated_robot_control/control_node.py
grep -q "<exec_depend>sensor_msgs</exec_depend>" package.xml || \
  sed -i 's#<exec_depend>rclpy</exec_depend>#<exec_depend>rclpy</exec_depend>\n  <exec_depend>sensor_msgs</exec_depend>#' package.xml
git -c user.name="lsirikh" -c user.email="lsirikh@naver.com" commit -qam "feat: publish measured and target wheel speeds (/wheel_states, /wheel_cmd)

The Pico already reports both wheel speeds in every state frame; publish them as
sensor_msgs/JointState for monitoring, plus the setpoint derived from cmd_vel with
the firmware's clamps and kinematics.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git log --oneline -1
sudo systemctl restart robot-bringup
sleep 12
source ~/robot_link/ros_env.sh
for t in /wheel_states /wheel_cmd; do echo -n "$t: "; timeout 6 ros2 topic hz $t 2>/dev/null | grep -m1 average || echo "없음"; done
EOF
