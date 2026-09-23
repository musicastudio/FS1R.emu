#!/usr/bin/env python3
"""Line the engine's render of each demo song up against a recording of the real unit playing the demo.

    python tools/extract_demo.py
    for f in captures/demo/[0-9]*.mid; do bin/fs1r_emu -r eprom.bin -smf "$f" \
        -w "captures/demo/render/$(basename "$f" .mid).wav" -d 2; done
    python tools/check_demo.py "captures/raw/FS1R DEMO.flac" captures/demo/render

The demo song is the one piece of hardware audio that needs no capture rig: the unit plays it from its
own EPROM, so the same byte stream drives the recording and our engine. The recording is one take of
all 15 songs back to back, so each render is found in it by cross-correlating log RMS envelopes, and
what comes out is three numbers per song:

  lag        where the song sits in the recording. The step between consecutive lags minus the song's
             own length is the pause the unit takes to reload between songs.
  env r      correlation of the 100 Hz log envelope over the song. This is the timing and dynamics
             check: a wrong tick rate or a missed note collapses it long before the tone matters.
  band dB    our level minus the unit's, in octave bands. This is the calibration signal. A constant
             offset across bands is gain; a tilt is the filter or the operator level curve; one band
             out on its own is a formant or an effect.

Envelope correlation, not sample difference: the two are not phase locked and never will be, since the
unit's own LFOs and its 48 kHz clock are free running.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

SR = 48000
HOP = 480                                     # 100 Hz envelope, one frame per player tick
BANDS = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240), (10240, 20000)]


def mono(path):
    x, sr = sf.read(str(path), dtype="float32", always_2d=True)
    if sr != SR:
        sys.exit("%s is %d Hz, expected %d" % (path, sr, SR))
    return x.mean(1)


def envelope(m):
    n = len(m) // HOP
    return np.sqrt((m[:n * HOP].reshape(n, HOP) ** 2).mean(1) + 1e-12)


def z(a):
    a = np.log10(a)
    return (a - a.mean()) / (a.std() + 1e-9)


def find(ref, own_z, lo, hi):
    """Best lag for own inside ref, searched over [lo, hi] envelope frames, with its correlation.

    Searched forward from where the previous song ended rather than over the whole recording: the
    songs play in a known order, and a 40 s envelope matches loosely enough in a 500 s take that a
    free search lands on the wrong song about half the time.
    """
    best = (lo, -2.0)
    for lag in range(max(0, lo), min(hi, len(ref) - len(own_z)) + 1):
        r = float(np.corrcoef(z(ref[lag:lag + len(own_z)]), own_z)[0, 1])
        if r > best[1]:
            best = (lag, r)
    return best


def bands(a, b):
    """Per-octave-band level difference in dB, a minus b, over whatever length both share."""
    k = min(len(a), len(b)) // HOP * HOP
    fa, fb = np.abs(np.fft.rfft(a[:k])) ** 2, np.abs(np.fft.rfft(b[:k])) ** 2
    f = np.fft.rfftfreq(k, 1.0 / SR)
    out = []
    for lo, hi in BANDS:
        s = (f >= lo) & (f < hi)
        out.append(10 * np.log10((fa[s].sum() + 1e-20) / (fb[s].sum() + 1e-20)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reference", help="recording of the real unit playing the whole demo")
    ap.add_argument("renders", help="directory of engine renders, one per song, named so they sort")
    ap.add_argument("--lead", type=float, default=30, help="seconds to search for the first song")
    ap.add_argument("--gap", type=float, default=10, help="seconds to search past the previous song's end")
    args = ap.parse_args()
    ref = mono(args.reference)
    renv = envelope(ref)
    files = sorted(f for f in Path(args.renders).glob("*.wav") if f.name[:2].isdigit())
    if not files:
        sys.exit("no .wav renders in %s" % args.renders)
    print("%-22s %7s %7s %6s  %s" % ("song", "lag s", "gap s", "env r", "  ".join("%5d" % lo for lo, _ in BANDS)))
    prev_end, rows = None, []
    for f in files:
        own = mono(f)
        # The render carries the song's own leading silence, so the first song starts at lag 0 give or
        # take whatever was recorded before ENTER, and each later one within a few seconds of the last.
        lo, hi = (0, int(args.lead * 100)) if prev_end is None else (int(prev_end * 100) - 100, int((prev_end + args.gap) * 100))
        oenv = envelope(own)
        lag, r = find(renv, z(oenv), lo, hi)
        seg = ref[lag * HOP:lag * HOP + len(own)]
        db = bands(own, seg)
        gap = lag / 100.0 - prev_end if prev_end is not None else float("nan")
        # Where the song itself stops, not where the render's tail does, so the gap is the unit's pause.
        loud = np.nonzero(oenv > oenv.max() / 3000.0)[0]
        prev_end = lag / 100.0 + (loud[-1] + 1) / 100.0
        rows.append((r, db))
        print("%-22s %7.2f %7.2f %6.3f  %s" % (f.stem, lag / 100.0, gap, r, "  ".join("%+5.1f" % d for d in db)))
    rs = np.array([r for r, _ in rows])
    dbs = np.array([d for _, d in rows])
    print("\nenvelope correlation: worst %.3f, median %.3f" % (rs.min(), np.median(rs)))
    print("band dB, median over songs: %s" % "  ".join("%+5.1f" % d for d in np.median(dbs, 0)))
    print("overall level offset (median of all bands and songs): %+.1f dB" % np.median(dbs))


if __name__ == "__main__":
    main()
