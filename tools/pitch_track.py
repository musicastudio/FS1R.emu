#!/usr/bin/env python3
"""Print the dominant frequency every 100 ms of a render: python tools/pitch_track.py file.wav"""
import struct, sys
import numpy as np
d = open(sys.argv[1], 'rb').read(); n = struct.unpack('<I', d[40:44])[0]
x = np.frombuffer(d[44:44 + n], dtype=np.int16).reshape(-1, 2)[:, 0] / 32768.0; sr = 44100
out = []
for i in range(0, len(x) - 8192, sr // 10):
    seg = x[i:i + 8192] * np.hanning(8192)
    if np.max(np.abs(seg)) < 1e-4: out.append("-"); continue
    S = np.abs(np.fft.rfft(seg, 65536)); k = np.argmax(S[20:]) + 20
    out.append(f"{k * sr / 65536:.1f}")
print(" ".join(out))
