# -*- coding: utf-8 -*-
"""Scan a firmware dump for 160x80 (1600 byte) LCD bitmap candidates.

usage: python tools/find_bitmaps.py <file> [--step N] [--top N] [--render out.png]

Scores every 1600-byte window by how "picture like" it is: a drawn screen has
a lot of non-0x00/0xFF bytes but is not pure noise, so we rank by the fraction
of bytes that are neither 0x00 nor 0xFF while keeping a minimum number of set
bits (ink).
"""
import sys
from PIL import Image, ImageDraw

W, H = 160, 80
NBYTES = W * H // 8


def score(buf, off):
    win = buf[off:off + NBYTES]
    if len(win) < NBYTES:
        return None
    mid = 0
    ink = 0
    for b in win:
        if b not in (0x00, 0xFF):
            mid += 1
        ink += bin(b).count("1")
    frac = ink / float(NBYTES * 8)
    if frac < 0.02:
        return None
    return mid, frac


def render(buf, off, scale=2):
    img = Image.new("L", (W, H), 255)
    px = img.load()
    for i, b in enumerate(buf[off:off + NBYTES]):
        for bit in range(8):
            if b & (0x80 >> bit):
                px[(i * 8 + bit) % W, (i * 8 + bit) // W] = 0
    return img.convert("RGB").resize((W * scale, H * scale), Image.NEAREST)


def main():
    path = sys.argv[1]
    step = 4
    top = 24
    out = None
    args = sys.argv[2:]
    for i, a in enumerate(args):
        if a == "--step":
            step = int(args[i + 1], 0)
        elif a == "--top":
            top = int(args[i + 1], 0)
        elif a == "--render":
            out = args[i + 1]
    buf = open(path, "rb").read()
    scored = []
    for off in range(0, len(buf) - NBYTES, step):
        s = score(buf, off)
        if s:
            scored.append((s[0], s[1], off))
    scored.sort(reverse=True)
    print("scanned %d bytes, %d candidates" % (len(buf), len(scored)))
    for mid, frac, off in scored[:top]:
        print("  %08x  mixed=%-5d ink=%.3f" % (off, mid, frac))
    if out and scored:
        picks = [s[2] for s in scored[:top]]
        scale = 2
        cols = 4
        pw, ph = W * scale, H * scale
        rows = (len(picks) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (pw + 6) + 6,
                                  rows * (ph + 16) + 6), (60, 60, 60))
        d = ImageDraw.Draw(sheet)
        for i, off in enumerate(picks):
            im = render(buf, off, scale)
            cx = 6 + (i % cols) * (pw + 6)
            cy = 6 + (i // cols) * (ph + 16)
            sheet.paste(im, (cx, cy))
            d.text((cx + 2, cy + ph + 2), "%08X" % off, fill=(230, 230, 230))
        sheet.save(out)
        print("sheet -> %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
