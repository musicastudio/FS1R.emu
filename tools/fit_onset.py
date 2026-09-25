#!/usr/bin/env python3
"""Read 24_onset: is there a step one control-tick after note-on, and in which segment?

    python tools/fit_onset.py captures/hardware/24_onset.wav
    python tools/fit_onset.py captures/engine/24_onset.wav --label engine

For each segment, every strike is aligned on its own note-on and the first differences are averaged
across the eight strikes, which is the point of striking it eight times: a real control-rate step sits
at the same sample offset every strike and survives averaging, while waveform detail and noise do not.

The number reported per segment is the averaged first difference in a 0.4 ms window around one tick
period after note-on, over the median first difference in the body of the note. A ratio near 1 means
nothing special happens there. The control segment (onset-plain) must come out near 1: it has nothing
that updates on the tick, so a step there would mean the tick period itself, the alignment, or this
analyzer is wrong, and no other segment could then be trusted.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from analyze_capture import read_wav

REQUESTS = ROOT / "captures" / "requests"
SR = 48000
TICK_MS = 1000.0 / 192.3          # 5.2002 ms
WIN_MS = 0.4                      # window around the tick, wide enough for clock drift over a minute


def align(x, sr, entry, marker_hz):
    """Scale and offset from the request's clock to the recording's, off the marker notes.

    Same approach as fit_damp: the marker note at a known frequency top and tail, so a clock that runs
    a few hundred ppm off does not smear a 0.4 ms window at the end of a one-minute file.
    """
    n = np.arange(len(x))
    z = x * np.exp(-2j * np.pi * marker_hz * n / sr)
    k = max(1, int(0.020 * sr))
    env = np.abs(np.convolve(z, np.ones(k) / k, mode="same"))
    thr = 0.25 * np.percentile(env, 99.5)
    on = env > thr
    edges = np.flatnonzero(np.diff(on.astype(int)) > 0) / sr
    if len(edges) < 2:
        return 1.0, 0.0
    want = entry["marker_start"] + entry["marker_end"]
    got_first = edges[edges < (want[2] + 2.0)]
    got_last = edges[edges > (want[-3] - 2.0)]
    if not len(got_first) or not len(got_last):
        return 1.0, 0.0
    a_req, b_req = want[0], want[-1]
    a_rec, b_rec = float(got_first[0]), float(got_last[-1])
    scale = (b_rec - a_rec) / (b_req - a_req) if b_req > a_req else 1.0
    off = a_rec - a_req * scale
    ppm = (scale - 1.0) * 1e6
    if abs(ppm) > 50:
        print(f"the recording runs {ppm:.0f} ppm off the request; clocks differ, corrected")
    return scale, off


def onset_profile(x, sr, strikes, scale, off, span_ms=12.0):
    """Averaged |first difference| across the strikes, from note-on out to span_ms."""
    n = int(span_ms * sr / 1000)
    acc = np.zeros(n)
    used = 0
    for t in strikes:
        i = int((t * scale + off) * sr)
        if i < 1 or i + n + 1 >= len(x):
            continue
        d = np.abs(np.diff(x[i - 1:i + n]))
        if len(d) < n:
            continue
        acc += d[:n]
        used += 1
    return (acc / used if used else acc), used


def body_step(x, sr, strikes, scale, off):
    """Median |first difference| well inside the note, the reference the tick window is read against."""
    vals = []
    for t in strikes:
        i = int((t * scale + off) * sr)
        a, b = i + int(0.050 * sr), i + int(0.300 * sr)
        if b < len(x):
            vals.append(np.median(np.abs(np.diff(x[a:b]))))
    return float(np.median(vals)) if vals else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--label", default="hardware")
    a = ap.parse_args()
    man = json.loads((REQUESTS / "manifest.json").read_text())
    entry = next((f for f in man["files"] if f["file"] == "24_onset.mid"), None)
    if entry is None:
        raise SystemExit("no 24_onset.mid in the manifest; run tools/make_capture_onset.py")
    sr, xs = read_wav(a.wav)
    x = xs.mean(axis=1) if xs.ndim > 1 else xs
    scale, off = align(x, sr, entry, man["marker_hz"])

    print(f"{a.label}: {a.wav}")
    print(f"the tick is {TICK_MS:.4f} ms after note-on, {TICK_MS * sr / 1000:.1f} samples\n")
    print(f"{'segment':14s} {'strikes':>7} {'at tick':>10} {'body':>10} {'ratio':>7} "
          f"{'peak':>10} {'peak at ms':>10}")
    rows = {}
    for seg in entry["segments"]:
        prof, used = onset_profile(x, sr, seg["strikes"], scale, off)
        if not used:
            print(f"{seg['id']:14s}   no strikes found")
            continue
        body = body_step(x, sr, seg["strikes"], scale, off)
        lo = int((TICK_MS - WIN_MS / 2) * sr / 1000)
        hi = int((TICK_MS + WIN_MS / 2) * sr / 1000)
        at_tick = float(prof[lo:hi].max()) if hi <= len(prof) else 0.0
        ratio = at_tick / body if body > 0 else 0.0
        j = int(np.argmax(prof))
        rows[seg["id"]] = {"ratio": ratio, "at_tick": at_tick, "body": body,
                           "peak_ms": j * 1000.0 / sr, "control": seg.get("control", False)}
        print(f"{seg['id']:14s} {used:7d} {at_tick:10.6f} {body:10.6f} {ratio:7.2f} "
              f"{prof[j]:10.6f} {j * 1000.0 / sr:10.3f}")

    ctrl = rows.get("onset-plain")
    if ctrl is None:
        print("\nno control segment, so nothing here can be trusted")
        return
    print()
    if ctrl["ratio"] > 2.5:
        print(f"CONTROL FAILED: onset-plain steps at the tick too ({ctrl['ratio']:.2f}x). Nothing in this")
        print("  voice updates on the tick, so the tick period, the alignment or this analyzer is wrong.")
        print("  Fix that before reading any other segment.")
        return
    print(f"control ok: onset-plain is flat at the tick ({ctrl['ratio']:.2f}x), so the window is honest")
    flagged = [(k, v["ratio"]) for k, v in rows.items()
               if k != "onset-plain" and v["ratio"] > max(2.5, 2.0 * ctrl["ratio"])]
    if not flagged:
        print("no segment steps at the tick: the unit does NOT have the discontinuity our engine has,")
        print("  and none of the tick-driven updates reproduces it on its own. Compare against the")
        print("  engine's own render of the same file to see which of ours does.")
        return
    for k, r in sorted(flagged, key=lambda kv: -kv[1]):
        print(f"  {k} steps at the tick, {r:.2f}x its own body")
    print("\nthe segment that steps names the update responsible; onset-b016 alone means the cause is")
    print("  in that patch's own parameters rather than in one subsystem.")


if __name__ == "__main__":
    main()
