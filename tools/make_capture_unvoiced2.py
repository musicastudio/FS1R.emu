#!/usr/bin/env python3
"""One extra capture file: the noise formant at a second note, which the NOISE_* constants never got.

    python tools/make_capture_unvoiced2.py        -> captures/requests/05b_unvoiced2.mid + README note

The noise formant's three constants — NOISE_BASE_HZ, NOISE_OCT and NOISE_BW_POW — are fitted at note 60
alone, which is exactly the hole the formant window fell into (a width law read in the wrong units
because nothing varied the fundamental). Everything the existing `05_unvoiced` group sweeps sits at
note 60: bandwidth, skirt, resonance, centre. This file repeats the bandwidth sweep and the centre
sweep at note 36, so the same law at two fundamentals separates "a fixed time" from "a fixed fraction
of the period" the way the formant's two-note reading did.

This needs a recording: the noise band's shape and level against bandwidth is only readable off the
digital output. It is NOT a register run and the debug monitor is not involved. Record it exactly like
the 0918 take: play the file in, record the whole thing, one WAV with the same name next to the rest.
The existing 05_unvoiced files are frozen against their takes, so this is additive, as 08b was for pan.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m

NOTE = 36        # a fourth below the existing set's note 60, where the drum's lows live


def g_unvoiced2():
    for b in range(0, 100, 4):
        yield m.Seg(f"ubw36-{b}", f"unvoiced formant at 1 kHz, bandwidth {b}, skirt 0, resonance 0, note 36",
                    ["NOISE_OCT"], m.noise(u_bw=b), note=NOTE, hold=3000, measure="spectrum")
    for hz in (250, 500, 1000, 2000, 4000, 8000):
        yield m.Seg(f"ucentre36-{hz}", f"unvoiced formant at {hz} Hz, bandwidth 40, skirt 2, note 36",
                    ["NOISE_OCT"], m.noise(hz=hz, u_bw=40, u_skirt=2), note=NOTE, hold=3000,
                    measure="spectrum")


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    segs = list(g_unvoiced2())
    entry = m.build_file(outdir / "05b_unvoiced2.mid", segs, m.marker_voice())
    print(f"05b_unvoiced2.mid  {len(segs)} segments  {entry['duration_s'] / 60:.1f} min")
    print("record it like the rest of the set: one WAV, same name, in the same folder.")


if __name__ == "__main__":
    main()
