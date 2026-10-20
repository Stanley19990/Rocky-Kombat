# rkc_blackwatch_capture.ps1
#
# Launches the packaged build windowed with both sides bot-driven and grabs a
# burst of frames from the live window, so an intermittent visual fault can be
# caught as a frame rather than reasoned about from a still.
#
# WHY THE WINDOW IS FOUND BY NAME, NOT BY LAUNCH PID: the packaged exe is
# observed to start as two processes with the same image name - a small stub and
# the real game - and the stub has no window. Tracking Start-Process's PID alone
# therefore waits forever on a handle that will never appear. The window is
# instead resolved from the process list, preferring the newest visible window
# created after launch.
#
# Frames land in -OutDir/<stamp> as frame_00001.png ... in capture order.

param(
    [string]$ExePath = "C:\RockyKombat\Packaged\Windows\RockyKombat.exe",
    [string]$ProcessName = "RockyKombat",
    [string]$OutDir = "C:\RockyKombat\Saved\Evidence\blackwatch",
    [int]$DurationSeconds = 120,
    [int]$IntervalMs = 400,
    [string]$ExtraArgs = "-rkc.BotSide=3 -rkc.FrameData=1 -rkc.Evidence=0",
    [int]$WindowWaitSeconds = 90,
    [switch]$KeepAlive
)

Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Runtime.InteropServices;

public class RkcBwWin {
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
Add-Type -TypeDefinition $sig

if (-not (Test-Path $ExePath)) { Write-Output "NO_EXE: $ExePath not found"; exit 2 }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runDir = Join-Path $OutDir $stamp
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

# Any earlier instance of this build would compete for the same window name and
# would make "the newest window" ambiguous, so clear them first.
Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("KILLING_STALE pid={0}" -f $_.Id)
    try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch {}
}
Start-Sleep -Seconds 2

$launchTime = Get-Date
$argList = @("-windowed", "-ResX=1280", "-ResY=720", "-WinX=10", "-WinY=10", "-nosplash") +
           ($ExtraArgs -split "\s+" | Where-Object { $_ })
Write-Output (("LAUNCH {0} {1}" -f $ExePath, ($argList -join " ")))
$proc = Start-Process -FilePath $ExePath -ArgumentList $argList -PassThru
if (-not $proc) { Write-Output "LAUNCH_FAILED"; exit 3 }
Write-Output ("LAUNCHER_PID {0}" -f $proc.Id)

# Resolve the real game window: any process with this image name that owns a
# visible window, preferring one started at or after our launch.
$deadline = (Get-Date).AddSeconds($WindowWaitSeconds)
$windowPid = 0
$handle = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
    $cands = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
               Where-Object { $_.MainWindowHandle -ne 0 -and [RkcBwWin]::IsWindowVisible($_.MainWindowHandle) })
    if ($cands.Count -gt 0) {
        $pick = $cands | Where-Object { $_.StartTime -ge $launchTime.AddSeconds(-5) } |
                Sort-Object StartTime -Descending | Select-Object -First 1
        if (-not $pick) { $pick = $cands | Sort-Object StartTime -Descending | Select-Object -First 1 }
        $windowPid = $pick.Id
        $handle = $pick.MainWindowHandle
        break
    }
    Start-Sleep -Milliseconds 500
}

if ($handle -eq [IntPtr]::Zero) {
    Write-Output "NO_WINDOW"
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch {} }
    exit 5
}
Write-Output ("WINDOW pid={0} handle={1} title='{2}'" -f $windowPid, $handle, (Get-Process -Id $windowPid).MainWindowTitle)

$rect = New-Object RkcBwWin+RECT
if (-not [RkcBwWin]::GetWindowRect($handle, [ref]$rect)) { Write-Output "NO_RECT"; exit 6 }
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
Write-Output ("SIZE {0}x{1}" -f $width, $height)
if ($width -le 0 -or $height -le 0) { Write-Output "BAD_SIZE"; exit 7 }

$end = (Get-Date).AddSeconds($DurationSeconds)
$index = 0
$darkFrames = 0
$flatFrames = 0

while ((Get-Date) -lt $end) {
    if (-not (Get-Process -Id $windowPid -ErrorAction SilentlyContinue)) {
        Write-Output "PROCESS_EXITED_MIDWAY"; break
    }

    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $gfx.GetHdc()
    $ok = [RkcBwWin]::PrintWindow($handle, $hdc, 2)
    $gfx.ReleaseHdc($hdc)
    $gfx.Dispose()

    if ($ok) {
        $index++
        $file = Join-Path $runDir ("frame_{0:d5}.png" -f $index)
        $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)

        $sum = 0.0; $count = 0; $minC = 255; $maxC = 0
        for ($y = 0; $y -lt $height; $y += 16) {
            for ($x = 0; $x -lt $width; $x += 16) {
                $c = $bmp.GetPixel($x, $y)
                $l = 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B
                $sum += $l; $count++
                if ($l -lt $minC) { $minC = $l }
                if ($l -gt $maxC) { $maxC = $l }
            }
        }
        $mean = if ($count -gt 0) { $sum / $count } else { 255 }
        if ($mean -lt 8.0) { $darkFrames++ }
        # A frame with no luminance spread at all is a capture artefact, not a
        # black game screen, and must not be counted as evidence of the fault.
        if (($maxC - $minC) -lt 6) { $flatFrames++ }
    }

    $bmp.Dispose()
    Start-Sleep -Milliseconds $IntervalMs
}

Write-Output ("FRAMES {0}" -f $index)
Write-Output ("NEAR_BLACK_FRAMES {0}" -f $darkFrames)
Write-Output ("FLAT_FRAMES {0}" -f $flatFrames)
Write-Output ("OUTDIR {0}" -f $runDir)

if (-not $KeepAlive) {
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch {} }
    Write-Output "GAME_STOPPED"
}
exit 0
