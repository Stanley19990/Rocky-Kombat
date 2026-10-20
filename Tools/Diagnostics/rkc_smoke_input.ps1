# rkc_smoke_input.ps1
# 60-second check that real key events reach the packaged game window at all,
# before committing to a full playthrough run. If the menu does not move, the
# driver is broken and no conclusion may be drawn from its screenshots.
param(
    [string]$Exe = "C:\RockyKombat\Packaged\Windows\RockyKombat.exe",
    [string]$OutDir = "C:\RockyKombat\Saved\Evidence\SmokeInput"
)
$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing
$sig = @'
using System;
using System.Runtime.InteropServices;
public class RkcSmoke {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
Add-Type -TypeDefinition $sig
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$dir = Split-Path -Parent $Exe
$p = Start-Process -FilePath $Exe -WorkingDirectory $dir `
      -ArgumentList @("-windowed","-ResX=1280","-ResY=720","-nosplash","-log") -PassThru
Write-Output "pid $($p.Id)"

$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $deadline) {
    $g = Get-Process -Name RockyKombat -ErrorAction SilentlyContinue |
         Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($g) { $hwnd = $g.MainWindowHandle; break }
    Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 2 }
Write-Output "window $hwnd"

function Shot($n) {
    $r = New-Object RkcSmoke+RECT
    [void][RkcSmoke]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b); $hdc = $g.GetHdc()
    $ok = [RkcSmoke]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $OutDir ($n + ".png")
    if ($ok) {
        $b.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        # a 64x36 thumbnail digest so the message can say whether the picture moved
        $sum = 0.0; $n2 = 0
        for ($y = 0; $y -lt $h; $y += 16) { for ($x = 0; $x -lt $w; $x += 16) {
            $c = $b.GetPixel($x,$y); $sum += ($c.R + $c.G + $c.B)/3.0; $n2++ } }
        Write-Output ("SHOT {0} {1}x{2} mean {3:N1}" -f $n, $w, $h, ($sum/$n2))
    } else { Write-Output "SHOT $n FAILED" }
    $b.Dispose()
}
function Press($vk) {
    [void][RkcSmoke]::ShowWindow($hwnd, 5)
    [void][RkcSmoke]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 150
    [RkcSmoke]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [RkcSmoke]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
}

Start-Sleep -Seconds 14
Shot "s1_boot"
Press 0x0D       # ENTER
Start-Sleep -Seconds 3
Shot "s2_after_enter"
Press 0x0D
Start-Sleep -Seconds 3
Shot "s3_after_enter2"
Press 0x53       # S = menu down
Start-Sleep -Milliseconds 600
Shot "s4_after_down"

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Output "done; exit 0"
exit 0
