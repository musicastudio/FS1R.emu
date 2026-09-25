#!/usr/bin/env python3
"""Read cal::DAMP_MS out of a recording of 23_damp.mid.

    python tools/fit_damp.py captures/hardware/23_damp.wav
    python tools/fit_damp.py captures/engine/23_damp.wav --label engine

`FUN_00023000` says the chip releases a channel the allocator takes rather than cutting it off; this
says how fast. The file holds five steals plus one no-steal reference (see tools/make_capture_damp.py
for why each one is there), and each steal is a steady tone that stops.

The fit is on the carrier's own amplitude, not on broadband level: each segment's tone is a fixed
frequency known from the manifest, so the envelope is read by heterodyning to DC at that frequency
and taking the magnitude. That keeps the filler notes, the room and anything else in the recording
out of the number.

Two shapes are fitted to each fade and the better one wins, because they are what the firmware's own
two envelope generators do and the frequency EG already turned out to be the one nobody expected:

  exponential   a(t) = a0 * exp(-t / tau)           a time constant: the fade takes the same time
                                                    whatever the starting level
  linear in dB  a(t) = a0 * 10^(-slope * t / 20)    a constant dB per second: a quieter note fades
                                                    quicker, as the amplitude EG's own rates do

The three segments at different part volumes are what separate them: a tau that is the same at all
three says exponential, and a fade time proportional to the starting level in dB says the ramp.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from analyze_capture import read_wav, align  # noqa: E402

REQUESTS = ROOT / "captures/requests"


def carrier_envelope(x, sr, hz, hop=32):
    """Amplitude of the component at hz, sample by sample, by heterodyne and a short moving average.

    The averaging window is the resolution limit: a fade shorter than the window reads as the window
    rather than as itself. Three carrier periods at 4 kHz is 0.75 ms, which is the same order as the
    fade being measured, so the window is capped at a fraction of a millisecond and the caller is told
    what it was. A 500 Hz carrier cannot be measured faster than its own period either way, which is
    why the file carries the same steal at three frequencies.
    """
    n = np.arange(len(x))
    z = x * np.exp(-2j * np.pi * hz * n / sr)
    w = max(8, min(int(round(sr / hz * 2)), int(0.0004 * sr)))
    k = np.ones(w) / w
    zz = np.convolve(z, k, mode="same")
    return np.abs(zz), w / sr * 1000


def fit_one(env, sr, t0_idx):
    """Fit both shapes to the decay starting at t0_idx. Returns a dict of what each one says."""
    a0 = float(np.median(env[max(0, t0_idx - int(0.02 * sr)):t0_idx]))
    if a0 <= 0:
        return None
    seg = env[t0_idx:t0_idx + int(0.25 * sr)]
    if not len(seg):
        return None
    floor = a0 * 10 ** (-40 / 20.0)
    below = np.where(seg < floor)[0]
    end = int(below[0]) if len(below) else len(seg)
    if end < 8:
        # faster than the analysis window can see: report the bound rather than a fitted number
        return {"a0": a0, "too_fast": True, "t40_ms": end / sr * 1000}
    t = np.arange(end) / sr
    y = np.maximum(seg[:end], a0 * 1e-6)
    db = 20 * np.log10(y / a0)
    # exponential: dB falls linearly in t with slope -8.686/tau, which is the same fit as the ramp.
    # The two shapes are only told apart across segments of different a0, so what is fitted here is
    # the slope, and the caller compares slopes.
    A = np.polyfit(t, db, 1)
    resid = float(np.sqrt(np.mean((db - np.polyval(A, t)) ** 2)))
    slope_db_s = float(-A[0])
    tau_ms = 8685.9 / slope_db_s if slope_db_s > 0 else float("inf")
    return {"a0": a0, "slope_db_s": slope_db_s, "tau_ms": tau_ms, "rms_db": resid,
            "t40_ms": end / sr * 1000, "too_fast": False}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--label", default="hardware")
    a = ap.parse_args()
    man = json.loads((REQUESTS / "manifest.json").read_text())
    entry = next((f for f in man["files"] if f["file"] == "23_damp.mid"), None)
    if entry is None:
        raise SystemExit("no 23_damp.mid in the manifest; run tools/make_capture_damp.py")
    sr, x = read_wav(a.wav)
    scale, off = align(x, sr, entry, man["marker_hz"])
    mono = x.mean(axis=1)

    print(f"{a.label}: {a.wav}")
    print(f"{'segment':12s} {'steal':6s} {'level':>9} {'slope dB/s':>11} {'tau ms':>9} "
          f"{'-40dB ms':>9} {'fit rms':>8} {'window ms':>10}")
    rows = {}
    for seg in entry["segments"]:
        hz = seg["carrier_hz"]
        env, win_ms = carrier_envelope(mono, sr, hz)
        i = int((seg["t_on"] * scale + off) * sr)
        r = fit_one(env, sr, i)
        if r is None:
            print(f"{seg['id']:12s} {'-':6s}   silent")
            continue
        r["window_ms"] = win_ms
        rows[seg["id"]] = r
        if r["too_fast"]:
            print(f"{seg['id']:12s} {str(seg['steal']):6s} {20*np.log10(r['a0']+1e-12):9.1f} "
                  f"{'faster than the window':>11}  (-40 dB in under {r['t40_ms']:.1f} ms)")
        else:
            flag = "  <- at the window, so this is an upper bound on the speed" \
                   if r["t40_ms"] < 3 * win_ms else ""
            print(f"{seg['id']:12s} {str(seg['steal']):6s} {20*np.log10(r['a0']+1e-12):9.1f} "
                  f"{r['slope_db_s']:11.0f} {r['tau_ms']:9.2f} {r['t40_ms']:9.1f} {r['rms_db']:8.2f} "
                  f"{win_ms:10.3f}{flag}")

    # the reference must NOT fade: if it does, the steal is not what stopped the tone
    ref = rows.get("damp-ref")
    if ref and not ref.get("too_fast") and ref["slope_db_s"] > 20:
        print("\nWARNING: damp-ref decays too, so something other than the steal is stopping the note. "
              "The damp segments cannot be trusted until that is explained.")

    # a steal segment that does not decay at all means the steal did not land on the audible channel,
    # which is what the stimulus is built to make visible. Say so instead of reporting a fitted tau.
    steals = [(k, rows[k]) for k in ("damp-loud", "damp-mid", "damp-quiet", "damp-lowf", "damp-highf")
              if k in rows]
    flat = [k for k, r in steals if not r.get("too_fast") and abs(r["slope_db_s"]) < 5.0]
    if len(flat) == len(steals) and steals:
        print(f"\nNO DAMP in this recording: every steal segment holds its level ({', '.join(flat)}).")
        print("  The tone survives the steal, so the unit did not take the audible channel. That is")
        print("  this file's designed-visible failure, not a rate: the allocator reading behind")
        print("  tools/make_capture_damp.py is wrong somewhere, and cal::DAMP_MS stays unmeasured.")
        print("  Read the allocator directly (FS1R.unlock session6) rather than re-recording this.")
        return

    three = [rows.get(k) for k in ("damp-loud", "damp-mid", "damp-quiet")]
    if all(r and not r.get("too_fast") for r in three):
        sl = [r["slope_db_s"] for r in three]
        t40 = [r["t40_ms"] for r in three]
        print(f"\nthe three levels: slopes {sl[0]:.0f}, {sl[1]:.0f}, {sl[2]:.0f} dB/s; "
              f"times to -40 dB {t40[0]:.1f}, {t40[1]:.1f}, {t40[2]:.1f} ms")
        spread = max(sl) / max(1e-9, min(sl))
        if spread < 1.25:
            print(f"  the slope is the same at every level (spread {spread:.2f}x), so the fade is a "
                  f"fixed rate: cal::DAMP_MS = {np.mean([r['tau_ms'] for r in three]):.2f}")
        else:
            print(f"  the slope moves with the level (spread {spread:.2f}x), so the fade is NOT a "
                  f"simple one pole; read the three curves before choosing a shape")
    lo, hi = rows.get("damp-lowf"), rows.get("damp-highf")
    if lo and hi and not lo.get("too_fast") and not hi.get("too_fast"):
        print(f"500 Hz vs 8 kHz: {lo['slope_db_s']:.0f} against {hi['slope_db_s']:.0f} dB/s "
              f"({'same, so it is on the level' if abs(lo['slope_db_s']-hi['slope_db_s']) < 0.25*max(lo['slope_db_s'],hi['slope_db_s']) else 'different, so it is not a plain level fade'})")


if __name__ == "__main__":
    main()
