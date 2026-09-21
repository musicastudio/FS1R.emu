#!/usr/bin/env python3
"""One capture file that can actually see the per-voice filter.

    python tools/make_capture_filter.py     -> captures/requests/15_filter.mid + its manifest row

`06_filter_1` and `_2` have been in the set since the beginning and neither of them measures a filter.
Their source is `sine(form=ALL1)` at note 24, which the 2026-09-19 formant work established is **one
partial at 32.7 Hz** on the unit, four octaves below any corner the filter reaches. So on rgwan's 0918
recording the eleven resonance segments read -34.906 to -34.908 dB across the whole 0 to 100 range, the
three lowpass slopes read identically, and of sixteen cutoff bytes only 0, 8 and 16 attenuate anything.
`LADDER_K`, `RESO_COMP`, `RESO_Q0` and `RESO_PER_OCT` have never been measured by anything, and the
`CUT_HZ0` and `CUT_OCT` fitted on 2026-09-21 rest on three points.

The fix is a source with energy everywhere, and `13_unvoiced3` found one on the unit: an unvoiced
operator at bandwidth 60 and skirt 7 puts its half-power band from 21 Hz to 16.4 kHz. One spectrum of
that through the filter traces the whole response instead of giving one number.

Two `filtoff` segments carry the same source with the part's filter switch off, one at each end of the
file. The response is the ratio to them, so whatever the engine's model of the source gets wrong
divides out of every measurement here and the drift between the two says whether anything moved.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m
from make_capture_0921 import merge_manifest

NOTE = 60           # the pan scaling every request file leaves at its extreme is centred here
CENTRE = 8000.0     # the unvoiced carrier; the band reaches from DC to Nyquist around it at skirt 7


def probe(on=1):
    p = fp.init_performance("Filter Wide ")
    fp.set_part(p, 0, filter=on)
    for i in (1, 2, 3):
        fp.set_part(p, i)
    return p


def src(cut=64, reso=0, typ=0):
    """The broadband unvoiced source, with the voiced half silent and the filter EG flat."""
    v = m.noise(hz=CENTRE, u_bw=60, u_skirt=7)
    v[0x54] = typ                      # filter type: LPF24, LPF18, LPF12, HPF, BPF, BEF
    v[0x55] = reso + 16                # resonance, byte 16 is displayed 0
    v[0x57] = cut                      # cutoff
    v[0x5D] = 12                       # input gain 0 dB
    v[0x64] = 64                       # filter EG depth 0, so nothing moves under the note
    return v


TYPES = ("LPF24", "LPF18", "LPF12", "HPF", "BPF", "BEF")


def g_filter():
    yield m.Seg("filtoff-a", "the source with the part filter switched off: every response below is a "
                "ratio to this", ["filter response"], src(), note=NOTE, perf=probe(0), hold=3000,
                measure="spectrum")
    for c in range(0, 128, 8):
        yield m.Seg(f"wcut-{c}", f"LPF24 at cutoff byte {c}, resonance 0: where the corner sits and how "
                    "steep it is", ["CUT_HZ0", "CUT_OCT"], src(cut=c), note=NOTE, perf=probe(),
                    hold=3000, measure="spectrum")
    for r in range(0, 101, 10):
        yield m.Seg(f"wreso-{r}", f"LPF24 at cutoff 64, resonance {r}: the peak, which nothing has ever "
                    "seen", ["LADDER_K", "RESO_COMP"], src(reso=r), note=NOTE, perf=probe(), hold=3000,
                    measure="spectrum")
    for r in (0, 40, 70, 100):
        yield m.Seg(f"wreso-cut32-{r}", f"LPF24 at cutoff 32, resonance {r}: the same peak an octave and "
                    "a half down, where the source is denser",
                    ["LADDER_K"], src(cut=32, reso=r), note=NOTE, perf=probe(), hold=3000,
                    measure="spectrum")
    for t, name in enumerate(TYPES):
        yield m.Seg(f"wtype-{name}", f"{name} at cutoff 64, resonance 0: the slope and the shape",
                    ["filter response"], src(typ=t), note=NOTE, perf=probe(), hold=3000,
                    measure="spectrum")
    for t, name in enumerate(TYPES):
        yield m.Seg(f"wtypeq-{name}", f"{name} at cutoff 64, resonance 60: the firmware zeroes resonance "
                    "table B for HPF and BEF, and this is where that shows",
                    ["RESO_COMP"], src(typ=t, reso=60), note=NOTE, perf=probe(), hold=3000,
                    measure="spectrum")
    for t, name in ((3, "HPF"), (4, "BPF")):
        for c in (16, 48, 80, 112):
            yield m.Seg(f"w{name.lower()}-cut{c}", f"{name} at cutoff byte {c}: whether the corner law "
                        "is the lowpass's", ["CUT_OCT"], src(cut=c, typ=t), note=NOTE, perf=probe(),
                        hold=3000, measure="spectrum")
    yield m.Seg("filtoff-b", "the source with the filter off again, at the other end of the file: the "
                "drift between the two says whether anything moved under the sweep",
                ["filter response"], src(), note=NOTE, perf=probe(0), hold=3000, measure="spectrum")


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    segs = list(g_filter())
    e = m.build_file(outdir / "15_filter.mid", segs, m.marker_voice())
    e["why"] = "the per-voice filter, against a source that has energy at every frequency"
    print(f"15_filter.mid  {len(segs)} segments  {e['duration_s'] / 60:.1f} min")
    merge_manifest(outdir, [e])
    print("manifest row written; record it like the rest of the set, one WAV of the same name")


if __name__ == "__main__":
    main()
