#!/usr/bin/env python3
"""Write the hardware capture requests: MIDI files to play into a real FS1R, and a manifest saying what
every segment is for.

    python tools/make_capture_set.py              -> captures/requests/*.mid, manifest.json, README.md
    python tools/make_capture_set.py --render     also render each file through our engine into
                                                  captures/engine/, which is both a check that the
                                                  patches make sound and the baseline to compare against

Each file is self contained: it sends a performance bulk and a voice bulk before every note, so the unit
needs no setting up and the state cannot drift. Each file opens and closes with a marker, three short
bursts of a fixed 1 kHz tone, so a recording can be aligned to the manifest's segment times whatever the
recorder was doing when playback started.

**The pan.** Every file is built on `fs1r_patch.init_performance`, which leaves the performance part's PAN
SCALING byte at 0. The Data List gives that parameter as 0 to 100, so 0 is not the centre, it is the extreme,
and the unit pans hard by key: a segment at note 24 comes out hard right and one at note 108 hard left. That
was not deliberate and it went unnoticed until `10_envelope2` was recorded, where it read as a level that
moved with the note. It stays as it is. The recordings that exist were made with these exact files, so
centring the byte now would throw them away, `tools/analyze_capture.py` takes the pan back out of every
measure that does not want it, and a pan sweep in every file is a free check on the pan key scaling law,
which is what measured it. `docs/aeg.md`, "The pan was in front of everything". A new group that wants a
centred image should pass `pan_scaling=50` to `fp.set_part`.

Every segment isolates one thing. What is being measured is the chip behaviour behind the register
values, which is the whole INFERRED list in namespace cal in src/fs1r_lib.cpp: nothing in the firmware
says what the YMP706 does with a level, a rate or a bandwidth, so it has to be measured.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fs1r_patch as fp

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "captures/requests"
ENGINE_OUT = ROOT / "captures/engine"
EXE = ROOT / "bin/fs1r_emu.exe"
RENDER = ROOT / ("bin/render_capture.exe" if os.name == "nt" else "bin/render_capture")

MAX_SEG = 36              # segments per file, so no file runs much past four minutes
SETTLE_PERF = 700         # ms after a performance bulk: it reloads all four parts
SETTLE_VOICE = 500        # ms after a voice bulk
MARK_HZ = None            # filled in by marker_voice()
T = fp.load_tables()


class Seg(dict):
    """One measurement: a patch, a note, and what to do with the recording of it."""

    def __init__(self, name, what, pins, voice, note=60, vel=100, hold=2500, tail=700,
                 perf=None, measure="steady", cc=()):
        super().__init__(name=name, what=what, pins=list(pins), voice=bytes(voice), note=note, vel=vel,
                         hold=hold, tail=tail, perf=None if perf is None else bytes(perf),
                         measure=measure, cc=list(cc))


# ------------------------------------------------------------------------------ patch helpers
def marker_voice():
    global MARK_HZ
    c, f, MARK_HZ = fp.fixed_bytes(1000.0)
    v = fp.init_voice("Marker    ")
    fp.set_op(v, 1, keysync=1, fixed=1, coarse=c, fine=f, level=99)
    return v


def sine(level=99, op=1, **kw):
    """One carrier in algorithm 1, where operator 2 takes no input at all. Nothing else sounds."""
    v = fp.init_voice()
    fp.set_op(v, op, keysync=1, level=level, **kw)
    return v


def fixed_sine(hz, level=99, **kw):
    c, f, actual = fp.fixed_bytes(hz)
    return sine(level=level, fixed=1, coarse=c, fine=f, **kw), actual


def noise(level=99, hz=1000.0, **kw):
    """One unvoiced operator, its voiced half silent."""
    c, f, _ = fp.fixed_bytes(hz)
    v = fp.init_voice()
    fp.set_op(v, 1, keysync=1, level=0, u_level=level, u_coarse=c, u_fine=f, **kw)
    return v


def two_op(mod_level, carrier_level=99, mod_coarse=1, **kw):
    """Algorithm 8: operator 1 modulates operator 2, which is the only thing in the mix."""
    v = fp.init_voice()
    v[0x2C] = 7                                            # algorithm 8
    fp.set_op(v, 0, keysync=1, level=mod_level, coarse=mod_coarse, **kw)
    fp.set_op(v, 1, keysync=1, level=carrier_level)
    return v


def click(level=99):
    """A 7 ms burst: the amplitude EG at its fastest rate. An impulse for the effect blocks."""
    return sine(level=level, eg_l=(99, 0, 0, 0), eg_t=(0, 0, 0, 0))


def eg_time_for_rate(r):
    t = fp.time_for_rate(r)
    assert t is not None, r
    return t


def level_for_eg_register(a):
    """The EG level whose register value is a, since the register is LEVTAB[level] >> 1."""
    for L in range(99, -1, -1):
        if T["LEVTAB"][L] >> 1 == a:
            return L
    return None


# ------------------------------------------------------------------------------ the groups
def g_reference():
    """What a register value is worth in dB and in Hz, and where the output clips. Everything else is
    read against these, so this file is the one to record first."""
    v = fp.init_voice("Silence   ")
    yield Seg("silence", "nothing sounding: the noise floor and any DC offset of the digital output",
              ["noise floor"], v, hold=3000, measure="steady")
    for hz in (55, 110, 220, 440, 880, 1760, 3520, 7040, 10000, 14000, 18000, 22000):
        v, actual = fixed_sine(hz)
        yield Seg(f"fixed-{hz}", f"fixed-frequency sine, the firmware's word lands on {actual:.2f} Hz: "
                  "the frequency scale, the sample rate, and where the operator starts to alias",
                  ["word_hz", "sample rate"], v, measure="spectrum")
    for n in (24, 36, 48, 60, 72, 84, 96):
        yield Seg(f"ratio-note{n}", "ratio-mode sine at coarse 1: ties the note table to Hz across the keyboard",
                  ["word_hz"], sine(), note=n, measure="spectrum")
    for L in range(99, -1, -4):
        yield Seg(f"level-{L}", f"one carrier at operator level {L} (register {2 * T['LEVTAB'][L]} + 15): "
                  "the dB per step of the 8-bit level register",
                  ["LEVEL_DB"], sine(level=L), measure="steady")
    for c in range(16):
        v = sine(level=99)
        v[0x2D + 1] = c                                    # carrier level correction for operator 2
        yield Seg(f"corr-{c}", f"carrier level correction {c} on the sounding operator: its dB per step",
                  ["CARRIER_DB"], v, measure="steady")
    for k in (1, 2, 4, 8):
        v = fp.init_voice()
        for o in range(1, k + 1):                          # operators 2..k+1, none of which takes an input
            fp.set_op(v, o % 8, keysync=1, level=99)
        yield Seg(f"stack-{k}", f"{k} carriers at full level on the same frequency and phase: the mixer's "
                  "headroom and where the output clips",
                  ["headroom"], v, measure="steady")


def g_eg():
    """The amplitude EG: what a rate is in dB per second, what shape it traverses, what a level register
    is worth, and how the key scales the rates. All of it is DX7 guesswork today.

    The carrier is a fixed 2 kHz sine rather than a ratio-mode one, so that an envelope measured off the
    recording is not limited by the carrier's own period: the fastest rates cross 96 dB in a few
    milliseconds, which is one cycle of a bass note. Fixed mode does not stop the key scaling the rates,
    since the chip's key code register comes from the channel pitch, not from the operator."""
    def eg_sine(**kw):
        c, f, _ = fp.fixed_bytes(2000.0)
        return sine(fixed=1, coarse=c, fine=f, **kw)

    for r in list(range(16, 48)) + [4, 8, 12, 52, 56, 60, 63]:
        yield Seg(f"decay-rate{r}", f"decay at chip rate {r} (EG time {eg_time_for_rate(r)}) from full to "
                  "silence: the rate table and the shape of the fall",
                  ["rate_secs"], eg_sine(eg_l=(99, 0, 0, 0), eg_t=(0, eg_time_for_rate(r), 0, 0)),
                  hold=6000, tail=400, measure="envelope")
    for r in (24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 63):
        yield Seg(f"attack-rate{r}", f"attack at chip rate {r} from silence to full: the rising shape, "
                  "which we model as an exponential aiming past its target",
                  ["EG_ATTACK_K", "EG_OVERSHOOT", "rate_secs"],
                  eg_sine(eg_l=(99, 99, 99, 0), eg_t=(eg_time_for_rate(r), 0, 0, 0)),
                  hold=4000, tail=400, measure="envelope")
    for r in (24, 32, 40, 48, 56, 63):
        yield Seg(f"release-rate{r}", f"release at chip rate {r} after a short note",
                  ["rate_secs"], eg_sine(eg_l=(99, 99, 99, 0), eg_t=(0, 0, 0, eg_time_for_rate(r))),
                  hold=1000, tail=6000, measure="envelope")
    for a in range(0, 64, 4):
        L = level_for_eg_register(a)
        if L is None:
            continue
        yield Seg(f"eglevel-{a}", f"EG sustain at level register {a} (EG level {L}): the dB per step of "
                  "the 6-bit EG level register",
                  ["EG_LEVEL_DB"], eg_sine(eg_l=(99, L, L, 0), eg_t=(0, 0, 0, 0)), measure="steady")
    for h in (0, 20, 40, 60, 80, 99):
        yield Seg(f"hold-{h}", f"EG hold {h} before a fixed decay: the hold rate and its +4 offset",
                  ["rate_secs"], eg_sine(hold=h, eg_l=(99, 0, 0, 0), eg_t=(0, eg_time_for_rate(40), 0, 0)),
                  hold=5000, tail=400, measure="envelope")
    for s in range(8):
        for n in (36, 60, 84):
            yield Seg(f"tscale{s}-note{n}", f"EG time scaling {s} at note {n}: how the key code register "
                      "shortens the rates",
                      ["rate scaling"],
                      eg_sine(tscale=s, eg_l=(99, 0, 0, 0), eg_t=(0, eg_time_for_rate(34), 0, 0)),
                      note=n, hold=5000, tail=400, measure="envelope")

def g_eg2():
    """The amplitude EG, second pass: what the first one measured but could not finish.

    Separate from `g_eg` rather than appended to it because 02_envelope_1 to _3 have been recorded on a
    real unit and their segment times are what that recording is read against. Adding to the group would
    redraw the file boundaries and throw the recording away. docs/aeg.md has what each of these settles.
    """
    def eg_sine(**kw):
        c, f, _ = fp.fixed_bytes(2000.0)
        return sine(fixed=1, coarse=c, fine=f, **kw)

    # Three notes do not sit on one line: the key code moves the rate one step per step below middle C and
    # three quarters of a step above it. Ten notes at two settings say whether that is a kink, a dead band
    # or a ceiling, and the ends of the keyboard say where the law stops.
    for s in (7, 3):
        for n in (12, 24, 36, 48, 60, 72, 84, 96, 108, 120):
            yield Seg(f"tkey{s}-note{n}", f"EG time scaling {s} at note {n}: the key code law across the "
                      "whole keyboard rather than the three notes tscale measured",
                      ["rate scaling"],
                      eg_sine(tscale=s, eg_l=(99, 0, 0, 0), eg_t=(0, eg_time_for_rate(34), 0, 0)),
                      note=n, hold=5000, tail=400, measure="envelope")
    # The hold comes out as half a traverse at its own rate plus a fixed 12 ms that nothing explains.
    # Four values between the ones already measured separate the two terms.
    for h in (10, 30, 50, 70):
        yield Seg(f"hold-{h}", f"EG hold {h} before a fixed decay: the hold fraction against its lag",
                  ["EG_HOLD_FRAC", "EG_HOLD_LAG"],
                  eg_sine(hold=h, eg_l=(99, 0, 0, 0), eg_t=(0, eg_time_for_rate(40), 0, 0)),
                  hold=5000, tail=400, measure="envelope")
    # Every attack measured so far runs from silence to full, where the floor the rise starts from and the
    # overshoot it aims past trade off against each other. A target part way up separates them.
    for L in (70, 40):
        yield Seg(f"attackto-{L}", f"attack at chip rate 32 to level {L} rather than to full: the "
                  "overshoot, which a rise to full cannot tell apart from the floor",
                  ["EG_OVERSHOOT"],
                  eg_sine(eg_l=(L, L, L, 0), eg_t=(eg_time_for_rate(32), 0, 0, 0)),
                  hold=4000, tail=400, measure="envelope")
    # And a rise that starts above the floor and one that starts below it: whether the floor is a level the
    # chip jumps to from anywhere or only where a rise from silence happens to begin.
    for L in (50, 20):
        yield Seg(f"attackfrom-{L}", f"attack at chip rate 32 from level {L} rather than from silence: "
                  "whether the floor is a jump the chip takes from any level below it",
                  ["EG_ATTACK_FLOOR"],
                  eg_sine(eg_l=(99, 99, 99, L), eg_t=(eg_time_for_rate(32), 0, 0, 0)),
                  hold=4000, tail=400, measure="envelope")

def g_fm():
    """Phase modulation depth and feedback: the two numbers that set the brightness of every FM patch."""
    alg8 = fp.algorithm(7)
    assert alg8[0]["input"] == "feedback" and alg8[1]["input"] == "chain" and alg8[1]["carrier"]
    for L in range(0, 100, 4):
        yield Seg(f"index-{L}", f"modulator at level {L} onto a carrier at the same frequency: the "
                  "sideband amplitudes give the modulation index for that level",
                  ["FM_INDEX"], two_op(L), measure="spectrum")
    for k in (1, 2, 3, 4, 7, 11):
        yield Seg(f"index-ratio{k}", f"modulator at coarse {k}, level 90: whether the index depends on "
                  "the modulator's own frequency",
                  ["FM_INDEX"], two_op(90, mod_coarse=k), measure="spectrum")
    alg1 = fp.algorithm(0)
    assert alg1[0]["input"] == "feedback" and alg1[0]["writes_feedback"] and alg1[0]["carrier"]
    for f in range(8):
        v = sine(level=99, op=0)                           # operator 1 in algorithm 1 feeds back on itself
        v[0x3D] = f
        yield Seg(f"feedback-{f}", f"self-feedback level {f} on a lone carrier: the feedback gain per step",
                  ["FEEDBACK"], v, measure="spectrum")


def g_formant():
    """The formant operator, which is what makes an FS1R an FS1R and is entirely a model from the patent
    today. The spectrum of each segment shows the window directly: where the peak sits, how wide it is
    and how fast it falls away."""
    for b in range(0, 100, 4):
        c, f, _ = fp.fixed_bytes(1000.0)
        v = sine(form=fp.FRMT, fixed=1, coarse=c, fine=f, bw=b, skirt=0)
        yield Seg(f"bw-{b}", f"formant at 1 kHz, bandwidth {b}, skirt 0, fundamental 261.6 Hz: the width "
                  "of the window against the bandwidth parameter",
                  ["FRMT_BW_DB"], v, measure="spectrum")
    for bw in (20, 60):
        for s in range(8):
            c, f, _ = fp.fixed_bytes(1000.0)
            yield Seg(f"skirt{s}-bw{bw}", f"formant at 1 kHz, bandwidth {bw}, skirt {s}: the roll-off "
                      "either side of the peak",
                      ["WIN_SKIRT"], sine(form=fp.FRMT, fixed=1, coarse=c, fine=f, bw=bw, skirt=s),
                      measure="spectrum")
    for hz in (250, 500, 1000, 2000, 4000, 6000):
        c, f, actual = fp.fixed_bytes(hz)
        yield Seg(f"centre-{hz}", f"formant at {actual:.0f} Hz, bandwidth 40, skirt 2: whether the window "
                  "scales with the centre frequency",
                  ["FRMT_BW_DB"], sine(form=fp.FRMT, fixed=1, coarse=c, fine=f, bw=40, skirt=2),
                  measure="spectrum")
    for n in (36, 48, 60, 72):
        c, f, _ = fp.fixed_bytes(1000.0)
        yield Seg(f"f0-note{n}", f"the same 1 kHz formant at note {n}: the window is pitch synchronous, so "
                  "this shows what the fundamental does to it",
                  ["FRMT_BW_DB"], sine(form=fp.FRMT, fixed=1, coarse=c, fine=f, bw=40, skirt=2),
                  note=n, measure="spectrum")
    for form in (fp.ALL1, fp.ALL2, fp.ODD1, fp.ODD2):
        yield Seg(f"form-{fp.FORM_NAMES[form]}", f"spectral form {fp.FORM_NAMES[form]} at coarse 1: what "
                  "the harmonic forms actually generate",
                  ["harmonic forms"], sine(form=form), measure="spectrum")
    for form in (fp.RES1, fp.RES2):
        for r in range(0, 100, 10):
            yield Seg(f"{fp.FORM_NAMES[form]}-res{r}", f"spectral form {fp.FORM_NAMES[form]}, resonance "
                      f"byte {r}: where the resonance parameter puts the peak",
                      ["harmonic forms"], sine(form=form, bw=r), measure="spectrum")


def g_unvoiced():
    """The noise formant: a noise source shaped into a band. Our model is a cascade of one-poles plus a
    DC term ring modulated up to the centre, which is a reading of the patent, not a measurement."""
    for b in range(0, 100, 4):
        yield Seg(f"ubw-{b}", f"unvoiced formant at 1 kHz, bandwidth {b}, skirt 0, resonance 0",
                  ["NOISE_OCT"], noise(u_bw=b), hold=3000, measure="spectrum")
    for s in range(8):
        yield Seg(f"uskirt-{s}", f"unvoiced formant at 1 kHz, bandwidth 40, skirt {s}",
                  ["noise skirt"], noise(u_bw=40, u_skirt=s), hold=3000, measure="spectrum")
    for r in range(8):
        yield Seg(f"ures-{r}", f"unvoiced formant at 1 kHz, bandwidth 40, skirt 2, resonance {r}",
                  ["noise resonance"], noise(u_bw=40, u_skirt=2, u_res=r), hold=3000, measure="spectrum")
    for hz in (250, 500, 1000, 2000, 4000, 8000):
        yield Seg(f"ucentre-{hz}", f"unvoiced formant at {hz} Hz, bandwidth 40, skirt 2",
                  ["NOISE_OCT"], noise(hz=hz, u_bw=40, u_skirt=2), hold=3000, measure="spectrum")
    yield Seg("umode-normal", "unvoiced operator at its own fixed frequency",
              ["link modes"], noise(u_bw=40, u_skirt=2, u_mode=fp.UV_NORMAL), hold=3000, measure="spectrum")
    yield Seg("umode-linkfo", "unvoiced operator in linkFO: its centre follows the fundamental",
              ["link modes"], noise(u_bw=40, u_skirt=2, u_mode=fp.UV_LINK_FO), hold=3000, measure="spectrum")
    c, f, _ = fp.fixed_bytes(1500.0)
    v = fp.init_voice()
    fp.set_op(v, 1, keysync=1, level=0, form=fp.FRMT, fixed=1, coarse=c, fine=f, bw=40, skirt=2,
              u_level=99, u_bw=40, u_skirt=2, u_mode=fp.UV_LINK_FF)
    yield Seg("umode-linkff", "unvoiced operator in linkFF against a 1.5 kHz voiced formant",
              ["link modes"], v, hold=3000, measure="spectrum")


def g_filter():
    """The per-voice filter, which is inside the chip and modelled from the Data List alone. The source is
    a dense harmonic comb at 32.7 Hz, so one spectrum traces the whole response."""
    def probe(**part):
        p = fp.init_performance("Filter Test ")
        fp.set_part(p, 0, filter=1, **part)
        for i in (1, 2, 3):
            fp.set_part(p, i)
        return p

    src = sine(form=fp.ALL1)
    for t in range(6):
        v = bytearray(src)
        v[0x54] = t
        yield Seg(f"type-{t}", f"filter type {t} (LPF24, LPF18, LPF12, HPF, BPF, BEF) at cutoff 64, "
                  "resonance 0",
                  ["filter response"], v, note=24, perf=probe(), measure="spectrum")
    for c in range(0, 128, 8):
        v = bytearray(src)
        v[0x57] = c
        yield Seg(f"cutoff-{c}", f"LPF24 at cutoff byte {c}: where the corner actually sits",
                  ["CUT_BASE_HZ", "CUT_PER_OCT"], v, note=24, perf=probe(), measure="spectrum")
    for r in range(16, 117, 10):
        v = bytearray(src)
        v[0x55] = r
        yield Seg(f"reso-{r - 16}", f"LPF24 at cutoff 64, resonance {r - 16}: the Q per step",
                  ["RESO_Q0", "RESO_PER_OCT"], v, note=24, perf=probe(), measure="spectrum")
    for g in (0, 6, 12, 18, 24):
        v = bytearray(src)
        v[0x5D] = g
        yield Seg(f"ingain-{g - 12}", f"filter input gain {g - 12} dB",
                  ["filter response"], v, note=24, perf=probe(), measure="steady")
    for d, t in ((127, 20), (127, 60), (0, 20), (1, 20)):
        v = bytearray(src)
        v[0x57] = 40                                       # start low so the EG has somewhere to go
        v[0x64] = d                                        # filter EG depth
        v[0x65] = 0                                        # EG level 4
        v[0x66] = 100                                      # EG level 1
        v[0x67] = v[0x68] = 60
        v[0x69] = t                                        # EG time 1
        v[0x6A] = v[0x6B] = v[0x6C] = 60
        yield Seg(f"flteg-d{d}-t{t}", f"filter EG depth {d - 64} with attack time {t}: the sweep",
                  ["filter response"], v, note=24, perf=probe(), hold=4000, measure="envelope")


def g_modulation():
    """The per-operator modulation sensitivities and the frequency EG. The LFO itself is read from the
    firmware, so these segments check that as well as pinning the per-operator scaling."""
    def lfo(v, wave=0, speed=40, pmd=0, amd=0, fmd=0, delay=0):
        v[0x10], v[0x11], v[0x12] = wave, speed, delay
        v[0x15], v[0x16], v[0x17] = pmd, amd, fmd
        return v

    for k in range(8):
        yield Seg(f"pms-{k}", f"pitch mod sensitivity {k} with LFO1 pitch depth 99: the frequency swing",
                  ["PMS_FRAC"], lfo(sine(pms=k), pmd=99), hold=4000, measure="envelope")
    for k in range(8):
        yield Seg(f"ams-{k}", f"amplitude mod sensitivity {k} with LFO1 amplitude depth 99: the depth in dB",
                  ["ams scaling"], lfo(sine(ams=k), amd=99), hold=4000, measure="envelope")
    c1k, f1k, _ = fp.fixed_bytes(1000.0)
    for k in range(8):
        v = sine(fixed=1, coarse=c1k, fine=f1k, fms=k)
        yield Seg(f"fms-{k}", f"frequency mod sensitivity {k} on a fixed operator with LFO1 frequency "
                  "depth 99",
                  ["fms scaling"], lfo(v, fmd=99), hold=4000, measure="envelope")
    for s in range(0, 100, 10):
        yield Seg(f"lfospeed-{s}", f"LFO1 speed {s} at full pitch depth: the LFO rate, which we read from "
                  "the firmware and have never heard",
                  ["LFO rate"], lfo(sine(pms=7), speed=s, pmd=99), hold=4000, measure="envelope")
    for w in range(6):
        yield Seg(f"lfowave-{w}", f"LFO1 waveform {w} (tri, saw down, saw up, square, sine, sample and hold)",
                  ["LFO shape"], lfo(sine(pms=7), wave=w, pmd=99), hold=4000, measure="envelope")
    for d in (0, 33, 66, 99):
        yield Seg(f"lfodelay-{d}", f"LFO1 delay {d}: the delay and the fade that follows it",
                  ["LFO delay"], lfo(sine(pms=7), pmd=99, delay=d), hold=6000, measure="envelope")
    for init, att, t in ((50, 0, 0), (-50, 0, 0), (25, 0, 40), (-25, 0, 40), (0, 50, 40), (0, -50, 40)):
        yield Seg(f"feg-i{init}-a{att}-t{t}", f"frequency EG from {init} to {att} at attack time {t}: the "
                  "range in semitones and the time it takes",
                  ["FEG_SEMIS", "FEG_TIME_K"],
                  sine(feg_init=init, feg_attack=att, feg_attack_t=t, feg_decay_t=40),
                  hold=4000, measure="envelope")
    for d in range(-15, 16, 3):
        yield Seg(f"detune-{d}", f"detune {d} on a ratio-mode carrier: the cents per step",
                  ["DETUNE_CENTS"], sine(detune=d), hold=3000, measure="spectrum")


def g_pan():
    """Pan and the part level path. The tables are read from the EPROM; what a pan register is worth in
    dB on each side is not."""
    for pan in range(1, 128, 9):
        p = fp.init_performance("Pan Test    ")
        fp.set_part(p, 0, pan=pan)
        yield Seg(f"pan-{pan}", f"part pan {pan} (64 is centre): the left and right gains",
                  ["pan law"], sine(), perf=p, measure="stereo")
    for scale in (0, 50, 100):
        for n in (36, 60, 84):
            p = fp.init_performance("PanScale    ")
            fp.set_part(p, 0, panscale=scale)
            yield Seg(f"panscale{scale}-note{n}", f"pan key scaling {scale} at note {n}",
                      ["pan law"], sine(), note=n, perf=p, measure="stereo")
    for d in (0, 33, 66, 99):
        p = fp.init_performance("PanLFO      ")
        fp.set_part(p, 0, panlfo=d)
        v = sine()
        v[0x11] = 40                                        # LFO1 speed
        yield Seg(f"panlfo-{d}", f"pan LFO depth {d}",
                  ["pan law"], v, perf=p, hold=4000, measure="stereo")
    for vol in range(0, 128, 8):
        p = fp.init_performance("Volume      ")
        fp.set_part(p, 0, volume=vol)
        yield Seg(f"volume-{vol}", f"part volume {vol}: the part level table in dB",
                  ["VNBAL"], sine(), perf=p, measure="steady")
    for e in range(0, 128, 16):
        yield Seg(f"expression-{e}", f"expression (CC11) {e} against volume 127",
                  ["VNBAL"], sine(), cc=[(11, e)], measure="steady")
    for b in range(0, 128, 16):
        p = fp.init_performance("Balance     ")
        fp.set_part(p, 0, balance=b)
        v = fp.init_voice()
        c, f, _ = fp.fixed_bytes(1000.0)
        fp.set_op(v, 1, keysync=1, level=99, u_level=99, u_coarse=c, u_fine=f, u_bw=40, u_skirt=2)
        yield Seg(f"balance-{b}", f"voiced/unvoiced balance {b} with both halves sounding: a 261.6 Hz "
                  "tone and a 1 kHz noise band, so the two levels are separable in one spectrum",
                  ["VNBAL"], v, perf=p, measure="spectrum")


def rom_effect_blocks():
    """The 112-byte effect block of every ROM performance, indexed by the type byte of each slot. A type
    driven with all-zero parameters is not that type at all: a delay with a time of zero feeds back on
    itself in one sample. These are factory parameter sets, so hardware and engine hear the same thing."""
    rom = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
    out = {}
    if not rom.exists():
        return out
    d = rom.read_bytes()
    for i in range(360):
        fx = d[0xC580 + i * 400 + 80: 0xC580 + i * 400 + 192]
        for slot, off in (("rev", 0x58), ("var", 0x5B), ("ins", 0x5F)):
            out.setdefault((slot, fx[off]), bytes(fx))
    return out


# Factory defaults for the types no ROM performance uses, transcribed from the Effect Parameter List
# (docs/FS1R_DataList_text.txt) into the engine's word order: reverb value = {"w": 8 words, "b": 8 trailing
# bytes}; variation / insertion = 16 words. Words the Data List doesn't list for a type are 0.
#   rev 11 Basement, 12 Canyon; var 15 Noise Gate, 22 Delay L,R, 24 CrossDelay, 25-28 Hall/Room/Stage/Plate;
#   ins 18 Wah+DS+Dly, 19 Wah+OD+Dly, 23 Noise Gate, 27 Cmp+OD+Dly, 33 Delay LCR, 35 Echo, 36 CrossDelay,
#   37 ER 1, 38 ER 2, 40 Revrs Gate (names per the Effect Type List; ins_to_common agrees).
FX_DEFAULTS = {
    ("rev", 11): {"w": [5, 6, 1, 0, 34, 67, 0, 97], "b": [15, 0, 32, 3, 74, 10, 36, 0]},
    ("rev", 12): {"w": [59, 6, 31, 0, 45, 89, 166, 253], "b": [13, 0, 11, 4, 72, 4, 100, 0]},
    ("var", 15): [0, 22, 82, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ("var", 22): [2500, 3750, 3752, 3750, 87, 3, 0, 0, 0, 0, 0, 0, 26, 64, 46, 64],
    ("var", 24): [3650, 3650, 88, 1, 5, 0, 0, 0, 0, 0, 0, 0, 25, 64, 50, 62],
    ("var", 25): [18, 10, 4, 13, 49, 0, 0, 0, 0, 0, 0, 2, 50, 8, 64, 0],
    ("var", 26): [5, 10, 8, 4, 49, 0, 0, 0, 0, 0, 0, 2, 64, 8, 64, 0],
    ("var", 27): [19, 10, 8, 7, 54, 0, 0, 0, 0, 0, 0, 2, 64, 6, 64, 0],
    ("var", 28): [25, 10, 3, 8, 49, 0, 0, 0, 0, 0, 0, 2, 64, 5, 64, 0],
    ("ins", 18): [1900, 84, 30, 60, 53, 68, 72, 0, 0, 127, 102, 20, 23, 13, 0, 0],
    ("ins", 19): [1600, 84, 50, 16, 87, 64, 64, 0, 0, 127, 80, 35, 30, 25, 0, 0],
    ("ins", 23): [0, 22, 82, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ("ins", 27): [1900, 74, 50, 18, 65, 68, 69, 0, 0, 127, 6, 3, 95, 6, 0, 0],
    ("ins", 33): [3333, 1667, 5000, 5000, 74, 100, 3, 0, 0, 32, 0, 0, 26, 64, 46, 64],
    ("ins", 35): [2200, 86, 2100, 85, 5, 2300, 2350, 62, 0, 32, 0, 0, 23, 58, 50, 63],
    ("ins", 36): [3650, 3650, 88, 1, 5, 0, 0, 0, 0, 32, 0, 0, 25, 64, 50, 62],
    ("ins", 37): [0, 40, 5, 3, 74, 0, 46, 0, 0, 29, 7, 1, 10, 0, 0, 0],
    ("ins", 38): [2, 20, 8, 2, 67, 0, 55, 0, 0, 29, 5, 3, 10, 0, 0, 0],
    ("ins", 40): [1, 25, 8, 1, 64, 0, 47, 0, 0, 127, 6, 3, 10, 0, 0, 0],
}


def data_list_block(slot, t):
    """Encode FX_DEFAULTS[(slot, t)] into a 112-byte effect block; None if not covered."""
    e = FX_DEFAULTS.get((slot, t))
    if e is None:
        return None
    blk = bytearray(112)
    base = {"rev": 0x00, "var": 0x18, "ins": 0x38}[slot]
    w, b = (e["w"], e["b"]) if isinstance(e, dict) else (e, None)
    for k, v in enumerate(w):
        blk[base + 2 * k] = v >> 7
        blk[base + 2 * k + 1] = v & 0x7F
    if b:
        for i, v in enumerate(b):
            blk[0x10 + i] = v
    return bytes(blk)


def g_effects():
    """Impulse responses of the three effect blocks. The effects run on a DSP whose instruction set
    nobody has decoded, so these are for modelling by ear and by measurement, not for exactness. Lowest
    priority: skip this whole group if time is short."""
    blocks = rom_effect_blocks()

    def with_block(slot, t, **part):
        p = fp.init_performance("Effect Test ")
        fx = blocks.get((slot, t))
        src = "a factory parameter set from the preset performances"
        if fx is None:
            fx = data_list_block(slot, t)
            src = "the Data List default parameter set"
        known = fx is not None
        if known:
            p[80:192] = fx
        p[80 + {"rev": 0x58, "var": 0x5B, "ins": 0x5F}[slot]] = t
        for other, off in (("rev", 0x58), ("var", 0x5B), ("ins", 0x5F)):
            if other != slot:
                p[80 + off] = 0                             # only one block in the path at a time
        for gain in (0x64, 0x68, 0x6B):
            p[80 + gain] = 64                               # master EQ flat
        fp.set_part(p, 0, **part)
        for i in (1, 2, 3):
            fp.set_part(p, i)
        return p, known, src

    for slot, count, ret, part, tail in (
            ("rev", 17, 0x5A, dict(dry=0, revsend=127), 5000),
            ("var", 29, 0x5D, dict(dry=0, varsend=127), 4000),
            ("ins", 41, None, dict(inssw=1), 4000)):
        for t in range(1, count):
            p, known, src = with_block(slot, t, **part)
            if ret is not None:
                p[80 + ret] = 127                           # return level
            name = {"rev": "reverb", "var": "variation", "ins": "insertion"}[slot]
            yield Seg(f"{name}-{t}", f"{name} type {t} with "
                      + (src if known else
                         "zeroed parameters: no preset or Data List entry covers this type")
                      + ", one click in",
                      [name], click(), perf=p, hold=200, tail=tail, measure="impulse")



def g_detune():
    """Detune, every step and across the keyboard.

    07_modulation_2 stepped detune by three and measured note 60 only, and that was enough to show the
    engine's 2 cents per step is wrong: the hardware runs 1.21 cents per step near zero and 2.7 at the
    ends, and is not quite symmetric. What it cannot say is what the sixteen steps in between do, and
    whether the control is a fixed frequency offset or a fixed ratio, which are the same thing at one
    note and nothing like it two octaves down. A detuned patch beats at the difference between its
    operators, so a wrong answer is heard as a warble at the wrong rate rather than as a wrong pitch,
    which is what sent the demo song "Full Tines" out with a vibrato the unit does not have.

    Every step first, then the four steps that matter most across five more notes. Read the frequency
    off the steady part of each, not off an FFT bin: the tone is stable to a millihertz for two seconds
    and the whole answer sits inside a couple of cents.
    """
    for d in range(-15, 16):
        yield Seg(f"det-{d}", f"detune {d} at note 60: the cents this step is worth",
                  ["DETUNE_CENTS"], sine(detune=d), hold=3000, measure="spectrum")
    for n in (36, 48, 72, 84, 96):
        for d in (-15, -7, 7, 15):
            yield Seg(f"detnote{n}-{d}", f"detune {d} at note {n}: whether detune is a fixed frequency "
                      "offset or a fixed ratio",
                      ["DETUNE_CENTS"], sine(detune=d), note=n, hold=3000, measure="spectrum")

GROUPS = [
    (1, "reference", "the dB and Hz that everything else is measured against", g_reference),
    (2, "envelope", "the amplitude EG: rates, shape, levels, key scaling", g_eg),
    (3, "fm", "modulation index and feedback", g_fm),
    (4, "formant", "the formant window and the spectral forms", g_formant),
    (5, "unvoiced", "the noise formant", g_unvoiced),
    (6, "filter", "the per-voice filter", g_filter),
    (7, "modulation", "per-operator sensitivities, the LFO and the frequency EG", g_modulation),
    (8, "panlevel", "pan and the part level path", g_pan),
    (9, "effects", "impulse responses of the three effect blocks (optional)", g_effects),
    (10, "envelope2", "the amplitude EG, second pass: the key code law, the hold, the attack shape", g_eg2),
    (11, "detune", "detune: every step, and whether it is a fixed offset or a fixed ratio", g_detune),
]


# ------------------------------------------------------------------------------ writing
def build_file(path, segs, marker):
    """One MIDI file: a known performance, marker, the segments, the same performance again, marker.

    The performance resets matter twice over. They stop a file depending on whatever the unit happened to
    be playing when it started, which would otherwise put that performance's pan, volume, filter, effects
    and EG offsets in front of every measurement. And they put the markers themselves on a centred,
    full-level state, so a file whose last segment panned hard right still ends with a marker we can find.
    """
    s = fp.Smf()
    default = fp.init_performance()
    out = {"file": path.name, "segments": []}
    s.at(0, fp.perf_bulk(default))
    s.at(SETTLE_PERF, fp.voice_bulk(marker))
    t = SETTLE_PERF + SETTLE_VOICE
    out["marker_start"] = []
    for k in range(3):
        s.note(t, 72, 110, 120)
        out["marker_start"].append(round(t / 1000.0, 4))
        t += 300
    t += 800
    prev_perf = default
    for seg in segs:
        perf = seg["perf"]
        if perf is not None and perf != prev_perf:
            s.at(t, fp.perf_bulk(perf))
            t += SETTLE_PERF
            prev_perf = perf
        s.at(t, fp.voice_bulk(seg["voice"]))
        t += SETTLE_VOICE
        for num, val in seg["cc"]:
            s.cc(t - 100, num, val)
        on = t
        s.note(on, seg["note"], seg["vel"], seg["hold"])
        off = on + seg["hold"]
        t = off + seg["tail"]
        out["segments"].append({
            "id": seg["name"], "what": seg["what"], "pins": seg["pins"], "measure": seg["measure"],
            "note": seg["note"], "velocity": seg["vel"],
            "t_on": round(on / 1000.0, 4), "t_off": round(off / 1000.0, 4), "t_end": round(t / 1000.0, 4),
            "steady": [round((on + 600) / 1000.0, 4), round((off - 200) / 1000.0, 4)],
        })
    if prev_perf != default:
        s.at(t, fp.perf_bulk(default))
        t += SETTLE_PERF
    s.at(t, fp.voice_bulk(marker))
    t += SETTLE_VOICE
    out["marker_end"] = []
    for k in range(3):
        s.note(t, 72, 110, 120)
        out["marker_end"].append(round(t / 1000.0, 4))
        t += 300
    total = s.write(path, tail_ms=1000)
    out["duration_s"] = round(total / 1000.0, 3)
    out["sha1"] = hashlib.sha1(path.read_bytes()).hexdigest()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true", help="also render each file through our own engine")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    marker = marker_voice()
    manifest = {"generated": date.today().isoformat(), "engine_rate_hz": 48000,
                "marker_hz": round(MARK_HZ, 3),
                "note": "Times are seconds from the start of the MIDI file. Align a recording with the "
                        "three marker bursts at each end, then read the segment times off this manifest.",
                "files": []}
    total = 0.0
    for num, name, why, gen in GROUPS:
        segs = list(gen())
        n = max(1, -(-len(segs) // MAX_SEG))               # split evenly, no three-segment tail files
        size = -(-len(segs) // n)
        parts = [segs[i:i + size] for i in range(0, len(segs), size)]
        for k, part in enumerate(parts):
            stem = f"{num:02d}_{name}" + (f"_{k + 1}" if len(parts) > 1 else "")
            entry = build_file(OUT / f"{stem}.mid", part, marker)
            entry.update(group=name, group_number=num, why=why, part=k + 1, parts=len(parts))
            manifest["files"].append(entry)
            total += entry["duration_s"]
            print(f"{stem}.mid  {len(part):3d} segments  {entry['duration_s'] / 60:5.1f} min")
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1))
    write_readme(manifest)
    print(f"\n{len(manifest['files'])} files, {sum(len(f['segments']) for f in manifest['files'])} segments, "
          f"{total / 60:.0f} minutes of playback")
    if args.render:
        ENGINE_OUT.mkdir(parents=True, exist_ok=True)
        # render_capture is the portable one and writes 32-bit float, so the render carries no
        # quantisation floor of its own against a 24-bit capture. Fall back to the console on Windows.
        if RENDER.exists():
            cmd = lambda mid, wav: [str(RENDER), "-f", "-d", "1", str(wav), str(mid)]
        elif EXE.exists():
            cmd = lambda mid, wav: [str(EXE), "-smf", str(mid), "-w", str(wav), "-d", "1"]
        else:
            sys.exit(f"neither {RENDER} nor {EXE} exists: build render_capture (cmake) or run build.bat")
        for f in manifest["files"]:
            wav = ENGINE_OUT / (Path(f["file"]).stem + ".wav")
            r = subprocess.run(cmd(OUT / f["file"], wav), capture_output=True, text=True)
            line = [x for x in r.stdout.splitlines() if x.startswith("wrote")]
            print(f["file"], line[0] if line else r.stdout.strip() or r.stderr.strip())


def write_readme(manifest):
    lines = [
        "# FS1R capture requests",
        "",
        "Generated by `tools/make_capture_set.py`. Each `.mid` file plays into a real FS1R and sets up every patch itself, so the unit needs no preparation: no panel edits, no memory to restore, and nothing left behind afterwards except the current performance and voice buffers.",
        "",
        "The recording procedure is in `docs/hardware_capture_request.md`. In short: play one file into the FS1R, record its digital output for the whole file, and save one WAV per MIDI file under the same name. Every file opens and closes with three short 1 kHz bursts, which is how a recording gets aligned to the segment times in `manifest.json`.",
        "",
        "| file | segments | minutes | what it settles |",
        "|---|---|---|---|",
    ]
    for f in manifest["files"]:
        lines.append(f"| `{f['file']}` | {len(f['segments'])} | {f['duration_s'] / 60:.1f} | {f['why']} |")
    lines += ["",
              f"Total {sum(len(f['segments']) for f in manifest['files'])} segments, "
              f"{sum(f['duration_s'] for f in manifest['files']) / 60:.0f} minutes.",
              "",
              "Files are numbered in priority order. 01 to 04 carry most of the value: without 01 nothing else can be read in absolute terms, and 02 to 04 settle the envelope, the modulation index and the formant window, which are the three models the engine leans on hardest.",
              ""]
    (OUT / "README.md").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
