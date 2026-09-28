"""按“词号/拼写 → 音频”导出可试听的 WAV（2026-09-26 第三轮）。

链路：拼写 --(词条表, 字母序)--> 词号 --(ahd_celp/wqx_celp/cb_celp)--> 记录号
      --(5 字节记录表 @ celp_data+0x284000)--> 每条记录的 (V 帧号, FC 帧数)
      --(celp_data 从偏移 10 起，18 字节/帧)--> 帧序列 --> 061 解码 --> WAV

记录表的 bit6 语义（厂方/固件 §9.1）：**1 = 同一词还有下一音节**，0 = 该词结束。
本轮提供两种"词 → 记录范围"的取法，供实听对照：
    chain : 从表值 N 开始，按 bit6 链走（N, N+1, … 直到某条 bit6=0）
    span  : 取 N .. N+step-1，step = 表里相邻两项之差（§8.3 的“步长 = 音节数”）

用法：
    python tools/nc3000_word_audio.py dreadlocks --mode chain
    python tools/nc3000_word_audio.py dreadlocks --mode span --table ahd
    python tools/nc3000_word_audio.py 25208 --table ahd --mode span -o out\\a.wav
"""
import argparse
import os
import struct
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, 'out', 'nc3000_full.bin')
NOR = os.path.join(ROOT, 'info', 'NC3KSYSNOR.nor')
CELP_BLK, CELP_BLOCKS = 2506, 313
REC_OFF = 0x284000
TABLES = {'ahd': (2820, 16), 'wqx': (2837, 20), 'cb': (2857, 16)}
# 记录区 = celp_data+0x284000 起，**按 32 KB 页寻址**（固件 bank0B $97F0）：
#   cluster = 0x0A6B + 页号*2 + (偏移>>14)，页内偏移 = 偏移 & 0x3FFF
# 页号来自固件 bank0B $9993 起的 3 字节升序页表（累计值）。
NOR_PAGE_TAB = 0x5D993

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass


def table(name):
    img = np.fromfile(IMG, dtype=np.uint8)
    blk, blocks = TABLES[name]
    a = img[blk * 0x4000:(blk + blocks) * 0x4000].astype(np.int64)
    return a[0::2] | (a[1::2] << 8)


_PAGES = None


def page_table():
    """固件 bank0B $9993 的 3 字节升序页表（累计值，步长 ~1.7K）。"""
    global _PAGES
    if _PAGES is None:
        nor = np.fromfile(NOR, dtype=np.uint8).astype(np.int64)
        out, prev = [], None
        for i in range(96):
            o = NOR_PAGE_TAB + 3 * i
            v = int(nor[o]) | (int(nor[o + 1]) << 8) | (int(nor[o + 2]) << 16)
            if prev is not None and not (0 < v - prev < 4000):
                break
            out.append(v)
            prev = v
        _PAGES = out
    return _PAGES


def page_of(key):
    for i, v in enumerate(page_table()):
        if key < v:              # $9722：找第一个 (表项-1) >= key 的页
            return i
    return len(page_table()) - 1


def rec_offset(key):
    """返回记录区内的字节偏移（相对 celp_data+0x284000）。"""
    img = np.fromfile(IMG, dtype=np.uint8)
    t = img[CELP_BLK * 0x4000 + 0x240000:CELP_BLK * 0x4000 + 0x279500].astype(np.int64)
    T = t[0::2] | (t[1::2] << 8)
    if not (0 <= key < len(T)):
        raise SystemExit('key %d 超出 0x240000 表范围' % key)
    desc = int(T[key])
    return page_of(key) * 0x8000 + desc          # = (页*2 + desc>>14)*0x4000 + (desc & 0x3FFF)


def entries():
    img = np.fromfile(IMG, dtype=np.uint8)
    b = img[868 * 0x4000:(868 + 1184) * 0x4000].astype(np.int64)
    ents, pos = [], 0x0E2768
    while pos < 0x10B44B9:
        L = int(b[pos - 2] | (b[pos - 1] << 8))
        ents.append((pos, L))
        pos += L
    return b, ents


def headword(b, pos, L):
    j = pos + (1 if b[pos] == 0 else 4)
    out = bytearray()
    while j < pos + L and j < len(b) - 1:
        if b[j] == 0xF0:
            c = int(b[j + 1])
            if c == 0x04:
                return bytes(out).decode('latin1')
            if c < 32 or c >= 127:
                return None
            out.append(c)
            j += 2
            continue
        return None
    return None


def find_word(spell):
    b, ents = entries()
    tgt = spell.lower()
    for i, (p, L) in enumerate(ents):
        hw = headword(b, p, L)
        if hw and hw.lower() == tgt:
            return i
    return None


def records(img, tab, W, mode):
    n = int(tab[W])
    if n == 0xFFFF:
        raise SystemExit('词号 %d 在这张表里没有发音（FFFF）' % W)
    if mode == 'span':
        step = 1
        for k in range(W + 1, min(W + 8, len(tab))):
            if int(tab[k]) != 0xFFFF:
                step = int(tab[k]) - n
                break
        keys = [n + j for j in range(max(1, step))]
    else:                                   # chain：从该 key 开始按 bit6=还有 连走
        keys = []
        k = n
        while True:
            off = rec_offset(k)
            b = img[CELP_BLK * 0x4000 + REC_OFF + off:
                    CELP_BLK * 0x4000 + REC_OFF + off + 5].astype(np.int64)
            keys.append(k)
            if not (int(b[2]) & 0x40) or len(keys) >= 16:
                break
            k += 1
    out = []
    for k in keys:
        off = rec_offset(k)
        b = img[CELP_BLK * 0x4000 + REC_OFF + off:
                CELP_BLK * 0x4000 + REC_OFF + off + 5].astype(np.int64)
        v = ((int(b[2]) >> 7) << 16) | (int(b[1]) << 8) | int(b[0])
        fc = (int(b[2]) & 0x3F) + (1 if int(b[4]) != 0 else 0)
        more = bool(int(b[2]) & 0x40)
        out.append((k, v, fc, more))
    return out


def frames_for(img, recs):
    celp = np.fromfile(IMG, dtype=np.uint8)[CELP_BLK * 0x4000:(CELP_BLK + CELP_BLOCKS) * 0x4000]
    chunks = []
    for _, v, fc, _ in recs:
        for f in range(v, v + fc):
            chunks.append(celp[10 + 18 * f:10 + 18 * (f + 1)])
    return b''.join(bytes(c) for c in chunks)


def decode(nframes, binpath, outwav):
    probe = os.path.join(ROOT, 'spce061a', 'build', 'word.exe')
    if not os.path.exists(probe):
        raise SystemExit('缺少探针 ' + probe)
    # 探针的工作目录是 spce061a\，第 1 个参数是它自己的临时记录文件（相对路径）
    r = subprocess.run([probe, os.path.join('build', 'word_rec.bin'), os.path.abspath(outwav),
                        '27', os.path.abspath(binpath), '0x21', str(nframes), '0'],
                       cwd=os.path.join(ROOT, 'spce061a'), capture_output=True, text=True)
    if not os.path.exists(outwav):
        print(r.stdout[-800:], r.stderr[-400:])
        raise SystemExit('探针没有生成 WAV')


def trim_and_fade(path, tail=0.02):
    w = wave.open(path, 'rb')
    n, rate = w.getnframes(), w.getframerate()
    data = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64)
    w.close()
    win = int(rate * 0.02)
    nf = len(data) // win
    if nf > 2:
        e = np.sqrt((data[:nf * win].reshape(nf, win) ** 2).mean(axis=1))
        thr = e.max() * 0.08
        k0 = int(np.argmax(e > thr))
        k1 = nf - 1 - int(np.argmax(e[::-1] > thr))
        a = max(0, k0 * win - int(rate * 0.03))
        b = min(len(data), (k1 + 1) * win + int(rate * 0.03))
        data = data[a:b]
    fade = int(rate * 0.006)
    data[:fade] *= np.linspace(0, 1, fade)
    t = min(int(rate * tail), len(data) - 1)
    data[-t:] *= np.linspace(1, 0, t)
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(np.clip(data, -32768, 32767).astype('<i2').tobytes())
    w.close()
    return len(data), rate


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('word', help='拼写（逐字母词头）或词号')
    ap.add_argument('--table', default='ahd', choices=sorted(TABLES))
    ap.add_argument('--mode', default='chain', choices=('chain', 'span'))
    ap.add_argument('-o', '--out', default='')
    a = ap.parse_args()

    W = int(a.word) if a.word.isdigit() else find_word(a.word)
    if W is None:
        raise SystemExit('在逐字母词头里没找到 "%s"（可能是压缩码词头，需先解码表）' % a.word)
    tab = table(a.table)
    img = np.fromfile(IMG, dtype=np.uint8)
    recs = records(img, tab, W, a.mode)
    nf = sum(r[2] for r in recs)
    print('词号 %d  %s[%d] = %d  mode=%s' % (W, a.table, W, int(tab[W]), a.mode))
    for i, v, fc, more in recs:
        print('   记录 %5d  V=%6d FC=%2d more=%d  帧 %d..%d' % (i, v, fc, int(more), v, v + fc - 1))
    print('   合计 %d 个音节 / %d 帧' % (len(recs), nf))
    out = a.out or os.path.join(ROOT, 'out', 'NC3000_词_%s_%s_%s.wav' % (a.word, a.table, a.mode))
    tmp = os.path.join(ROOT, 'out', '_word_audio_frames.bin')
    with open(tmp, 'wb') as f:
        f.write(frames_for(img, recs))
    decode(nf, tmp, out)
    samples, rate = trim_and_fade(out)
    print('   -> %s  (%.2f s @ %d Hz)' % (out, samples / rate, rate))


if __name__ == '__main__':
    main()
