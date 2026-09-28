#!/usr/bin/env python3
"""列出某个进程的所有顶层窗口（用来确认 exe 有没有多出一个控制台窗口）。

用法: python win_enum.py <进程名，如 nc3000.exe>
"""
import ctypes
import ctypes.wintypes as wintypes
import subprocess
import sys

user32 = ctypes.windll.user32


def pids_of(image):
    out = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq " + image, "/FO", "CSV"]
    ).decode("gbk", "ignore")
    pids = set()
    for line in out.splitlines()[1:]:
        parts = line.split('","')
        if len(parts) > 1:
            try:
                pids.add(int(parts[1].strip('"')))
            except ValueError:
                pass
    return pids


def main():
    image = sys.argv[1] if len(sys.argv) > 1 else "nc3000.exe"
    want = pids_of(image)
    print("pids:", want)
    if not want:
        return
    found = []

    def cls(h):
        b = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(h, b, 256)
        return b.value

    def txt(h):
        b = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(h, b, 512)
        return b.value

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _l):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value in want:
            found.append((hex(h), cls(h), txt(h), bool(user32.IsWindowVisible(h))))
        return True

    user32.EnumWindows(cb, 0)
    for f in found:
        print(f)


if __name__ == "__main__":
    main()
