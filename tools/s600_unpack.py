"""SACM-S600: transform the PC "Speech Packing Format" (18 bytes/frame) into
the "Receiving Format" (15 words/frame) that the SPDS104A / 061 S600 decoder
accepts.

Spec sources (SPDS104A PROGRAMMING GUIDE v0.5):
  * Table 3, page 17  - receiving format: per-package field widths.
  * page 18 notes     - d11..d8 voicing flags, 8-bit/5-bit words, control codes.
  * page 20 figure    - byte 1/2 layout of the packing format.
  * page 27 example   - a 16-bit word is written low byte first, high byte last.

Bit accounting (this is what pins the layout down):
  4-bit SubFrame_Header (= Package(3) B11..B8) + 140 payload bits = 144 bits
  = 18 bytes.  The 140 payload bits are the packages' *valid* bits only; every
  "X" (don't care) column of Table 3 is simply not stored:

    P0 11  P1 11  P2 12  P3 8  P4 10  P5 10  P6 5  P7 10  P8 10
    P9  8  P10 10 P11 10 P12 5  P13 10 P14 10        -> 140

  P6/P12 are 5-bit only: note 2 says subframes 1 and 3 carry a 5-bit word.

Within one package the sample code on page 20 shows the parameter arriving as
`(byte2 << 4) | (byte1 & 0x0F)`, i.e. the low nibble (B3..B0) is stored BEFORE
the remaining high bits.  `--order` selects that (`nibble`, default) or a plain
MSB-first field order (`msb`) while the bit order is being pinned down.

Usage:
  python s600_unpack.py frame <file.vnt> --frame 0 [--order nibble|msb]
  python s600_unpack.py dump  <file.vnt> <out.words> [--order ...] [--frames N]
  python s600_unpack.py check <file.vnt>            # structural self-test
"""
import argparse
import sys

# ---------------------------------------------------------------------------
# Table 3 (page 17): Package ID and the *valid* parameter bits of each package.
# `lo`/`hi` are inclusive bit indices of the 12-bit parameter (B11..B0).
# ---------------------------------------------------------------------------
PACKAGES = [
    # name    id   valid bits            note
    ("P0", 0b0000, (10, 0), "Initial, Byte0_B10..B0"),
    ("P1", 0b0001, (10, 0), "Initial, Byte1_B10..B0"),
    ("P2", 0b0010, (11, 0), "Initial, Byte2_B11..B0"),
    ("P3", 0b0011, (7, 0), "Subframe0 byte; B11..B8 are the frame header"),
    ("P4", 0b0100, (9, 0), "Subframe0"),
    ("P5", 0b0101, (9, 0), "Subframe0"),
    ("P6", 0b0011, (4, 0), "Subframe1 (5-bit word, note 2)"),
    ("P7", 0b0100, (9, 0), "Subframe1"),
    ("P8", 0b0101, (9, 0), "Subframe1"),
    ("P9", 0b0011, (7, 0), "Subframe2 (8-bit word, note 2)"),
    ("P10", 0b0100, (9, 0), "Subframe2"),
    ("P11", 0b0101, (9, 0), "Subframe2"),
    ("P12", 0b0011, (4, 0), "Subframe3 (5-bit word, note 2)"),
    ("P13", 0b0100, (9, 0), "Subframe3"),
    ("P14", 0b0101, (9, 0), "Subframe3"),
]

FRAME_BYTES = 18


class BitReader:
    """Reads a byte array MSB-first, bit by bit."""

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def bit(self) -> int:
        if self.pos >= len(self.data) * 8:
            raise EOFError("out of bits")
        byte = self.data[self.pos >> 3]
        value = (byte >> (7 - (self.pos & 7))) & 1
        self.pos += 1
        return value

    def bits(self, n: int) -> int:
        value = 0
        for _ in range(n):
            value = (value << 1) | self.bit()
        return value


def field_bit_order(lo: int, hi: int, order: str) -> list:
    """Bit indices of a package field, in the order they appear in the stream.

    `lo`/`hi` are inclusive indices in the 12-bit parameter (B11..B0).
    `hi` is the most significant valid bit, `lo` the least significant.
    """
    if order == "msb":
        return list(range(hi, lo - 1, -1))
    # nibble: the low four bits (B3..B0, msb first) come first, then the rest
    # of the field from its most significant bit down to B4.
    low_bits = [b for b in range(min(3, hi), lo - 1, -1)]
    high_bits = [b for b in range(hi, 3, -1)]
    return low_bits + high_bits


def unpack_frame(frame: bytes, order: str = "nibble") -> list:
    """18 packed bytes -> list of 15 16-bit receiving words (ID<<12 | param)."""
    if len(frame) != FRAME_BYTES:
        raise ValueError("frame must be %d bytes" % FRAME_BYTES)
    rd = BitReader(frame)
    header = rd.bits(4)  # Package(3) B11..B8 = subframe voicing flags
    words = []
    for _name, pkg_id, (hi, lo), _note in PACKAGES:
        placed = 0
        for bit_index in field_bit_order(lo, hi, order):
            if rd.bit():
                placed |= 1 << bit_index
        words.append((pkg_id << 12) | placed)
    # Package(3) B11..B8 came from the frame header
    p3 = [i for i, p in enumerate(PACKAGES) if p[0] == "P3"][0]
    words[p3] = (PACKAGES[p3][1] << 12) | (header << 8) | (words[p3] & 0xFF)
    return words, header


def iter_frames(path: str, limit=None):
    with open(path, "rb") as fh:
        data = fh.read()
    usable = len(data) - (len(data) % FRAME_BYTES)
    count = usable // FRAME_BYTES
    if limit:
        count = min(count, limit)
    for i in range(count):
        yield i, data[i * FRAME_BYTES : (i + 1) * FRAME_BYTES]


def cmd_frame(args) -> int:
    for i, frame in iter_frames(args.path, args.frame + 1):
        if i != args.frame:
            continue
        words, header = unpack_frame(frame, args.order)
        print("frame %d: %s" % (i, " ".join("%02X" % b for b in frame)))
        print("SubFrame_Header = %X  (d11..d8 = %s)"
              % (header, " ".join(str((header >> k) & 1) for k in (3, 2, 1, 0))))
        for (name, pkg_id, rng, note), word in zip(PACKAGES, words):
            param = word & 0xFFF
            print("  %-4s id=%s param=%03X  B%d..B%d   %s"
                  % (name, format((word >> 12) & 0xF, "04b"), param,
                     rng[0], rng[1], note))
        return 0
    print("no such frame", file=sys.stderr)
    return 1


def cmd_dump(args) -> int:
    n = 0
    with open(args.out, "wb") as fh:
        for _i, frame in iter_frames(args.path, args.frames):
            words, _h = unpack_frame(frame, args.order)
            for w in words:
                fh.write(bytes((w & 0xFF, (w >> 8) & 0xFF)))
            n += 1
    print("wrote %d frames (%d words) to %s" % (n, n * 15, args.out))
    return 0


def cmd_check(args) -> int:
    """Structural self-test on real data: bit accounting + header consistency."""
    import collections

    widths = sum(p[2][0] - p[2][1] + 1 for p in PACKAGES) + 4
    print("bit accounting: header 4 + payload %d = %d bits = %d bytes"
          % (widths - 4, widths, widths // 8))
    assert widths == FRAME_BYTES * 8, "field widths do not add up!"

    headers = collections.Counter()
    total = 0
    for _i, frame in iter_frames(args.path):
        headers[frame[0] >> 4] += 1
        total += 1
    print("frames=%d  SubFrame_Header nibble histogram: %s"
          % (total, sorted(headers.items())))
    print("every frame starts with the 0xF header nibble: %s"
          % ("yes" if list(headers) == [0xF] else "NO -> check alignment"))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("frame", help="decode one frame into 15 words")
    p.add_argument("path")
    p.add_argument("--frame", type=int, default=0)
    p.add_argument("--order", choices=("nibble", "msb"), default="nibble")
    p.set_defaults(func=cmd_frame)

    p = sub.add_parser("dump", help="convert a .vnt into 15-word frames")
    p.add_argument("path")
    p.add_argument("out")
    p.add_argument("--order", choices=("nibble", "msb"), default="nibble")
    p.add_argument("--frames", type=int, default=None)
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("check", help="bit accounting + header self-test")
    p.add_argument("path")
    p.set_defaults(func=cmd_check)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
