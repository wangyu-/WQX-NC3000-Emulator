#!/usr/bin/env python3
"""定位"产品信息有误"提示的来源：找谁引用了那句提示串，并解码 NOR info block。"""
import re

NOR = "roms/lee2/nc3000.nor"

b = open(NOR, "rb").read()

print("== 谁引用了字符串 $9D63（bank3 里那句『产品信息有误』）==")
pats = [
    (bytes.fromhex("bd639d"), "LDA $9D63,X"),
    (bytes.fromhex("ad639d"), "LDA $9D63"),
    (bytes.fromhex("b9639d"), "LDA $9D63,Y"),
    (bytes.fromhex("9d639d"), "STA $9D63,X"),
    (bytes.fromhex("639d"), "bytes 63 9D"),
    (bytes.fromhex("a963"), "LDA #$63"),
    (bytes.fromhex("a29d"), "LDX #$9D"),
]
for p, n in pats:
    hits = [m.start() for m in re.finditer(re.escape(p), b)]
    print("%-16s %3d  %s" % (n, len(hits), [hex(h) for h in hits[:16]]))

print()
print("== 谁引用了 $8A8E（另一句『请接洽代理商!』）==")
for p, n in [(bytes.fromhex("bd8e8a"), "LDA $8A8E,X"), (bytes.fromhex("ad8e8a"), "LDA $8A8E"),
             (bytes.fromhex("8e8a"), "bytes 8E 8A")]:
    hits = [m.start() for m in re.finditer(re.escape(p), b)]
    print("%-16s %3d  %s" % (n, len(hits), [hex(h) for h in hits[:16]]))

print()
print("== 模拟器现在写死的 NOR info block（nor.cpp 的 nor_info_block0）前 24 字节 ==")
info = bytes([0xbd,0xf0,0xd4,0xb6,0xbc,0xfb]) + b"NC" + bytes([0xd0,0x07]) + b"J"
info += bytes([0x01,0x02,0x03,0x04,0x01,0x01,0x01,0x01])
print(" ".join("%02X" % x for x in info))
for enc in ("gbk", "big5", "cp936"):
    try:
        print("  前 6 字节按 %-5s = %r" % (enc, info[:6].decode(enc)))
    except Exception as e:
        print("  前 6 字节按 %-5s 解不出: %s" % (enc, e))
print("  模型号字段（偏移 8-9）= 0x%04X = 十进制 %d" % (0x07D0, 0x07D0))
