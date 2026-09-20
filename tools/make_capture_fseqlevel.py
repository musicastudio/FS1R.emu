#!/usr/bin/env python3
"""One extra capture file: what the chip does with a level register that jumps, which is what an Fseq does.

    python tools/make_capture_fseqlevel.py        -> captures/requests/12_fseqlevel.mid

An Fseq rewrites all sixteen level registers of a channel once per frame, and the frames are a vocoder
analysis, so adjacent frames sit 30 to 80 dB apart. The CPU writes them raw: `FUN_00012a32` reads the
frame's level byte, shifts it left one and stores it, with nothing between one frame and the next. So
whatever keeps a real unit from clicking its way through an Fseq happens inside the chip, and the demo
recording says something does: Vokodrone's intro is part 4 alone running a 35.6 Hz Fseq, and averaging
the 5 kHz-and-up envelope over its 120 frame boundaries puts the recording 1.2 dB above its own floor
at the boundary where the engine, applying the register per sample, sat 12.3 dB above its.

Latching the level at the grain the write lands in rather than mid-grain brings the engine to 1.2 dB,
the recording's number to two digits, and it costs no constant: the grain window is zero at both ends,
so a level that only moves there cannot step the waveform. The competing explanation is that the level
register itself slews, on a time constant nobody has measured; a 3 to 5 ms one fits the demo nearly as
well. The two differ in a way one recording settles outright.

**The lever is the grain rate.** A grain is one period of the fundamental, so per-grain latching gives a
transition whose width tracks the note and ignores the frame rate, and a slewed register gives one whose
width is the same milliseconds whatever the note and whatever the frame rate. Segments 1-6 hold the Fseq
and the frame rate still and move the note over five octaves. Segments 13-14 hold the note still and move
the frame rate from 16 Hz to 176 Hz.

**The second question is the operators that have no grain.** A sine operator and the unvoiced (noise
formant) operator both take the same frame level through the same register, and neither has a window to
hide a step in. If the chip latches per grain they should step where the formant does not; if the
register slews, none of the three should. Segments 7-9 are the sine and 10-12 the noise.

Preset Fseq 34 "RndArp4", 128 frames, is chosen off the ROM: it has the largest per-frame level swings
of any preset on both the voiced and the unvoiced side, 250 register steps at the extreme, and twenty-six
frames where track 1 alone moves more than 15 dB.

Record it exactly like the rest of the set: play the file in, record the whole thing, one WAV with the
same name next to the others. The analysis is frame-synchronous and works off the WAV directly, so no
new measure kind is involved.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m

FSEQ = 33           # performance common 0x17, zero based: preset Fseq 34 "RndArp4"
END = 127           # its last frame, and the loop end the performance has to carry itself
HOLD = 4000         # two passes at 100 %, half a pass at 20 %
NOTES = (36, 48, 60, 72, 84, 96)


def perf(ratio=1000):
    """Part 1 with the Fseq assigned to it, at `ratio` tenths of a percent of its own speed.

    The loop points are the performance's, and `init_performance` leaves them at zero. A zero length
    loop is not a missing setting the player fills in: FUN_0001A894 wraps to the loop start and clears
    the run flags where start and end are the same step, so the sequence advances once and holds frame
    0 for the whole note. rgwan's first take of this file is that, four seconds of frame 0 per segment,
    digital silence wherever the voiced track is attenuated there and steady noise from the unvoiced.
    """
    p = fp.init_performance("Fseq Level")
    p[0x15] = 1                              # Fseq part = part 1
    p[0x16] = 1                              # preset bank
    p[0x17] = FSEQ
    p[0x18], p[0x19] = ratio >> 7, ratio & 0x7F
    p[0x1C], p[0x1D] = 0, 0                  # loop start: the first frame
    p[0x1E], p[0x1F] = END >> 7, END & 0x7F  # loop end: the last, so a held note plays the whole thing
    p[0x20] = 0                              # one way, so it wraps rather than turning round
    return p


def voiced(form=fp.FRMT):
    """Operator 1 alone, taking track 1 of the frame for both its level and its formant frequency."""
    v = fp.init_voice("FseqLevel ")
    fp.set_op(v, 0, keysync=1, level=99, form=form, bw=40, fseqtrk=0)
    v[0x29] = 1                              # Fseq voiced switch, operator 1
    return v


def unvoiced():
    v = fp.init_voice("FseqNoise ")
    fp.set_op(v, 0, keysync=1, level=0, u_level=99, u_bw=40, u_skirt=2, fseqtrk=0)
    v[0x2B] = 1                              # Fseq unvoiced switch, operator 1
    return v


def g_fseqlevel():
    base = perf()
    for n in NOTES:
        yield m.Seg(f"frmt-{n}", f"formant operator on Fseq 34, note {n}: the grain rate is the note",
                    ["Fseq level: per grain or a slew"], voiced(), note=n, hold=HOLD, perf=base,
                    measure="envelope")
    for n in (36, 60, 84):
        yield m.Seg(f"sine-{n}", f"sine operator on the same frame level, note {n}: no grain to hide a step in",
                    ["Fseq level: per grain or a slew"], voiced(fp.SINE), note=n, hold=HOLD, perf=base,
                    measure="envelope")
    for n in (36, 60, 84):
        yield m.Seg(f"uv-{n}", f"unvoiced operator on the frame's noise level, note {n}",
                    ["Fseq level: per grain or a slew"], unvoiced(), note=n, hold=HOLD, perf=base,
                    measure="envelope")
    for ratio, hz in ((200, 15.8), (5000, 175.6)):
        yield m.Seg(f"frmt-rate{ratio}", f"formant operator, note 60, Fseq at {hz:.0f} frames a second",
                    ["Fseq level: per grain or a slew"], voiced(), note=60, hold=HOLD, perf=perf(ratio),
                    measure="envelope")


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    segs = list(g_fseqlevel())
    entry = m.build_file(outdir / "12_fseqlevel.mid", segs, m.marker_voice())
    print(f"12_fseqlevel.mid  {len(segs)} segments  {entry['duration_s'] / 60:.1f} min")
    print("record it like the rest of the set: one WAV, same name, in the same folder.")


if __name__ == "__main__":
    main()
