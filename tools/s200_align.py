"""Align an S200 ROM-path WAV (32 kHz, s200_probe) against the validated
direct-drive 8 kHz reference and report where they agree.

usage: python s200_align.py <rom.wav> <direct.wav> [--windows 4]
"""
import argparse
import wave

import numpy as np


def read_wav(path):
    with wave.open(path, "rb") as f:
        n, ch, sw, rate = f.getnframes(), f.getnchannels(), f.getsampwidth(), f.getframerate()
        raw = f.readframes(n)
    assert sw == 2
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if ch > 1:
        x = x[::ch]
    return rate, x


def resample(x, src, dst, phase=0.0):
    n = int((len(x) - 1) / (src / dst))
    pos = phase + np.arange(n) * (src / dst)
    i = pos.astype(int)
    f = pos - i
    return x[i] * (1 - f) + x[i + 1] * f


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d else 0.0


def find_lag(ref, sig):
    """FFT cross-correlation lag (sig delayed relative to ref)."""
    n = 1
    m = len(ref) + len(sig)
    while n < m:
        n *= 2
    c = np.fft.irfft(np.fft.rfft(sig, n) * np.conj(np.fft.rfft(ref, n)), n)
    c = np.concatenate((c[-len(ref) + 1:], c[:len(sig)]))
    return int(np.argmax(c) - (len(ref) - 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("direct")
    ap.add_argument("--windows", type=int, default=6, help="report N equal windows")
    a = ap.parse_args()

    rr, rom = read_wav(a.rom)
    dr, ref = read_wav(a.direct)
    print("rom   : %u Hz %u samples (%.3f s)" % (rr, len(rom), len(rom) / rr))
    print("direct: %u Hz %u samples (%.3f s)" % (dr, len(ref), len(ref) / dr))

    best = None
    for ph in (0.0, 0.25, 0.5, 0.75):
        dec = resample(rom, rr, dr, ph)
        lag = find_lag(ref, dec)
        if lag < 0:
            continue
        seg = dec[lag:lag + len(ref)]
        if len(seg) < len(ref):
            continue
        c = ncc(ref, seg)
        if best is None or abs(c) > abs(best[2]):
            k = float((ref - ref.mean()) @ (seg - seg.mean()) / ((ref - ref.mean()) ** 2).sum())
            best = (ph, lag, c, k, dec)
    if best is None:
        print("no alignment found")
        return
    ph, lag, c, k, dec = best
    seg = dec[lag:lag + len(ref)]
    print("best: phase=%.2f lag=%d (%.1f ms) corr=%+.4f scale=%+.3f" %
          (ph, lag, lag / dr * 1000, c, k))
    print("  rom decimated: rms=%.0f peak=%.0f | direct: rms=%.0f peak=%.0f" %
          (seg.std(), np.abs(seg).max(), ref.std(), np.abs(ref).max()))

    rail = int(((seg >= 32000) | (seg <= -32000)).sum())
    print("  samples at the ±32k rail inside the matched window: %d" % rail)

    # per-window agreement.  The ROM clock (31958.39 Hz) is not exactly 4x the
    # reference rate, so the alignment drifts ~0.12% - each window gets its own
    # small lag search, which is the only way a long file can be compared.
    w = len(ref) // a.windows
    print("  window-by-window (%.2f s each, local lag search):" % (w / dr))
    for i in range(a.windows):
        r = ref[i * w:(i + 1) * w]
        nom = lag + i * w
        best = (nom, -9.0)
        for cand in range(nom - 600, nom + 601):
            if cand < 0 or cand + w > len(dec):
                continue
            c = ncc(r, dec[cand:cand + w])
            if c > best[1]:
                best = (cand, c)
        s = dec[best[0]:best[0] + w]
        kk = float((r - r.mean()) @ (s - s.mean()) / ((r - r.mean()) ** 2).sum())
        print("    t=%5.2fs corr=%+.4f scale=%+.3f lag=%d  rom_rms=%7.0f ref_rms=%7.0f" %
              (i * w / dr, best[1], kk, best[0], s.std(), r.std()))

    # where does the ROM stream leave the aligned path?
    print("  envelope check (rms per 100 ms, rom vs ref*scale):")
    step = dr // 10
    n = min(len(ref), len(dec) - lag) // step
    line = []
    for i in range(0, min(n, 40)):
        a1 = dec[lag + i * step:lag + (i + 1) * step].std()
        a2 = ref[i * step:(i + 1) * step].std()
        line.append("%.1f/%.1f" % (a1, a2))
    print("   " + " ".join(line))


if __name__ == "__main__":
    main()
