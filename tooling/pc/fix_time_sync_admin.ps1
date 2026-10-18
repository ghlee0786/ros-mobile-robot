# Enable Windows NTP time sync (run elevated). The PC clock was ~2.85 s ahead (source: Local CMOS
# Clock), which breaks ROS TF between the PC (SLAM/Nav2) and the Pi, whose clock is NTP-accurate.
$log = "$env:TEMP\robot_time_sync.log"
Start-Transcript -Path $log -Force | Out-Null
try {
    Set-Service w32time -StartupType Automatic
    if ((Get-Service w32time).Status -ne 'Running') { Start-Service w32time }
    w32tm /config /manualpeerlist:"time.windows.com,0x9 time.google.com,0x9" /syncfromflags:manual /reliable:no /update
    Restart-Service w32time
    Start-Sleep 2
    w32tm /resync /force
    w32tm /query /status
    'RESULT=OK'
} catch {
    "ERROR: $($_.Exception.Message)"
    'RESULT=FAIL'
} finally {
    Stop-Transcript | Out-Null
}
