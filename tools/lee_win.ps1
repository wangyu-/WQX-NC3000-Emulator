<#
Lee NC3000 模拟器窗口操作小工具（截图 / 点击 / 按键 / 置前）。
窗口类名 MyNc3000Class，标题 "NC3000模拟器"。

用法:
  powershell -File tools\lee_win.ps1 info
  powershell -File tools\lee_win.ps1 shot out\nc3000\lee_shot.png
  powershell -File tools\lee_win.ps1 click 420 300          # 屏幕坐标
  powershell -File tools\lee_win.ps1 cclick 420 300         # 客户区坐标
  powershell -File tools\lee_win.ps1 key VK_F1              # 发送 WM_KEYDOWN/UP
  powershell -File tools\lee_win.ps1 text "abc"             # 发送字符
  powershell -File tools\lee_win.ps1 focus                  # 抢前台
#>
param(
    [Parameter(Position = 0)][string]$Action = "info",
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

if (-not ("LeeWin" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class LeeWin {
    [DllImport("user32.dll", SetLastError=true)] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr hMenu, int nPos);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr hMenu);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr hMenu, int nPos);
    [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetMenuString(IntPtr hMenu, uint uIDItem, StringBuilder lpString, int cchMax, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetSystemMenu(IntPtr hWnd, bool bRevert);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    public static IntPtr FindByClass(string cls) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(256);
            GetClassName(h, sb, 256);
            if (sb.ToString() == cls) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string ClassOf(IntPtr h) { var sb = new StringBuilder(256); GetClassName(h, sb, 256); return sb.ToString(); }
    public static string TextOf(IntPtr h) { var sb = new StringBuilder(512); GetWindowText(h, sb, 512); return sb.ToString(); }
    public static IntPtr ClientToScreenPt(IntPtr h, int x, int y) {
        POINT p; p.X = x; p.Y = y; ClientToScreen(h, ref p);
        return new IntPtr((p.Y << 16) | (p.X & 0xFFFF));
    }
    public static string ClickScreenPt(IntPtr h, int sx, int sy) {
        POINT p; p.X = sx; p.Y = sy; ScreenToClient(h, ref p);
        IntPtr lp = new IntPtr((p.Y << 16) | (p.X & 0xFFFF));
        SendMessage(h, 0x0201, new IntPtr(1), lp);
        System.Threading.Thread.Sleep(80);
        SendMessage(h, 0x0202, new IntPtr(0), lp);
        return string.Format("screen({0},{1}) -> client({2},{3})", sx, sy, p.X, p.Y);
    }
    public static string ClientOrigin(IntPtr h) {
        POINT p; p.X = 0; p.Y = 0; ClientToScreen(h, ref p);
        return string.Format("{0},{1}", p.X, p.Y);
    }
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, IntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);

    /*
     * 给窗口发 WM_KEYDOWN/WM_KEYUP，lParam 里要带**正确的 scancode**
     * （bits16-23 = MapVirtualKey(vk, 0)）。如果 scancode 传 0，SDL2 会把
     * 这个键翻译成别的 keycode（实测会变成 ` 从而打开模拟器内置命令行），
     * 所以这里必须算 scancode。
     */
    public static void SendKeyWithScan(IntPtr h, uint vk, int holdMs) {
        uint sc = MapVirtualKey(vk, 0);
        long lpDown = 1 | ((long)sc << 16);
        long lpUp   = 1 | ((long)sc << 16) | (1L << 30) | (1L << 31);
        SendMessage(h, 0x0100, new IntPtr((int)vk), new IntPtr(lpDown));
        System.Threading.Thread.Sleep(holdMs);
        SendMessage(h, 0x0101, new IntPtr((int)vk), new IntPtr(lpUp));
    }

    public static void RealMove(int x, int y) {
        SetCursorPos(x, y);
        mouse_event(0x0001, 0, 0, 0, IntPtr.Zero); // MOUSEEVENTF_MOVE
    }
    public static void RealClick(int x, int y, int holdMs) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(40);
        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); // LEFTDOWN
        System.Threading.Thread.Sleep(holdMs);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero); // LEFTUP
    }
    public static void RealKey(byte vk, int holdMs) {
        keybd_event(vk, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(holdMs);
        keybd_event(vk, 0, 0x0002, IntPtr.Zero); // KEYEVENTF_KEYUP
    }
    public static void SendCommandMsg(IntPtr h, int id) {
        SendMessage(h, 0x0111, new IntPtr(id), IntPtr.Zero);
    }
}
"@
}

$target = [LeeWin]::FindByClass("MyNc3000Class")
if ($target -eq [IntPtr]::Zero) { $target = [LeeWin]::FindByClass("SDL_app") }   # 我们自己的模拟器
if ($target -eq [IntPtr]::Zero) {
    # 退路：按标题找
    $p = Get-Process | Where-Object { $_.MainWindowTitle -like "*NC3000*" -and $_.ProcessName -like "*nc3000*" } | Select-Object -First 1
    if ($p) { $target = $p.MainWindowHandle }
}
if ($target -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 1 }

function Get-Info {
    $wr = New-Object LeeWin+RECT
    $cr = New-Object LeeWin+RECT
    [void][LeeWin]::GetWindowRect($target, [ref]$wr)
    [void][LeeWin]::GetClientRect($target, [ref]$cr)
    $fg = [LeeWin]::GetForegroundWindow()
    [pscustomobject]@{
        Handle       = $target
        Class        = [LeeWin]::ClassOf($target)
        Title        = [LeeWin]::TextOf($target)
        Visible      = [LeeWin]::IsWindowVisible($target)
        Iconic       = [LeeWin]::IsIconic($target)
        WindowRect   = "$($wr.Left),$($wr.Top) - $($wr.Right),$($wr.Bottom)"
        ClientSize   = "$($cr.Right)x$($cr.Bottom)"
        Foreground   = $fg
        FGClass      = [LeeWin]::ClassOf($fg)
        FGTitle      = [LeeWin]::TextOf($fg)
    }
}

function Do-Focus {
    $fg = [LeeWin]::GetForegroundWindow()
    $pidFg = 0
    $tidFg = [LeeWin]::GetWindowThreadProcessId($fg, [ref]$pidFg)
    $tidMe = [LeeWin]::GetCurrentThreadId()
    if ($tidFg -ne $tidMe) { [void][LeeWin]::AttachThreadInput($tidMe, $tidFg, $true) }
    [void][LeeWin]::ShowWindow($target, 9)   # SW_RESTORE
    [void][LeeWin]::BringWindowToTop($target)
    [void][LeeWin]::SetForegroundWindow($target)
    [void][LeeWin]::SetFocus($target)
    if ($tidFg -ne $tidMe) { [void][LeeWin]::AttachThreadInput($tidMe, $tidFg, $false) }
    Start-Sleep -Milliseconds 250
    $fg2 = [LeeWin]::GetForegroundWindow()
    Write-Output ("FG_NOW={0} class={1} title={2}" -f $fg2, [LeeWin]::ClassOf($fg2), [LeeWin]::TextOf($fg2))
}

function Do-Shot([string]$path) {
    $wr = New-Object LeeWin+RECT
    [void][LeeWin]::GetWindowRect($target, [ref]$wr)
    $w = $wr.Right - $wr.Left; $h = $wr.Bottom - $wr.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # 2 = PW_RENDERFULLCONTENT
    [void][LeeWin]::PrintWindow($target, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $dir = Split-Path -Parent $path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "SAVED $path ($w x $h)"
}

function Do-Click([int]$x, [int]$y, [bool]$client) {
    if ($client) {
        $org = [LeeWin]::ClientOrigin($target).Split(",")
        $x += [int]$org[0]; $y += [int]$org[1]
    }
    Write-Output ("CLICK " + [LeeWin]::ClickScreenPt($target, $x, $y))
}

function Do-Key([string]$name, [int]$repeat = 1) {
    $code = 0
    if ($name -match '^0x([0-9a-fA-F]+)$') { $code = [Convert]::ToInt32($matches[1], 16) }
    elseif ($name -match '^VK_(\w+)$') {
        $map = @{
            RETURN = 0x0D; ENTER = 0x0D; ESCAPE = 0x1B; ESC = 0x1B; SPACE = 0x20; TAB = 0x09
            UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27
            F1 = 0x70; F2 = 0x71; F3 = 0x72; F4 = 0x73; F5 = 0x74; F6 = 0x75
            LSHIFT = 0xA0; RSHIFT = 0xA1; LCONTROL = 0xA2; CONTROL = 0xA2; RCONTROL = 0xA3
            LMENU = 0xA4; ALT = 0xA4; RMENU = 0xA5; LWIN = 0x5B
        }
        $k = $matches[1].ToUpper()
        if ($map.ContainsKey($k)) { $code = $map[$k] }
    }
    elseif ($name -match '^[0-9A-Z]$') { $code = [int][char]$name }
    if ($code -eq 0) { Write-Output "UNKNOWN_KEY $name"; return }
    for ($i = 0; $i -lt $repeat; $i++) {
        [void][LeeWin]::SendMessage($target, 0x0100, [IntPtr]$code, [IntPtr]1)         # WM_KEYDOWN
        Start-Sleep -Milliseconds 60
        [void][LeeWin]::SendMessage($target, 0x0101, [IntPtr]$code, [IntPtr]0xC0000001) # WM_KEYUP
        Start-Sleep -Milliseconds 60
    }
    Write-Output "KEY $name($code) x$repeat"
}

function Do-Text([string]$s) {
    foreach ($ch in $s.ToCharArray()) {
        [void][LeeWin]::SendMessage($target, 0x0102, [IntPtr][int][char]$ch, [IntPtr]1) # WM_CHAR
        Start-Sleep -Milliseconds 60
    }
    Write-Output "TEXT $s"
}

function Do-Menu {
    $menu = [LeeWin]::GetMenu($target)
    $n = [LeeWin]::GetMenuItemCount($menu)
    Write-Output "top menu items: $n"
    for ($i = 0; $i -lt $n; $i++) {
        $sb = New-Object System.Text.StringBuilder 256
        [void][LeeWin]::GetMenuString($menu, [uint32]$i, $sb, 256, 0x400)
        $sub = [LeeWin]::GetSubMenu($menu, $i)
        Write-Output ("  [$i] '{0}' sub={1}" -f $sb.ToString(), $sub)
        if ($sub -ne [IntPtr]::Zero) {
            $m = [LeeWin]::GetMenuItemCount($sub)
            for ($j = 0; $j -lt $m; $j++) {
                $sb2 = New-Object System.Text.StringBuilder 256
                [void][LeeWin]::GetMenuString($sub, [uint32]$j, $sb2, 256, 0x400)
                $id = [LeeWin]::GetMenuItemID($sub, $j)
                Write-Output ("      [$j] id={0} '{1}'" -f $id, $sb2.ToString())
            }
        }
    }
}

switch ($Action) {
    "info" { Get-Info | Format-List }
    "focus" { Do-Focus }
    "shot" { Do-Focus | Out-Null; Do-Shot ($Rest[0]) }
    "shotquiet" { Do-Shot ($Rest[0]) }
    "click" { Do-Click ([int]$Rest[0]) ([int]$Rest[1]) $false }
    "cclick" { Do-Click ([int]$Rest[0]) ([int]$Rest[1]) $true }
    "rclick" {
        $hold = 90
        if ($Rest.Count -gt 2) { $hold = [int]$Rest[2] }
        [LeeWin]::RealClick([int]$Rest[0], [int]$Rest[1], $hold)
        Write-Output "REALCLICK $($Rest[0]),$($Rest[1]) hold=$hold"
    }
    "rmove" { [LeeWin]::RealMove([int]$Rest[0], [int]$Rest[1]); Write-Output "REALMOVE $($Rest[0]),$($Rest[1])" }
    "focuskey" {
        # 抢前台 + 立刻发真实按键（要在同一次调用里做：分两次调用时，
        # 中间新起的进程会把焦点抢走，keybd_event 就打不到目标窗口了）
        Do-Focus | Out-Null
        Start-Sleep -Milliseconds 150
        $hold = 90
        if ($Rest.Count -gt 1) { $hold = [int]$Rest[1] }
        [LeeWin]::RealKey([byte][int]$Rest[0], $hold)
        Write-Output "FOCUSKEY vk=$($Rest[0]) hold=$hold"
    }
    "rkey" {
        $hold = 90
        if ($Rest.Count -gt 1) { $hold = [int]$Rest[1] }
        $vk = [byte][int]$Rest[0]
        [LeeWin]::RealKey($vk, $hold)
        Write-Output "REALKEY vk=$vk hold=$hold"
    }
    "sendkey" {
        # 带正确 scancode 的 WM_KEYDOWN/UP（SDL 能正确翻译）
        $hold = 90
        if ($Rest.Count -gt 1) { $hold = [int]$Rest[1] }
        [LeeWin]::SendKeyWithScan($target, [uint32][int]$Rest[0], $hold)
        Write-Output "SENDKEY vk=$($Rest[0]) hold=$hold"
    }
    "key" { if ($Rest.Count -gt 1) { Do-Key $Rest[0] ([int]$Rest[1]) } else { Do-Key $Rest[0] } }
    "text" { Do-Text $Rest[0] }
    "menu" { Do-Menu }
    "size" {
        $x = [int]$Rest[0]; $y = [int]$Rest[1]; $w = [int]$Rest[2]; $h = [int]$Rest[3]
        # SWP_NOZORDER=4, SWP_SHOWWINDOW=0x40
        [void][LeeWin]::SetWindowPos($target, [IntPtr]::Zero, $x, $y, $w, $h, 0x44)
        Start-Sleep -Milliseconds 300
        Get-Info | Format-List
    }
    "menucmd" {
        $id = [int]$Rest[0]
        [LeeWin]::SendCommandMsg($target, $id)
        Write-Output "WM_COMMAND $id"
    }
    default { Write-Output "unknown action $Action" }
}
