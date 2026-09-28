"""Dump the syllable record chain for one or more ahd_celp indices.

chain: celp_data + 0x284000 + N*5, 5 bytes per record.  按厂方/固件语义
(docs/NC3000_单词发音数据.md §9.1)：
    b2 bit6 = 1 → **同一词还有下一音节**（继续），bit6 = 0 → 该词结束。
    V  = ((b2>>7)<<16)|(b1<<8)|b0   （17 位，S600 帧号）
    FC = (b2 & 0x3F) + (b4 != 0)
    SO/EO = b3/b4                   （发给 061 的描述符）

⚠ 2026-09-26 修正两处旧 bug：
  1) 旧版把 bit6==1 当“结束”，方向反了（本轮实听锚点因此只听到第一个音节）；
  2) 旧版 `celp[:,1] << 8` 在 uint8 上溢出，把 16 位表值读成了低字节
     （导致 ahd_celp 里 47.5% 的 FFFF 被读成 0xFF）。
"""
import sys

import numpy as np

IMG = r'out\nc3000_full.bin'
CELP_BASE = 2506 * 0x4000          # inode 26, first block
REC_OFF = 0x284000


def celp_table():
    img = np.fromfile(IMG, dtype=np.uint8)
    blob = img[2820 * 0x4000:(2820 + 16) * 0x4000].astype(np.int64)
    t = blob[:131072 * 2].reshape(-1, 2)
    return img, (t[:, 0] | (t[:, 1] << 8)).astype(np.int64)


def chain(img, n):
    out = []
    p = CELP_BASE + REC_OFF + 5 * n
    for _ in range(64):
        b = img[p:p + 5]
        v = ((int(b[2]) >> 7) << 16) | (int(b[1]) << 8) | int(b[0])
        fc = (int(b[2]) & 0x3F) + (1 if int(b[4]) != 0 else 0)
        more = bool(int(b[2]) & 0x40)      # 1 = 后面还有音节
        out.append((v, fc, more, int(b[3]), int(b[4])))
        p += 5
        if not more:
            break
    return out


def main():
    img, tab = celp_table()
    idxs = [int(x) for x in sys.argv[1:]] or [24844]
    for i in idxs:
        n = int(tab[i])
        c = chain(img, n)
        frames = sum(x[1] for x in c)
        print('celp idx %6d -> record %6d : %d syllables, %d frames' % (i, n, len(c), frames))
        for v, fc, more, so, eo in c:
            print('    V=%6d FC=%2d more=%d SO=%3d EO=%3d  frames %d..%d' % (v, fc, int(more), so, eo, v, v + fc - 1))


if __name__ == '__main__':
    main()
