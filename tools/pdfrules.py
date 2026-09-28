"""Extract the table rule geometry from a page's content stream.

The manuals draw their tables as filled rectangles ('re' + 'f'), so the
vertical column rules are tall thin rectangles.  Their coordinates are exact
and let us map a text span's x position onto a specific bit column without
guessing from a raster.

Usage:
  python pdfrules.py <pdf> <page> [--min-h 20] [--min-w 0]
"""
import sys

import pypdf
from pypdf.generic import ContentStream


def main() -> int:
    pdf_path = sys.argv[1]
    pno = int(sys.argv[2])
    min_h = 20.0
    min_w = 0.0
    yband = None
    if "--min-h" in sys.argv:
        min_h = float(sys.argv[sys.argv.index("--min-h") + 1])
    if "--min-w" in sys.argv:
        min_w = float(sys.argv[sys.argv.index("--min-w") + 1])
    if "--band" in sys.argv:
        lo, hi = sys.argv[sys.argv.index("--band") + 1].split(",")
        yband = (float(lo), float(hi))

    reader = pypdf.PdfReader(pdf_path)
    page = reader.pages[pno - 1]
    cs = ContentStream(page.get_contents(), reader)
    ctm = [1, 0, 0, 1, 0, 0]
    rects = []
    stack = []
    for operands, operator in cs.operations:
        op = operator.decode("latin-1") if isinstance(operator, bytes) else operator
        if op == "q":
            stack.append(list(ctm))
        elif op == "Q":
            if stack:
                ctm = stack.pop()
        elif op == "cm":
            a, b, c, d, e, f = [float(x) for x in operands]
            ctm = [
                a * ctm[0] + b * ctm[2],
                a * ctm[1] + b * ctm[3],
                c * ctm[0] + d * ctm[2],
                c * ctm[1] + d * ctm[3],
                e * ctm[0] + f * ctm[2] + ctm[4],
                e * ctm[1] + f * ctm[3] + ctm[5],
            ]
        elif op == "re":
            x, y, w, h = [float(v) for v in operands]
            x0 = x * ctm[0] + y * ctm[2] + ctm[4]
            y0 = x * ctm[1] + y * ctm[3] + ctm[5]
            w0 = w * ctm[0]
            h0 = h * ctm[3]
            rects.append((round(x0, 2), round(y0, 2), round(w0, 2), round(h0, 2)))

    w, h = page.mediabox.width, page.mediabox.height
    print(f"# page {pno} size {float(w):.1f} x {float(h):.1f}; {len(rects)} rects")

    def inband(r):
        if yband is None:
            return True
        y0, y1 = r[1], r[1] + r[3]
        return not (y1 < yband[0] or y0 > yband[1])

    vert = [r for r in rects if abs(r[3]) >= min_h and abs(r[2]) <= 6 and inband(r)]
    vert.sort(key=lambda r: (round(r[0], 1), -r[1]))
    print(f"# vertical rules (>= {min_h} pt tall, <= 6 pt wide): {len(vert)}")
    for x, y, rw, rh in vert:
        print(f"x={x:8.2f} y0={y:7.2f} w={rw:5.2f} h={rh:7.2f}  xfrac={x / float(w):.4f}")

    horiz = [r for r in rects if abs(r[2]) >= 20 and abs(r[3]) <= 6 and inband(r)]
    horiz.sort(key=lambda r: (-r[1], r[0]))
    print(f"# horizontal rules (>= 20 pt wide, <= 6 pt tall): {len(horiz)}")
    for x, y, rw, rh in horiz:
        print(f"y={y:8.2f} x0={x:7.2f} w={rw:7.2f} h={rh:5.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
