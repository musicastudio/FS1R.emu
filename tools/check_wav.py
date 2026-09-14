#!/usr/bin/env python3
"""Sanity check one rendered WAV: expected pitch, harmonic peaks, envelope, stereo width.

    python tools/check_wav.py file.wav [midi_note]

For the whole preset list with a stored reference, use tools/regress.py instead.
"""
import struct
import sys

import numpy as np


def read_wav(path):
    d = open(path, "rb").read()
    sr = struct.unpack("<I", d[24:28])[0]
    i = 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"data":
            return sr, np.frombuffer(d[i + 8:i + 8 + n], dtype=np.int16).reshape(-1, 2) / 32768.0
        i += 8 + n + (n & 1)
    raise SystemExit("no data chunk")


def main():
    p = sys.argv[1]
    note = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    sr, xy = read_wav(p)
    x, y = xy[:, 0], xy[:, 1]
    n = 16384
    seg = x[int(0.3 * sr):int(0.3 * sr) + n] * np.hanning(n)
    S = np.abs(np.fft.rfft(seg))
    fr = np.fft.rfftfreq(n, 1 / sr)
    peaks = sorted(((S[i], fr[i]) for i in range(2, len(S) - 2)
                    if S[i] > S[i - 1] and S[i] > S[i + 1] and S[i] > S.max() * 0.03), reverse=True)[:8]
    f0 = 440 * 2 ** ((note - 69) / 12)
    print(f"{p}: {sr} Hz; expected f0 {f0:.1f} Hz; peaks (Hz, dB):",
          [(round(fq, 1), round(20 * np.log10(a / S.max()), 1)) for a, fq in peaks])
    env = [20 * np.log10(np.sqrt(np.mean(x[i:i + sr // 4] ** 2)) + 1e-9) for i in range(0, len(x), sr // 4)]
    print("   rms dB per 250 ms:", [round(e) for e in env])
    print(f"   stereo L-R rms {np.sqrt(np.mean((x - y) ** 2)):.5f}, peak {np.abs(xy).max():.3f}")


if __name__ == "__main__":
    main()
