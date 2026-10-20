# capture_window.ps1
#
# Grabs a bitmap of a running Rocky Kombat window so the visible-rendering claim
# can be checked against an actual frame rather than against the absence of a
# crash. PrintWindow with PW_RENDERFULLCONTENT (flag 2) is used deliberately:
# a plain desktop grab would capture whatever window happens to be on top,
# which on this machine is not necessarily the game.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools\capture_window.ps1 -OutFile C:\path\shot.png

param(
    [string]$ProcessName = "UnrealEditor-Cmd",
    [string]$OutFile = "C:\RockyKombat\Saved\Evidence\window_shot.png",
    [int]$TimeoutSeconds = 60
)

Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Runtime.InteropServices;

public class RkcWin {
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int GetWindowTextLength(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
Add-Type -TypeDefinition $sig

# Wait for the window to exist and be visible before trying to capture it.
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$target = $null
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } |
            Select-Object -First 1
    if ($proc) {
        $len = [RkcWin]::GetWindowTextLength($proc.MainWindowHandle)
        $title = ""
        if ($len -gt 0) {
            $sb = New-Object System.Text.StringBuilder ($len + 1)
            [void][RkcWin]::GetWindowText($proc.MainWindowHandle, $sb, $sb.Capacity)
            $title = $sb.ToString()
        }
        $target = [pscustomobject]@{
            Pid    = $proc.Id
            Handle = $proc.MainWindowHandle
            Title  = $title
        }
        break
    }
    Start-Sleep -Milliseconds 500
}

if (-not $target) {
    Write-Output "NO_WINDOW: no visible '$ProcessName' window appeared within $TimeoutSeconds s"
    exit 2
}

Write-Output ("WINDOW pid={0} handle={1} title='{2}'" -f $target.Pid, $target.Handle, $target.Title)

$rect = New-Object RkcWin+RECT
if (-not [RkcWin]::GetWindowRect($target.Handle, [ref]$rect)) {
    Write-Output "NO_RECT: GetWindowRect failed"
    exit 3
}

$width  = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
Write-Output ("SIZE {0}x{1}" -f $width, $height)

if ($width -le 0 -or $height -le 0) {
    Write-Output "BAD_SIZE: refusing to capture a zero-area window"
    exit 4
}

$bmp = New-Object System.Drawing.Bitmap $width, $height
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $gfx.GetHdc()

# Flag 2 = PW_RENDERFULLCONTENT, required for DirectX-composited windows.
$ok = [RkcWin]::PrintWindow($target.Handle, $hdc, 2)
$gfx.ReleaseHdc($hdc)
$gfx.Dispose()

if (-not $ok) {
    Write-Output "PRINTWINDOW_FAILED: PrintWindow returned false"
    $bmp.Dispose()
    exit 5
}

$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)

# A screenshot that is entirely one colour is a capture failure, not a black
# game screen, so report the colour spread as part of the evidence.
$distinct = New-Object 'System.Collections.Generic.HashSet[int]'
for ($y = 0; $y -lt $height; $y += 7) {
    for ($x = 0; $x -lt $width; $x += 7) {
        [void]$distinct.Add($bmp.GetPixel($x, $y).ToArgb())
    }
}
$bmp.Dispose()

Write-Output ("SAVED {0}" -f $OutFile)
Write-Output ("DISTINCT_SAMPLED_COLOURS {0}" -f $distinct.Count)
exit 0
