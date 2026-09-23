#!/usr/bin/env python3
"""One capture file for the two filter numbers that live inside VOP3-1 and need a recording.

    python tools/make_capture_fltmod.py     -> captures/requests/20_fltmod.mid + its manifest row

The register run in FS1R.unlock (`capture3.py flteg` and `lfo2`) reads the filter EG's time per rate
word and settles that LFO2 is the chip's own; what it cannot read is what the chip does with the words:

* **feg depth**: the CPU ships `0x120 * depth` (register 0x28) and the EG level as `(L - 50) * 256 / 50`
  (0x2C), and how many cutoff bytes the corner moves per level per depth is inside the chip
  (`cal::FEG_DEPTH_BYTES`, a guess). `14_sens` parked the cutoff at 0 so the sweep saturated. Here the
  cutoff sits at 96 with the EG held at a fixed level (L1 = L2 = L3, T1 = 0) and the depth walks both
  ways, so each segment's corner against the `filtoff` reference is one point on the line.
* **lfo2 rate**: `FUN_0000C130` hands the chip `0x374A04[speed]` and the chip runs the LFO
  (`cal::LFO2_INC_K`, a guess). Full LFO2 filter depth, a slow sweep, and the corner's period is the rate.
  Six speeds, plus one segment with LFO2 delay to see the fade the same way LFO1's was measured.

The source is `15_filter`'s: the broadband unvoiced operator at bandwidth 60 and skirt 7, a `filtoff`
segment at each end.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_capture_set as m
from make_capture_0921 import merge_manifest
from make_capture_filter import NOTE, probe, src


def held(depth, level=99):
    """The EG parked at `level` from the first tick, at `depth`."""
    v = src(cut=96)
    v[0x64] = depth
    v[0x65] = 0
    v[0x66] = v[0x67] = v[0x68] = level
    v[0x69] = 0
    v[0x6A] = v[0x6B] = v[0x6C] = 60
    return v


def lfo2(speed, delay=0):
    v = src(cut=64)
    v[0x18], v[0x19], v[0x1A] = 0, speed, delay       # LFO2 triangle
    v[0x5A] = 99                                       # LFO2 filter depth
    return v


def g_fltmod():
    yield m.Seg("filtoff-a", "the source with the part filter off: every corner below is a ratio to this",
                ["filter response"], src(cut=96), note=NOTE, perf=probe(0), hold=3000, measure="spectrum")
    yield m.Seg("held-l0-d64", "cutoff 96, EG level 0, depth 0: the corner the depth line pivots on",
                ["FEG_DEPTH_BYTES"], held(64, level=0), note=NOTE, perf=probe(), hold=3000, measure="spectrum")
    for d in (72, 80, 96, 112, 127, 56, 48, 32, 16, 0):
        yield m.Seg(f"held-l99-d{d}", f"cutoff 96, EG held at level 99, depth byte {d}: the corner's shift "
                    "against the pivot is the chip's depth scale", ["FEG_DEPTH_BYTES"], held(d),
                    note=NOTE, perf=probe(), hold=3000, measure="spectrum")
    for lvl in (75, 50, 25):
        yield m.Seg(f"held-l{lvl}-d127", f"cutoff 96, EG held at level {lvl}, depth 127: whether the level "
                    "is linear in cutoff bytes", ["FEG_DEPTH_BYTES"], held(127, level=lvl), note=NOTE,
                    perf=probe(), hold=3000, measure="spectrum")
    for sp in (20, 40, 60, 80, 100, 127):
        yield m.Seg(f"lfo2-s{sp}", f"LPF24 at cutoff 64, LFO2 triangle at speed {sp} on the filter at full "
                    "depth: the corner's period is the chip's rate for the 0x374A04 word",
                    ["LFO2_INC_K"], lfo2(sp), note=NOTE, perf=probe(), hold=8000, measure="envelope")
    yield m.Seg("lfo2-s60-dly60", "LFO2 speed 60 with delay 60: the fade-in against LFO1's",
                ["LFO2_INC_K"], lfo2(60, delay=60), note=NOTE, perf=probe(), hold=8000, measure="envelope")
    yield m.Seg("filtoff-b", "the source with the filter off again at the far end of the file",
                ["filter response"], src(cut=96), note=NOTE, perf=probe(0), hold=3000, measure="spectrum")


def main():
    outdir = ROOT / "captures" / "requests"
    segs = list(g_fltmod())
    e = m.build_file(outdir / "20_fltmod.mid", segs, m.marker_voice())
    e["why"] = "the filter EG depth scale and the LFO2 rate, both inside VOP3-1"
    print(f"20_fltmod.mid  {len(segs)} segments  {e['duration_s'] / 60:.1f} min")
    merge_manifest(outdir, [e])


if __name__ == "__main__":
    main()
