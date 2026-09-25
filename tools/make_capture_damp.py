#!/usr/bin/env python3
"""23_damp: how fast does the chip fade a channel the allocator takes from a sounding note?

    python tools/make_capture_damp.py        writes captures/requests/23_damp.mid + its manifest rows

`FUN_00023000` proves the fade exists. Note-on calls it with a one-hot mask out of the table at
0x35B260 indexed by the channel the allocator just claimed, and the call sets that channel's EG stage
word to 4, the release stage, then writes register 0xFC/FD. So a note landing on a channel that is
still sounding does not cut the old waveform off: the chip releases it. What the firmware does not say
is the rate, and `cal::DAMP_MS` in FSVR is currently a plausible number rather than a measured one.

Nothing already recorded can settle it. A damp only happens when the allocator runs out of channels,
which in the whole capture set never happens (every file plays one note at a time), and in the demo
songs happens four times in fifteen songs, never twice the same way.

**The stimulus.** Fill every channel, keep exactly one of them audible, then strike one more note so
the allocator has to take the audible one. What the recording then holds is the damp on its own: a
steady tone that stops, with the shape of the stopping being the answer. Three readings of it, because
one would not separate a time from a rate:

  damp-loud / damp-mid / damp-quiet   the same steal at three levels. A fixed time constant gives the
                                      same decay shape at all three; a fixed slope in dB per second
                                      (a linear ramp, as the frequency EG turned out to be) gives a
                                      shorter fade from a lower level.
  damp-lowf / damp-highf              the same steal at 500 Hz and at 4 kHz, so a fade that is on the
                                      level register can be told from one that mutes the output.
  damp-ref                            the same voice and note with NO steal, as the reference for what
                                      the note does when it is left alone. Without this the note's own
                                      envelope cannot be separated from the damp.

**How the one audible note is the one that gets taken.** `FUN_0000f7dc` picks the part that is furthest
over its note reserve, then within that part the channel whose stored note number is above the
arriving note's. Both handles are used here rather than trusted: the audible note is on part 1 with
reserve 0 (so part 1 is the only part over its reserve, part 2 holding 31 notes under a reserve of 32),
and it is the highest note number of the lot while the note that steals is the lowest. The filler
notes on part 2 are a voice with every level at 0, so they occupy channels and make no sound.

That reading of the allocator is a reading, not a measurement, so the file is built to fail loudly
rather than quietly: if the steal lands on a filler instead, the audible tone simply keeps sounding
past the steal time and the segment reads as "no damp", which is a result and not a silent wrong
number. `damp-ref` is what it gets compared against.

Every voice here is fixed-frequency, so the note numbers that drive the allocator do not change any
pitch: the tone is the same whichever channel it lands on.

`FS1R.unlock/captures/fs1r_capture_session5.py record` should play this alongside anything else that
session records. It needs no monitor, no patched firmware and no register access: it is ordinary MIDI
into a stock unit with a recorder running, the lowest hardware risk class there is.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m
from make_capture_0921 import merge_manifest

NCHAN = 32                 # the allocator walks 32 channels (FUN_0000f7dc)
HOLD_BEFORE = 2500         # ms the audible note sounds before the steal, so its own level is readable
HOLD_AFTER = 1500          # ms recorded after it, so a slow fade is not cut off
FILLERS = NCHAN - 1        # every channel but the audible one


def audible_voice(hz, level=99):
    """One fixed-frequency carrier, instant attack, holds at full until released."""
    c, f, actual = fp.fixed_bytes(hz)
    v = fp.init_voice("DampTone  ")
    fp.set_op(v, 1, form=0, fixed=1, coarse=c, fine=f, keysync=1, level=level,
              eg_l=(99, 99, 99, 0), eg_t=(0, 0, 0, 99))
    return v, actual


def silent_voice():
    """A voice that claims a channel and makes no sound: every operator level 0."""
    return fp.init_voice("DampNull  ")


def damp_perf(part1_volume=127):
    """Part 1 audible with note reserve 0, part 2 the silent filler with reserve 32.

    The reserve is what decides which part the allocator takes from: it maximizes
    (notes sounding - reserve) over the parts and skips any part not above its own reserve
    (FUN_0000f7dc). Part 2 holding 31 notes under a reserve of 32 is never a candidate, so part 1,
    one note over its reserve of 0, is the only one.
    """
    p = fp.init_performance("Damp Test   ")
    fp.set_part(p, 0, on=True, channel=0, volume=part1_volume, pan=64, panscale=50)
    fp.set_part(p, 1, on=True, channel=1, volume=127, pan=64, panscale=50)
    p[192 + 0 * 52 + 0x00] = 0        # part 1 note reserve 0
    p[192 + 1 * 52 + 0x00] = NCHAN    # part 2 reserves the lot
    return p


def build(path, marker):
    """The file, and its manifest rows. One row per steal, each row's t_on being the steal."""
    s = fp.Smf()
    default = fp.init_performance()
    out = {"file": path.name, "segments": []}
    s.at(0, fp.perf_bulk(default))
    s.at(m.SETTLE_PERF, fp.voice_bulk(marker))
    t = m.SETTLE_PERF + m.SETTLE_VOICE
    out["marker_start"] = []
    for _ in range(3):
        s.note(t, 72, 110, 120)
        out["marker_start"].append(round(t / 1000.0, 4))
        t += 300
    t += 800

    # level, frequency, whether to steal, and what the row is for
    cases = [
        ("damp-ref", 4000.0, 127, False,
         "the audible note with no steal at all: the reference the three damp segments are read "
         "against, since the note's own envelope has to be separated from the fade"),
        ("damp-loud", 4000.0, 127, True,
         "the steal at full part volume: the fade's shape and how long it takes"),
        ("damp-mid", 4000.0, 100, True,
         "the same steal 8 dB down. A fixed time constant fades in the same time as damp-loud; a "
         "fixed dB-per-second slope fades quicker from the lower level"),
        ("damp-quiet", 4000.0, 80, True,
         "the same steal quieter again, the third point that separates a time from a rate"),
        ("damp-lowf", 500.0, 127, True,
         "the steal at 500 Hz: a fade on the level register looks the same as at 4 kHz, a mute on the "
         "output does not"),
        ("damp-highf", 8000.0, 127, True,
         "the steal at 8 kHz, the other end of the same question"),
    ]

    for name, hz, vol, steal, why in cases:
        v, actual = audible_voice(hz)
        s.at(t, fp.perf_bulk(damp_perf(vol)))
        t += m.SETTLE_PERF
        s.at(t, fp.voice_bulk(v, part=0))
        s.at(t + 60, fp.voice_bulk(silent_voice(), part=1))
        t += m.SETTLE_VOICE + 100

        # the audible note: part 1, channel 1, the HIGHEST note number of the lot
        on = t
        s.at(on, [0x90, 120, 127])
        # the fillers: part 2, channel 2, low note numbers, struck 4 ms apart so nothing is dropped
        for k in range(FILLERS):
            s.at(on + 40 + k * 4, [0x91, 40 + (k % 60), 100])
        steal_at = on + 40 + FILLERS * 4 + HOLD_BEFORE
        if steal:
            # one more note on part 2, the LOWEST number, so the channel it takes is the highest
            # stored note, which is the audible one
            s.at(steal_at, [0x91, 24, 100])
        end = steal_at + HOLD_AFTER
        s.at(end, [0x80, 120, 0])
        for k in range(FILLERS):
            s.at(end + 20 + k * 2, [0x81, 40 + (k % 60), 0])
        if steal:
            s.at(end + 20 + FILLERS * 2, [0x81, 24, 0])
        t = end + 20 + FILLERS * 2 + 400

        out["segments"].append({
            "id": name,
            "what": why + f" (fixed carrier at {actual:.1f} Hz, part volume {vol})",
            "pins": ["DAMP_MS"],
            "measure": "envelope",
            "note": 120, "velocity": 127,
            # t_on is the steal, so the envelope the analyzer writes is centred on the fade
            "t_on": round(steal_at / 1000.0, 4),
            "t_off": round(end / 1000.0, 4),
            "t_end": round(t / 1000.0, 4),
            "steady": [round((steal_at - 1200) / 1000.0, 4), round((steal_at - 200) / 1000.0, 4)],
            "carrier_hz": round(actual, 3),
            "steal": steal,
        })

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
    import hashlib
    out["sha1"] = hashlib.sha1(path.read_bytes()).hexdigest()
    out["why"] = ("the note-on damp rate (cal::DAMP_MS): how fast the chip fades a channel the "
                  "allocator takes from a sounding note")
    out["group"] = "damp"
    out["group_number"] = 23
    return out


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    e = build(outdir / "23_damp.mid", m.marker_voice())
    print(f"23_damp.mid  {len(e['segments'])} segments  {e['duration_s'] / 60:.1f} min")
    for seg in e["segments"]:
        print(f"   {seg['id']:12s} steal {str(seg['steal']):5s} at {seg['t_on']:8.2f} s  "
              f"{seg['carrier_hz']:8.1f} Hz")
    merge_manifest(outdir, [e])
    print(f"\nmanifest updated: {outdir / 'manifest.json'}")


if __name__ == "__main__":
    main()
