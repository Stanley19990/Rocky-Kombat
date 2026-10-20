# List every top-level window owned by any RockyKombat.exe process, so the game
# viewport can be told apart from the -log console window.
$sig = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WinLister {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static List<string> All() {
        List<string> rows = new List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint pid; GetWindowThreadProcessId(h, out pid);
            int len = GetWindowTextLength(h);
            StringBuilder sb = new StringBuilder(len + 400);
            GetWindowText(h, sb, sb.Capacity);
            StringBuilder cn = new StringBuilder(300);
            GetClassName(h, cn, cn.Capacity);
            RECT r; GetWindowRect(h, out r);
            rows.Add(string.Format("pid={0} hwnd={1} visible={2} size={3}x{4} class='{5}' title='{6}'",
                pid, (long)h, IsWindowVisible(h), r.Right - r.Left, r.Bottom - r.Top, cn.ToString(), sb.ToString()));
            return true;
        }, IntPtr.Zero);
        return rows;
    }
}
'@
Add-Type -TypeDefinition $sig

Write-Output "=== processes named RockyKombat ==="
Get-Process -Name RockyKombat -ErrorAction SilentlyContinue |
    Select-Object Id, MainWindowHandle, MainWindowTitle |
    Format-Table -AutoSize | Out-String | Write-Output

Write-Output "=== all top-level windows of those processes ==="
$pids = @(Get-Process -Name RockyKombat -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
foreach ($row in [WinLister]::All()) {
    foreach ($p in $pids) {
        if ($row -like "pid=$p *") { Write-Output $row; break }
    }
}
