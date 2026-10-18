param([string]$Out, [int]$WaitSec = 15, [string]$Clicks = "", [int]$Height = 0)
# 대시보드를 화면 밖 좌표에 띄우고(사용자 화면을 가리지 않게) PrintWindow 로 창 내용만 캡처한 뒤 종료한다.
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\..\..\dashboard"
$env:PATH = "E:\Qt\6.8.3\mingw_64\bin;E:\Qt\Tools\mingw1310_64\bin;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = "1"
$log = "$PSScriptRoot\run.log"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 public struct R { public int L, T, Ri, B; } }
"@
[W]::SetProcessDPIAware() | Out-Null
$p = Start-Process -FilePath "$root\build\robot_dashboard.exe" -WorkingDirectory $root -PassThru -RedirectStandardError $log
$t0 = Get-Date
for ($i = 0; $i -lt 900 -and $p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh() }
$h = $p.MainWindowHandle
"창 뜨기까지 " + ((Get-Date) - $t0).TotalSeconds + " s"
if ($Height -gt 0) { [W]::SetWindowPos($h, [IntPtr]::Zero, -4000, 0, 1456, $Height, 0x0004 -bor 0x0010) | Out-Null }
else { [W]::SetWindowPos($h, [IntPtr]::Zero, -4000, 0, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null }   # NOSIZE|NOZORDER|NOACTIVATE
Start-Sleep -Seconds $WaitSec
foreach ($c in ($Clicks -split ';' | Where-Object { $_ })) {   # "x,y,대기s" 클릭 · "w:x,y,대기s" 아래로 휠
  if ($c.StartsWith('w:') -or $c.StartsWith('u:')) {   # u: 위로 휠(확대)
    $wd = if ($c.StartsWith('u:')) { 0x0078 } else { 0xFF88 }
    $x, $y, $d = $c.Substring(2) -split ','
    $r0 = New-Object W+R; [W]::GetWindowRect($h, [ref]$r0) | Out-Null
    $sx = ($r0.L + 8 + [int]$x) -band 0xFFFF; $sy = ($r0.T + 31 + [int]$y) -band 0xFFFF
    for ($k = 0; $k -lt 10; $k++) { [W]::PostMessage($h, 0x020A, [IntPtr](($wd -shl 16)), [IntPtr](($sy -shl 16) -bor $sx)) | Out-Null; Start-Sleep -Milliseconds 50 }
    Start-Sleep -Seconds ([double]$d); continue
  }
  if ($c -match '^(d|m):') {   # d: 왼쪽 끌기 · m: 가운데 끌기  "d:x1,y1,x2,y2,대기s"
    $dn, $up, $mk = if ($c.StartsWith('m:')) { 0x0207, 0x0208, 0x10 } else { 0x0201, 0x0202, 1 }
    $x1, $y1, $x2, $y2, $d = $c.Substring(2) -split ','
    [W]::PostMessage($h, $dn, [IntPtr]$mk, [IntPtr](([int]$y1 -shl 16) -bor [int]$x1)) | Out-Null
    for ($k = 1; $k -le 10; $k++) {
      $mx = [int]$x1 + ([int]$x2 - [int]$x1) * $k / 10; $my = [int]$y1 + ([int]$y2 - [int]$y1) * $k / 10
      [W]::PostMessage($h, 0x0200, [IntPtr]$mk, [IntPtr](([int]$my -shl 16) -bor [int]$mx)) | Out-Null; Start-Sleep -Milliseconds 40 }
    [W]::PostMessage($h, $up, [IntPtr]0, [IntPtr](([int]$y2 -shl 16) -bor [int]$x2)) | Out-Null
    Start-Sleep -Seconds ([double]$d); continue
  }
  if ($c.StartsWith('r:')) {   # 오른쪽 클릭
    $x, $y, $d = $c.Substring(2) -split ','
    $lp = [IntPtr](([int]$y -shl 16) -bor [int]$x)
    [W]::PostMessage($h, 0x0204, [IntPtr]2, $lp) | Out-Null; [W]::PostMessage($h, 0x0205, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Seconds ([double]$d); continue
  }
  $x, $y, $d = $c -split ','
  $lp = [IntPtr](([int]$y -shl 16) -bor [int]$x)
  [W]::PostMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null
  [W]::PostMessage($h, 0x0202, [IntPtr]0, $lp) | Out-Null
  Start-Sleep -Seconds ([double]$d)
}
$p.Refresh(); "exited=" + $p.HasExited + " handle=" + $p.MainWindowHandle + " was=" + $h; if ($p.MainWindowHandle -ne 0) { $h = $p.MainWindowHandle }
$r =New-Object W+R
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.Ri - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
$bmp.Save($Out)
Stop-Process -Id $p.Id
Start-Sleep -Milliseconds 500
Get-Content $log -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch 'Direct3D 12' } | Select-Object -First 60
