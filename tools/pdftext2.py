"""Exact per-span text extraction with font attribution and positions.

Why this exists: the Sunplus manuals use Type1 subset fonts whose
/Differences arrays rename every glyph to /G01, /G02, ... starting at code 1,
so the character codes carry no ASCII meaning and there is no /ToUnicode.
Each font therefore needs its own code->char Rosetta, which this script
applies from a JSON file (see tools/glyphmaps/*.json).

Usage:
  python pdftext2.py <pdf> <page> [--maps DIR] [--json OUT] [--min-y Y]
"""
import json
import os
import sys

import pypdf
from pypdf.generic import ContentStream


def glyph_name_map(page, reader) -> dict:
    """font resource key -> {glyphname: char} placeholder (identity by name)."""
    out = {}
    fonts = page.get("/Resources", {}).get("/Font", {})
    for key, ref in fonts.items():
        f = ref.get_object()
        base = str(f.get("/BaseFont", ""))
        out[str(key)] = base
    return out


def load_maps(path: str) -> dict:
    if not os.path.isdir(path):
        return {}
    maps = {}
    for name in os.listdir(path):
        if name.endswith(".json"):
            with open(os.path.join(path, name), "r", encoding="utf-8") as fh:
                maps[os.path.splitext(name)[0]] = json.load(fh)
    return maps


def raw_bytes(obj) -> bytes:
    """Original bytes of a PDF string object (no re-encoding round trip)."""
    b = getattr(obj, "original_bytes", None)
    if b is not None:
        return b
    return str(obj).encode("latin-1", "replace")


def decode(bs: bytes, table: dict) -> str:
    """Byte N selects glyph /GNN (hex), which the per-font table maps to a char.

    Unmapped codes are shown as {NN} so gaps stay visible while building the
    Rosetta from the rendered page.
    """
    out = []
    for b in bs:
        name = "G%02X" % b
        if not table:
            out.append("{%s}" % name)
        else:
            out.append(table.get(name, "{%s}" % name))
    return "".join(out)


def main() -> int:
    pdf_path = sys.argv[1]
    pno = int(sys.argv[2])
    maps_dir = "tools/glyphmaps"
    if "--maps" in sys.argv:
        maps_dir = sys.argv[sys.argv.index("--maps") + 1]
    min_y = None
    if "--min-y" in sys.argv:
        min_y = float(sys.argv[sys.argv.index("--min-y") + 1])

    reader = pypdf.PdfReader(pdf_path)
    page = reader.pages[pno - 1]
    names = glyph_name_map(page, reader)
    maps = load_maps(maps_dir)

    cs = ContentStream(page.get_contents(), reader)
    font_key = None
    tm = [1, 0, 0, 1, 0, 0]
    tlm = list(tm)
    spans = []

    def emit(text: str) -> None:
        if not text:
            return
        base = names.get(font_key, "")
        table = None
        for stem, mp in maps.items():
            if stem and stem in base:
                table = mp
                break
        spans.append((round(tlm[5], 1), round(tlm[4], 1), font_key, base, decode(text, table)))

    for operands, operator in cs.operations:
        op = operator.decode("latin-1") if isinstance(operator, bytes) else operator
        if op == "Tf":
            font_key = str(operands[0])
        elif op == "Tm":
            tm = [float(x) for x in operands]
            tlm = list(tm)
        elif op in ("Td", "TD"):
            tlm[4] = tlm[4] + float(operands[0])
            tlm[5] = tlm[5] + float(operands[1])
        elif op == "T*":
            tlm[5] = tlm[5] - 12
        elif op == "BT":
            tm = [1, 0, 0, 1, 0, 0]
            tlm = list(tm)
        elif op == "Tj":
            emit(raw_bytes(operands[0]))
        elif op == "'":
            tlm[5] = tlm[5] - 12
            emit(raw_bytes(operands[0]))
        elif op == '"':
            tlm[5] = tlm[5] - 12
            emit(raw_bytes(operands[2]))
        elif op == "TJ":
            arr = operands[0]
            parts = [
                raw_bytes(x)
                for x in arr
                if isinstance(x, (pypdf.generic.TextStringObject, pypdf.generic.ByteStringObject))
            ]
            if parts:
                emit(b"".join(parts))

    spans.sort(key=lambda s: (-s[0], s[1]))
    for y, x, key, base, text in spans:
        if min_y is not None and y < min_y:
            continue
        print(f"y={y:7.1f} x={x:6.1f} {key:5s} {base[-24:]:24s} {text!r}")
    print(f"# {len(spans)} spans")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
