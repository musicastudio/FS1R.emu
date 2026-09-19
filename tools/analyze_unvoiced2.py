#!/usr/bin/env python3
"""Analyze the 05b_unvoiced2 take off a real unit against the engine's own render of the same file.

    python tools/analyze_unvoiced2.py <hardware.wav> [engine.wav]

The 05b file is additive and not in captures/requests/manifest.json (the frozen set's takes align
against that), so this script recomputes its segment times from the same generators make_capture_set
uses, aligns by the marker bursts, and compares each segment's spectrum band-for-band against the
render (bin/render_capture -f engine.wav 05b_unvoiced2.mid), reusing tools/analyze_capture.py's own
align/spectrum/bands/unpan helpers.
"""
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_capture_set as m
import fs1r_patch as fp
import analyze_capture as ac


def entry_05b():
    """The build_file entry for 05b, the way main() writes it into the manifest for a frozen group."""
    def g_unvoiced2():
        for b in range(0, 100, 4):
            yield m.Seg(f"ubw36-{b}", "", ["NOISE_OCT"], m.noise(u_bw=b), note=36, hold=3000,
                        measure="spectrum")
        for hz in (250, 500, 1000, 2000, 4000, 8000):
            yield m.Seg(f"ucentre36-{hz}", "", ["NOISE_OCT"], m.noise(hz=hz, u_bw=40, u_skirt=2),
                        note=36, hold=3000, measure="spectrum")
    segs = list(g_unvoiced2())
    import io
    # build_file writes a .mid as its side effect; throw the bytes away, keep the timing
    sink = Path("/tmp/05b_sink.mid")
    entry = m.build_file(sink, segs, m.marker_voice())
    sink.unlink(missing_ok=True)
    return entry


# bands are log-spaced sixths of an octave from 20 Hz; report them as edge labels
def band_hz(b):
    lo, per = b["lo_hz"], b["per_octave"]
    return [round(lo * 2 ** (i / per)) for i in range(len(b["db"]))]


def main():
    hw = sys.argv[1] if len(sys.argv) > 1 else None
    en = sys.argv[2] if len(sys.argv) > 2 else "/tmp/unvoiced2_eng.wav"
    if hw is None:
        sys.exit("usage: analyze_unvoiced2.py <hardware.wav> [engine.wav]")
    entry = entry_05b()

    def read(path):
        import soundfile as sf
        x, sr = sf.read(str(path), dtype="float32", always_2d=True)
        return x, sr

    out = {}
    for label, path in (("hardware", hw), ("engine", en)):
        x, sr = read(path)
        scale, offset = ac.align(x, sr, entry, 1002.3)
        out[label] = {s["id"]: ac.measure_segment(x, sr, s, scale, offset) for s in entry["segments"]}

    # per-segment band-shape error, level divided out: the NOISE_* question
    print(f"{'segment':16} {'shape rms dB':>12}   notes")
    bw_errs, centre_errs = [], []
    for s in entry["segments"]:
        sid = s["id"]
        h, e = out["hardware"][sid], out["engine"][sid]
        if "bands" not in h or "bands" not in e:
            continue
        a, b = np.array(h["bands"]["db"]), np.array(e["bands"]["db"])
        mask = (a > -90) | (b > -90)
        if mask.sum() < 3:
            continue
        d = np.clip(a[mask], -90, None) - np.clip(b[mask], -90, None)
        d -= np.median(d)
        rms = float(np.sqrt(np.mean(d ** 2)))
        (bw_errs if sid.startswith("ubw36") else centre_errs).append((sid, rms))
        flag = ""
        if sid == "ubw36-40" or sid == "ucentre36-1000":
            flag = "  <- the bw/center the old set sat on"
        print(f"{sid:16} {rms:12.2f}{flag}")
    if bw_errs:
        print(f"\nbandwidth sweep, shape rms {np.mean([e for _, e in bw_errs]):.2f} dB "
              f"over {len(bw_errs)} settings (note 36)")
    if centre_errs:
        print(f"centre sweep, shape rms {np.mean([e for _, e in centre_errs]):.2f} dB "
              f"over {len(centre_errs)} centres (note 36)")

    # the level-vs-bandwidth law, NOISE_BW_POW's territory: engine vs hardware band-energy per ubw step.
    # The shape rms above divides each segment's level out; this row keeps it, which is what the noise
    # level law changes.
    print("\nlevel across the bandwidth sweep, engine minus hardware (1 kHz band, dB; 0 = matched):")
    errs = []
    for s in entry["segments"]:
        sid = s["id"]
        if not sid.startswith("ubw36"):
            continue
        h, e = out["hardware"][sid], out["engine"][sid]
        if "bands" not in h or "bands" not in e:
            continue
        hz = band_hz(h["bands"])
        i1k = min(range(len(hz)), key=lambda i: abs(hz[i] - 1000))
        d = e["bands"]["db"][i1k] - h["bands"]["db"][i1k]
        errs.append(d)
        print(f"  {sid:12} {d:+7.2f}")
    if errs:
        # A silent segment (ubw36-0, bandwidth 0) reads engine-exact-zero against a real floor and would
        # otherwise swamp the mean with a 127 dB disagreement on a segment that is silent on both.
        nonzero = [e for e in errs if abs(e) < 60]
        print(f"\n  engine-vs-hardware level error over the ubw sweep (sounding segments): rms "
              f"{float(np.sqrt(np.mean(np.array(nonzero) ** 2))):.2f} dB, mean {float(np.mean(nonzero)):+.2f} dB "
              f"over {len(nonzero)} of {len(errs)}")
    return errs


if __name__ == "__main__":
    main()
