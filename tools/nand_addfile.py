# -*- coding: utf-8 -*-
# Add files to an NC3000 NGFFS NAND image, for content the Lee dump lacks
# (e.g. /midi/musicN.mid).
#
# Layout facts (verified against the firmware's own NAND reads):
#   FS "sector" N  <->  NAND device page 32*N      (identity mapping)
#   .nand file page p <-> device page 64 + p
#   => file offset of sector N = (32*N - 64) * 528
#   inode table = .nand[0x0000:0x4000], 512 x 32-byte records ending A8 A8
#   directory block = 16-byte records [2B inode][14B name, NUL padded FF]
#
# usage: nand_addfile.py <nand> --dir <sector> --name <gbk> --file <host>
#                         [--inode N] [--sector S] [--dry-run]
import argparse
import struct
import sys

SECTOR_BYTES = 32 * 528
PAGE = 528
INODE_TAB = 0x4000


def sec_off(sec):
    return (32 * sec - 64) * PAGE


def parse_inodes(b):
    out = {}
    i = 0
    while i + 32 <= INODE_TAB:
        if b[i + 30] == 0xA8 and b[i + 31] == 0xA8:
            rid = struct.unpack_from("<H", b, i)[0]
            if 0 < rid <= 1024:
                cnt = struct.unpack_from("<H", b, i + 14)[0]
                blk = [struct.unpack_from("<H", b, i + 18 + 2 * k)[0] for k in range(4)]
                out[rid] = {"off": i, "count": cnt, "blk": blk}
                i += 30
        i += 2
    return out


def read_dir(b, off):
    ents = []
    k = 0
    while k < SECTOR_BYTES:
        rec = b[off + k:off + k + 16]
        if len(rec) < 16 or rec[:2] == b"\xff\xff":
            break
        rid = struct.unpack_from("<H", rec, 0)[0]
        name = bytes(rec[2:16]).split(b"\x00")[0]
        ents.append((rid, name))
        k += 16
    return ents, off + k


def used_sectors(inos):
    s = set()
    for n in inos.values():
        if n["count"] == 0 or n["blk"][0] == 0xFFFF:
            continue
        s.update(range(n["blk"][0], n["blk"][0] + n["count"]))
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nand")
    ap.add_argument("--dir", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--file", required=True)
    ap.add_argument("--inode", type=int, default=0)
    ap.add_argument("--sector", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--date", type=lambda x: int(x, 0), default=0x7A0800)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    b = bytearray(open(a.nand, "rb").read())
    inos = parse_inodes(b)
    used = used_sectors(inos)
    data = open(a.file, "rb").read()
    nsec = (len(data) + SECTOR_BYTES - 1) // SECTOR_BYTES
    ino = a.inode
    if not ino:
        # first completely empty 32-byte slot (records are 32 bytes apart)
        for k in range(1, 1025):
            o = (k - 1) * 32
            if all(v == 0xFF for v in b[o:o + 32]):
                ino = k
                break
        if not ino:
            sys.exit("inode table full")
    print("inodes=%d maxid=%d | %s: %d bytes -> %d sectors | new inode %d"
          % (len(inos), max(inos), a.file, len(data), nsec, ino))
    if ino in inos:
        sys.exit("inode %d already used" % ino)

    if a.sector:
        secs = list(range(a.sector, a.sector + nsec))
    else:
        secs = []
        cand = 0x100
        while len(secs) < nsec and cand < 4090:
            if cand not in used:
                o = sec_off(cand)
                if o + SECTOR_BYTES <= len(b) and all(v == 0xFF for v in b[o:o + SECTOR_BYTES]):
                    secs.append(cand)
            cand += 1
    if len(secs) != nsec:
        sys.exit("no free sector found (need %d)" % nsec)
    if any(s in used for s in secs):
        sys.exit("sector already in use")

    doff = sec_off(a.dir)
    ents, term = read_dir(b, doff)
    print("dir sector 0x%X: %d entries, terminator +0x%X" % (a.dir, len(ents), term - doff))
    name = a.name.encode("gbk")
    if len(name) > 13:
        sys.exit("name too long")
    if any(n == name for _, n in ents):
        sys.exit("name already in that directory")
    print("plan: inode %d sectors %s" % (ino, [hex(s) for s in secs]))
    if a.dry_run:
        return 0

    for i, s in enumerate(secs):
        o = sec_off(s)
        chunk = data[i * SECTOR_BYTES:(i + 1) * SECTOR_BYTES]
        b[o:o + SECTOR_BYTES] = chunk + b"\xff" * (SECTOR_BYTES - len(chunk))

    rec = bytearray(b"\xff" * 32)
    struct.pack_into("<H", rec, 0, ino)
    struct.pack_into("<H", rec, 2, 0x808A)
    rec[4] = 0xC0
    rec[5:8] = struct.pack("<I", a.date)[:3]
    rec[8:11] = struct.pack("<I", a.date)[:3]
    struct.pack_into("<H", rec, 14, nsec)
    struct.pack_into("<H", rec, 16, 0)
    blk = [secs[0], secs[1] if nsec > 1 else 0xFFFF,
           secs[2] if nsec > 2 else 0xFFFF, secs[-1]]
    for k, v in enumerate(blk):
        struct.pack_into("<H", rec, 18 + 2 * k, v)
    struct.pack_into("<H", rec, 26, 0xFFFF)
    struct.pack_into("<H", rec, 28, 0xFFFF)
    rec[30:32] = b"\xa8\xa8"
    off = (ino - 1) * 32
    if b[off + 30:off + 32] != b"\xff\xff":
        sys.exit("inode slot %d not free (bytes %02X %02X)"
                 % (ino, b[off + 30], b[off + 31]))
    b[off:off + 32] = rec

    ent = struct.pack("<H", ino) + name + b"\x00" + b"\xff" * (13 - len(name))
    b[term:term + 16] = ent
    b[term + 16:term + 18] = b"\xff\xff"

    open(a.nand, "wb").write(bytes(b))
    print("OK: %s | inode %d | sector 0x%X | name %s" % (a.nand, ino, secs[0], a.name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
