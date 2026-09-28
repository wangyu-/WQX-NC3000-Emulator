"""NC3000 AHD 词典（inode 9）**词条枚举**工具 —— 2026-09-26 第二轮的突破。

背景（见 docs/NC3000_词典包结构_20260926.md 与
docs/archive/交接文档_2026-09-24_09-26.md 里源文件 交接文档_20260926c.md）：

* 词条数据从文件偏移 0x0E2768 开始，每个词条 = **`[长度 2B LE][文法状态 1B][数据…]`**；
  长度 = 从这个词条的“状态字节”到**下一个词条状态字节**的距离
  （厂方 `doc_format.txt` 第二章：“当前单词压缩数据长度 2 Bytes / 文法状态 1 Byte”）。
  判据：27,303 个逐字母词头的相邻间距中 **38.2% 恰好等于该长度域**，
  而逐字母词头占全部词条的比例正好是 28.5%~36.5% ⇒ 长度域成立。
* 因此可以**按长度逐条走遍全表**：实测 0x0E2768..0x10B44B9 共 **95,684 个词条**。
* 词条顺序 = **字母序**，这个序号就是 `word_ID`(inode 25) 里的“词号”：
  实测 word_ID 里 flag=2 的第 2 个值 = 11 → 词条 #11 = `Aarhus` ✓
  （#54=Abaya, #82=Abdias, #86=abducens 同样吻合）。

用法：
    python tools/dict_entries.py walk                 # 只看统计（词条数/长度/状态分布）
    python tools/dict_entries.py head <词号> [n]       # 看某几个词条的词头
    python tools/dict_entries.py audio                # 导出“有发音且拼写可直读”的词表
    python tools/dict_entries.py find <拼写>           # 按拼写定位词号/邻近词条
    python tools/dict_entries.py range <起> <止>        # 打印一段词号
"""
import sys

import numpy as np

IMG = r'out\nc3000_full.bin'
AHD_BLK, AHD_BLOCKS = 868, 1184
ENTRY_START = 0x0E2768
ENTRY_END = 0x10B44B9
WORD_ID_BLK, WORD_ID_BLOCKS = 2501, 4

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass


def load():
    img = np.fromfile(IMG, dtype=np.uint8)
    b = img[AHD_BLK * 0x4000:(AHD_BLK + AHD_BLOCKS) * 0x4000].astype(np.int64)
    return img, b


def walk(b):
    ents = []
    pos = ENTRY_START
    while pos < ENTRY_END:
        L = int(b[pos - 2] | (b[pos - 1] << 8))
        if L <= 0:
            break
        ents.append((pos, L))
        pos += L
    return ents


def headword(b, pos, L):
    """返回 (拼写 or None, 说明)。状态字节为 0 时词头紧随其后；否则先跳过文法 ID。"""
    j = pos
    if b[j] != 0:
        # 状态非 0：文法 ID 3 字节（厂方 data_stru.txt）
        j += 4
    else:
        j += 1
    out = bytearray()
    while j < pos + L and j < len(b) - 1:
        if b[j] == 0xF0:
            c = int(b[j + 1])
            if c == 0x04:                    # 词头结束 / 音标起始
                return bytes(out).decode('latin1'), 'literal'
            if c < 32 or c >= 127:
                return None, 'ctrl'
            out.append(c)
            j += 2
            continue
        return None, 'code'                      # 压缩码（常用词）
    return None, 'end'


def word_id():
    img = np.fromfile(IMG, dtype=np.uint8)
    w = img[WORD_ID_BLK * 0x4000:(WORD_ID_BLK + WORD_ID_BLOCKS) * 0x4000].astype(np.int64)
    r = w[:7413 * 3].reshape(-1, 3)
    flag, u16 = r[:, 0], r[:, 1] | (r[:, 2] << 8)
    return {f: np.sort(u16[flag == f]) for f in (1, 2)}, flag, u16


def cmd_walk():
    _, b = load()
    ents = walk(b)
    lens = np.array([L for _, L in ents])
    status = np.array([int(b[p]) for p, _ in ents])
    lit = 0
    for p, L in ents:
        hw, kind = headword(b, p, L)
        if kind == 'literal': lit += 1
    print('词条数 %d   长度: 中位 %d 均值 %.1f 最大 %d' % (len(ents), int(np.median(lens)), lens.mean(), lens.max()))
    print('状态字节 == 0 的比例 %.3f   逐字母词头 %d (%.1f%%)' % (float((status == 0).mean()), lit, 100.0 * lit / len(ents)))
    print('首词条 @0x%X len=%d, 末词条 @0x%X len=%d' % (ents[0][0], ents[0][1], ents[-1][0], ents[-1][1]))


def cmd_head(args):
    _, b = load()
    ents = walk(b)
    i0 = int(args[0]); n = int(args[1]) if len(args) > 1 else 8
    for i in range(i0, min(i0 + n, len(ents))):
        p, L = ents[i]
        hw, kind = headword(b, p, L)
        print('词号 %6d  @0x%07X len=%4d  %s' % (i, p, L, hw if hw else '(' + kind + ')'))


def cmd_range(args):
    cmd_head([args[0], str(int(args[1]) - int(args[0]) + 1)])


def cmd_audio():
    _, b = load()
    ents = walk(b)
    segs, _, _ = word_id()
    rows = []
    for f in (1, 2):
        for W in segs[f]:
            W = int(W)
            if W in (0, 65535) or W >= len(ents):
                continue
            p, L = ents[W]
            hw, kind = headword(b, p, L)
            rows.append((W, f, hw or '', kind, p, L))
    lit = [r for r in rows if r[2]]
    with open(r'out\ahd_audio_words.txt', 'w', encoding='utf-8') as fh:
        fh.write('# 词号\tflag\t拼写(逐字母者可直读)\t类型\t文件偏移\t词条长度\n')
        for W, f, hw, kind, p, L in rows:
            fh.write('%d\t%d\t%s\t%s\t0x%07X\t%d\n' % (W, f, hw, kind, p, L))
    print('word_ID 词号合计 %d（flag1 %d / flag2 %d），落在词条范围内 %d' % (
        sum(len(segs[f]) for f in (1, 2)), len(segs[1]), len(segs[2]), len(rows)))
    print('其中拼写可直读（逐字母词头）%d 条 -> out/ahd_audio_words.txt' % len(lit))
    print('  例：', ', '.join('%s(#%d)' % (w, W) for W, f, w, k, p, L in lit[:12]))


def cmd_find(args):
    target = args[0].lower()
    _, b = load()
    ents = walk(b)
    lit = {}
    for i, (p, L) in enumerate(ents):
        hw, kind = headword(b, p, L)
        if hw:
            lit[i] = hw
    keys = sorted((w.lower(), i, w) for i, w in lit.items())
    import bisect
    k = bisect.bisect_left(keys, (target, -1, ''))
    print('按字母序，"%s" 落在词号 %d 附近：' % (args[0], keys[max(0, k - 1)][1] if k else -1))
    for j in range(max(0, k - 3), min(len(keys), k + 3)):
        print('   %6d  %s' % (keys[j][1], keys[j][2]))
    lo = keys[k - 1][1] if k else 0
    hi = keys[k][1] if k < len(keys) else len(ents) - 1
    print('   区间 [%d, %d]：' % (lo, hi))
    for i in range(lo, hi + 1):
        p, L = ents[i]
        hw, kind = headword(b, p, L)
        print('     %6d  len=%4d  %s' % (i, L, hw if hw else '(' + kind + ')'))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd, a = sys.argv[1], sys.argv[2:]
    {'walk': cmd_walk,
     'head': lambda: cmd_head(a),
     'range': lambda: cmd_range(a),
     'audio': cmd_audio,
     'find': lambda: cmd_find(a)}.get(cmd, lambda: print(__doc__))()


if __name__ == '__main__':
    main()
