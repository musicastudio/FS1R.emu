#!/usr/bin/env python3
"""Sanity check a rendered WAV: expected pitch, harmonic peaks, envelope. python tools/check_wav.py file.wav [midi_note]"""
import struct, sys
import numpy as np

def main():
    p = sys.argv[1]; note = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    d = open(p, 'rb').read(); n = struct.unpack('<I', d[40:44])[0]
    x = np.frombuffer(d[44:44 + n], dtype=np.int16).reshape(-1, 2)[:, 0] / 32768.0; sr = 44100
    seg = x[int(0.3 * sr):int(0.3 * sr) + 16384] * np.hanning(16384)
    S = np.abs(np.fft.rfft(seg)); fr = np.fft.rfftfreq(16384, 1 / sr)
    peaks = sorted(((S[i], fr[i]) for i in range(2, len(S) - 2) if S[i] > S[i - 1] and S[i] > S[i + 1] and S[i] > S.max() * 0.03), reverse=True)[:8]
    f0 = 440 * 2 ** ((note - 69) / 12)
    print(f"{p}: expected f0 {f0:.1f} Hz; peaks (Hz, dB):", [(round(fq, 1), round(20 * np.log10(a / S.max()), 1)) for a, fq in peaks])
    env = [20 * np.log10(np.sqrt(np.mean(x[i:i + sr // 4] ** 2)) + 1e-9) for i in range(0, len(x), sr // 4)]
    print("   rms dB per 250 ms:", [round(e) for e in env])

if __name__ == "__main__":
    main()
