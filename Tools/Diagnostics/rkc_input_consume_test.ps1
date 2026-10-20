# rkc_input_consume_test.ps1
#
# Question this answers: does the SHIPPED build consume injected keyboard input
# at all, once the window has been genuinely activated?
#
# The earlier probe injected keys into a window that was merely foreground and
# saw no menu change. This test makes activation unambiguous first (a real mouse
# click inside the client area, which is how a person focuses a window), then
# injects the same key three different ways and measures whether the main-menu
# highlight moves. Each method gets its own frame so a working method cannot be
# masked by a failing one.
#
# Evidence: Saved/Evidence/ConsumeTest/

$ErrorActionPreference = 'Stop'
$Exe     = 'C:\RockyKombat\Packaged\Windows\RockyKombat.exe'
$WorkDir = 'C:\RockyKombat\Packaged\Windows'
$OutDir  = 'C:\RockyKombat\Saved\Evidence\ConsumeTest'
$Log     = Join-Path $OutDir 'consumetest.txt'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $Log) { Remove-Item $Log -Force }

function Say([string]$msg) {
    Write-Host $msg
    Add-Content -Path $Log -Value $msg
}

Add-Type -Namespace RkcIn -Name Win32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, System.UIntPtr dwExtraInfo);
[DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, System.UIntPtr dwExtraInfo);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[DllImport("user32.dll")] public static extern System.IntPtr GetActiveWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, out uint pid);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT r);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr p);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(System.IntPtr hWnd, System.Text.StringBuilder s, int n);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
public delegate bool EnumProc(System.IntPtr hWnd, System.IntPtr lParam);
'@

function Get-WindowsByClass([string]$Class) {
    $found = New-Object System.Collections.ArrayList
    $cb = [RkcIn.Win32+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [void][RkcIn.Win32]::GetClassName($h, $sb, 256)
        if ($sb.ToString() -eq $Class) {
            $wpid = [uint32]0
            [void][RkcIn.Win32]::GetWindowThreadProcessId($h, [ref]$wpid)
            $r = New-Object RkcIn.Win32+RECT
            [void][RkcIn.Win32]::GetWindowRect($h, [ref]$r)
            [void]$found.Add([pscustomobject]@{ Hwnd=$h; Pid=$wpid; Rect=$r })
        }
        return $true
    }
    [void][RkcIn.Win32]::EnumWindows($cb, [System.IntPtr]::Zero)
    return $found
}

function Grab([string]$path) {
    if (Test-Path $path) { Remove-Item $path -Force }
    & ffmpeg -hide_banner -loglevel error -f gdigrab -i desktop -frames:v 1 -y $path 2>$null
    if (Test-Path $path) { return $true }
    return $false
}

# --- launch -----------------------------------------------------------------
Say "=== launch $Exe ==="
$proc = Start-Process -FilePath $Exe -WorkingDirectory $WorkDir `
    -ArgumentList @('-windowed','-ResX=1280','-ResY=720','-nosplash') -PassThru
Say ("launcher pid {0}" -f $proc.Id)

$game = $null
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 1000
    $wins = Get-WindowsByClass 'UnrealWindow'
    foreach ($w in $wins) {
        $p = Get-Process -Id $w.Pid -ErrorAction SilentlyContinue
        if ($p -and $p.ProcessName -like 'RockyKombat*') { $game = $w; break }
    }
    if ($game) { break }
}
if (-not $game) { Say 'FAIL: no RockyKombat UnrealWindow found'; exit 1 }
$r = $game.Rect
Say ("game window hwnd {0} pid {1} rect {2},{3},{4},{5}" -f $game.Hwnd, $game.Pid, $r.Left, $r.Top, $r.Right, $r.Bottom)
Say ("window size {0}x{1} at +{2}+{3}" -f ($r.Right-$r.Left), ($r.Bottom-$r.Top), $r.Left, $r.Top)

# The front end needs ~20 s in this build to reach the main menu even on a warm
# start; wait for the budget rather than trusting a fixed sleep.
Say 'waiting 50 s for the front end to reach the main menu ...'
Start-Sleep -Seconds 50

Say ("foreground now: {0}" -f [RkcIn.Win32]::GetForegroundWindow())

# --- activation: a real click inside the client area ------------------------
$cx = $r.Left + 100
$cy = $r.Top + [int](($r.Bottom - $r.Top) * 0.83)   # bottom-left, clear of the menu panel
Say ("activating: click at {0},{1} (client area, clear of the menu rows)" -f $cx, $cy)
[void][RkcIn.Win32]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 300
[RkcIn.Win32]::mouse_event(0x0002, 0, 0, 0, [System.UIntPtr]::Zero)  # LEFTDOWN
Start-Sleep -Milliseconds 120
[RkcIn.Win32]::mouse_event(0x0004, 0, 0, 0, [System.UIntPtr]::Zero)  # LEFTUP
Start-Sleep -Milliseconds 1200
Say ("foreground after click: {0}  active: {1}" -f [RkcIn.Win32]::GetForegroundWindow(), [RkcIn.Win32]::GetActiveWindow())

# Park the cursor away from the menu so it cannot be mistaken for a highlight.
[void][RkcIn.Win32]::SetCursorPos($r.Left + 20, $r.Bottom - 20)
Start-Sleep -Milliseconds 500

[void](Grab (Join-Path $OutDir 'a_baseline.png'))
Say 'shot a_baseline'

$VK_S    = 0x53
$scS     = [RkcIn.Win32]::MapVirtualKey($VK_S, 0)

function PressVK([byte]$vk) {
    [RkcIn.Win32]::keybd_event($vk, 0, 0, [System.UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [RkcIn.Win32]::keybd_event($vk, 0, 0x0002, [System.UIntPtr]::Zero)
}
function PressScan([int]$scan) {
    [RkcIn.Win32]::keybd_event(0, [byte]$scan, 0x0008, [System.UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [RkcIn.Win32]::keybd_event(0, [byte]$scan, 0x0008 -bor 0x0002, [System.UIntPtr]::Zero)
}

# --- method 1: virtual key, held longer -------------------------------------
Say 'method 1: S as virtual key (held 90 ms)'
PressVK $VK_S
Start-Sleep -Milliseconds 1200
[void](Grab (Join-Path $OutDir 'b_after_vk.png'))
Say 'shot b_after_vk'

# --- method 2: scan code -----------------------------------------------------
Say ("method 2: S as scan code (0x{0:X2})" -f $scS)
PressScan $scS
Start-Sleep -Milliseconds 1200
[void](Grab (Join-Path $OutDir 'c_after_scancode.png'))
Say 'shot c_after_scancode'

# --- method 3: window messages posted directly -------------------------------
Say 'method 3: WM_KEYDOWN/WM_KEYUP posted to the window'
Add-Type -Namespace RkcIn -Name Msg -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr hWnd, uint msg, System.IntPtr w, System.IntPtr l);
'@
$lParamDown = [System.IntPtr]((1 -shl 0) -bor ($scS -shl 16))
$lParamUp   = [System.IntPtr]((1 -shl 0) -bor ($scS -shl 16) -bor (1 -shl 30) -bor ([int64]1 -shl 31))
[void][RkcIn.Msg]::PostMessage($game.Hwnd, 0x0100, [System.IntPtr]$VK_S, $lParamDown)
Start-Sleep -Milliseconds 120
[void][RkcIn.Msg]::PostMessage($game.Hwnd, 0x0101, [System.IntPtr]$VK_S, $lParamUp)
Start-Sleep -Milliseconds 1200
[void](Grab (Join-Path $OutDir 'd_after_postmessage.png'))
Say 'shot d_after_postmessage'

# --- liveness: F10 must quit if keys are consumed ---------------------------
Say 'liveness: F10 (RKC_QuitMatch)'
PressVK 0x79
Start-Sleep -Seconds 6
$alive = @(Get-Process -Name RockyKombat -ErrorAction SilentlyContinue)
Say ("RockyKombat process(es) alive after F10: {0}" -f $alive.Count)

[void](Grab (Join-Path $OutDir 'e_after_f10.png'))
Say 'shot e_after_f10'

Say '=== done ==='
