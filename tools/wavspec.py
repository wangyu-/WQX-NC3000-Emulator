# -*- coding: utf-8 -*-
"""Spectrogram + basic stats for WAV files (used to tell speech from noise).

usage: wavspec.py <out.png> <in.wav> [in2.wav ...] [--nfft 512] [--rate R]

Prints per-file: sample rate, RMS, nonzero ratio, and the spectral flatness
(near 1.0 = noise-like, near 0 = tonal/speech-like).  The PNG stacks one
spectrogram per input file, 0..8 kHz.
"""
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw


def load(path):
    w = wave.open(path, "rb")
    ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    w.close()
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    elif sw == 1:
        x = np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0
    else:
        raise SystemExit("unsupported sample width %d" % sw)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def spec(x, nfft, hop):
    win = np.hanning(nfft)
    n = 1 + max(0, (len(x) - nfft) // hop)
    out = np.zeros((n, nfft // 2 + 1))
    for i in range(n):
        seg = x[i * hop:i * hop + nfft] * win
        out[i] = np.abs(np.fft.rfft(seg))
    return out


def main():
    args = list(sys.argv[1:])
    nfft, hop, rate = 512, 256, 0
    if "--nfft" in args:
        i = args.index("--nfft")
        nfft = int(args[i + 1])
        del args[i:i + 2]
    if "--rate" in args:
        i = args.index("--rate")
        rate = int(args[i + 1])
        del args[i:i + 2]
    out_png, files = args[0], args[1:]
    cw, chh = 900, 170
    img = Image.new("L", (cw, chh * len(files)), 0)
    dr = ImageDraw.Draw(img)
    for k, f in enumerate(files):
        x, sr = load(f)
        if rate:
            sr = rate
        rms = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
        nz = float(np.count_nonzero(x)) / max(1, len(x))
        S = spec(x, nfft, hop)
        keep = min(S.shape[1], int(8000.0 / (sr / 2.0) * (S.shape[1] - 1)) + 1)
        S = S[:, :keep]
        S = np.log10(S + 1.0)
        band = np.arange(keep) * (sr / 2.0) / max(1, S.shape[1] - 1)
        hi = (band > 1000) & (band < 7000)
        p = S[:, hi] if hi.any() else S
        gm = np.exp(np.mean(np.log(p + 1e-9), axis=1))
        am = np.mean(p, axis=1) + 1e-9
        flat = float(np.mean(gm / am))
        print("%s: %d samples @ %d Hz  rms=%.1f  nonzero=%.1f%%  flatness=%.3f"
              % (f, len(x), sr, rms, 100.0 * nz, flat))
        v = S.T[::-1]
        v = v - v.min()
        if v.max() > 0:
            v = v / v.max()
        im = Image.fromarray((255 * v ** 0.6).astype(np.uint8)).resize((cw, chh))
        img.paste(im, (0, k * chh))
        dr.text((4, k * chh + 2), "%s flat=%.3f" % (f.split("/")[-1][-28:], flat),
                fill=255)
    img.save(out_png)
    print("wrote", out_png)


if __name__ == "__main__":
    main()
