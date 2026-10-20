# rkc_keytest.ps1
#
# Fast check (about 60 s) that a key press actually moves the game's menu
# selection. Prints a hash of the game frame before and after the press: if the
# two hashes match, the input never reached the game and no conclusion may be
# drawn from a driven playthrough.
#
# Input is delivered as REAL key events (keybd_event) after forcing the game
# window to the foreground, because UE ignores synthetic WM_KEYDOWN messages
# posted to a window that does not have keyboard focus.
param(
    [string]$Exe = "C:\RockyKombat\Packaged\Windows\RockyKombat.exe",
    [string]$OutDir = "C:\RockyKombat\Saved\Evidence\KeyTest"
)
$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing
$sig = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class RkcKey {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static string FindGame() {
        string found = "";
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h)) return true;
            StringBuilder cn = new StringBuilder(300); GetClassName(h, cn, cn.Capacity);
            if (cn.ToString() != "UnrealWindow") return true;
            StringBuilder sb = new StringBuilder(400); GetWindowText(h, sb, sb.Capacity);
            if (sb.ToString().IndexOf("RockyKombat") < 0) return true;
            uint pid; GetWindowThreadProcessId(h, out pid);
            RECT r; GetWindowRect(h, out r);
            found = ((long)h).ToString() + "|" + pid + "|" + (r.Right-r.Left) + "x" + (r.Bottom-r.Top);
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static bool Force(IntPtr h) {
        ShowWindow(h, 5);
        uint pid; uint target = GetWindowThreadProcessId(h, out pid);
        uint cur = GetCurrentThreadId();
        AttachThreadInput(cur, target, true);
        bool ok = SetForegroundWindow(h);
        SetFocus(h);
        AttachThreadInput(cur, target, false);
        return ok;
    }

    public static void Key(byte vk, bool down) {
        byte scan = (byte)MapVirtualKey(vk, 0);
        uint flags = 0x0008;                 // KEYEVENTF_SCANCODE
        if (!down) flags |= 0x0002;          // KEYEVENTF_KEYUP
        keybd_event(vk, scan, flags, UIntPtr.Zero);
    }
}
'@
Add-Type -TypeDefinition $sig
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) `
     -ArgumentList @("-windowed","-ResX=1280","-ResY=720","-nosplash","-log") -PassThru
Write-Output "launcher pid $($p.Id)"

$hwnd = [IntPtr]::Zero; $gpid = 0
$deadline = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 700
    $f = [RkcKey]::FindGame()
    if ($f) { $parts = $f -split "\|"; $hwnd = [IntPtr][long]$parts[0]; $gpid = [int]$parts[1]; break }
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 2 }
Write-Output "game hwnd $hwnd pid $gpid"

function SnapHash($tag) {
    $r = New-Object RkcKey+RECT; [void][RkcKey]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b); $hdc = $g.GetHdc()
    [void][RkcKey]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $OutDir ($tag + ".png")
    $b.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $ms = New-Object System.IO.MemoryStream
    $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray(); $ms.Dispose(); $b.Dispose()
    $sha = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    $hex = -join ($sha | ForEach-Object { $_.ToString("x2") })
    # Write-Host keeps this line off the pipeline so the caller receives only $hex
    Write-Host ("{0}: {1}x{2} sha256 {3}" -f $tag, $w, $h, $hex.Substring(0, 16))
    return $hex
}

# pixel difference between two saved PNGs: (changed, total) sampled every 4 px
function DiffPngs($p1, $p2) {
    $i1 = [System.Drawing.Bitmap]::FromFile($p1)
    $i2 = [System.Drawing.Bitmap]::FromFile($p2)
    if ($i1.Width -ne $i2.Width -or $i1.Height -ne $i2.Height) { $i1.Dispose(); $i2.Dispose(); return "size-mismatch" }
    $changed = 0; $total = 0
    for ($y = 0; $y -lt $i1.Height; $y += 4) {
        for ($x = 0; $x -lt $i1.Width; $x += 4) {
            $c1 = $i1.GetPixel($x, $y); $c2 = $i2.GetPixel($x, $y)
            $diff = [math]::Abs($c1.R - $c2.R) + [math]::Abs($c1.G - $c2.G) + [math]::Abs($c1.B - $c2.B)
            if ($diff -gt 24) { $changed++ }
            $total++
        }
    }
    $i1.Dispose(); $i2.Dispose()
    $pct = [math]::Round(100.0 * $changed / [math]::Max(1, $total), 2)
    return ("{0} of {1} sampled px changed ({2}%)" -f $changed, $total, $pct)
}

Start-Sleep -Seconds 16
Write-Output ("foreground before: " + [RkcKey]::GetForegroundWindow())
Write-Output ("force -> " + [RkcKey]::Force($hwnd))
Start-Sleep -Milliseconds 500
$a = SnapHash "a_before_key"
$shaA = $a
Write-Host "hash A = $shaA"

# S = menu down. Real key events, foreground window forced first.
[void][RkcKey]::Force($hwnd)
Start-Sleep -Milliseconds 250
[RkcKey]::Key(0x53, $true)
Start-Sleep -Milliseconds 120
[RkcKey]::Key(0x53, $false)
Start-Sleep -Seconds 2
$b = SnapHash "b_after_key"
Write-Host "hash B = $b"
Write-Host ("pixel diff A->B: " + (DiffPngs (Join-Path $OutDir "a_before_key.png") (Join-Path $OutDir "b_after_key.png")))
if ($a -eq $b) { Write-Host "RESULT: INPUT NOT REACHING THE GAME (identical frames)" }
else { Write-Host "RESULT: the press changed the frame - input reaches the game" }

# a second press to see the selection move again
[RkcKey]::Force($hwnd)
Start-Sleep -Milliseconds 250
[RkcKey]::Key(0x53, $true); Start-Sleep -Milliseconds 120; [RkcKey]::Key(0x53, $false)
Start-Sleep -Seconds 2
$c = SnapHash "c_after_down2"
Write-Host ("pixel diff A->C: " + (DiffPngs (Join-Path $OutDir "a_before_key.png") (Join-Path $OutDir "c_after_down2.png")))
Write-Host ("pixel diff B->C: " + (DiffPngs (Join-Path $OutDir "b_after_key.png") (Join-Path $OutDir "c_after_down2.png")))

Stop-Process -Id $gpid -Force -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
taskkill /F /IM RockyKombat.exe 2>&1 | Out-Null
Write-Output "done"
exit 0
