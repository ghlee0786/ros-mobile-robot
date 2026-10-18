# Install Qt 6 (MinGW build) + MinGW 13.1 + CMake + Ninja to E:\Qt with aqtinstall (no Qt account).
# MinGW is used instead of MSVC because Qt's MSVC binaries are built with a newer MSVC than the
# VS 2022 17.5 toolset (14.35) installed here, and linking newer objects with an older toolset fails.
param(
    [string]$QtVersion = '6.8.3',   # LTS; 6.11+ moved to a per-arch repo layout aqtinstall 3.3 cannot read
    [string]$Out = 'E:\Qt',
    [string[]]$Mirrors = @('https://download.qt.io/', 'https://mirrors.ocf.berkeley.edu/qt/', 'https://qt.mirror.constant.com/', 'https://mirror.accum.se/mirror/qt.io/qtproject/')
)
$ErrorActionPreference = 'Stop'
$modules = 'qtquick3d', 'qtgraphs', 'qtwebsockets', 'qtshadertools', 'qtquicktimeline'

function Invoke-Aqt([string[]]$AqtArgs) {
    foreach ($m in $Mirrors) {
        Write-Host "aqt $($AqtArgs -join ' ')  (mirror $m)"
        & uvx --from aqtinstall aqt @AqtArgs -O $Out -b $m
        if ($LASTEXITCODE -eq 0) { return }
    }
    throw "aqt failed on all mirrors: $($AqtArgs -join ' ')"
}

Invoke-Aqt (@('install-qt', 'windows', 'desktop', $QtVersion, 'win64_mingw', '-m') + $modules)
Invoke-Aqt @('install-tool', 'windows', 'desktop', 'tools_mingw1310')
Invoke-Aqt @('install-tool', 'windows', 'desktop', 'tools_cmake')
Invoke-Aqt @('install-tool', 'windows', 'desktop', 'tools_ninja')
Get-ChildItem $Out | Select-Object Name
