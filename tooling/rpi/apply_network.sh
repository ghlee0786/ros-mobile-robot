#!/usr/bin/env bash
# Configure the Pi's network so it is always reachable at fixed addresses on the ipTIME LAN.
#   eth0  : 192.168.1.200/24 (+ DHCP)    wlan0 : 192.168.1.201/24 (+ DHCP, SSID from WIFI_SSID)
# Run as root on the Pi with WIFI_SSID / WIFI_PASSWORD in the environment.
# Safety net: the previous config is restored automatically after REVERT_MIN minutes
# unless /root/netplan-keep is created (the caller does that once it reconnects).
set -euo pipefail

: "${WIFI_SSID:?WIFI_SSID required}"
: "${WIFI_PASSWORD:?WIFI_PASSWORD required}"
ETH_ADDR="${ETH_ADDR:-192.168.1.200/24}"
WLAN_ADDR="${WLAN_ADDR:-192.168.1.201/24}"
DNS="${DNS:-192.168.1.1, 8.8.8.8}"
REVERT_MIN="${REVERT_MIN:-4}"

BACKUP="/root/netplan-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
cp -a /etc/netplan/. "$BACKUP/"
rm -f /root/netplan-keep

# Watchdog: restore the old netplan unless the new one is confirmed in time.
nohup bash -c "sleep $((REVERT_MIN * 60)); [ -f /root/netplan-keep ] && exit 0; \
  rm -f /etc/netplan/*.yaml; cp -a '$BACKUP'/. /etc/netplan/; netplan apply" \
  >/root/netplan-revert.log 2>&1 &

# cloud-init would regenerate 50-cloud-init.yaml on reboot otherwise.
echo 'network: {config: disabled}' > /etc/cloud/cloud.cfg.d/99-disable-network-config.cfg

rm -f /etc/netplan/*.yaml
umask 077
cat > /etc/netplan/60-robot-net.yaml <<EOF
network:
  version: 2
  renderer: networkd
  ethernets:
    eth0:
      dhcp4: true
      dhcp4-overrides: {route-metric: 100}
      addresses: [${ETH_ADDR}]
      nameservers: {addresses: [${DNS}]}
      optional: true
  wifis:
    wlan0:
      dhcp4: true
      dhcp4-overrides: {route-metric: 600}
      addresses: [${WLAN_ADDR}]
      nameservers: {addresses: [${DNS}]}
      optional: true
      access-points:
        "${WIFI_SSID}":
          password: "${WIFI_PASSWORD}"
EOF
netplan generate
echo "backup=$BACKUP revert_in=${REVERT_MIN}min"
# Apply detached so the SSH session dropping does not interrupt it.
nohup bash -c 'sleep 1; netplan apply' >/root/netplan-apply.log 2>&1 &
