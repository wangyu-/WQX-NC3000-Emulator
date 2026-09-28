"""Dump text spans with font name + position from a PDF page.

The SPDS104A manual uses subset fonts whose /Differences give meaningless
glyph names (/G01, /G02, ...), so extracted text arrives obfuscated.  This
script prints, for every span, the font, the raw glyph-name text and the
device-space position, which is enough to (a) build a glyph->char Rosetta
from strings we can read off the rendered page image, and (b) reconstruct
table cell membership from x coordinates.

Usage:
  python pdfspans.py <pdf> <page> [--font SUBSTR]
"""
import sys

import pypdf


def main() -> int:
    pdf_path = sys.argv[1]
    pno = int(sys.argv[2])
    want_font = None
    if "--font" in sys.argv:
        want_font = sys.argv[sys.argv.index("--font") + 1]

    reader = pypdf.PdfReader(pdf_path)
    page = reader.pages[pno - 1]
    rows = []

    def visitor(text, cm, tm, font_dict, font_size):
        base = ""
        if font_dict:
            bf = font_dict.get("/BaseFont")
            base = str(bf)
        if want_font and want_font not in base:
            return
        if not text.strip():
            return
        x, y = tm[4], tm[5]
        rows.append((round(y, 1), round(x, 1), base, text.replace("\n", "\\n")))

    page.extract_text(visitor_text=visitor)
    rows.sort(key=lambda r: (-r[0], r[1]))
    for y, x, base, text in rows:
        print(f"y={y:8.1f} x={x:7.1f} {base:28s} {text!r}")
    print(f"# {len(rows)} spans")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
