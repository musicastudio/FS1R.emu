#!/usr/bin/env python3
"""Check the formant operator against the firmware's own frequency formula.

    python tools/check_formant.py

Formant synthesis has one defining behaviour: the formant sits at an absolute frequency and stays there
while the fundamental follows the key. This builds a voice with a single formant operator at a known
setting, works out where the firmware's tables say that formant should land, renders it at several
pitches, and measures where it actually is.

It is a check on the part of the engine that is read from the firmware (the frequency word, through
TRANS, FRMDET and the ratio tables). The window *shape* around that centre is still a model from the
patent, so this says the formant is in the right place, not that it has the right timbre.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "fs1r_emu.exe"
OUT = ROOT / "build"
TABLES = ROOT / "src/fs1r_rom_tables.h"


def load_tables():
    txt = TABLES.read_text()
    t = {}
    for m in re.finditer(r"(\w+)\[\d+\] = \{(.*?)\};", txt, re.S):
        t[m.group(1)] = [int(x) for x in m.group(2).replace("\n", "").split(",") if x.strip()]
    return t


T = load_tables()


def word_hz(w):
    return 440.0 * 2.0 ** ((w - 26861) / 1024.0)


def expected_formant(coarse, fine, transpose, detune, note):
    """The firmware's formant centre: 8*(coarse*128+fine) + 0x28ED, plus the transpose word and the
    note-band detune, with note scaling left at zero so the key does not move it."""
    freq = 8 * (coarse * 128 + fine) + 0x28ED
    pitch = T["NOTETAB"][note]                       # master tune and shifts are all neutral here
    band = (pitch >> 8) + 10
    band = 0 if band < 0x50 else (((band ^ 0x10) & 0x1F) if band < 0x70 else 0x1F)
    dd = (~detune) & 0xFF if detune < 15 else detune - 15
    idx = (dd & 0x1F) * 32 + band
    det = T["FRMDET"][idx & 0x1FF]
    if det > 127:
        det -= 256
    if idx & 0x200:
        det = -det
    trans = T["TRANS"][max(0, min(48, transpose + 24))]
    if trans > 32767:
        trans -= 65536
    return word_hz(freq + trans + det)


def build_voice(coarse, fine, transpose, detune, bw, skirt):
    v = bytearray(608)
    v[0:10] = b"FormTest  "
    v[0x1E] = 24                                      # note shift 0
    for k in (0x1F, 0x20, 0x21, 0x22, 0x3E):
        v[k] = 50                                     # pitch EG flat
    v[0x2C] = 0                                       # algorithm 1: every operator is a carrier
    v[0x3D] = 0                                       # no feedback
    v[0x57] = 127                                     # filter wide open, switch is off anyway
    for o in range(8):
        p = 112 + o * 62
        v[p + 0] = 24                                 # transpose 0, key sync off
        v[p + 1] = 1
        v[p + 4] = 7 << 3                             # bw bias sense 7 = none, form 0 (sine)
        v[p + 5] = o
        v[p + 7] = 15
        v[p + 8] = v[p + 9] = 50                      # frequency EG flat
        v[p + 12] = v[p + 13] = v[p + 14] = 99        # EG L1-L3 full
        v[p + 15] = 0                                 # L4 silent, so the note starts from nothing
        v[p + 16] = 0                                 # T1: on this scale 0 is the fastest rate, 99 the slowest
        v[p + 17] = v[p + 18] = 50
        v[p + 19] = 40
        v[p + 22] = 0                                 # silent
        v[p + 23] = 39
        v[p + 31] = 7 << 3                            # freq bias sense 7 = none, pms 0
        v[p + 32] = 7                                 # freq mod sense 0, freq velocity sense 7 = none
        v[p + 33] = 7
        v[p + 34] = 7
        q = p + 35
        v[q + 0] = 24
        v[q + 7] = v[q + 8] = 50
        v[q + 12] = 7
        v[q + 23] = 7
        v[q + 24] = 7
        v[q + 25] = 7
        v[q + 26] = 7
    p = 112                                           # operator 1 only: one formant
    v[p + 1] = coarse
    v[p + 2] = fine
    v[p + 0] = 24 + transpose
    v[p + 3] = 0                                      # note scaling 0: the key must not move it
    v[p + 4] = (7 << 3) | 7                           # form 7 = frmt
    v[p + 5] = (skirt << 3)
    v[p + 6] = bw
    v[p + 7] = detune
    v[p + 22] = 99                                    # full level
    return bytes(v)


def to_syx(voice):
    body = bytes([0x40, 0x00, 0x00]) + bytes(b & 0x7F for b in voice)
    n = len(voice)
    head = bytes([0xF0, 0x43, 0x00, 0x5E, (n >> 7) & 0x7F, n & 0x7F])
    s = sum(body) + (n >> 7 & 0x7F) + (n & 0x7F)
    return head + body + bytes([(-s) & 0x7F, 0xF7])


def measure(path, note):
    d = path.read_bytes()
    sr = struct.unpack("<I", d[24:28])[0]
    i = 12
    x = None
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"data":
            x = np.frombuffer(d[i + 8:i + 8 + n], dtype=np.int16).reshape(-1, 2)[:, 0] / 32768.0
            break
        i += 8 + n + (n & 1)
    N = 32768
    seg = x[int(0.3 * sr):int(0.3 * sr) + N]
    seg = np.pad(seg, (0, max(0, N - len(seg))))[:N]
    S = np.abs(np.fft.rfft(seg * np.hanning(N)))
    f = np.fft.rfftfreq(N, 1 / sr)
    # The spectrum is a harmonic comb under a formant envelope. Smooth over a few harmonics so the
    # envelope shows through, then take its peak: that is the formant centre.
    f0 = 440 * 2 ** ((note - 69) / 12)
    w = max(3, int(2.5 * f0 / (f[1] - f[0]))) | 1
    env = np.convolve(S, np.hanning(w) / np.hanning(w).sum(), mode="same")
    lo = np.searchsorted(f, f0 * 1.2)
    centre = f[lo + int(np.argmax(env[lo:]))]
    # The fundamental can be weak under a high formant, so take it from the waveform period rather than
    # from a spectral peak: autocorrelation over the plausible lag range.
    y = seg - seg.mean()
    ac = np.correlate(y, y, mode="full")[N - 1:]
    lo_lag, hi_lag = int(sr / (f0 * 1.3)), int(sr / (f0 * 0.7))
    fund = sr / (lo_lag + int(np.argmax(ac[lo_lag:hi_lag])))
    return fund, centre


def main():
    if not EXE.exists():
        raise SystemExit("build fs1r_emu.exe first")
    OUT.mkdir(exist_ok=True)
    fails = 0
    print("%-22s %8s %8s %9s %9s %7s %5s" % ("case", "want f0", "got f0", "want frmt", "got frmt", "error", "tol"))
    # Coarse is one octave per step on this scale and fine is 1/128 of an octave, so these are roughly
    # 400 Hz, 800 Hz, 1.6 kHz and a transposed 800 Hz: real formant territory for a vowel.
    for coarse, fine, transpose, detune, bw, skirt, label in [
        (15, 110, 0, 15, 40, 1, "~400 Hz"),
        (16, 110, 0, 15, 40, 1, "~800 Hz"),
        (17, 110, 0, 15, 40, 1, "~1.6 kHz"),
        (16, 110, 12, 15, 40, 1, "~800 Hz +12"),
        (16, 110, 0, 15, 25, 2, "~800 Hz narrow"),
    ]:
        syx = OUT / "formtest.syx"
        syx.write_bytes(to_syx(build_voice(coarse, fine, transpose, detune, bw, skirt)))
        for note in (40, 52, 64):
            wav = OUT / f"formtest_{note}.wav"
            r = subprocess.run([str(EXE), "-v", str(syx), "-w", str(wav), "-n", str(note), "-d", "1.5"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print("render failed:", r.stdout, r.stderr)
                fails += 1
                continue
            want = expected_formant(coarse, fine, transpose, detune, note)
            wantF0 = 440 * 2 ** ((note - 69) / 12)
            gotF0, got = measure(wav, note)
            err = abs(got - want) / want
            # A formant is only located to about half the harmonic spacing: with a 330 Hz fundamental
            # under a 400 Hz formant there are barely five harmonics to place it between, so the
            # tolerance has to widen with f0 / formant rather than stay a flat percentage.
            tol = max(0.06, 0.6 * wantF0 / want)
            ok = err < tol and abs(gotF0 - wantF0) / wantF0 < 0.02
            if not ok:
                fails += 1
            print("%-22s %8.1f %8.1f %9.1f %9.1f %6.1f%% %5.0f%% %s"
                  % (f"{label} n{note}", wantF0, gotF0, want, got, err * 100, tol * 100,
                     "" if ok else "  FAIL"))
    print("check_formant: FAILED" if fails else "check_formant: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
