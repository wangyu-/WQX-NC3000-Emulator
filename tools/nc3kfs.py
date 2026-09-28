#!/usr/bin/env python3
"""NC3000 NAND 文件系统小工具（只读分析）。

已知映射（见 docs/NC3000_NAND_文件系统分析.md §7）:
    FS 扇区 N  <->  NAND 设备页 32*N
    .nand 文件偏移 = (32*N - 64) * 528        （.nand 第 0 页 = 设备第 64 页）
    一个扇区 = 32 * 528 = 16896 字节

子命令:
  dirs   <nand>                 列出所有目录块（连续 16 字节目录项）
  find   <nand> <GBK 名字>       按名字定位目录项
  list   <nand> <块内偏移>        打印某个目录块的完整内容
  map    <nand> <offset>         文件偏移 -> 设备页/扇区
  hex    <nand> <offset> <len>   十六进制查看
"""
import sys


def page_of_off(off):
    """文件偏移 -> 设备页号（.nand 里 page 0 == 设备页 64）"""
    p, r = divmod(off, 528)
    return 64 + p, r


def sector_of_off(off):
    page, r = page_of_off(off)
    return page // 32, page % 32, r


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    data = open(sys.argv[2], "rb").read()

    if cmd == "map":
        off = int(sys.argv[3], 0)
        page, r = page_of_off(off)
        sec, pin, _ = sector_of_off(off)
        print("file offset 0x%X -> device page %d (in-sector page %d) -> FS sector 0x%X (%d), sector byte 0x%X"
              % (off, page, pin, sec, sec, pin * 528 + r))
        return 0

    if cmd == "hex":
        off = int(sys.argv[3], 0)
        ln = int(sys.argv[4], 0) if len(sys.argv) > 4 else 256
        for i in range(0, ln, 16):
            chunk = data[off + i:off + i + 16]
            print("%08X  %-47s  %s" % (off + i, chunk.hex(" "),
                                       "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)))
        return 0

    def entry_name(off):
        raw = data[off + 2:off + 16]
        raw = raw.split(b"\x00")[0]
        try:
            return raw.decode("gbk")
        except UnicodeDecodeError:
            return None

    def runs():
        out = []
        off = 0
        while off + 16 <= len(data):
            idv = int.from_bytes(data[off:off + 2], "little")
            nm = entry_name(off)
            if idv == 0 or idv > 4096 or not nm or not nm.strip() or "\ufffd" in nm:
                off += 2
                continue
            ids = [idv]
            names = [nm]
            p = off + 16
            while p + 16 <= len(data):
                x = int.from_bytes(data[p:p + 2], "little")
                if x == 0xFFFF or x == 0:
                    break
                if x > 4096:
                    break
                n2 = entry_name(p)
                if not n2 or not n2.strip() or "\ufffd" in n2:
                    break
                ids.append(x)
                names.append(n2)
                p += 16
            if len(names) >= 2:
                out.append((off, ids, names))
                off = p
            else:
                off += 2
        return out

    if cmd == "dirs":
        mn = int(sys.argv[3]) if len(sys.argv) > 3 else 3
        rs = runs()
        print("directory blocks: %d" % len(rs))
        for off, ids, names in rs:
            if len(names) < mn:
                continue
            page, r = page_of_off(off)
            sec, pin, _ = sector_of_off(off)
            print("\n@0x%08X  device page %d  FS sector 0x%X  in-sector page %d  byte 0x%X  (%d entries)"
                  % (off, page, sec, pin, r, len(names)))
            print("   " + " | ".join("%d:%s" % (i, n) for i, n in zip(ids, names)))
        return 0

    if cmd == "find":
        target = sys.argv[3]
        key = target.encode("gbk")
        hits = 0
        pos = 0
        while True:
            i = data.find(key, pos)
            if i < 0:
                break
            hits += 1
            off = i - 2
            page, r = page_of_off(off)
            sec, pin, _ = sector_of_off(off)
            idv = int.from_bytes(data[off:off + 2], "little")
            print("entry @0x%08X inode=%d device page=%d FS sector=0x%X in-sector page=%d byte=0x%X"
                  % (off, idv, page, sec, pin, r))
            pos = i + 1
        print("hits: %d" % hits)
        return 0

    if cmd == "list":
        off = int(sys.argv[3], 0)
        for k in range(64):
            o = off + k * 16
            idv = int.from_bytes(data[o:o + 2], "little")
            nm = entry_name(o)
            print("  +0x%03X  inode=%-5d name=%r" % (k * 16, idv, nm))
            if idv == 0xFFFF:
                break
        return 0

    if cmd == "norlog":
        # NOR 里那份 inode 日志表：0xFD000 起，每条 32 字节
        base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0xFD000
        end = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0xFF000
        last = None
        counts = {}
        for o in range(base, end, 0x20):
            rec = data[o:o + 0x20]
            if rec == b"\xff" * 0x20:
                continue
            idv = int.from_bytes(rec[0:2], "little")
            attr = int.from_bytes(rec[2:4], "little")
            st = rec[4]
            cnt = int.from_bytes(rec[0x0E:0x10], "little")
            blocks = [int.from_bytes(rec[k:k + 2], "little") for k in (0x12, 0x14, 0x16, 0x18)]
            term = rec[0x1E:0x20]
            print("@0x%06X id=%-5d attr=%04X st=%02X cnt=%-5d blk=%s term=%s"
                  % (o, idv, attr, st, cnt, [hex(x) for x in blocks], term.hex()))
            last = o
            counts[idv] = counts.get(idv, 0) + 1
        print("last non-empty record slot: %s" % (hex(last) if last else None))
        dup = {k: v for k, v in counts.items() if v > 1}
        print("ids with more than one record: %s" % (sorted(dup.items())[:20],))
        return 0

    if cmd == "tree":
        # 从 NAND 的 inode 表 + 目录块走一遍目录树
        def inode_rec(idv):
            o = 0 + (idv - 1) * 0x20
            if o + 0x20 > 0x4000:
                return None
            r = data[o:o + 0x20]
            if r[0x1E:0x20] != b"\xa8\xa8":
                return None
            return {
                "attr": int.from_bytes(r[2:4], "little"),
                "cnt": int.from_bytes(r[0x0E:0x10], "little"),
                "blk": [int.from_bytes(r[k:k + 2], "little") for k in (0x12, 0x14, 0x16, 0x18)],
            }

        def dir_entries(block):
            # FS 扇区 block -> 设备页 32*block -> 文件偏移 (page-64)*528
            page = block * 32
            off = (page - 64) * 528
            out = []
            for k in range(1024):
                o = off + k * 16
                if o + 16 > len(data):
                    break
                idv = int.from_bytes(data[o:o + 2], "little")
                if idv == 0xFFFF:
                    break
                nm = entry_name(o)
                if idv == 0 or nm is None:
                    continue
                out.append((idv, nm))
            return out

        roots = []
        for roff in (0x4000, (0xCF0 * 32 - 64) * 528):
            if roff + 16 <= len(data):
                ids = []
                for k in range(16):
                    o = roff + k * 16
                    idv = int.from_bytes(data[o:o + 2], "little")
                    if idv == 0xFFFF:
                        break
                    ids.append(idv)
                roots.append((roff, ids))
        print("root blocks:", [(hex(o), len(i)) for o, i in roots])
        roff = roots[0][0] if roots else 0x4000

        def walk(block, depth, indent):
            ents = dir_entries(block)
            shown = 0
            for idv, nm in ents:
                if nm in (".", ".."):
                    continue
                rec = inode_rec(idv)
                kind = "?"
                if rec:
                    kind = "DIR" if (rec["attr"] & 0xF000) == 0xE000 else "FILE"
                print("%s%-14s id=%-5d %s" % (indent, nm, idv, kind))
                shown += 1
                if rec and kind == "DIR" and depth < 3 and rec["blk"][0] != 0xFFFF:
                    walk(rec["blk"][0], depth + 1, indent + "    ")
            if shown == 0:
                print(indent + "(空)")

        # 根目录块先从候选里挑一个能解析出 sysdir 的
        for o, ids in roots:
            if 1 in ids:
                roff = o
        print("using root block @0x%X" % roff)
        ents = []
        for k in range(16):
            o = roff + k * 16
            idv = int.from_bytes(data[o:o + 2], "little")
            if idv == 0xFFFF:
                break
            nm = entry_name(o)
            if nm and nm not in (".", ".."):
                ents.append((idv, nm))
        for idv, nm in ents:
            rec = inode_rec(idv)
            kind = "DIR" if rec and (rec["attr"] & 0xF000) == 0xE000 else "FILE"
            print("%-14s id=%-5d %s" % (nm, idv, kind))
            if kind == "DIR" and rec and rec["blk"][0] != 0xFFFF:
                for id2, nm2 in dir_entries(rec["blk"][0]):
                    if nm2 in (".", ".."):
                        continue
                    r2 = inode_rec(id2)
                    k2 = "DIR" if r2 and (r2["attr"] & 0xF000) == 0xE000 else "FILE"
                    print("    %-14s id=%-5d %s" % (nm2, id2, k2))
        return 0

    print("unknown command %s" % cmd)
    return 1


if __name__ == "__main__":
    sys.exit(main())
