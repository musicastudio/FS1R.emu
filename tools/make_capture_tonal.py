#!/usr/bin/env python3
"""Four capture files for what the 2026-09-22 demo recording could not settle on its own.

    python tools/make_capture_tonal.py     -> captures/requests/16_velocity_1.mid, 16_velocity_2.mid,
                                              17_egdecay.mid, 18_fmchain.mid, 19_drums.mid + manifest rows

rgwan's 2026-09-22 take of the demo with the effects and the filter stripped, with Vokodrone's bass and
drum parts recorded on their own, put the engine's drum part 2 to 3 dB above the unit on every hit and
its snare 4 dB hot in the 100 to 200 Hz band, where its two sine carriers sit. The capture set has never
measured three of the things that make that sound, and none of them can be read off a song:

- **The velocity law.** Every capture segment plays at velocity 100 with the sensitivity at 0, so the
  0918 calibration absorbed one point of `vel_att` into OUT_GAIN and the rest of the curve has never
  been seen. The drum's carriers sit at sensitivity +1 and +3 and its modulators at +2 and +4, and the
  firmware's `(15 - 2 * s) + s * VELW[v] / 8` puts a different fixed offset on every sensitivity.
- **The shape of a decay to a level that is not silence.** `02_envelope` measures every decay from full to
  silence, and the engine takes the rate as a straight line in dB to whatever the target is. The snare's
  modulator drops 99 to 80 in its first milliseconds and the unit keeps the modulation deep through 6 ms
  where the engine's carrier line is already 12 dB clear, which is what a decay that slows into its
  target would do.
- **A modulator on a modulator.** `03_fm` settled the index for one modulator on one carrier. The drum runs
  a three-deep chain onto one carrier and a feedback operator onto the other, and the engine assumes the
  same index on every link.

The fourth file is the drum voice itself, byte for byte as the demo sends it, played one hit at a time
with a second of silence around each, at the three notes and four velocities the song uses. That is the
recording to hold the other three against: whichever of them is wrong, this is where it shows.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m
from make_capture_0921 import merge_manifest
import demo_probe as dp

NOTE = 60


def probe():
    """A centred part, no filter, so a hit at note 41 and one at 91 land at the same pan."""
    p = fp.init_performance("Tonal Probe ")
    fp.set_part(p, 0, panscale=50)
    for i in (1, 2, 3):
        fp.set_part(p, i)
    return p


def sine2k(**kw):
    c, f, _ = fp.fixed_bytes(2000.0)
    return m.sine(fixed=1, coarse=c, fine=f, **kw)


# ---------------------------------------------------------------------------------- 16: velocity
VEL_SENS = (-7, -3, 0, 2, 3, 5, 7)
VELS = (1, 16, 32, 48, 64, 80, 96, 112, 127)


def g_velocity():
    for s in VEL_SENS:
        for v in VELS:
            yield m.Seg(f"vel-s{s}-v{v}", f"one fixed 2 kHz carrier at velocity {v}, amplitude velocity "
                        f"sensitivity {s:+d}: the level against velocity for this sensitivity, and the "
                        "fixed offset each sensitivity carries at full velocity",
                        ["vel_att"], sine2k(avs=s), note=NOTE, vel=v, perf=probe(), hold=1500, tail=500,
                        measure="steady")


# ---------------------------------------------------------------------------------- 17: decay shape
def g_egdecay():
    t24, t40 = m.eg_time_for_rate(24), m.eg_time_for_rate(40)
    for L in (95, 90, 80, 70, 50, 30):
        yield m.Seg(f"decayto-{L}", f"decay at chip rate 24 from full to EG level {L} and hold there: "
                    "whether the fall is a straight line in dB that stops at the target, or an approach "
                    "that slows into it", ["EG decay shape"],
                    sine2k(eg_l=(99, L, L, 0), eg_t=(0, t24, 0, 0)), note=NOTE, perf=probe(),
                    hold=4000, tail=400, measure="envelope")
    for L in (90, 70):
        yield m.Seg(f"decayto-{L}-r40", f"the same decay to level {L} at chip rate 40, where a straight "
                    "line reaches it in a few hundred milliseconds", ["EG decay shape"],
                    sine2k(eg_l=(99, L, L, 0), eg_t=(0, t40, 0, 0)), note=NOTE, perf=probe(),
                    hold=3000, tail=400, measure="envelope")
    yield m.Seg("decay-2stage", "full to 80 and then to silence, both at chip rate 30: the join between "
                "two decay segments", ["EG decay shape"],
                sine2k(eg_l=(99, 80, 0, 0), eg_t=(0, m.eg_time_for_rate(30), m.eg_time_for_rate(30), 0)),
                note=NOTE, perf=probe(), hold=6000, tail=400, measure="envelope")
    yield m.Seg("decay-snare", "the snare carrier's own envelope, 99 to 76 at rate 40 then to silence at "
                "rate 40, on a plain carrier: the shape the drum's 143 Hz line follows",
                ["EG decay shape"],
                sine2k(eg_l=(99, 76, 0, 0), eg_t=(0, 36, 36, 36)), note=NOTE, perf=probe(),
                hold=2000, tail=400, measure="envelope")


# ---------------------------------------------------------------------------------- 18: FM chains
def chain(levels, coarse=(1, 1, 1, 1)):
    """Algorithm 2: op1 -> op2 -> op3 -> op4, op4 the only carrier. levels[k] is operator k+1's level, 0 off."""
    v = fp.init_voice("FM Chain  ")
    v[0x2C] = 1
    for o in range(4):
        fp.set_op(v, o, keysync=1, level=levels[o], coarse=coarse[o])
    return v


def g_fmchain():
    for L in (70, 80, 90):
        yield m.Seg(f"chain2-{L}", f"one modulator at level {L} on the carrier, algorithm 2: the reference "
                    "03_fm already measured, in this algorithm", ["FM_INDEX"], chain((0, 0, L, 99)),
                    note=NOTE, perf=probe(), hold=2500, measure="spectrum")
    for L in (60, 70, 80, 90):
        yield m.Seg(f"chain3-{L}", f"a modulator at level 80 on a modulator at level {L} on the carrier: "
                    "whether the second link carries the same index as the first",
                    ["FM_INDEX"], chain((0, 80, L, 99)), note=NOTE, perf=probe(), hold=2500,
                    measure="spectrum")
    for L in (70, 90):
        yield m.Seg(f"chain4-{L}", f"three modulators deep, the top one at level {L}, the two below at 80",
                    ["FM_INDEX"], chain((L, 80, 80, 99)), note=NOTE, perf=probe(), hold=2500,
                    measure="spectrum")
    for L in (70, 90):
        yield m.Seg(f"chain3r2-{L}", f"the three-operator chain with the top modulator at coarse 2 and "
                    f"level {L}: the second link at a different ratio", ["FM_INDEX"],
                    chain((0, L, 80, 99), coarse=(1, 2, 1, 1)), note=NOTE, perf=probe(), hold=2500,
                    measure="spectrum")


# ---------------------------------------------------------------------------------- 19: the drum voice
def demo_drums():
    _, voices = dp.song_bulks(ROOT / "captures/demo_nofilterfx/01_Vokodrone.mid")
    return bytearray(voices[2])


def g_drums():
    base = demo_drums()
    noise_off = bytearray(base)
    voiced_off = bytearray(base)
    for o in range(8):
        noise_off[112 + o * 62 + 35 + 11] = 0
        voiced_off[112 + o * 62 + 22] = 0
    for note in (41, 60, 91):
        for vel in (44, 72, 100, 127):
            yield m.Seg(f"drum-n{note}-v{vel}", f"DemoDrums, the demo's own voice, note {note} at velocity "
                        f"{vel}, alone: the hit the song plays, with nothing else sounding",
                        ["drum hit"], base, note=note, vel=vel, perf=probe(), hold=100, tail=1100,
                        measure="impulse")
    for note, vel in ((41, 72), (60, 127)):
        yield m.Seg(f"drumnoise-off-n{note}", f"the same hit at note {note}, velocity {vel}, with every "
                    "unvoiced operator silenced: the tonal half alone", ["drum hit"], noise_off,
                    note=note, vel=vel, perf=probe(), hold=100, tail=1100, measure="impulse")
        yield m.Seg(f"drumvoiced-off-n{note}", f"the same hit with every voiced operator silenced: the "
                    "noise half alone", ["drum hit"], voiced_off, note=note, vel=vel, perf=probe(),
                    hold=100, tail=1100, measure="impulse")


WHY = {
    "16_velocity_1.mid": "the velocity law, seven sensitivities by nine velocities, first half",
    "16_velocity_2.mid": "the velocity law, second half",
    "17_egdecay.mid": "the shape of an EG decay that stops at a level rather than at silence",
    "18_fmchain.mid": "the modulation index on the second and third links of a chain",
    "19_drums.mid": "the demo's drum voice one hit at a time, to hold the three files above against",
}


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    entries = []
    vel = list(g_velocity())
    half = -(-len(vel) // 2)
    for name, segs in (("16_velocity_1.mid", vel[:half]), ("16_velocity_2.mid", vel[half:]),
                       ("17_egdecay.mid", list(g_egdecay())), ("18_fmchain.mid", list(g_fmchain())),
                       ("19_drums.mid", list(g_drums()))):
        e = m.build_file(outdir / name, segs, m.marker_voice())
        e["why"] = WHY[name]
        entries.append(e)
        print(f"{name:20} {len(segs):3} segments  {e['duration_s'] / 60:.1f} min")
    merge_manifest(outdir, entries)
    print("manifest rows written; record each like the rest of the set, one WAV of the same name")


if __name__ == "__main__":
    main()
