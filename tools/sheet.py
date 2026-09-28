# -*- coding: utf-8 -*-
"""Tile a list of LCD BMP shots into one contact sheet with frame captions."""
import sys, os, glob
from PIL import Image, ImageDraw


def main():
    if len(sys.argv) < 3:
        print("usage: sheet.py <out.png> <glob|files...> [--cols N] [--scale N]")
        return 1
    out = sys.argv[1]
    args = sys.argv[2:]
    cols = 4
    scale = 2
    if "--cols" in args:
        i = args.index("--cols")
        cols = int(args[i + 1])
        del args[i:i + 2]
    if "--scale" in args:
        i = args.index("--scale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    files = []
    for a in args:
        files.extend(sorted(glob.glob(a)))
    if not files:
        print("no input files")
        return 1
    ims = [Image.open(f).convert("RGB") for f in files]
    w, h = ims[0].size
    w *= scale
    h *= scale
    pad = 4
    label_h = 14
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad,
                              rows * (h + label_h + pad) + pad), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (f, im) in enumerate(zip(files, ims)):
        cx = pad + (i % cols) * (w + pad)
        cy = pad + (i // cols) * (h + label_h + pad)
        sheet.paste(im.resize((w, h), Image.NEAREST), (cx, cy))
        d.text((cx + 2, cy + h + 1), os.path.basename(f), fill=(230, 230, 230))
    sheet.save(out)
    print("%s  (%d frames, %dx%d)" % (out, len(files), sheet.size[0], sheet.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
