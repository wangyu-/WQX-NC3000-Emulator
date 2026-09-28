"""Inspect raw PDF text bytes + font encodings for a page.

Some subset fonts rename glyphs (/G01, /G02, ...) in /Differences while
keeping the original character CODES, in which case the literal bytes in the
content stream are still ASCII.  This script dumps the font encodings and the
literal string operands (Tj/TJ) from the page content stream so that
hypothesis can be checked directly.

Usage:
  python pdfraw.py <pdf> <page> [--strings]
"""
import re
import sys

import pypdf


def main() -> int:
    pdf_path = sys.argv[1]
    pno = int(sys.argv[2])
    show_strings = "--strings" in sys.argv

    reader = pypdf.PdfReader(pdf_path)
    page = reader.pages[pno - 1]
    res = page.get("/Resources", {})
    fonts = res.get("/Font", {})
    print(f"# page {pno} fonts: {len(fonts)}")
    for key, ref in fonts.items():
        f = ref.get_object()
        bf = f.get("/BaseFont")
        sub = f.get("/Subtype")
        enc = f.get("/Encoding")
        print(f"  {key}: {bf} subtype={sub}")
        if enc:
            eo = enc.get_object() if hasattr(enc, "get_object") else enc
            if isinstance(eo, dict):
                print(f"      BaseEncoding={eo.get('/BaseEncoding')}")
                diffs = eo.get("/Differences")
                if diffs is not None:
                    d = diffs.get_object() if hasattr(diffs, "get_object") else diffs
                    print(f"      Differences({len(d)}): {list(d)[:12]} ...")
            else:
                print(f"      Encoding={eo}")
        print(f"      ToUnicode={'yes' if f.get('/ToUnicode') else 'no'}")

    if not show_strings:
        return 0

    data = page.get_contents().get_data()
    print("# literal string operands:")
    # very small scanner: captures (...)  and <...> operands
    for m in re.finditer(rb"\((?:\\.|[^\\()])*\)", data):
        raw = m.group(0)[1:-1]
        if len(raw) < 2:
            continue
        txt = raw.decode("latin-1")
        print(f"  {raw[:40]!r}  -> {txt[:40]!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
