#!/usr/bin/env python3
"""One extra capture file: the performance pan, the one term of the pan sum no request file moves.

    python tools/make_capture_perpan.py           -> captures/requests/08b_panlevel_perfpn.mid + manifest row

The capture set in `captures/requests/` is FIXED: each file there has a recording already, and the
analyzer aligns against the manifest's segment times, so nothing in an existing file can change without
orphaning its take. This file is additive instead. It measures the one pan term the existing set left
centred — the performance pan byte at performance 0x11 — so the pan summing domain can be confirmed
against a real recording, the way `10_envelope2` measured the pan scaling.

`make_capture_set.py`'s `g_pan` sweeps the part pan, the pan scaling and the pan LFO depth; all three
have takes. The performance pan is exactly the same kind of segment: a held tone at a fixed perf pan,
eight settings, measured stereo. It belongs in the pan group but needs its own file because group 08's
set is frozen.

This needs a recording: the pan law is a ratio of left and right gains, readable only off the digital
output. It is NOT a register run and the debug monitor is not involved. Record it exactly like the
0918 take: play the file in, record the whole thing, one WAV with the same name next to the rest.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m


def g_perpan():
    for pp in range(0, 128, 16):
        p = fp.init_performance("PerfPan     ")
        p[0x11] = pp                                        # performance pan, 64 centre
        yield m.Seg(f"perfpan-{pp}",
                    f"performance pan {pp} (64 is centre): the last unmeasured term of the pan sum",
                    ["pan law"], m.sine(), perf=p, measure="stereo")


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    segs = list(g_perpan())
    entry = m.build_file(outdir / "08b_panlevel_perfpn.mid", segs, m.marker_voice())
    print(f"08b_panlevel_perfpn.mid  {len(segs)} segments  {entry['duration_s'] / 60:.1f} min")
    print("record it like the rest of the set: one WAV, same name, in the same folder.")


if __name__ == "__main__":
    main()
