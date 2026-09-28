"""NC3000 词典包（inode 8 / inode 9）结构勘查工具 —— 2026-09-26 这一轮。

厂方文档在 src_nc2000/nc2000f/dict_packet/h/：
    data_stru.txt  压缩数据空间摆放（索引表 / 高频词库 / 单词压缩数据 / 例句 / 文法）
    doc_format.txt 第四章=字典数据压缩方案（编码表 + 分块 + 生成各种索引）
    INDEX.H        各区起始地址（块号+块内偏移，16 KB 一块）+ 各区长度

本工具只做「只读勘查」：把这一轮实测到的结构固化成可复现的命令，避免下一轮重做。

用法（在仓库根目录）：
    python tools/dict_pack.py inodes                  # 列 inode 8/9 的块表与大小
    python tools/dict_pack.py map 9                   # 每 16 KB 块统计 -> 区域图
    python tools/dict_pack.py index 9                 # 解 0..0x41050 的 4 字节地址表
    python tools/dict_pack.py targets 9 flat          # 地址表落点：flat / base99 / base104 ...
    python tools/dict_pack.py text 9 0x41050 240      # 按 GBK 打印一段（控制码用可见符号）
    python tools/dict_pack.py order 9                 # 文本区「词序」采样：升序/降序？
    python tools/dict_pack.py escapes 9               # F0 xx 转义统计（0x00..0x12 等）
    python tools/dict_pack.py nodes 9 0xC0000 40      # 12 字节节点表预览
    python tools/dict_pack.py pair 9 0x792A39        # 看某个地址（flat）附近的字节
"""
import collections
import sys

import numpy as np

try:  # Windows 控制台默认 GBK，中文/替换字符会炸，这里统一成 UTF-8
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

IMG = r'out\nc3000_full.bin'
INODE_BLK = {8: 99, 9: 868, 24: 2501 - 1, 25: 2501, 26: 2506, 27: 2820, 28: 2837, 29: 2857}
INODE_CNT = {8: 768, 9: 1184, 25: 4, 26: 313, 27: 16, 28: 20, 29: 16}
BLK = 0x4000

# 本轮实测：两份词典文件的低频区布局一致
TEXT_START = 0x41050      # 4 字节地址表结束处，正文开始
INDEX_ENTRIES = 66580     # = 66579 个词条 + 1 个虚拟结束项
IDX_BYTES = INDEX_ENTRIES * 4   # 0x41050


def inode_bytes(n):
    img = np.fromfile(IMG, dtype=np.uint8)
    blk = INODE_BLK[n]
    cnt = INODE_CNT[n]
    return np.asarray(img[blk * BLK:(blk + cnt) * BLK])


def index_values(b):
    tb = b[0:IDX_BYTES].reshape(-1, 4)
    return ((tb[:, 3].astype(np.int64) << 16) | (tb[:, 2].astype(np.int64) << 8)
            | tb[:, 1].astype(np.int64))


def cmd_inodes():
    img = np.fromfile(IMG, dtype=np.uint8)
    head = img[:BLK]
    for i in range(0, 40):
        rec = head[i * 32:(i + 1) * 32].tobytes()
        cnt = int.from_bytes(rec[14:18], 'little')
        blk0 = int.from_bytes(rec[18:20], 'little')
        endblk = int.from_bytes(rec[24:26], 'little')
        print('id=%3d cnt=%5d blk0=%5d end=%5d size=%9d  %s'
              % (i + 1, cnt, blk0, endblk, cnt * BLK, rec.hex(' ')))


def cmd_map(n):
    b = inode_bytes(n)
    prev = None
    print('inode%d  len=0x%x (%d)' % (n, len(b), len(b)))
    for i in range(len(b) // BLK):
        w = b[i * BLK:(i + 1) * BLK]
        f0 = float((w == 0xF0).mean())
        asc = float(((w >= 0x20) & (w < 0x7F)).mean())
        hi = float((w >= 0x81).mean())
        z = float((w == 0).mean())
        sig = (round(f0, 2), round(asc, 1), round(hi, 1), round(z, 2))
        if sig != prev:
            print('%4d %07x  f0=%.2f asc=%.1f hi=%.2f zero=%.2f  %s'
                  % (i, i * BLK, f0, asc, hi, z, w[:16].tobytes().hex(' ')))
            prev = sig


def cmd_index(n):
    b = inode_bytes(n)
    v = index_values(b)
    page = v >> 16
    off = v & 0xFFFF
    print('条目 %d (=%d 词条 + 1 虚拟)；表长 0x%X' % (len(v), len(v) - 1, IDX_BYTES))
    print('首字节取值:', np.unique(b[0:IDX_BYTES].reshape(-1, 4)[:, 0], return_counts=True))
    print('页号范围 %d..%d  页内偏移 0..0x%X' % (page.min(), page.max(), off.max()))
    print('页号取值:', sorted(set(page.tolist())))
    d = np.diff(v)
    small = d[(d > 0) & (d < 20)]
    print('差值：小步(0<d<20) %d 个，合计 %d 字节；大步 %d 个；负值 %d 个'
          % (len(small), int(small.sum()), int((d >= 20).sum()), int((d < 0).sum())))
    print('小步直方图:', sorted(collections.Counter(small.tolist()).items())[:20])
    big = [(i, int(x)) for i, x in enumerate(d) if x >= 20 or x < 0]
    print('跳变处:', big[:12], '...' if len(big) > 12 else '')
    print('最后 6 个值:', [hex(int(x)) for x in v[-6:]])


def mapping(val, mode):
    """地址编码：ROM $718C 用 page = N>>14、offset = N&0x3FFF（= 64 KB 块里只用前 16 KB）。
    所以「文件内偏移」= (N>>14)*0x4000 + (N&0x3FFF) == N 本身（这就是 flat）。
    page16/baseNN 是另外两种试过的读法，留作对照。"""
    if mode in ('flat', 'p14'):
        return (val >> 14) * BLK + (val & 0x3FFF)
    if mode == 'page':
        return (val >> 16) * BLK + (val & 0xFFFF)
    if mode.startswith('base'):
        base = int(mode[4:])
        return ((val >> 16) - base) * BLK + (val & 0xFFFF)
    raise SystemExit('未知映射 %s' % mode)


def cmd_targets(n, mode):
    b = inode_bytes(n)
    v = index_values(b)
    v = v[(v >> 16) != 32]          # 最后一项是虚拟结束项（页号 0x20），不是真地址
    t = mapping(v, mode)
    ok = (t > 0) & (t < len(b) - 2)
    t = t[ok]
    print('映射 %s：%d 个落点，范围 0x%X..0x%X' % (mode, len(t), t.min(), t.max()))
    c = collections.Counter(b[t].tolist())
    print('落点首字节 top:', [(hex(k), int(x)) for k, x in c.most_common(10)])
    prev = b[t - 1].astype(np.int64)
    cur = b[t].astype(np.int64)
    bad = int(np.count_nonzero((prev >= 0x81) & (prev <= 0xFE) & (cur >= 0x40)
                               & (cur <= 0xFE) & (cur != 0x7F)))
    print('落点落在 GBK 汉字内部（非法边界）: %d / %d = %.3f' % (bad, len(t), bad / max(1, len(t))))
    for k in (0, 1, 2, 3, 4, 5, 100, 1000):
        if k < len(t):
            p = int(t[k])
            print('  #%-5d 0x%06X  %s' % (k, p, visible(b[p - 8:p + 16].tobytes().decode('gbk', 'replace'))))


def visible(s):
    out = []
    for ch in s:
        o = ord(ch)
        out.append({0: '\\0', 1: '\\x01', 2: '\\x02', 3: '\\x03', 4: '\\x04', 5: '\\x05',
                    0x12: '\\x12', 0x2F: '/'}.get(o, ch if o >= 0x20 else '\\x%02x' % o))
    return ''.join(out)


def cmd_text(n, off, ln):
    b = inode_bytes(n)
    s = b[off:off + ln].tobytes().decode('gbk', 'replace')
    print(visible(s))


def cmd_order(n, step=0x8000, width=56):
    b = inode_bytes(n)
    end = 0xBC000
    for off in range(TEXT_START, end, step):
        s = b[off:off + width].tobytes().decode('gbk', 'replace').replace('\n', ' ')
        print('%06x %s' % (off, visible(s)))


def cmd_escapes(n):
    b = inode_bytes(n)
    print('文件长 0x%X' % len(b))
    print('正文区 0x%X..0xBC000 控制码直方图:' % TEXT_START)
    reg = b[TEXT_START:0xBC000]
    c = collections.Counter(reg.tolist())
    print(' ', {hex(k): int(v) for k, v in sorted(c.items()) if k < 0x20 or k == 0x2F})
    t = b.tobytes()
    print('F0 xx 转义计数（整个文件）:')
    for x in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x12, 0x20, 0x2F):
        print('   F0 %02X : %d' % (x, t.count(bytes([0xF0, x]))))


def cmd_nodes(n, off, count, stride=12):
    b = inode_bytes(n)
    for i in range(count):
        r = b[off + i * stride:off + (i + 1) * stride]
        A = int(r[0]) | int(r[1]) << 8 | int(r[2]) << 16 | int(r[3]) << 24
        B = int(r[4]) | int(r[5]) << 8 | int(r[6]) << 16 | int(r[7]) << 24
        C = int(r[8]) | int(r[9]) << 8 | int(r[10]) << 16
        D = int(r[11])
        print('%3d  A=%08x B=%08x C=%06x D=%02x  %s'
              % (i, A, B, C, D, r.tobytes().hex(' ')))


def cmd_pair(n, off):
    b = inode_bytes(n)
    off = int(off)
    print('inode%d @0x%X' % (n, off))
    print(b[off - 32:off + 64].tobytes().hex(' '))
    print(visible(b[off - 32:off + 64].tobytes().decode('gbk', 'replace')))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    a = sys.argv[2:]
    if cmd == 'inodes':
        cmd_inodes()
    elif cmd == 'map':
        cmd_map(int(a[0]))
    elif cmd == 'index':
        cmd_index(int(a[0]))
    elif cmd == 'targets':
        cmd_targets(int(a[0]), a[1] if len(a) > 1 else 'flat')
    elif cmd == 'text':
        cmd_text(int(a[0]), int(a[1], 0), int(a[2], 0))
    elif cmd == 'order':
        cmd_order(int(a[0]))
    elif cmd == 'escapes':
        cmd_escapes(int(a[0]))
    elif cmd == 'nodes':
        cmd_nodes(int(a[0]), int(a[1], 0), int(a[2], 0))
    elif cmd == 'pair':
        cmd_pair(int(a[0]), int(a[1], 0))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
