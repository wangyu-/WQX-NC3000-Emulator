"""Compare an S200 ROM-path WAV (32 kHz, from s200_probe) with the validated
direct-drive WAV (8 kHz, from vnt2wav.ps1 / s200_map).

The direct path is the reference: it matches the official S200.exe with a
per-frame envelope correlation of 0.9911.

Usage: python s200_cmp.py <rom.wav> <direct.wav> [--plot]
"""
import argparse
import struct
import sys
import wave


def read_wav(path):
    with wave.open(path, "rb") as f:
        assert f.getsampwidth() == 2, "expect 16-bit PCM"
        n = f.getnframes()
        ch = f.getnchannels()
        raw = f.readframes(n)
        rate = f.getframerate()
    s = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
    if ch > 1:
        s = s[::ch]
    return rate, s


def stats(x):
    n = len(x)
    if not n:
        return 0.0, 0, 0, 0
    mean = sum(x) / n
    rms = (sum((v - mean) ** 2 for v in x) / n) ** 0.5
    return rms, max(x), min(x), sum(1 for v in x if v >= 32767 or v <= -32767)


def corr(a, b):
    n = min(len(a), len(b))
    if n < 2:
        return 0.0
    a = a[:n]
    b = b[:n]
    ma = sum(a) / n
    mb = sum(b) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = sum((x - ma) ** 2 for x in a) ** 0.5
    db = sum((y - mb) ** 2 for y in b) ** 0.5
    return num / (da * db) if da and db else 0.0


def lsq_scale(ref, sig):
    """scale k minimizing |sig - k*ref| (zero-mean), and the correlation."""
    n = min(len(ref), len(sig))
    if n < 2:
        return 0.0, 0.0
    ref, sig = ref[:n], sig[:n]
    mr = sum(ref) / n
    ms = sum(sig) / n
    num = sum((r - mr) * (s - ms) for r, s in zip(ref, sig))
    den = sum((r - mr) ** 2 for r in ref)
    k = num / den if den else 0.0
    return k, corr(ref, sig)


def resample_to(x, src_rate, dst_rate, phase=0.0):
    """Linear-interpolated resample of x from src_rate to dst_rate."""
    if not x:
        return []
    step = src_rate / dst_rate
    n = int((len(x) - 1) / step)
    out = []
    pos = phase
    for _ in range(n):
        i = int(pos)
        f = pos - i
        if i + 1 < len(x):
            out.append(int(x[i] * (1 - f) + x[i + 1] * f))
        else:
            out.append(x[i])
        pos += step
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("direct")
    ap.add_argument("--plot", action="store_true")
    ap.add_argument("--factor", type=int, default=4)
    a = ap.parse_args()

    r_rate, rom = read_wav(a.rom)
    d_rate, direct = read_wav(a.direct)
    print("rom   : %s  %u Hz  %u samples (%.3f s)" % (a.rom, r_rate, len(rom),
                                                      len(rom) / r_rate))
    print("direct: %s  %u Hz  %u samples (%.3f s)" % (a.direct, d_rate, len(direct),
                                                      len(direct) / d_rate))
    for tag, x in (("rom", rom), ("direct", direct)):
        rms, mx, mn, rail = stats(x)
        print("  %-6s rms=%7.1f peak=%6d/%6d rails=%u" % (tag, rms, mx, mn, rail))

    # decimate the ROM path and look for the alignment that best matches the
    # reference (the probe keeps running past the end of the content, so the
    # lag is not zero)
    best = None
    lag_max = min(len(resample_to(rom, r_rate, d_rate)), 4 * len(direct) + 1)
    if len(direct) > 4000:
        lag_max = 2000            # long files: the pipeline delay is a fixed few ms
    for phase in (0.0, 0.25, 0.5, 0.75):
        dec = resample_to(rom, r_rate, d_rate, phase)
        for lag in range(0, lag_max):
            k, c = lsq_scale(direct, dec[lag:lag + len(direct)])
            if best is None or abs(c) > abs(best[2]):
                best = (phase, lag, c, k)
    phase, lag, c, k = best
    print("best match: phase=%.2f lag=%d samples (%.1f ms) corr=%+.4f scale=%+.3f"
          % (phase, lag, lag / d_rate * 1000, c, k))

    dec = resample_to(rom, r_rate, d_rate, phase)[lag:lag + len(direct)]
    rms, mx, mn, rail = stats(dec)
    print("  decimated rom vs direct: rms=%7.1f (direct 1525) peak=%d rails=%u"
          % (rms, mx, rail))

    if a.plot:
        for tag, x in (("direct", direct), ("rom(dec)", dec)):
            print("\n%s:" % tag)
            step = max(1, len(x) // 96)
            for i in range(0, len(x) - step, step):
                seg = x[i:i + step]
                v = max(seg, key=abs)
                col = int((v + 32768) / 65536 * 63)
                print("  %s|" % (" " * col) if col else "  |", end="")
                print()


if __name__ == "__main__":
    main()
