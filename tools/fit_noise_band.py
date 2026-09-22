#!/usr/bin/env python3
"""Read the noise formant's two coefficients and its peak gain off the hardware recordings.

    python tools/fit_noise_band.py                 every unvoiced segment in the four noise files
    python tools/fit_noise_band.py --rate 24000    the same fit with the poles at 24 kHz, which is wrong

This is where cal.h's NOISE_A1, NOISE_A2, NOISE_G_DB and the three skirt slices come from. Each
hardware segment's PSD is fitted with two digital one-poles in series at 48 kHz, each of unit DC gain,
driven by white noise, ring modulated to the segment's centre (both sidebands), and multiplied by the
two-sample mean's cos^2 roll-off. Three numbers per segment: a1, a2 and the peak in dB on the
recording's own scale. The fit runs over 200 Hz to 23 kHz in 11.7 Hz bins, so it is the band's shape
across the whole spectrum and not its width at one level. docs/noise.md, 2026-09-22.

The 8 kHz and the 1 kHz takes give the same coefficients at every register they share, which is what
makes these measured rather than fitted: see the ubw-* rows against the u3bw-* rows at the same register.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import minimize

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_capture as ac

MAN = json.loads((ROOT / "captures/requests/manifest.json").read_text())
SR = 48000.0

SEGS = ([("13_unvoiced3", "u3bw-%d" % b, 8000, b, 0)
         for b in (4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 68, 76, 96)]
        + [("13_unvoiced3", "u3skirt-bw20-%d" % s, 8000, 20, s) for s in range(8)]
        + [("13_unvoiced3", "u3skirt-bw60-%d" % s, 8000, 60, s) for s in range(8)]
        + [("05_unvoiced_1", "ubw-%d" % b, 1000, b, 0) for b in (16, 32, 64)]
        + [("05_unvoiced_2", "ucentre-4000", 4000, 40, 2), ("05_unvoiced_2", "ucentre-8000", 8000, 40, 2),
           ("05_unvoiced_2", "uskirt-3", 1000, 40, 3), ("05_unvoiced_2", "uskirt-7", 1000, 40, 7)])

_cache = {}


def segment(name, sid):
    if name not in _cache:
        sr, x = ac.read_wav(ROOT / "captures/hardware" / (name + ".wav"))
        entry = next(f for f in MAN["files"] if f["file"] == name + ".mid")
        scale, off = ac.align(x, sr, entry, MAN["marker_hz"])
        _cache[name] = (x.mean(1), sr, entry, scale, off)
    x, sr, entry, scale, off = _cache[name]
    s = next(s for s in entry["segments"] if s["id"] == sid)
    a = int((scale * s["steady"][0] + off) * sr)
    b = int((scale * s["steady"][1] + off) * sr)
    return x[a:b]


def psd(y, n=4096):
    w = np.hanning(n)
    S = np.zeros(n // 2 + 1)
    k = 0
    for s in range(0, len(y) - n, n // 2):
        S += np.abs(np.fft.rfft(y[s:s + n] * w)) ** 2
        k += 1
    return np.fft.rfftfreq(n, 1 / SR), S / k / (w ** 2).sum()


def eb70(v):
    return ((v << 1) * 0xA5) >> 8


def one_pole_power(f, a, rate):
    w = 2 * np.pi * f / rate
    p = 1 - a
    return a * a / (1 - 2 * p * np.cos(w) + p * p)


def fit(y, centre, rate):
    f, S = psd(y)
    sel = (f > 200) & (f < 23000)
    fs, Sd = f[sel], 10 * np.log10(S[sel] + 1e-30)
    roll = 20 * np.log10(np.abs(np.cos(np.pi * fs / SR)) + 1e-9)

    def model(p):
        g, a1, a2 = p
        a1, a2 = min(abs(a1), 1.0), min(abs(a2), 1.0)
        tot = sum(one_pole_power(fc, a1, rate) * one_pole_power(fc, a2, rate) for fc in (fs - centre, fs + centre))
        return g + 10 * np.log10(tot + 1e-30) + roll

    best = None
    for a0 in (0.02, 0.05, 0.15, 0.4):
        for r in (1, 2, 4):
            res = minimize(lambda p: np.mean((model(p) - Sd) ** 2), [Sd.max(), a0, min(1, a0 * r)],
                           method="Nelder-Mead", options={"maxiter": 6000, "xatol": 1e-7, "fatol": 1e-7})
            if best is None or res.fun < best.fun:
                best = res
    g, a1, a2 = best.x
    a1, a2 = min(abs(a1), 1.0), min(abs(a2), 1.0)
    if a1 > a2:
        a1, a2 = a2, a1
    return a1, a2, g, float(np.sqrt(best.fun))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=float, default=SR, help="sample rate the poles run at")
    a = ap.parse_args()
    print("two digital one-poles at %.0f Hz on white noise, ring modulated, times cos^2(w/2)" % a.rate)
    print("%-16s %4s %5s %7s %7s %7s %6s" % ("segment", "reg", "skirt", "a1", "a2", "peak", "rms"))
    for name, sid, centre, bw, skirt in SEGS:
        try:
            y = segment(name, sid)
        except (StopIteration, SystemExit, FileNotFoundError):
            continue
        a1, a2, g, rms = fit(y, centre, a.rate)
        print("%-16s %4d %5d %7.4f %7.4f %7.1f %6.2f" % (sid, min(eb70(bw), 77), skirt, a1, a2, g, rms))


if __name__ == "__main__":
    main()
