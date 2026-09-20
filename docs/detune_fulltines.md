# The vibrato in "Full Tines", and where it came from

Found 2026-09-20, from a report that the engine's render of demo song 2 has a vibrato the real unit does not.

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

## What is still open

The capture stepped by three, so sixteen of the thirty-one settings have never been played. And it played one note, which cannot separate a fixed frequency offset from a fixed ratio: at note 60 they are the same number, four octaves down they are an octave apart. That second question is the one that reaches the sound, since an offset beats at the same rate wherever a patch is played and a ratio warbles faster the higher it goes.

The frequency deltas at note 60 are suggestive but settle nothing. Against a unit of 48000/2^19 Hz, the DDS step at the sample rate, they come out as the integers 6, 12, 19, 29, 42 down and 6, 11, 19, 29, 43 up, to within four parts in ten thousand. A phase increment is an integer either way, so a ratio law would land on integers too.

Two ways to close it, either one enough:

- `captures/requests/11_detune_1.mid` and `11_detune_2.mid`, 51 segments and under four minutes, every step at note 60 and the four ends at notes 36, 48, 72, 84 and 96.
- `FS1R.unlock/captures/sweep.py gate smoke detune`, the same measurement through the debug monitor, which reads voice byte 7 back off the unit before it records anything.
