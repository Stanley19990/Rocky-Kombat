# rkc_inputprobe.ps1
#
# ONE question, answered with a control: does a real key press reach the game?
#
#   STEP 0  CONTROL. The same injection code types 's' into Notepad. Notepad
#           marks its title with '*' as soon as its text is modified, so the
#           control needs no screenshot and cannot be argued with: if the title
#           gains a '*', the injection path works and any failure later is the
#           game's, not the driver's. If it does not, the driver is at fault and
#           nothing may be concluded about the game.
#   STEP 1  Launch the SHIPPED packaged build (no -log, so no console window can
#           take the focus) and force its window to the foreground. Whether the
#           force actually worked is verified against GetForegroundWindow and
#           printed, because SetForegroundWindow returns a value that does not
#           mean "this window is now foreground".
#   STEP 2  Press S (RKC_MenuDown). Compare ONLY the menu panel region, so the
#           fighters' idle animation cannot be mistaken for menu movement. This
#           is the mistake the first version of this test made: a whole-frame
#           hash differs on animation alone and reads as "input arrived".
#   STEP 3  Press the DOWN arrow (the other RKC_MenuDown binding).
#   STEP 4  Press ENTER (RKC_MenuAccept). A screen change is not explainable by
#           animation, so the whole-frame difference is meaningful here.
#   STEP 5  Press F10 (RKC_QuitMatch). In a packaged build this ends the
#           process, so process liveness is an unambiguous input witness.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\Diagnostics\rkc_inputprobe.ps1

param(
    [string]$Exe = "C:\RockyKombat\Packaged\Windows\RockyKombat.exe",
    [string]$OutDir = "C:\RockyKombat\Saved\Evidence\InputProbe"
)

$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class RkcProbe {
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
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    public const uint KEYEVENTF_KEYUP       = 0x0002;
    public const uint KEYEVENTF_SCANCODE    = 0x0008;

    public static string Describe(IntPtr h) {
        if (h == IntPtr.Zero) return "<null>";
        StringBuilder cn = new StringBuilder(300); GetClassName(h, cn, cn.Capacity);
        StringBuilder sb = new StringBuilder(400); GetWindowText(h, sb, sb.Capacity);
        uint pid; GetWindowThreadProcessId(h, out pid);
        RECT r; GetWindowRect(h, out r);
        return ((long)h).ToString() + " class='" + cn.ToString() + "' pid=" + pid +
               " rect=" + r.Left + "," + r.Top + "," + r.Right + "," + r.Bottom +
               " title='" + sb.ToString() + "'";
    }

    public static string Find(string cls, string marker) {
        string found = "";
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h)) return true;
            StringBuilder cn = new StringBuilder(300); GetClassName(h, cn, cn.Capacity);
            if (cn.ToString() != cls) return true;
            StringBuilder sb = new StringBuilder(400); GetWindowText(h, sb, sb.Capacity);
            if (marker != "" && sb.ToString().IndexOf(marker) < 0) return true;
            found = ((long)h).ToString();
            return false;
        }, IntPtr.Zero);
        return found;
    }

    // Every visible top-level window, so an unexpected foreground window can be
    // identified instead of guessed at.
    public static List<string> All() {
        List<string> found = new List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (IsWindowVisible(h)) { found.Add(Describe(h)); }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string Title(IntPtr h) {
        StringBuilder sb = new StringBuilder(400); GetWindowText(h, sb, sb.Capacity);
        return sb.ToString();
    }

    // Three escalating attempts. Windows refuses SetForegroundWindow to a process
    // that is not already foreground (the foreground lock), so the input queues of
    // this thread, the current foreground thread and the target thread are joined
    // first; SwitchToThisWindow is the fallback that ignores the lock.
    public static bool Force(IntPtr h) {
        ShowWindow(h, 9);                                  // SW_RESTORE
        uint pid; uint target = GetWindowThreadProcessId(h, out pid);
        uint cur = GetCurrentThreadId();
        IntPtr fg = GetForegroundWindow();
        uint fgt = 0; if (fg != IntPtr.Zero) { uint p2; fgt = GetWindowThreadProcessId(fg, out p2); }

        AttachThreadInput(cur, fgt, true);
        AttachThreadInput(cur, target, true);
        SetForegroundWindow(h);
        BringWindowToTop(h);
        SetFocus(h);
        AttachThreadInput(cur, target, false);
        AttachThreadInput(cur, fgt, false);

        if (GetForegroundWindow() != h) { SwitchToThisWindow(h, true); }
        if (GetForegroundWindow() != h) {
            // a lone ALT tap releases the foreground lock
            keybd_event(0x12, 0, 0, UIntPtr.Zero);
            keybd_event(0x12, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
            SetForegroundWindow(h);
        }
        return GetForegroundWindow() == h;
    }

    public static void Key(byte vk, bool extended, bool down) {
        byte scan = (byte)MapVirtualKey(vk, 0);
        uint flags = KEYEVENTF_SCANCODE;
        if (extended) flags |= KEYEVENTF_EXTENDEDKEY;
        if (!down) flags |= KEYEVENTF_KEYUP;
        keybd_event(vk, scan, flags, UIntPtr.Zero);
    }
}
'@
Add-Type -TypeDefinition $sig

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$log = New-Object System.Collections.Generic.List[string]
function Say($m) { Write-Host $m; $log.Add([string]$m) }

$VK_S = 0x53; $VK_DOWN = 0x28; $VK_ENTER = 0x0D; $VK_F10 = 0x79

function Press($vk, $extended) {
    [RkcProbe]::Key([byte]$vk, [bool]$extended, $true)
    Start-Sleep -Milliseconds 90
    [RkcProbe]::Key([byte]$vk, [bool]$extended, $false)
    Start-Sleep -Milliseconds 120
}

function Snap($hwnd, $tag) {
    $r = New-Object RkcProbe+RECT; [void][RkcProbe]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { Say "  snap $tag FAILED (zero area)"; return $null }
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b); $hdc = $g.GetHdc()
    [void][RkcProbe]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $p = Join-Path $OutDir ("$tag.png")
    $b.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
    $b.Dispose()
    return $p
}

# Region diff (default: whole frame). Reports changed-pixel count and mean
# absolute difference, so a sparse animation change and a whole-layout change
# are distinguishable numbers rather than a yes/no hash.
function ImgDiff($p1, $p2, $x0, $y0, $x1, $y1) {
    $i1 = [System.Drawing.Bitmap]::FromFile($p1)
    $i2 = [System.Drawing.Bitmap]::FromFile($p2)
    if ($i1.Width -ne $i2.Width -or $i1.Height -ne $i2.Height) {
        $i1.Dispose(); $i2.Dispose(); return "size-mismatch"
    }
    if ($x1 -le 0) { $x0 = 0; $y0 = 0; $x1 = $i1.Width; $y1 = $i1.Height }
    if ($x1 -gt $i1.Width) { $x1 = $i1.Width }
    if ($y1 -gt $i1.Height) { $y1 = $i1.Height }
    $changed = 0; $n = 0; $sum = 0.0
    for ($y = $y0; $y -lt $y1; $y += 4) {
        for ($x = $x0; $x -lt $x1; $x += 4) {
            $a = $i1.GetPixel($x, $y); $b = $i2.GetPixel($x, $y)
            $d = [math]::Abs($a.R - $b.R) + [math]::Abs($a.G - $b.G) + [math]::Abs($a.B - $b.B)
            if ($d -gt 24) { $changed++ }
            $sum += $d; $n++
        }
    }
    $i1.Dispose(); $i2.Dispose()
    $mad = [math]::Round($sum / [Math]::Max(1, $n), 2)
    $pct = [math]::Round(100.0 * $changed / [Math]::Max(1, $n), 2)
    return ("changed {0}/{1} ({2}%)  MAD {3}" -f $changed, $n, $pct, $mad)
}

# ---------------------------------------------------------------- STEP 0 control
Say "=== STEP 0  control: inject 's' into Notepad ==="
$npExe = Join-Path $env:SystemRoot "System32\notepad.exe"
Say ("  control program: {0} (exists: {1})" -f $npExe, (Test-Path $npExe))
$np = $null
try { $np = Start-Process -FilePath $npExe -PassThru -ErrorAction Stop } catch { Say ("  could not start notepad: " + $_.Exception.Message) }
$nph = [IntPtr]::Zero
$controlOk = $null
if ($np -ne $null) {
    $dl = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $dl) {
        Start-Sleep -Milliseconds 400
        $f = [RkcProbe]::Find("Notepad", "")
        if ($f) { $nph = [IntPtr][long]$f; break }
    }
    if ($nph -eq [IntPtr]::Zero) { Say "  no window of class 'Notepad' appeared" }
}
if ($nph -ne [IntPtr]::Zero) {
    Say ("  notepad: " + [RkcProbe]::Describe($nph))
    $titleBefore = [RkcProbe]::Title($nph)
    $fgNow = [RkcProbe]::Force($nph)
    Start-Sleep -Milliseconds 600
    Say ("  force ok: {0}   foreground: {1}" -f $fgNow, [RkcProbe]::Describe([RkcProbe]::GetForegroundWindow()))
    Press $VK_S $false
    Start-Sleep -Milliseconds 900
    $titleAfter = [RkcProbe]::Title($nph)
    Say ("  title before: '{0}'" -f $titleBefore)
    Say ("  title after:  '{0}'" -f $titleAfter)
    if ($titleAfter.StartsWith("*")) {
        Say "  CONTROL PASS: an injected key reached a real application (title gained '*')."
        $controlOk = $true
    } else {
        Say "  CONTROL FAIL: the injected key did not reach Notepad either - the driver is at fault."
        $controlOk = $false
    }
    Stop-Process -Id $np.Id -Force -ErrorAction SilentlyContinue
} else {
    Say "  CONTROL SKIPPED: no Notepad window to test against."
}

# ---------------------------------------------------------------- STEP 1 launch
Say ""
Say "=== STEP 1  launch the shipped build ==="
$dir = Split-Path -Parent $Exe
$proc = Start-Process -FilePath $Exe -WorkingDirectory $dir `
        -ArgumentList @("-windowed","-ResX=1280","-ResY=720","-nosplash") -PassThru
Say ("  launcher pid {0}" -f $proc.Id)

$hwnd = [IntPtr]::Zero; $gamePid = 0
$dl = (Get-Date).AddSeconds(180)
while ((Get-Date) -lt $dl) {
    Start-Sleep -Milliseconds 700
    $f = [RkcProbe]::Find("UnrealWindow", "RockyKombat")
    if ($f) { $hwnd = [IntPtr][long]$f; break }
}
if ($hwnd -eq [IntPtr]::Zero) {
    Say "  NO GAME WINDOW"; $log | Set-Content (Join-Path $OutDir "inputprobe.txt"); exit 2
}
Say ("  game window: " + [RkcProbe]::Describe($hwnd))

Start-Sleep -Seconds 14
Say ("  foreground before force: {0}" -f [RkcProbe]::Describe([RkcProbe]::GetForegroundWindow()))
$okForce = [RkcProbe]::Force($hwnd)
Start-Sleep -Milliseconds 800
$fg = [RkcProbe]::GetForegroundWindow()
Say ("  force ok: {0}   foreground after force: {1}" -f $okForce, [RkcProbe]::Describe($fg))
if ($fg -ne $hwnd) {
    Say "  !! the game window is NOT foreground: any key sent now goes somewhere else."
    Say "  visible top-level windows at this moment:"
    foreach ($w in [RkcProbe]::All()) { Say ("    " + $w) }
}
# window handle's pid, for the liveness check later
$gamePid = (Get-Process -Name RockyKombat -ErrorAction SilentlyContinue | Select-Object -First 1).Id

# the menu panel, from the captured Main Menu frame. Menu rows occupy
# x 384..893, y 240..380; a 6 px margin keeps antialiasing out.
$PX0 = 378; $PY0 = 234; $PX1 = 900; $PY1 = 386

$p0 = Snap $hwnd "p0_main_menu"
Say ("  p0 captured: {0}" -f $p0)

Say ""
Say "=== STEP 2  press S (RKC_MenuDown) ==="
[void][RkcProbe]::Force($hwnd); Start-Sleep -Milliseconds 250
Say ("  foreground at press: {0}" -f [RkcProbe]::Describe([RkcProbe]::GetForegroundWindow()))
Press $VK_S $false
Start-Sleep -Milliseconds 1200
$p1 = Snap $hwnd "p1_after_S"
Say ("  menu panel p0->p1: " + (ImgDiff $p0 $p1 $PX0 $PY0 $PX1 $PY1))
Say ("  whole frame p0->p1: " + (ImgDiff $p0 $p1 0 0 -1 -1))

Say ""
Say "=== STEP 3  press DOWN arrow (the other RKC_MenuDown binding) ==="
[void][RkcProbe]::Force($hwnd); Start-Sleep -Milliseconds 250
Press $VK_DOWN $true
Start-Sleep -Milliseconds 1200
$p2 = Snap $hwnd "p2_after_down"
Say ("  menu panel p1->p2: " + (ImgDiff $p1 $p2 $PX0 $PY0 $PX1 $PY1))
Say ("  menu panel p0->p2: " + (ImgDiff $p0 $p2 $PX0 $PY0 $PX1 $PY1))

Say ""
Say "=== STEP 4  press ENTER (RKC_MenuAccept) - a screen change is unambiguous ==="
[void][RkcProbe]::Force($hwnd); Start-Sleep -Milliseconds 250
Press $VK_ENTER $false
Start-Sleep -Seconds 3
$p3 = Snap $hwnd "p3_after_enter"
Say ("  whole frame p0->p3: " + (ImgDiff $p0 $p3 0 0 -1 -1))

Say ""
Say "=== STEP 5  press F10 (RKC_QuitMatch) - liveness is an unambiguous witness ==="
[void][RkcProbe]::Force($hwnd); Start-Sleep -Milliseconds 250
Press $VK_F10 $false
Start-Sleep -Seconds 6
$alive = @(Get-Process -Name RockyKombat -ErrorAction SilentlyContinue).Count
Say ("  RockyKombat process(es) alive after F10: {0}" -f $alive)
if ($alive -gt 0) { Say "  F10 did NOT quit the game." } else { Say "  F10 DID quit the game - input reaches the game." }

# ---------------------------------------------------------------- shutdown
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Get-Process -Name RockyKombat -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
taskkill /F /IM RockyKombat.exe 2>&1 | Out-Null

Say ""
Say "=== VERDICT ==="
if ($controlOk -eq $true)      { Say "  injection control (Notepad): PASS - keys reach a real application" }
elseif ($controlOk -eq $false) { Say "  injection control (Notepad): FAIL - the driver is at fault" }
else                           { Say "  injection control (Notepad): not run" }
Say ("  game window was foreground at the press: {0}" -f ($fg -eq $hwnd))
Say ("  RockyKombat alive after F10: {0}" -f $alive)
$log | Set-Content -Path (Join-Path $OutDir "inputprobe.txt") -Encoding UTF8
Say ("  log written: " + (Join-Path $OutDir "inputprobe.txt"))
exit 0
