#!/usr/bin/env python3
"""在 NOR 固件里找与 inode 记录结尾 A8 A8 有关的代码。"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "roms/lee2/nc3000.nor"
b = open(path, "rb").read()

pats = [
    (b"\xa9\xa8", "LDA #$A8"),
    (b"\xa9\xa8\x8d", "LDA #$A8 / STA abs"),
    (b"\xa8\xa8", "A8 A8"),
]
for pat, name in pats:
    hits = [m.start() for m in re.finditer(re.escape(pat), b)]
    print("%-16s %d hits: %s" % (name, len(hits), [hex(h) for h in hits[:60]]))
