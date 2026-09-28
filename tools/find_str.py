# -*- coding: utf-8 -*-
"""Search the NC3000 NOR / NAND dumps for GB2312 encoded Chinese strings.

usage: python tools/find_str.py <string> [string...]

Prints the file offset and the CPU address (NOR only) of each hit, plus the
surrounding text so the string table layout can be inspected.
"""
import re
import sys

ROOT = r"C:\Users\YYGSM\Documents\work\WQX_NC3K_SYS"
FILES = {
    "NOR": ROOT + r"\roms\lee2\nc3000.nor",
    "NAND": ROOT + r"\roms\lee2\nc3000.nand",
}


def cpu_of(off):
    bank = off // 0x8000
    base = 0x8000 if bank == 0 else 0x4000
    return bank, base + (off % 0x8000)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = {name: open(path, "rb").read() for name, path in FILES.items()}
    for text in sys.argv[1:]:
        pat = text.encode("gb2312")
        for name, buf in data.items():
            hits = [m.start() for m in re.finditer(re.escape(pat), buf)]
            print("%-14s %-4s %3d hits %s" % (
                text, name, len(hits), [hex(h) for h in hits[:8]]))
            for h in hits[:4]:
                ctx = buf[max(0, h - 24):h + len(pat) + 24].decode(
                    "gb2312", errors="replace")
                bank, cpu = cpu_of(h)
                print("      %06x (bank %02x cpu $%04x): ...%s..." % (
                    h, bank, cpu, ctx))
    return 0


if __name__ == "__main__":
    sys.exit(main())
