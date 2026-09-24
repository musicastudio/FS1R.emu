#!/usr/bin/env python3
"""The two request files left after 2026-09-24, both for things the register sessions cannot reach:

    python tools/make_capture_keycode.py        writes 21_keycode.mid and 22_ures.mid + manifest rows

21_keycode: one decay per semitone from note 12 to 120 at time scaling 7, chip rate 34 with no key
  scaling. Every key code from 77 to 113 in steps of one, where `10_envelope2` had one note per octave
  (four key codes apart) and the engine interpolates between them. Register 0xC0 has no event the debug
  monitor can drive (`FS1R.unlock/docs/unknowns.md`, experiment 8), so the note is the only handle on
  it; each step's decay slope lands on the (4 + (q & 3)) << (q >> 2) ladder and names the chip's rate
  outright. The same decay at time scaling 0 sits at the start as the reference, and the hold on every
  note says whether the scaling reaches the hold register.

22_ures: the unvoiced resonance carrier against the bandwidth register at one centre. `13_unvoiced3`
  (register 25) and `05_unvoiced_2` (register 51) put the carrier 2 to 3 dB apart in opposite
  directions at settings 6 and 7, so its level is a function of the register that two points cannot
  fit. Five bandwidths by the four settings that carry a tone, at 8 kHz where nothing folds.

`FS1R.unlock/captures/fs1r_capture_session4.py record` plays both.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m
from make_capture_0921 import merge_manifest

CENTRE = 8000.0
T = fp.load_tables()


def keycode(note):
    return (T["NOTETAB"][note] >> 8) + 10


def eg_sine(**kw):
    c, f, _ = fp.fixed_bytes(2000.0)
    return m.sine(fixed=1, coarse=c, fine=f, **kw)


def g_keycode():
    t34 = m.eg_time_for_rate(34)
    yield m.Seg("kc-ref-note60", "the same decay at time scaling 0: the rate with no key term, the ladder's origin",
                ["rate scaling"], eg_sine(tscale=0, hold=40, eg_l=(99, 0, 0, 0), eg_t=(0, t34, 0, 0)),
                note=60, hold=4000, tail=300, measure="envelope")
    for n in range(12, 121):
        yield m.Seg(f"kc-note{n}", f"time scaling 7 at note {n}, key code {keycode(n)}: the chip's "
                    "rate at this key code, and its hold", ["rate scaling"],
                    eg_sine(tscale=7, hold=40, eg_l=(99, 0, 0, 0), eg_t=(0, t34, 0, 0)),
                    note=n, hold=4000, tail=300, measure="envelope")


def g_ures():
    for bw in (8, 20, 36, 52, 68):
        for r in (4, 5, 6, 7):
            yield m.Seg(f"ures-bw{bw}-r{r}", f"unvoiced resonance {r} at 8 kHz, bandwidth {bw}, skirt 0: the "
                        "carrier's level against the bandwidth register", ["noise resonance"],
                        m.noise(hz=CENTRE, u_bw=bw, u_res=r), hold=3000, measure="spectrum")


WHY = {
    "21_keycode.mid": "the EG key code table at every semitone, where 10_envelope2 had one note per octave",
    "22_ures.mid": "the unvoiced resonance carrier's level against the bandwidth register",
}


def main():
    outdir = ROOT / "captures" / "requests"
    entries = []
    for name, gen in (("21_keycode.mid", g_keycode), ("22_ures.mid", g_ures)):
        segs = list(gen())
        e = m.build_file(outdir / name, segs, m.marker_voice())
        e["why"] = WHY[name]
        entries.append(e)
        print(f"{name:20} {len(segs):3} segments  {e['duration_s'] / 60:.1f} min")
    merge_manifest(outdir, entries)


if __name__ == "__main__":
    main()
