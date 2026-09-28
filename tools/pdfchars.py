"""Per-character boxes in device space (pypdfium2 / PDFium).

pypdfium2 exposes each glyph's bounding box in page coordinates, which is
exactly what is needed to place a text run inside a table column.  The
character values are the raw font codes (this PDF has no /ToUnicode), so a
per-font Rosetta from tools/glyphmaps is applied to make them readable.

Usage:
  python pdfchars.py <pdf> <page> [--maps DIR] [--yband LO,HI] [--xband LO,HI]
"""
import json
import os
import sys

import pypdfium2 as pdfium


def load_maps(path):
    maps = {}
    if os.path.isdir(path):
        for name in os.listdir(path):
            if name.endswith(".json"):
                with open(os.path.join(path, name), "r", encoding="utf-8") as fh:
                    maps[os.path.splitext(name)[0]] = json.load(fh)
    return maps


def main() -> int:
    pdf_path = sys.argv[1]
    pno = int(sys.argv[2])
    maps = {}
    if "--maps" in sys.argv:
        maps = load_maps(sys.argv[sys.argv.index("--maps") + 1])
    yband = xband = None
    if "--yband" in sys.argv:
        lo, hi = sys.argv[sys.argv.index("--yband") + 1].split(",")
        yband = (float(lo), float(hi))
    if "--xband" in sys.argv:
        lo, hi = sys.argv[sys.argv.index("--xband") + 1].split(",")
        xband = (float(lo), float(hi))

    doc = pdfium.PdfDocument(pdf_path)
    page = doc[pno - 1]
    tp = page.get_textpage()
    n = tp.count_chars()
    merged = {}
    for k, mp in maps.items():
        for a, b in mp.items():
            merged.setdefault(a, b)
    for i in range(n):
        s = tp.get_text_range(i, 1)
        l, b, r, t = tp.get_charbox(i)
        if yband and not (yband[0] <= b <= yband[1]):
            continue
        if xband and not (xband[0] <= l <= xband[1]):
            continue
        if not s:
            print(f"code=<none>      ?  x=[{l:7.2f},{r:7.2f}] y=[{b:7.2f},{t:7.2f}]")
            continue
        code = ord(s)
        name = "G%02X" % code
        ch = merged.get(name, "{%s}" % name)
        print(f"code=0x{code:02X} {name:4s} {ch:>4s} x=[{l:7.2f},{r:7.2f}] y=[{b:7.2f},{t:7.2f}]")
    print(f"# {n} chars")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
