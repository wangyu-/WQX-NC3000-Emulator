"""Disassemble the SPCE061A firmware image (spce061a/rom/061.dat).

The image maps at 0x8200, so file_offset = (addr - 0x8200) * 2.
Decoding is done by spce061a/emu/unspcore.py (a Python port of MAME's
unSP disassembler), so semantics match the emulator we already trust.

Usage:
  python unspdis.py <addr> [-n COUNT] [--rom PATH] [--comments]
  python unspdis.py <addr> -n 40 --follow        # recursive from <addr>
  python unspdis.py <addr> --verify FILE         # compare against a dump
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "spce061a", "emu"))

import unspcore  # noqa: E402

ROM_BASE = 0x8200
DEFAULT_ROM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "spce061a", "rom", "061.dat")

# Hardware registers.  Names/addresses taken from the vendor's own header
# (SPCE061A SDK: Example\A1600\[5] A1600_ASM\SPCE061A.h) - the previous table
# here was wrong for most of the port/ADC/UART entries (it looked like the map
# of a *different* Sunplus chip), which is how "0x7015" ended up labelled
# P_DAC1_DATA when it is really P_ADC_Ctrl and P_DAC1 lives at 0x7017.
IO = {
    0x7000: "P_IOA_Data", 0x7001: "P_IOA_Buffer", 0x7002: "P_IOA_Dir",
    0x7003: "P_IOA_Attrib", 0x7004: "P_IOA_Latch",
    0x7005: "P_IOB_Data", 0x7006: "P_IOB_Buffer", 0x7007: "P_IOB_Dir",
    0x7008: "P_IOB_Attrib", 0x7009: "P_Feedback",
    0x700A: "P_TimerA_Data", 0x700B: "P_TimerA_Ctrl",
    0x700C: "P_TimerB_Data", 0x700D: "P_TimerB_Ctrl",
    0x700E: "P_TimeBase_Setup", 0x700F: "P_TimeBase_Clear",
    0x7010: "P_INT_Ctrl", 0x7011: "P_INT_Clear", 0x7012: "P_Watchdog_Clear",
    0x7013: "P_SystemClock", 0x7014: "P_ADC", 0x7015: "P_ADC_Ctrl",
    0x7016: "P_DAC2", 0x7017: "P_DAC1", 0x7019: "P_LVD_Ctrl",
    0x701A: "P_SIO_Data", 0x701B: "P_SIO_Addr_Low", 0x701C: "P_SIO_Addr_Mid",
    0x701D: "P_SIO_Addr_High", 0x701E: "P_SIO_Ctrl", 0x701F: "P_SIO_Start",
    0x7020: "P_SIO_Stop",
    0x7021: "P_UART_Command1", 0x7022: "P_UART_Command2", 0x7023: "P_UART_Data",
    0x7024: "P_UART_BaudScalarLow", 0x7025: "P_UART_BaudScalarHigh",
    0x702A: "P_DAC_Ctrl", 0x702B: "P_ADC_MUX_Ctrl", 0x702C: "P_ADC_MUX_Data",
    0x702D: "P_INT_Mask",
}

# RAM scalars named in docs/NC3000_单词发音数据.md and earlier passes
SCALARS = {
    0x0000: "g_status_flags (bit7 = stream started)",
    0x0001: "g_volume",
    0x04C2: "g_filter_out",
    0x04C3: "g_ring_slot_bytes",
    0x04C4: "g_ring_size",
    0x04CE: "g_codec_index",
    0x06E8: "g_rec_u4",
    0x0713: "g_init_state (0x14 after codec4 init)",
    0x075A: "g_stage_wptr",
}


class Image:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()

    def word(self, addr):
        off = (addr - ROM_BASE) * 2
        if off < 0 or off + 2 > len(self.data):
            return None
        return struct.unpack_from("<H", self.data, off)[0]

    def words(self, addr, n):
        out = []
        for i in range(n):
            w = self.word(addr + i)
            if w is None:
                break
            out.append(w)
        return out


def comment(text, addr):
    """Annotate a line: [0x7012] style operands and branch targets."""
    for reg, name in IO.items():
        if ("0x%04X" % reg) in text:
            return "  ; " + name
    scalar = None
    for reg, name in SCALARS.items():
        if ("0x%04X" % reg) in text:
            scalar = name
            break
    if scalar:
        return "  ; " + scalar
    return ""


def disasm(img, addr, count, show_bytes=True, comments=True):
    out = []
    pc = addr
    for _ in range(count):
        op = img.word(pc)
        if op is None:
            out.append("  %04X: <past end of image>" % pc)
            break
        nxt = img.word(pc + 1)
        dec = unspcore.decode(op, 0 if nxt is None else nxt, pc=pc)
        raw = "%04X" % op
        if dec.length == 2 and nxt is not None:
            raw += " %04X" % nxt
        line = "%04X:%s %s" % (pc, ("%-9s" % raw).ljust(9),
                               dec.text)
        if comments:
            line += comment(dec.text, dec.target)
        out.append(line)
        pc += dec.length
    return out, pc


def follow(img, start, limit=4000):
    """Recursive-descent listing from `start` until the closure is covered."""
    seen = {}
    todo = [start]
    order = []
    while todo and len(seen) < limit:
        addr = todo.pop()
        while addr is not None and addr not in seen and len(seen) < limit:
            op = img.word(addr)
            if op is None:
                break
            nxt = img.word(addr + 1)
            dec = unspcore.decode(op, 0 if nxt is None else nxt, pc=addr)
            seen[addr] = (op, nxt, dec)
            order.append(addr)
            if dec.kind in ("call", "jmp", "branch") and isinstance(dec.target, int):
                todo.append(dec.target)
            if dec.kind in ("ret", "jmp") or dec.bad:
                break
            addr = addr + dec.length
    return seen, order


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("addr", help="start address, e.g. 0x9EA3")
    ap.add_argument("-n", "--count", type=int, default=40)
    ap.add_argument("--rom", default=DEFAULT_ROM)
    ap.add_argument("--comments", action="store_true", default=True)
    ap.add_argument("--follow", action="store_true",
                    help="recursive descent instead of linear")
    ap.add_argument("--verify", metavar="FILE",
                    help="compare linear output against a previous dump")
    args = ap.parse_args()

    try:
        start = int(args.addr, 0)
    except ValueError:
        start = int(args.addr, 16)
    img = Image(args.rom)

    if args.follow:
        seen, order = follow(img, start)
        for addr in sorted(seen):
            op, nxt, dec = seen[addr]
            raw = "%04X" % op
            if dec.length == 2 and nxt is not None:
                raw += " %04X" % nxt
            line = "%04X:%-9s %s" % (addr, raw, dec.text)
            line += comment(dec.text, dec.target)
            print(line)
        print("# %d instructions reached from 0x%04X" % (len(seen), start))
        return 0

    lines, end = disasm(img, start, args.count)
    if args.verify:
        with open(args.verify, "r", encoding="utf-8") as fh:
            want = [l.rstrip("\n") for l in fh]
        mine = {}
        for l in lines:
            if ":" in l:
                a = int(l.split(":")[0], 16)
                mine[a] = l.split(":", 1)[1].strip()
        bad = 0
        for l in want:
            if ":" not in l:
                continue
            a = int(l.split(":")[0], 16)
            if a not in mine:
                continue
            their = l.split(":", 1)[1].strip()
            their_core = their.split("  ; ")[0].strip()
            theirs_core = " ".join(their_core.split())
            ours_core = " ".join(mine[a].split("  ; ")[0].split())
            if ours_core != theirs_core:
                bad += 1
                if bad < 12:
                    print("MISMATCH %04X\n  ours : %s\n  theirs: %s"
                          % (a, mine[a], their_core))
        print("# verified %d lines, %d mismatches" % (len(mine), bad))
        return 0 if bad == 0 else 1

    for line in lines:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
