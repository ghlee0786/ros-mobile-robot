#!/usr/bin/env bash
# Provision the Ubuntu-ROS WSL distro: ROS 2 Jazzy desktop (RViz2), Gazebo Harmonic,
# rosbridge, foxglove_bridge and the Gazebo MCP server. Run as root inside WSL.
set -euo pipefail

LINUX_USER="${LINUX_USER:-lsirikh}"
export DEBIAN_FRONTEND=noninteractive
echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries

# Mirrors occasionally serve mid-sync indexes (hash sum mismatch); retry until clean.
apt_update() {
  for i in 1 2 3 4 5; do
    if apt-get update 2>&1 | tee /tmp/apt-update.log && ! grep -qE '^(E|W): (Failed|Some index)' /tmp/apt-update.log; then
      return 0
    fi
    echo "apt-get update attempt ${i} failed; retrying in 30s"
    rm -rf /var/lib/apt/lists/partial/*
    sleep 30
  done
  return 1
}

echo "==> [1/7] base packages + locale"
apt_update
apt-get install -y locales curl gnupg lsb-release ca-certificates software-properties-common \
  sudo git openssh-client build-essential cmake python3-pip python3-venv pipx \
  mesa-utils usbutils net-tools iputils-ping
locale-gen en_US en_US.UTF-8
update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8

echo "==> [2/7] user ${LINUX_USER}"
if ! id "${LINUX_USER}" >/dev/null 2>&1; then
  useradd -m -s /bin/bash -G sudo,dialout,video "${LINUX_USER}"
fi
echo "${LINUX_USER} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/${LINUX_USER}"
chmod 0440 "/etc/sudoers.d/${LINUX_USER}"
cat > /etc/wsl.conf <<EOF
[boot]
systemd=true
[user]
default=${LINUX_USER}
EOF

echo "==> [3/7] ROS 2 apt source"
add-apt-repository -y -n universe
ROS_APT_SOURCE_VERSION=$(curl -fsSL https://api.github.com/repos/ros-infrastructure/ros-apt-source/releases/latest | grep -F '"tag_name"' | awk -F'"' '{print $4}')
. /etc/os-release
curl -fsSL -o /tmp/ros2-apt-source.deb \
  "https://github.com/ros-infrastructure/ros-apt-source/releases/download/${ROS_APT_SOURCE_VERSION}/ros2-apt-source_${ROS_APT_SOURCE_VERSION}.${UBUNTU_CODENAME:-${VERSION_CODENAME}}_all.deb"
dpkg -i /tmp/ros2-apt-source.deb
apt_update
apt-get upgrade -y

echo "==> [4/7] ROS 2 Jazzy desktop + Gazebo Harmonic + bridges"
apt-get install -y \
  ros-jazzy-desktop ros-dev-tools \
  ros-jazzy-ros-gz \
  ros-jazzy-rosbridge-suite ros-jazzy-foxglove-bridge \
  ros-jazzy-rmw-cyclonedds-cpp \
  ros-jazzy-xacro ros-jazzy-robot-state-publisher ros-jazzy-joint-state-publisher-gui \
  ros-jazzy-teleop-twist-keyboard ros-jazzy-rqt-graph

echo "==> [5/7] rosdep"
[ -f /etc/ros/rosdep/sources.list.d/20-default.list ] || rosdep init
sudo -u "${LINUX_USER}" rosdep update

echo "==> [6/7] shell environment"
BASHRC="/home/${LINUX_USER}/.bashrc"
grep -q '/opt/ros/jazzy/setup.bash' "${BASHRC}" || cat >> "${BASHRC}" <<'EOF'

# --- ROS 2 Jazzy ---
source /opt/ros/jazzy/setup.bash
[ -f "$HOME/ros_ws/install/setup.bash" ] && source "$HOME/ros_ws/install/setup.bash"
export ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-0}
EOF
sudo -u "${LINUX_USER}" mkdir -p "/home/${LINUX_USER}/ros_ws/src" "/home/${LINUX_USER}/mcp"

echo "==> [7/7] Gazebo MCP server"
sudo -u "${LINUX_USER}" bash -s <<'EOF'
set -euo pipefail
cd "$HOME/mcp"
[ -d gazebo-mcp ] || git clone https://github.com/kvgork/gazebo-mcp.git
cd gazebo-mcp
git pull --ff-only || true
python3 -m venv --system-site-packages .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt
EOF

echo "==> done"
