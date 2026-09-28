"""Render PDF pages to PNG (optionally cropped) for visual inspection.

Usage:
  python pdfpage.py <pdf> <outdir> <dpi> <page1,page2,...> [--tag NAME]
  python pdfpage.py <pdf> <outdir> <dpi> <page> --cropfrac L,T,R,B
      render the full page, then keep the window [L..R] x [T..B]
      (fractions 0..1 with origin at the TOP-LEFT; done with PIL, so it is
      exact and independent of pypdfium2's own crop quirks)

Page numbers are 1-based.
"""
import os
import sys

import pypdfium2 as pdfium


def main() -> int:
    args = [a for a in sys.argv[1:]]
    if len(args) < 4:
        print(__doc__)
        return 2
    pdf_path, outdir, dpi = args[0], args[1], float(args[2])
    pages_spec = args[3]
    tag = ""
    crop = None
    frac = None
    rest = args[4:]
    i = 0
    while i < len(rest):
        if rest[i] == "--tag":
            tag = rest[i + 1]
            i += 2
        elif rest[i] == "--box":
            crop = [float(x) for x in rest[i + 1].split(",")]
            i += 2
        elif rest[i] == "--cropfrac":
            frac = [float(x) for x in rest[i + 1].split(",")]
            i += 2
        else:
            print("unknown arg", rest[i])
            return 2

    os.makedirs(outdir, exist_ok=True)
    doc = pdfium.PdfDocument(pdf_path)
    scale = dpi / 72.0
    for spec in pages_spec.split(","):
        pno = int(spec)
        page = doc[pno - 1]
        bitmap = page.render(scale=scale)
        img = bitmap.to_pil()
        if frac or crop:
            fl, ft, fr, fb = frac if frac else crop
            w, h = img.size
            img = img.crop(
                (
                    int(round(fl * w)),
                    int(round(ft * h)),
                    int(round(fr * w)),
                    int(round(fb * h)),
                )
            )
        name = f"{tag + '_' if tag else ''}pdf_p{pno:02d}.png"
        path = os.path.join(outdir, name)
        img.save(path)
        print(f"{path}  {img.size[0]}x{img.size[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
