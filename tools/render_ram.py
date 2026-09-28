# -*- coding: utf-8 -*-
"""Render 160x80 1bpp LCD snapshots out of an internal-RAM dump.

usage: python tools/render_ram.py <ram.bin> <out.png> <addr> [addr...]

Each address is a candidate LCD buffer start; the 1600 bytes from there are
drawn as a 160x80 bitmap (1 = dark) so we can see which RAM window holds the
screen the firmware is really displaying.
"""
import sys
from PIL import Image, ImageDraw

W, H = 160, 80


def window_bitmap(data, addr):
    buf = data[addr:addr + W * H // 8]
    img = Image.new("L", (W, H), 255)
    px = img.load()
    for i, b in enumerate(buf):
        for bit in range(8):
            if b & (0x80 >> bit):
                x = (i * 8 + bit) % W
                y = (i * 8 + bit) // W
                px[x, y] = 0
    return img


def main():
    data = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    addrs = [int(a, 0) for a in sys.argv[3:]]
    scale = 2
    cols = 3
    pw, ph = W * scale, H * scale
    label = 14
    rows = (len(addrs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (pw + 6) + 6, rows * (ph + label + 6) + 6),
                      (60, 60, 60))
    d = ImageDraw.Draw(sheet)
    for i, a in enumerate(addrs):
        im = window_bitmap(data, a).convert("RGB").resize((pw, ph), Image.NEAREST)
        cx = 6 + (i % cols) * (pw + 6)
        cy = 6 + (i // cols) * (ph + label + 6)
        sheet.paste(im, (cx, cy))
        d.text((cx + 2, cy + ph + 1), "$%04X" % a, fill=(230, 230, 230))
    sheet.save(out)
    print("%s (%d windows)" % (out, len(addrs)))


if __name__ == "__main__":
    main()
