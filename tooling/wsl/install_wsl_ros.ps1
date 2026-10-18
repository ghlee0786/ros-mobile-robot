# Import an Ubuntu 24.04 WSL distro onto E: and provision ROS 2 Jazzy + Gazebo Harmonic.
param(
    [string]$Distro = 'Ubuntu-ROS',
    [string]$InstallDir = 'E:\wsl\Ubuntu-ROS',
    [string]$CacheDir = 'E:\wsl\cache',
    [string]$LinuxUser = 'lsirikh'
)
$ErrorActionPreference = 'Stop'
$env:WSL_UTF8 = 1
$base = 'https://cloud-images.ubuntu.com/wsl/releases/24.04/current'
$file = 'ubuntu-noble-wsl-amd64-wsl.rootfs.tar.gz'
$tar = Join-Path $CacheDir $file

New-Item -ItemType Directory -Force $InstallDir, $CacheDir | Out-Null

if (-not ((wsl --list --quiet) -contains $Distro)) {
    if (-not (Test-Path $tar)) {
        Write-Host "Downloading $file"
        Invoke-WebRequest -UseBasicParsing "$base/$file" -OutFile $tar
    }
    $sums = Invoke-RestMethod "$base/SHA256SUMS"
    $expected = (($sums -split "`n" | Where-Object { $_ -match [regex]::Escape($file) }) -split '\s+')[0]
    $actual = (Get-FileHash $tar -Algorithm SHA256).Hash.ToLower()
    if ($expected -ne $actual) { throw "SHA256 mismatch for $file" }
    Write-Host "Importing $Distro into $InstallDir"
    wsl --import $Distro $InstallDir $tar --version 2
}

$script = (wsl -d $Distro -u root -- wslpath -a ($PSScriptRoot -replace '\\', '/')) + '/setup_ros2_jazzy.sh'
wsl -d $Distro -u root -- bash -c "tr -d '\r' < '$script' | LINUX_USER=$LinuxUser bash"
if ($LASTEXITCODE -ne 0) { throw "provisioning failed ($LASTEXITCODE)" }

# Restart so /etc/wsl.conf (systemd + default user) takes effect.
wsl --terminate $Distro
wsl -d $Distro -- bash -lc 'source /opt/ros/jazzy/setup.bash && echo ROS_DISTRO=$ROS_DISTRO && gz sim --versions && whoami'
