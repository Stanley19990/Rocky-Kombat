param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [int]$Count = 5,
    [int]$IntervalMs = 900
)
Add-Type -AssemblyName System.Drawing
$sig = @'
using System;
using System.Runtime.InteropServices;
public static class RkcPair
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT pt);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@
Add-Type -TypeDefinition $sig
$proc = Get-Process -Id $ProcessId -ErrorAction Stop
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { Write-Error "no window"; exit 1 }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }
for ($i = 1; $i -le $Count; $i++) {
    $rect = New-Object RkcPair+RECT
    [void][RkcPair]::GetClientRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
    $pt = New-Object RkcPair+POINT; $pt.X = 0; $pt.Y = 0
    [void][RkcPair]::ClientToScreen($hwnd, [ref]$pt)
    # A: PrintWindow (the method every earlier burst used)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    $ok = [RkcPair]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save((Join-Path $OutDir ("pw_{0:d3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    # B: the pixels actually on screen
    $bmp2 = New-Object System.Drawing.Bitmap $w, $h
    $g2 = [System.Drawing.Graphics]::FromImage($bmp2)
    $g2.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g2.Dispose()
    $bmp2.Save((Join-Path $OutDir ("sc_{0:d3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png); $bmp2.Dispose()
    "pair $i : ${w}x${h} printwindow=$ok"
    Start-Sleep -Milliseconds $IntervalMs
}
