#!/usr/bin/env python3
"""24_onset: does the unit have a step one control-tick after note-on, the way our engine does?

    python tools/make_capture_onset.py       writes captures/requests/24_onset.mid + manifest rows

B016 Dyno Rose clicks in the engine and not on James's unit. Everything about that step in our render
says control rate rather than waveform:

  * it sits at 5.188 ms after note-on, which is 249 samples at 48 kHz, i.e. exactly one period of the
    192.3 Hz MTU2 tick that drives every per-note update (LFO, PEG, portamento, filter EG, the
    register refresh)
  * its TIME does not move across twelve different random seeds while its SIZE does, and the seed only
    changes the note's start phase and pan. Waveform content would move with the phase; a control-rate
    discontinuity would not
  * it survives with either part muted, and both parts show it, so it is not the converted DX7 voice
    that part 2 uses. That was the earlier suspicion and it is wrong
  * it is absent from the raw channel sum and present in Device::process, so it enters at or after the
    per-voice filter

What that does NOT say is which update jumps, and our engine is not evidence about the unit either
way. The firmware writes the tone generator registers as part of note-on (FUN_000112c0) and then again
on each tick, so if any of those first values is wrong in our engine the first tick corrects it
audibly. One recording settles whether the unit does the same thing and, if it does not, which update
is responsible.

The file plays six cases, each one note struck eight times with a second of silence between, so the
onset can be averaged rather than read off a single strike:

  onset-b016        B016 Dyno Rose itself, note 72, straight off the ROM. The reported patch, for
                    reference against everything below
  onset-plain       one sine carrier, no LFO, no filter, flat EGs, flat FEG, no PEG. NOTHING updates
                    on the tick for this voice, so there must be no step at 5.19 ms. If the unit (or
                    our engine) shows one here, the tick model itself is wrong and everything else in
                    this file is unreadable. This is the control and it is the most important segment
  onset-lfo         the same voice with LFO1 at full pitch depth and zero delay. If the step is the
                    LFO's first value, it appears here and not in onset-plain
  onset-filter      the same voice with the filter on and a fast filter EG. If the step is the filter
                    EG or the cutoff coefficients, it appears here
  onset-peg         the same voice with a fast pitch EG. Same question for the PEG
  onset-feg         the same voice with a fast operator frequency EG (init +25, attack time 20)

Each case is the same carrier at a fixed 1 kHz so the onsets can be compared directly, and each voice
differs from onset-plain in exactly one respect. Whichever segment reproduces the step names the
update responsible; if only onset-b016 shows it, the cause is specific to that patch's own
parameters rather than to any one subsystem.

Read it back with tools/fit_onset.py.
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m

SR = 48000
TICK_HZ = 192.3                      # the MTU2 tick every per-note update runs on
TICK_MS = 1000.0 / TICK_HZ           # 5.2002 ms, the time the step is expected at
STRIKES = 8                          # note-ons per case, averaged by the analyzer
NOTE = 72
GAP = 1000                           # ms between strikes: long enough for a full release
HOLD = 400                           # ms each note is held


HZ = 1000.0                          # the fixed carrier every synthetic case uses
COARSE, FINE, CARRIER_HZ = fp.fixed_bytes(HZ)


def plain_voice():
    """One sine carrier at a fixed frequency, with every tick-driven thing switched off.

    Fixed frequency so the carrier is identical in every case and the note number cannot shift it.
    init_voice already gives flat PEG (0x1F-0x22 = 50), LFO depths 0 (0x15-0x17), a flat filter EG
    (0x65-0x68 = 100, depth 0x64 = 64) and the filter switch off at the part; the operator's own
    amplitude EG and frequency EG are set flat here through set_op.
    """
    v = fp.init_voice("Onset     ")
    fp.set_op(v, 0, form=fp.SINE, level=99, keysync=1, fixed=1, coarse=COARSE, fine=FINE,
              eg_l=(99, 99, 99, 99), eg_t=(0, 0, 0, 0),
              feg_init=0, feg_attack=0, feg_attack_t=0, feg_decay_t=0)
    for o in range(1, 8):
        fp.set_op(v, o, level=0)
    return v, CARRIER_HZ


def case_voices():
    """onset-plain and the four one-difference variants, as (id, voice, needs, why)."""
    out = []
    v, hz = plain_voice()
    out.append(("onset-plain", bytearray(v), None,
                "one fixed sine carrier with every tick-driven update switched off: flat amplitude EG, "
                "flat PEG, flat frequency EG, LFO depths 0, filter off. Nothing updates on the tick, so "
                "there must be NO step one tick period after note-on. The control: if this segment "
                "steps, the tick model is wrong and the rest of the file cannot be read"))

    # onset-peg and onset-lfo have to use a RATIO operator, not a fixed one: a fixed-frequency operator
    # takes its frequency word from its own coarse/fine bytes and ignores the channel pitch entirely, so
    # the PEG and the LFO's pitch depth would have nothing to act on. Ratio 1 at note 72 lands near the
    # same frequency as the fixed cases, which keeps them comparable.
    def ratio_voice(**op):
        vv = fp.init_voice("Onset     ")
        kw = dict(form=fp.SINE, level=99, keysync=1, coarse=1,
                  eg_l=(99, 99, 99, 99), eg_t=(0, 0, 0, 0),
                  feg_init=0, feg_attack=0, feg_attack_t=0, feg_decay_t=0)
        kw.update(op)
        fp.set_op(vv, 0, **kw)
        for o in range(1, 8):
            fp.set_op(vv, o, level=0)
        return vv

    v2 = ratio_voice(pms=7)
    v2[0x11] = 99                     # LFO1 speed
    v2[0x12] = 0                      # LFO1 delay 0, so the fade is open immediately
    v2[0x13] = 0                      # LFO1 key sync OFF: the phase is whatever it was, as B016 has it
    v2[0x15] = 99                     # LFO1 pitch modulation depth
    out.append(("onset-lfo", v2, None,
                "a ratio-1 carrier with LFO1 at full pitch depth, speed 99, zero delay, key sync off "
                "and the operator's pitch mod sensitivity at 7. A fixed-frequency operator ignores the "
                "channel pitch, so this case cannot use one. The LFO value is recomputed by the tick, so "
                "if the step is the LFO's first value it shows here and not in onset-plain"))

    v3 = bytearray(v)
    v3[0x54] = 0                      # LPF24
    v3[0x55] = 16 + 40                # resonance up, so a cutoff move is clearly audible
    v3[0x57] = 100                    # cutoff high: the EG swings DOWN from here, so the tone survives
    v3[0x64] = 64 - 40                # filter EG depth well off centre, negative
    for k, val in zip((0x65, 0x66, 0x67, 0x68), (100, 60, 60, 60)):
        v3[k] = val                   # a wide, fast filter EG swing that leaves the carrier audible
    out.append(("onset-filter", v3, "filter",
                "the same voice with the filter on, resonance up, and a wide fast filter EG. The cutoff "
                "coefficients and the filter EG are both tick-driven, so a step from the filter path "
                "appears here"))

    v4 = ratio_voice()
    v4[0x1F] = 80                     # PEG initial level well above centre
    for k in (0x20, 0x21, 0x22, 0x3E):
        v4[k] = 50                    # the rest centred: it returns to pitch and stays
    v4[0x23] = 20                     # PEG attack time, fast
    v4[0x3B] = 0                      # pitch EG range: the widest of the four
    out.append(("onset-peg", v4, None,
                "a ratio-1 carrier with a fast pitch EG starting well above centre, widest range. Also "
                "cannot be a fixed operator. The PEG is stepped by the tick and read by the register "
                "refresh, so a wrong first value shows as a step here"))

    v5 = bytearray(v)
    fp.set_op(v5, 0, form=fp.SINE, level=99, keysync=1, fixed=1,
              coarse=COARSE, fine=FINE,
              eg_l=(99, 99, 99, 99), eg_t=(0, 0, 0, 0),
              feg_init=25, feg_attack=0, feg_attack_t=20, feg_decay_t=50)
    out.append(("onset-feg", v5, None,
                "the same voice with a fast operator frequency EG, init +25 ramping to centre. The FEG "
                "is a linear ramp stepped once per tick (docs/findings.md 2026-09-25), so a first-value "
                "error shows as a step here"))
    return out, hz


def onset_perf(filter_on=False):
    """Part 1 alone, the voice in the common buffer, filter switch as asked."""
    p = fp.init_performance("Onset Test  ")
    fp.set_part(p, 0, on=True, channel=0, volume=127, pan=64, panscale=50,
                filter=1 if filter_on else 0)
    for i in (1, 2, 3):
        fp.set_part(p, i, on=False)
    return p


def build(path, marker):
    s = fp.Smf()
    out = {"file": path.name, "segments": []}
    default = fp.init_performance()
    s.at(0, fp.perf_bulk(default))
    s.at(m.SETTLE_PERF, fp.voice_bulk(marker))
    t = m.SETTLE_PERF + m.SETTLE_VOICE
    out["marker_start"] = []
    for _ in range(3):
        s.note(t, 72, 110, 120)
        out["marker_start"].append(round(t / 1000.0, 4))
        t += 300
    t += 800

    cases, hz = case_voices()

    # B016 Dyno Rose first. Send its performance bulk out of presets/ rather than a bank select and
    # program change: the bulk is exactly what the unit holds for B016 (presets/index.csv), it needs no
    # bank mapping to be right, and it leaves nothing to go wrong between here and the note. The voice
    # buffers come with it, so no voice bulk follows.
    b016 = (ROOT / "presets/performances/143_Dyno_Rose.syx").read_bytes()
    s.at(t, b016)
    t += m.SETTLE_PERF + m.SETTLE_VOICE
    strikes = []
    for _ in range(STRIKES):
        s.note(t, NOTE, 100, HOLD)
        strikes.append(round(t / 1000.0, 4))
        t += GAP
    out["segments"].append({
        "id": "onset-b016", "what": "B016 Dyno Rose, its own performance bulk, note 72, struck eight "
        "times. The reported patch itself: our render puts a step 5.19 ms after each note-on, one tick "
        "period, and this says whether the unit does too",
        "pins": [], "measure": "onset", "note": NOTE, "velocity": 100,
        "strikes": strikes, "t_on": strikes[0], "t_end": round(t / 1000.0, 4),
        "carrier_hz": None, "tick_ms": round(TICK_MS, 4), "performance": "B016 Dyno Rose",
    })
    t += 500

    # then the synthetic cases, each one difference from onset-plain
    for cid, voice, needs, why in cases:
        s.at(t, fp.perf_bulk(onset_perf(filter_on=needs == "filter")))
        t += m.SETTLE_PERF
        s.at(t, fp.voice_bulk(bytes(voice), part=0))
        t += m.SETTLE_VOICE
        strikes = []
        for _ in range(STRIKES):
            s.note(t, NOTE, 100, HOLD)
            strikes.append(round(t / 1000.0, 4))
            t += GAP
        out["segments"].append({
            "id": cid, "what": why, "pins": [], "measure": "onset",
            "note": NOTE, "velocity": 100, "strikes": strikes,
            "t_on": strikes[0], "t_end": round(t / 1000.0, 4),
            "carrier_hz": round(hz, 3), "tick_ms": round(TICK_MS, 4),
            "control": cid == "onset-plain",
        })
        t += 500

    s.at(t, fp.perf_bulk(default))
    t += m.SETTLE_PERF
    s.at(t, fp.voice_bulk(marker))
    t += m.SETTLE_VOICE
    out["marker_end"] = []
    for _ in range(3):
        s.note(t, 72, 110, 120)
        out["marker_end"].append(round(t / 1000.0, 4))
        t += 300

    total = s.write(path, tail_ms=1000)
    out["duration_s"] = round(total / 1000.0, 3)
    out["sha1"] = hashlib.sha1(path.read_bytes()).hexdigest()
    out["why"] = ("whether the unit steps one control-tick (5.20 ms) after note-on the way our engine "
                  "does on B016 Dyno Rose, and if so which tick-driven update is responsible")
    out["group"] = "onset"
    out["group_number"] = 24
    return out


def merge_manifest(outdir, entries):
    path = outdir / "manifest.json"
    man = json.loads(path.read_text()) if path.exists() else {"files": []}
    by = {f["file"]: f for f in man["files"]}
    for e in entries:
        by[e["file"]] = e
    man["files"] = sorted(by.values(), key=lambda f: f["file"])
    path.write_text(json.dumps(man, indent=1))
    return path


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    e = build(outdir / "24_onset.mid", m.marker_voice())
    print(f"24_onset.mid  {len(e['segments'])} segments  {e['duration_s'] / 60:.1f} min")
    for seg in e["segments"]:
        tag = "  <- CONTROL" if seg.get("control") else ""
        print(f"   {seg['id']:14s} {len(seg['strikes'])} strikes from {seg['t_on']:7.2f} s{tag}")
    print(f"\nthe step to look for sits {TICK_MS:.3f} ms after each note-on "
          f"({TICK_MS * SR / 1000:.0f} samples at {SR} Hz)")
    merge_manifest(outdir, [e])
    print(f"manifest updated: {outdir / 'manifest.json'}")


if __name__ == "__main__":
    main()
