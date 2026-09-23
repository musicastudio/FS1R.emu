#!/usr/bin/env python3
"""Split one unbroken take of several request files into one WAV per file, by their own markers.

    python tools/split_take.py take.flac 16_velocity_1 16_velocity_2 17_egdecay 18_fmchain 19_drums 20_fltmod

Every request file opens and closes with the same three-burst marker, so the take is walked in the
order given: find the first file's start marker from the beginning, its end marker after that, cut,
and carry on from the end marker for the next. Each cut goes to captures/hardware/<name>.wav with the
lead-in the manifest expects (marker_start[0] seconds before the first burst), so analyze_capture.py
aligns it like any other take.
"""
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from analyze_capture import tone_envelope, HOP_MS


def marker_scores(e, entry):
    want = entry["marker_start"]
    burst = int(round(120 / HOP_MS))
    gap = int(round((want[1] - want[0]) * 1000 / HOP_MS))
    tmpl = np.zeros(2 * gap + burst)
    for k in range(3):
        tmpl[k * gap: k * gap + burst] = 1.0
    L = len(tmpl)
    t0 = (tmpl - tmpl.mean()) / np.linalg.norm(tmpl - tmpl.mean())
    dot = np.correlate(e, t0, mode="valid")
    cs, cs2 = np.cumsum(np.insert(e, 0, 0)), np.cumsum(np.insert(e * e, 0, 0))
    s, s2 = cs[L:] - cs[:-L], cs2[L:] - cs2[:-L]
    var = s2 - s * s / L
    floor = (0.02 * float(np.std(e))) ** 2 * L
    return np.where(var > floor, dot / np.sqrt(np.maximum(var, 1e-30)), 0.0)


def main():
    take = Path(sys.argv[1]); names = sys.argv[2:]
    x, sr = sf.read(str(take))
    mono = np.mean(x, axis=1) if x.ndim > 1 else x
    man = json.load(open(ROOT / "captures/requests/manifest.json"))
    files = {f["file"].replace(".mid", ""): f for f in man["files"]}
    e = tone_envelope(mono, sr, man.get("marker_hz", 1002.3))
    hop = sr * HOP_MS / 1000
    pos = 0
    out = ROOT / "captures/hardware"
    for n in names:
        ent = files[n]
        sc = marker_scores(e, ent)
        ms = ent["marker_start"]; me = ent["marker_end"]
        span = int((me[0] - ms[0]) * 1000 / HOP_MS)               # hops from start marker to end marker
        # the start marker: the first clean match from pos (every file's markers score alike, so the
        # global best is any of them); the end marker: the best within a few seconds of where it should be
        first = pos + int(np.argmax(sc[pos:] > 0.9))
        i0 = first + int(np.argmax(sc[first:first + 100]))
        lo, hi = i0 + span - 600, min(len(sc) - 1, i0 + span + 600)
        i1 = lo + int(np.argmax(sc[lo:hi]))
        a = int((i0 - ms[0] * 1000 / HOP_MS) * hop); b = int((i1 + 1.0 * 1000 / HOP_MS) * hop)
        a = max(0, a)
        sf.write(str(out / f"{n}.wav"), x[a:b], sr, subtype="PCM_24")
        print(f"{n:16s} start {i0 * HOP_MS / 1000:8.2f}s end {i1 * HOP_MS / 1000:8.2f}s  (expected span {me[0] - ms[0]:.1f}s, "
              f"found {(i1 - i0) * HOP_MS / 1000:.1f}s) scores {sc[i0]:.2f} {sc[i1]:.2f} -> {out / n}.wav")
        pos = i1 + 100


if __name__ == "__main__":
    main()
