# rkc_realplay.ps1
#
# Plays the SHIPPED build the way a person plays it: the real packaged
# executable, the real front end, real keyboard input, no self-test, no
# autopilot, no -rkc.* switch that changes a menu choice. The only switches it
# adds are diagnostic ones that write a log (rkc.LogPositions) and the normal
# evidence record.
#
# Why this exists: the user's observations ("selecting Shadow still gives the
# human Rocky", "the CPU does not move or attack") are claims about a HUMAN
# session. Automated self-test summaries cannot confirm or refute them, because
# the self-test drives the simulation directly and never walks the menu. This
# script walks the menu with real key events and photographs the result.
#
# Two details this script gets right, both learned the hard way:
#   * a packaged build launched with -log opens a SECOND top-level window (the
#     log console) whose title is the exe path. Capturing "the main window"
#     photographs the console, and keys sent to it never reach the game. The
#     game viewport is picked by title.
#   * keys are posted as WM_KEYDOWN/WM_KEYUP with a real scan code, so they
#     arrive through the game window's own message queue without depending on
#     which window the desktop considers focused.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools\Diagnostics\rkc_realplay.ps1 `
#       -Exe C:\RockyKombat\Packaged\Windows\RockyKombat.exe `
#       -OutDir C:\RockyKombat\Saved\Evidence\RealPlay_20261020

param(
    [string]$Exe = "C:\RockyKombat\Packaged\Windows\RockyKombat.exe",
    [string]$OutDir = "C:\RockyKombat\Saved\Evidence\RealPlay",
    [string]$ExtraArgs = "-rkc.LogPositions=1 -rkc.Evidence=1",
    [int]$MatchWaitSeconds = 260,
    [int]$ShotEverySeconds = 15
)

$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class RkcPlay {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    // The packaged layout is Windows/RockyKombat.exe (a small launcher) plus
    // Windows/RockyKombat/Binaries/Win64/RockyKombat.exe (the game). The launcher
    // exits after spawning the game, so a window must be looked for by CLASS
    // across every RockyKombat process, not by the launcher's pid.
    public static string FindGameWindow() {
        string found = "";
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h)) { return true; }
            StringBuilder cn = new StringBuilder(300);
            GetClassName(h, cn, cn.Capacity);
            if (cn.ToString() != "UnrealWindow") { return true; }
            StringBuilder sb = new StringBuilder(400);
            GetWindowText(h, sb, sb.Capacity);
            if (sb.ToString().IndexOf("RockyKombat") < 0) { return true; }
            uint pid; GetWindowThreadProcessId(h, out pid);
            RECT r; GetWindowRect(h, out r);
            found = ((long)h).ToString() + "|" + pid + "|" + (r.Right - r.Left) + "x" + (r.Bottom - r.Top) + "|" + sb.ToString();
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static List<string> WindowsFor(uint wantPid) {
        List<string> found = new List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == wantPid && IsWindowVisible(h)) {
                int len = GetWindowTextLength(h);
                StringBuilder sb = new StringBuilder(len + 400);
                GetWindowText(h, sb, sb.Capacity);
                RECT r; GetWindowRect(h, out r);
                found.Add(((long)h).ToString() + "|" + (r.Right - r.Left) + "x" + (r.Bottom - r.Top) + "|" + sb.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
Add-Type -TypeDefinition $sig

$VK = @{
    ENTER = 0x0D; ESC = 0x1B; UP = 0x26; DOWN = 0x28
    W = 0x57; A = 0x41; S = 0x53; D = 0x44
    Z = 0x5A; X = 0x58; C = 0x43; R = 0x52; P = 0x50; F1 = 0x70; F10 = 0x79
}
$WM_KEYDOWN = 0x0100
$WM_KEYUP = 0x0101
$WM_SYSKEYDOWN = 0x0104
$WM_SYSKEYUP = 0x0105
$EXTENDED = @(0x26, 0x28, 0x25, 0x27)   # arrows

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$timeline = New-Object System.Collections.Generic.List[string]
$t0 = Get-Date
function Stamp($msg) {
    $t = ((Get-Date) - $t0).TotalSeconds
    $line = ("[{0,7:N1}s] {1}" -f $t, $msg)
    Write-Output $line
    $timeline.Add($line)
}

# ---------------------------------------------------------------- launch
$dir = Split-Path -Parent $Exe
Stamp ("launch: $Exe  (cwd $dir)  extra '$ExtraArgs'")
$argList = @("-windowed", "-ResX=1280", "-ResY=720", "-nosplash", "-log") +
           ($ExtraArgs -split "\s+" | Where-Object { $_ })
$proc = Start-Process -FilePath $Exe -WorkingDirectory $dir -ArgumentList $argList -PassThru
Stamp ("pid {0}" -f $proc.Id)

$hwnd = [IntPtr]::Zero
$gamePid = 0
$deadline = (Get-Date).AddSeconds(180)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 700
    $found = [RkcPlay]::FindGameWindow()
    if ($found) {
        $parts = $found -split "\|"
        $hwnd = [IntPtr][long]$parts[0]
        $gamePid = [int]$parts[1]
        Stamp ("game viewport: {0} {1} pid {2} title '{3}'" -f $parts[0], $parts[2], $parts[1], $parts[3])
        break
    }
}
if ($hwnd -eq [IntPtr]::Zero) {
    Stamp "NO_WINDOW: no game viewport window appeared"
    Stamp ("windows seen: " + ([string]::Join(" ;; ", [RkcPlay]::WindowsFor([uint32]$proc.Id))))
    taskkill /F /IM RockyKombat.exe | Out-Null
    exit 2
}

function Get-Rect { $rr = New-Object RkcPlay+RECT; [void][RkcPlay]::GetWindowRect($hwnd, [ref]$rr); return $rr }

function Shot($name) {
    $r = Get-Rect
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { Stamp "shot $name FAILED (zero area)"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $gfx.GetHdc()
    $ok = [RkcPlay]::PrintWindow($hwnd, $hdc, 2)
    $gfx.ReleaseHdc($hdc); $gfx.Dispose()
    $path = Join-Path $OutDir ("{0}.png" -f $name)
    if ($ok) {
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        # mean luminance of the whole frame: a "dark obstruction" claim has to be
        # measurable, not remembered
        $sum = 0.0; $n = 0; $dark = 0
        for ($y = 0; $y -lt $h; $y += 8) {
            for ($x = 0; $x -lt $w; $x += 8) {
                $c = $bmp.GetPixel($x, $y)
                $l = ($c.R + $c.G + $c.B) / 3.0
                $sum += $l; $n++
                if ($l -lt 16) { $dark++ }
            }
        }
        $mean = [math]::Round($sum / [math]::Max(1, $n), 1)
        $pctDark = [math]::Round(100.0 * $dark / [math]::Max(1, $n), 2)
        Stamp ("shot {0}  {1}x{2}  mean-luma {3}  near-black {4}%" -f $name, $w, $h, $mean, $pctDark)
    } else { Stamp "shot $name FAILED (PrintWindow)" }
    $bmp.Dispose()
}

function SendKey($vk, $down) {
    $scan = [RkcPlay]::MapVirtualKey([uint32]$vk, 0)
    $ext = if ($EXTENDED -contains $vk) { 1 } else { 0 }
    $lparam = 1 -bor ($scan -shl 16) -bor ($ext -shl 24)
    if (-not $down) { $lparam = $lparam -bor (1 -shl 30) -bor (1 -shl 31) }
    else { $lparam = $lparam -bor (1 -shl 30) }
    $msg = if ($ext -eq 1) { if ($down) { $WM_SYSKEYDOWN } else { $WM_SYSKEYUP } }
           else { if ($down) { $WM_KEYDOWN } else { $WM_KEYUP } }
    [void][RkcPlay]::PostMessage($hwnd, [uint32]$msg, [IntPtr][long]$vk, [IntPtr][long]$lparam)
}

function Press($key, $holdMs = 80, $settleMs = 300) {
    $vk = $VK[$key]
    [void][RkcPlay]::SetForegroundWindow($hwnd)
    SendKey $vk $true
    Start-Sleep -Milliseconds $holdMs
    SendKey $vk $false
    Start-Sleep -Milliseconds $settleMs
}

function Hold($key, $ms) {
    $vk = $VK[$key]
    [void][RkcPlay]::SetForegroundWindow($hwnd)
    SendKey $vk $true
    Start-Sleep -Milliseconds $ms
    SendKey $vk $false
    Start-Sleep -Milliseconds 120
}

# ---------------------------------------------------------------- front end
Start-Sleep -Seconds 14
Shot "01_boot_or_title"

# Boot and Title auto-advance; ENTER only shortens them. On the Menu it starts
# PLAY, which is the flow under test.
Press "ENTER"; Start-Sleep -Seconds 3
Shot "02_after_enter"

Press "ENTER"; Start-Sleep -Seconds 3      # experience: ROCKY BOXING ARENA
Shot "03_arrangement"

Press "ENTER"; Start-Sleep -Seconds 3      # arrangement: PLAY AGAINST COMPUTER
Shot "04_charselect_stage0"

# Pick SHADOW for the human (P1). Row 1 is SHADOW.
Press "DOWN"; Start-Sleep -Milliseconds 500
Shot "05_shadow_highlighted_p1"
Press "ENTER"; Start-Sleep -Seconds 3
Shot "06_charselect_stage1"

# Pick ROCKY for the CPU (P2): go to the top row.
Press "UP"; Start-Sleep -Milliseconds 500
Press "ENTER"; Start-Sleep -Seconds 3
Shot "07_difficulty"

# Difficulty: force the top row, then CHAMPION (the most active CPU).
Press "UP"; Press "UP"; Press "UP"
Shot "08_difficulty_top_row"
Press "DOWN"; Press "DOWN"
Shot "09_difficulty_champion"
Press "ENTER"; Start-Sleep -Seconds 3
Shot "10_confirm"

Press "ENTER"; Start-Sleep -Seconds 6
Shot "11_loading_or_versus"

# Loading then Versus then the match. Do nothing at all for the first stretch:
# with no human input the human fighter must not attack, and the CPU must still
# move and attack.
Start-Sleep -Seconds 30
Shot "12_match_no_human_input"

# ---- human walk -----------------------------------------------------------
Hold "D" 1500; Start-Sleep -Milliseconds 300
Shot "13_walk_forward"
Hold "A" 1200
Shot "14_walk_back"

# ---- human jab / heavy at range ------------------------------------------
Press "Z"; Start-Sleep -Milliseconds 800
Shot "15_jab"
Press "X"; Start-Sleep -Milliseconds 1000
Shot "16_heavy"

# ---- guard and evade ------------------------------------------------------
Hold "S" 1200; Start-Sleep -Milliseconds 200
Shot "17_block"
Press "C"; Start-Sleep -Milliseconds 600
Shot "18_dodge"

# ---- close range: walk in and keep punching -------------------------------
Hold "D" 2500
for ($i = 0; $i -lt 5; $i++) { Press "Z" 80 200 }
Shot "19_close_range_punching"

# ---- ride out the rest of the match --------------------------------------
$left = $MatchWaitSeconds
$n = 0
while ($left -gt 0) {
    $s = [math]::Min($ShotEverySeconds, $left)
    Start-Sleep -Seconds $s
    $left -= $s
    $n++
    Shot ("20_match_t{0:d2}" -f $n)
}
Shot "21_final_or_results"
Start-Sleep -Seconds 4
Shot "22_after_settle"

# ---------------------------------------------------------------- shutdown
# The packaged layout means the process that owns the window is the game itself,
# not the launcher Start-Process returned, so the window's pid is what is closed
# (and the launcher, if it is still around).
if ($gamePid -ne 0) {
    Stamp ("closing game pid {0}" -f $gamePid)
    Stop-Process -Id $gamePid -Force -ErrorAction SilentlyContinue
}
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
taskkill /F /IM RockyKombat.exe 2>&1 | Out-Null
Start-Sleep -Seconds 1
$still = @(Get-Process -Name RockyKombat -ErrorAction SilentlyContinue)
Stamp ("game processes still alive after shutdown: {0}" -f $still.Count)

$timelinePath = Join-Path $OutDir "timeline.txt"
$timeline | Set-Content -Path $timelinePath -Encoding UTF8
Stamp ("timeline written: {0}" -f $timelinePath)
exit 0
