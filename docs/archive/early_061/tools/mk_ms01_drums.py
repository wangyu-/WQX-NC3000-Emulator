"""Build the missing MS01 drum resources into the demo image.

The chapter9 MS01 example was linked without any drum resources: its
`T_SACM_MS01_DrumTable` (ROM 0x8045) holds a single bogus entry (0xFA00) and the
20 `T_SACM_MS01_DrumSampleRate` words follow it.  The engine's drum path
(0x857B -> 0x8630) still runs for channels 0/1, so all it needs is real data:

    T_SACM_MS01_DrumTable[drum] -> 4-word descriptor (sa, seg, ea, seg)
                                     |         |
                                     |         +-- 4-bit ADPCM words, little endian
                                     +-- address of the descriptor

`instrument & 0x7F` is the drum index (0x80..0x93 in the scores -> drums 0..19),
and the drum order is the one from the newer GPCE063 example:

    0 BD1 1 SD1 2 HT1 3 CYM1 4 CBL 5 CBH 6 HAP1 7 JB1 8 CC 9 CHT1
   10 OHT1 11 PHT1 12 HTM 13 HFT 14 LT 15 LFT 16 HB 17 LB 18 HBQ 19 LBQ

The data itself is byte-identical to what 061.dat carries at 0xD33D.. (19 of the
20 files), so the .ADP files from the SDK are the real ROM format.

Free ROM in the linked image is 0xE414..0xFFF4 (the last song ends at 0xE413,
the interrupt vectors start at 0xFFF5).  That is 14274 bytes, less than the
22378 bytes of all twenty drums, so the drums are packed by (songs using them,
size); anything that does not fit gets a silent one-word blob and is reported.

usage: python work/mk_ms01_drums.py [--fit] [--sacrifice NAME] [--out FILE]

`--sacrifice LA1` (the default) reclaims that song's ROM so that all twenty
drums fit, and repoints the song's resource at a one-word score holding 0x3F
("Song ended" - the same marker HK1 ends with), so selecting it plays silence
instead of interpreting ADPCM bytes as a score.  `--fit` keeps every song and
embeds only the drums that fit in the free area (15 of 20).
"""

import os
import sys

sys.path.insert(0, "work")
from ms01_run import load_s37, S37  # noqa: E402

DRUM_NAMES = ["BD1", "SD1", "HT1", "CYM1", "CBL", "CBH", "HAP1", "JB1", "CC",
              "CHT1", "OHT1", "PHT1", "HTM", "HFT", "LT", "LFT", "HB", "LB",
              "HBQ", "LBQ"]
DRUM_DIR = r"work\gpce063\demoCode\MS01\DRUM"

# how many of the 12 demo songs use each drum (from the SONG/*.ASM comments)
USAGE = {0: 8, 1: 8, 2: 8, 3: 2, 4: 6, 5: 6, 6: 3, 7: 1, 8: 4, 9: 3,
         10: 4, 11: 1, 12: 4, 13: 3, 14: 3, 15: 4, 16: 5, 17: 5, 18: 5, 19: 5}

FREE_LO = 0xE414          # first free word
FREE_HI = 0xFFF5          # first vector word
# opcode addresses of the two "rX = rY + 0x8045" instructions; the immediate
# operand sits in the following word.
DRUM_TABLE_REF = (0x8146, 0x8587)
OLD_TABLE = 0x8045

SONGS = ["WISH1", "ALA1", "ALAINS", "DECK1", "GREEN1", "HK1", "LA1", "PP1",
         "S31", "TW1", "WINE1", "1231"]
RES_TABLE = 0x8009          # 12 entries x 4 words: sa, seg, ea, seg
SONG_END = 0x003F           # the score byte the .ASM calls ";Song ended"


def load_adp(name):
    with open(os.path.join(DRUM_DIR, name + ".ADP"), "rb") as f:
        return f.read()


class Alloc:
    """Sequential allocator over a list of [lo, hi) word ranges."""

    def __init__(self, ranges):
        self.ranges = [list(r) for r in sorted(ranges)]

    def alloc(self, nwords, align=1):
        for r in self.ranges:
            lo = (r[0] + align - 1) & ~(align - 1)
            if lo + nwords <= r[1]:
                r[0] = lo + nwords
                return lo
        return None


def main():
    fit_only = "--fit" in sys.argv
    sacrifice = None
    if "--sacrifice" in sys.argv:
        sacrifice = sys.argv[sys.argv.index("--sacrifice") + 1].upper()
    elif not fit_only:
        sacrifice = "LA1"
    out = "outputs/emulator/ms01_image_drums.c"
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]

    rom = load_s37(S37)                       # word address -> word
    adp = {i: load_adp(n) for i, n in enumerate(DRUM_NAMES)}
    img = {a: rom.get(a, 0) for a in range(0x0800, 0x10000)}

    # ---- which ROM do we get to use? ---------------------------------------
    ranges = [[FREE_LO, FREE_HI]]
    lost = []
    if sacrifice:
        idx = SONGS.index(sacrifice)
        sa = img[RES_TABLE + idx * 4 + 0]
        ea = img[RES_TABLE + idx * 4 + 2]
        ranges.append([sa, ea])
        lost.append((sacrifice, sa, ea))
        print("sacrificing %s (ROM 0x%04X..0x%04X, %d bytes)"
              % (sacrifice, sa, ea - 1, (ea - sa) * 2))

    order = sorted(range(20), key=lambda i: (-USAGE[i], len(adp[i])))
    payload = sum(r[1] - r[0] for r in ranges) - 20 - 20 * 4 - 2
    chosen, used = [], 0
    for i in order:
        words = (len(adp[i]) + 1) // 2
        if used + words <= payload:
            chosen.append(i)
            used += words
    missing = [i for i in range(20) if i not in chosen]

    # ---- lay everything out -------------------------------------------------
    al = Alloc(ranges)
    blob_at = {}
    for i in sorted(chosen):
        words = (len(adp[i]) + 1) // 2
        a = al.alloc(words)
        if a is None:
            print("ERROR: drum data does not fit")
            return 1
        blob_at[i] = a
    descr_at = {}
    for i in range(20):
        descr_at[i] = al.alloc(4)
    table_at = al.alloc(20)
    silent_at = al.alloc(1)           # one zero word for a missing drum
    end_at = al.alloc(1)              # the one-word "Song ended" score
    if descr_at[19] is None or table_at is None or end_at is None:
        print("ERROR: tables do not fit")
        return 1

    # data words
    for i in chosen:
        d = adp[i]
        for k in range(0, len(d), 2):
            lo = d[k]
            hi = d[k + 1] if k + 1 < len(d) else 0
            img[blob_at[i] + k // 2] = lo | (hi << 8)
    # descriptors: sa, seg, ea, seg   (seg = 0: everything lives in bank 0)
    img[silent_at] = 0
    for i in range(20):
        if i in chosen:
            sa = blob_at[i]
            ea = sa + (len(adp[i]) + 1) // 2
        else:
            sa, ea = silent_at, silent_at + 1
        img[descr_at[i] + 0] = sa
        img[descr_at[i] + 1] = 0
        img[descr_at[i] + 2] = ea
        img[descr_at[i] + 3] = 0
    # drum table
    for i in range(20):
        img[table_at + i] = descr_at[i]
    # patch the two "+= 0x8045" immediates
    for pc in DRUM_TABLE_REF:
        assert img[pc + 1] == OLD_TABLE, "unexpected instruction at %04X" % pc
        img[pc + 1] = table_at
    # a sacrificed song gets a one-word score that ends immediately
    if sacrifice:
        idx = SONGS.index(sacrifice)
        img[end_at] = SONG_END
        img[RES_TABLE + idx * 4 + 0] = end_at
        img[RES_TABLE + idx * 4 + 1] = 0
        img[RES_TABLE + idx * 4 + 2] = end_at + 1
        img[RES_TABLE + idx * 4 + 3] = 0
    # ---- emit C -------------------------------------------------------------
    words = [img[a] for a in range(0x0800, 0x10000)]
    ram = [rom.get(a, 0) for a in range(0x0800)]
    with open(out, "w") as f:
        f.write("/* generated by work/mk_ms01_drums.py from MS01.S37 + DRUM/*.ADP\n"
                " * %d of 20 drums embedded (%d bytes); drum table 0x%04X, "
                "descriptors 0x%04X..0x%04X, silent word 0x%04X%s */\n"
                % (len(chosen), sum(len(adp[i]) for i in chosen), table_at,
                   min(descr_at.values()), max(descr_at.values()), silent_at,
                   ("; %s sacrificed (resource -> 0x%04X = 0x003F \"Song ended\")"
                    % (sacrifice, end_at)) if sacrifice else ""))
        f.write("#include <stdint.h>\n\n")
        f.write("const uint32_t ms01_rom_base = 0x0800;\n")
        f.write("const uint32_t ms01_rom_count = %d;\n" % len(words))
        f.write("uint16_t ms01_rom[%d] = {\n" % len(words))
        for i in range(0, len(words), 12):
            f.write("    " + " ".join("0x%04X," % v for v in words[i:i + 12]) + "\n")
        f.write("};\n\n")
        f.write("uint16_t ms01_ram_init[%d] = {\n" % len(ram))
        for i in range(0, len(ram), 12):
            f.write("    " + " ".join("0x%04X," % v for v in ram[i:i + 12]) + "\n")
        f.write("};\n")

    print("%s: %d/%d drums, %d bytes of ADPCM" %
          (out, len(chosen), 20, sum(len(adp[i]) for i in chosen)))
    print("  ADPCM data: " + ", ".join(
        ("0x%04X..0x%04X" % (blob_at[i], blob_at[i] + (len(adp[i]) + 1) // 2 - 1))
        for i in sorted(chosen)[:1]) + " ... ")
    print("  descriptors 0x%04X..0x%04X, drum table 0x%04X..0x%04X, "
          "silent 0x%04X, end-score 0x%04X"
          % (min(descr_at.values()), max(descr_at.values()), table_at,
             table_at + 19, silent_at, end_at))
    print("  patched the DrumTable operand at %s -> 0x%04X"
          % (" and ".join("%04X" % (p + 1) for p in DRUM_TABLE_REF), table_at))
    print("  included: " + ", ".join("%d:%s" % (i, DRUM_NAMES[i])
                                     for i in sorted(chosen)))
    if missing:
        print("  NOT included (silent): " + ", ".join("%d:%s" % (i, DRUM_NAMES[i])
                                                       for i in missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
