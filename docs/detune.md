# Detune

What the FS1R's per-operator detune actually does, measured. It is the EPROM's own key scaled table, not a constant per step and not a constant in cents either, and the engine reads that table now.

The whole thing started from a report that the engine's render of demo song 2, "Full Tines", has a vibrato the real unit does not, so that is where this begins.

## The vibrato in "Full Tines"

## It is not an LFO

The song's own sysex carries everything: one performance and two voice bulks, then the notes. Reading them:

- Voice `FullTine 1` has LFO1 wave 4 (sine), speed 1, delay 32, **PMD 1**. PMD 1 converts to a chip depth of 2 out of 255, which at the fade's full value gives a pitch word swing of ±8 units, about three cents, at 0.13 Hz. Setting it to zero and re-rendering produces a byte-identical file.
- The part's LFO offsets are all 64, so nothing adds to it.
- No controller set targets destination 39 (LFO1 pitch mod). The four sets in the performance are Decay, SendIns-Rev, SendIns-Var and Insertion Var send, on KN1 to KN4 and the wheel. The song sends no CC1 anyway, only volume, sustain and reset-all-controllers.
- The pitch EG is flat: every level 50, every time 0.
- The insertion Chorus sweeps its delay line between 8.0 and 12.7 ms at 0.14 Hz, which is 3.5 cents peak. Switching the insertion block to Thru leaves the warble untouched.

## It is the detune

One carrier at C4 with the song's own patch, one part, no insertion effect, shows three lines: 259.35, 261.42 and 263.57 Hz. Those are the patch's operators. Three of the six carriers are detuned: op2 at +4, op3 at +7, op7 at -7, the rest at 0. At the engine's 2.0 cents per step that is ±14 cents at the ends, ±2.1 Hz at C4, so the operators beat against each other at 2.1 and 4.2 Hz. That beat is the vibrato.

The hardware's own recording of the song says the same thing from the other side. Taking the envelope of the 200 to 400 Hz band over the held chord at 9.9 to 13.2 s and looking at what rate it moves:

| | strongest modulation rates |
|---|---|
| hardware | 2.42, 1.21 Hz |
| engine, before | 3.94, 4.24 Hz |
| engine, after | 1.21, 1.51 Hz |

The 1.65 ratio between the first two rows is exactly the ratio between 2.0 cents a step and the 1.21 the hardware runs near zero.

## What the capture already measured

`07_modulation_2` plays detune -15 to +15 in steps of three at note 60. `captures/analysis/07_modulation_2.json` records the FFT peak of each, which is a bin wide; reading the frequency off the audio instead, by the phase slope of the analytic signal over the steady two seconds, gives millihertz:

| detune | hardware Hz | cents | engine was |
|---|---|---|---|
| -15 | 257.9041 | -25.621 | -30.000 |
| -12 | 259.0942 | -17.650 | -24.000 |
| -9 | 260.0098 | -11.544 | -18.000 |
| -6 | 260.6506 | -7.282 | -12.000 |
| -3 | 261.2000 | -3.637 | -6.000 |
| 0 | 261.7493 | 0.000 | 0.000 |
| +3 | 262.2986 | +3.629 | +6.000 |
| +6 | 262.7563 | +6.648 | +12.000 |
| +9 | 263.4888 | +11.467 | +18.000 |
| +12 | 264.4043 | +17.472 | +24.000 |
| +15 | 265.6860 | +25.844 | +30.000 |

Every one of those holds to the fourth decimal for two seconds and reads the same off the third harmonic. The asymmetry at ±6 and ±15 is the unit's, not the measurement's.

`cal::DETUNE_CENTS` is a 31-entry table of that curve now, the measured points exactly and straight lines between them.

## The law: the EPROM's own key scaled table

`11_detune` and the `sweep.py detune` session arrived on 2026-09-20 and settle it. Two recordings, two alignments, two estimators, and they agree on all 31 steps at note 60 to under a millihertz.

Detune is neither. Not a fixed ratio, because the cents shrink as the note rises; not a fixed frequency offset, because the hertz grow. It is the EPROM's own table, **`FRMDET[magnitude][band]`, in pitch word units of 1200/1024 cents**, the same table the CPU already applies to a formant operator's transpose word. The chip does the same thing to a ratio operator's register 0x80 that the CPU does to a formant operator's 0x230.

The band is the one `setup_ops` already computes, `(pitchNote >> 8) + 10` folded into 0..31, which advances four per octave. All seven measured notes land on it exactly:

| note | band | ±7 span, hardware | FRMDET | ±15 span, hardware | FRMDET |
|---|---|---|---|---|---|
| 24 | 1 | 30.285 | 30.469 | 86.574 | 86.719 |
| 36 | 5 | 25.432 | 25.781 | 74.480 | 75.000 |
| 48 | 9 | 21.198 | 21.094 | 63.577 | 63.281 |
| 60 | 13 | 16.353 | 16.406 | 51.466 | 51.562 |
| 72 | 17 | 13.930 | 14.063 | 39.967 | 39.844 |
| 84 | 21 | 9.689 | 9.375 | 27.855 | 28.125 |
| 96 | 25 | 4.844 | 4.688 | 16.353 | 16.406 |

Fifty-five measurements over six octaves, worst error 0.52 cents. That residual is the chip's own quantisation: every measured frequency at note 60 is an exact integer multiple of 48000/2^19, so the detune lands on the phase increment, and half an increment is 0.3 cents there. The engine does not quantise its increments, so it cannot be closer and does not need to be.

`cal::DETUNE_CENTS` is gone. It was a constant, then a measured curve, and now it is a table the engine already had in ROM and was only using for one of the two operator kinds.

## What it was worth

The curve fitted at note 60 alone, which is what the engine carried between 2026-09-20 morning and this, was right in the middle of the keyboard and wrong at both ends:

| | ±15 span, hardware | engine before | error |
|---|---|---|---|
| note 24 | 86.6 cents | 51.6 | −35.0 |
| note 60 | 51.5 cents | 51.6 | +0.1 |
| note 96 | 16.4 cents | 51.6 | +35.2 |

And before that, at a flat 2.0 cents a step, every note read 60.0.

An error in detune is heard as a beat rate, not as a pitch, so a patch that beat correctly at middle C warbled at two and a half times the rate two octaves up and at half the rate two octaves down.

## Two things the sweep rig taught

The run sends a note on per step and never a note off between them, so the unit stacks voices. It measures cleanly because the patch's own EG has finished by the time the window opens, but the take has no silence between steps inside a block, which is why `sweep_check.py` aligns on the attack train rather than on silences.

Reading the frequency off an interpolated FFT bin is not enough at the bottom of the keyboard. At note 24 a bin is a hertz wide and a cent is 0.02 Hz, and the bin reading missed FRMDET by 1.45 cents where the phase-slope reading misses by 0.18. `tone_hz_fine` is that second reading.
