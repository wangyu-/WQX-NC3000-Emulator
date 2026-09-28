"""Extract the AHD headword list from the compressed entry data.

Entry layout found empirically: `00 00` marker, then the headword written as
F0-escaped ASCII literals (F0 xx = literal byte xx), then `F0 04` and the
rest of the entry (compressed definition).
"""
import re
import sys

import numpy as np

AHD = r'out\file_ahddata.bin'
DICT = r'out\nc3000_full.bin'
WORD_ENTRY = re.compile(r"[A-Za-z][A-Za-z' .-]*")


def extract(path):
    """Return (offset, headword, marker) where marker is the control byte that
    terminates the headword's literal run (0x04 => entry carries a phonetic)."""
    a = np.fromfile(path, dtype=np.uint8)
    n = len(a)
    i = 0
    out = []
    while i < n - 4:
        if a[i] == 0:
            j = i + 1
            s = ''
            mark = None
            while j + 1 < n and a[j] == 0xF0:
                c = int(a[j + 1])
                if c < 32 or c >= 127:
                    mark = c
                    break
                s += chr(c)
                j += 2
            if mark == 0x04 and 1 <= len(s) <= 25 and WORD_ENTRY.fullmatch(s):
                out.append((i, s, mark))
                i = j
                continue
        i += 1
    return out


def main():
    res = extract(AHD)
    print('headwords:', len(res))
    marks = {}
    for _, _, m in res:
        marks[m] = marks.get(m, 0) + 1
    print('marker histogram:', {('0x%02X' % k if k is not None else None): v for k, v in sorted(marks.items(), key=lambda x: -x[1])[:8]})
    print('first 15:', [(s, m) for _, s, m in res[:15]])
    print('sample 3000:', [(s, m) for _, s, m in res[3000:3012]])
    print('last 15:', [(s, m) for _, s, m in res[-15:]])
    with open(r'out\ahd_headwords.txt', 'w', encoding='utf-8') as f:
        for off, s, m in res:
            f.write('%07X\t%s\t%s\n' % (off, s, ('%02X' % m) if m is not None else '--'))
    print('wrote out/ahd_headwords.txt')

    # cross-check: word_ID values vs ahd_celp
    img = np.fromfile(DICT, dtype=np.uint8)
    wd = img[2501 * 0x4000: 2501 * 0x4000 + 0x10000]
    rec = wd[:7413 * 3].reshape(-1, 3)
    addr = (rec[:, 0].astype(np.int64) << 16) | rec[:, 1] | (rec[:, 2].astype(np.int64) << 8)
    blob = img[2820 * 0x4000:(2820 + 16) * 0x4000]
    celp = blob[:131072 * 2].reshape(-1, 2)
    celp = celp[:, 0] | (celp[:, 1] << 8)
    for name, shift in [('as-is', 0), ('-0x10000', 0x10000), ('-0x1610C', 0x1610C)]:
        idx = addr - shift
        ok = (idx >= 0) & (idx < len(celp))
        if ok.any():
            print('celp hit with shift %-8s: %.3f  (%d tested)' % (name, (celp[idx[ok]] != 0xFFFF).mean(), int(ok.sum())))


if __name__ == '__main__':
    main()
