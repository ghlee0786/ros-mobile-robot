# One-time PC setup that needs administrator rights (run elevated).
#  1) Host routes so traffic to the Pi always leaves through Wi-Fi. The wired NIC also carries
#     192.168.1.x static addresses for another device, so without these routes Windows can pick
#     the wrong interface and SSH/ROS traffic to the Pi times out intermittently.
#  2) Allow inbound traffic to WSL (mirrored networking) so ROS 2 DDS from the Pi reaches WSL.
param(
    [string[]]$PiAddresses = @('192.168.1.200', '192.168.1.201'),
    [string]$Interface = 'Wi-Fi',
    [string]$LogFile = "$env:TEMP\robot_pc_network_setup.log"
)
$ErrorActionPreference = 'Stop'
Start-Transcript -Path $LogFile -Force | Out-Null
try {
    foreach ($ip in $PiAddresses) {
        $prefix = "$ip/32"
        Get-NetRoute -DestinationPrefix $prefix -ErrorAction SilentlyContinue | Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue
        # Without -PolicyStore the route goes to both the active and the persistent store.
        New-NetRoute -DestinationPrefix $prefix -InterfaceAlias $Interface -NextHop 0.0.0.0 -RouteMetric 1 | Out-Null
        "route $prefix -> $Interface : OK"
    }
    # WSL VM creator id for the Hyper-V firewall.
    Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
    "Hyper-V firewall (WSL) inbound: Allow"
    'RESULT=OK'
} catch {
    "ERROR: $($_.Exception.Message)"
    'RESULT=FAIL'
} finally {
    Stop-Transcript | Out-Null
}
