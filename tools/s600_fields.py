"""S600 frame field layout, as reversed from the SPCE061A firmware.

Source of truth (all addresses in spce061a/rom/061.dat, base 0x8200):

  * 0xC8C6  codec dispatch: index 4 (and 5) -> init 0x9EA3, per-block 0xD225
  * 0xD225 -> 0xA694 -> 0xA8B6 (per sub-frame) / 0xA6B7 (frame start)
  * 0xA6B7  reads **9 words** per frame from the ring (18 bytes) via 0xA08F;
            0xA08F hands out a word low byte first, then high byte, so the
            frame's byte order is the file's byte order.
  * 0xA71B(width) extracts a bit field; 0xA706 consumes it **LSB first**
            inside each 16-bit word -> i.e. within each byte, bit 0 first.
  * 0xA8C9..0xA907  frame-level fields (12 of them)
  * 0xA94A/0xA950   per-sub-frame word: 8 bits on sub-frames 0/2, 5 bits on
            sub-frames 1/3  == manual note 2 (page 18)
  * 0xA760          the remaining 11 bits of every sub-frame

Bit accounting (this is the proof the layout is complete):

    38 (frame level) + (8+25) + (5+25) + (8+25) + (5+25) = 144 bits = 18 bytes

Voicing flags: field 3, 4 bits, MSB first; bit3 = Subframe0 ... bit0 =
Subframe3; per Table 3 note 1 a 0 means that sub-frame is unvoiced.

Usage:
  python s600_fields.py <file.vnt> [--frame N] [--count K] [--stats]
"""
import argparse
import collections
import sys

FRAME_BITS = 144

# (width, name, destination) -- extracted at sub-frame 0, in stream order
FRAME_LEVEL = [
    (3, "f0", "[0x03B3]"),
    (1, "f1", "[0x03B4] low"),
    (4, "voiced_flags", "[0x04A5] bit3..bit0 = sf0..sf3"),
    (3, "f3", "[0x03B4] |= v<<1"),
    (4, "f4", "[0x03B5]"),
    (4, "f5", "[0x03B6]"),
    (4, "f6", "[0x03B7]"),
    (3, "f7", "[0x03B8]"),
    (3, "f8", "[0x03B9]"),
    (3, "f9", "[0x03BA]"),
    (3, "f10", "[0x03BB]"),
    (3, "f11", "[0x03BC]"),
]

SUBFRAME_TAIL = 11  # 1 + 4 + (4 voiced | 5 unvoiced) + 1 + 1


class LsbBitReader:
    """Bits are taken from bit 0 of each byte upward, bytes in order."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def bit(self):
        byte = self.data[self.pos >> 3]
        value = (byte >> (self.pos & 7)) & 1
        self.pos += 1
        return value

    def field(self, width):
        """First bit read becomes the field's MSB (matches 0xA71B + 0xA706)."""
        value = 0
        for _ in range(width):
            value = (value << 1) | self.bit()
        return value


def parse_frame(frame):
    """Return (frame_level_values, per_subframe_values, flags)."""
    rd = LsbBitReader(frame)
    frame_level = []
    for width, name, _dst in FRAME_LEVEL:
        frame_level.append((name, width, rd.field(width)))
    flags = frame_level[2][2]
    subs = []
    for sf in range(4):
        voiced = (flags >> (3 - sf)) & 1
        word_width = 8 if sf in (0, 2) else 5
        rec = {
            "sf": sf,
            "voiced": bool(voiced),
            "width": word_width,
            "word": rd.field(word_width),
            "pitch": rd.field(9),
        }
        rec["sign0"] = rd.field(1)
        rec["gain_idx"] = rd.field(4)
        if voiced:
            # 0xA77E: 1, 4, 4, 1, 1
            rec["tail"] = rd.field(4)
            rec["sign1"] = rd.field(1)
            rec["sign2"] = rd.field(1)
        else:
            # 0xA7AA: 1, 4, 5, 1
            rec["tail"] = rd.field(5)
            rec["sign1"] = rd.field(1)
            rec["sign2"] = None
        subs.append(rec)
    return frame_level, subs, flags, rd.pos


def iter_frames(path, count=None, skip=0):
    with open(path, "rb") as fh:
        data = fh.read()
    n = len(data) // 18
    for i in range(skip, n if count is None else min(n, skip + count)):
        yield i, data[i * 18:(i + 1) * 18]


def cmd_show(args):
    for i, frame in iter_frames(args.path, 1, args.frame):
        fl, subs, flags, bits = parse_frame(frame)
        print("frame %d: %s" % (i, " ".join("%02X" % b for b in frame)))
        print("bits consumed = %d (frame = %d)" % (bits, FRAME_BITS))
        print("frame level:")
        for name, width, value in fl:
            print("   %-12s %2d bits = %d (0x%X)" % (name, width, value, value))
        print("voiced flags = 0x%X  sf0..sf3 = %s"
              % (flags, " ".join(str((flags >> (3 - s)) & 1) for s in range(4))))
        for r in subs:
            print("  subframe%d %s  word(%d)=%d pitch9=%d gain=%d tail=%d "
                  "s=%d%d%d"
                  % (r["sf"], "voiced  " if r["voiced"] else "unvoiced",
                     r["width"], r["word"], r["pitch"], r["gain_idx"],
                     r["tail"], r["sign0"], r["sign1"], r["sign2"]))
        return 0
    return 1


def cmd_stats(args):
    hist = collections.Counter()
    flags_hist = collections.Counter()
    bits_ok = True
    total = 0
    for _i, frame in iter_frames(args.path, args.count):
        fl, subs, flags, bits = parse_frame(frame)
        if bits != FRAME_BITS:
            bits_ok = False
        flags_hist[flags] += 1
        for r in subs:
            hist[(r["sf"], r["voiced"])] += 1
        total += 1
    print("frames=%d  every frame consumed exactly %d bits: %s"
          % (total, FRAME_BITS, "YES" if bits_ok else "NO"))
    print("voiced-flag histogram: %s"
          % sorted((hex(k), v) for k, v in flags_hist.items())[:16])
    for key in sorted(hist):
        print("  subframe%d voiced=%s : %d" % (key[0], key[1], hist[key]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("show")
    p.add_argument("path")
    p.add_argument("--frame", type=int, default=0)
    p.set_defaults(func=cmd_show)
    p = sub.add_parser("stats")
    p.add_argument("path")
    p.add_argument("--count", type=int, default=20000)
    p.set_defaults(func=cmd_stats)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
