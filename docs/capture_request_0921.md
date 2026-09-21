# Capture request, 2026-09-21

Five files, about fifteen minutes of playing. Three of them are re-records of files you already have, two are new. The reasoning behind each is in [`fidelity_plan.md`](fidelity_plan.md); this is the short version and the procedure.

Same rig and same procedure as the 0918 take: play one file into the FS1R, record the digital output board for the whole file, one file per recording or one continuous take with the files back to back, whichever is less work. Every file opens and closes with three 1 kHz bursts and `tools/split_capture.py` finds them, so a continuous take costs us nothing. Leave the master volume wherever it sits, since the tap is ahead of it.

| file | minutes | why |
|---|---|---|
| `09_effects_1.mid` | 2.9 | **re-record** |
| `09_effects_2.mid` | 2.6 | **re-record** |
| `09_effects_3.mid` | 2.6 | **re-record** |
| `13_unvoiced3.mid` | 3.9 | new |
| `14_sens.mid` | 3.1 | new |

All five are in `captures/requests/` and all five are in `manifest.json`.

## `09_effects_1` to `_3`: re-record, highest value per minute

Your 0918 take of these three is unreadable and it is our fault, not the recording's. The generator sends a whole 112 byte effect block per segment, and for the twenty-odd effect types no preset performance uses it was sending zeros. A noise gate at output level 0 is silent and a delay at time 0 feeds back on itself, so those segments measured the request rather than the unit. The files were rebuilt on 2026-09-20 with the factory parameter set for every one of those types, transcribed out of the Effect Parameter List, so all 84 segments now drive a real effect. The MIDI files changed; the recording did not.

Right now the comparison reports 16, 33 and 29 dB on the three files and every one of those numbers is a new MIDI file measured against an old take. With the re-record we get an impulse response of all 87 effect types off the real unit, which is the only check the effect model has ever had, and it takes a floor off every demo song number at the same time since both takes carry effects.

If only one thing gets recorded, make it these three.

## `13_unvoiced3`: the noise formant where it does not fold

55 segments, 3.9 minutes. One unvoiced operator, no voiced half, note 60 throughout.

The noise formant is the largest gap left in the engine, 7.1 to 7.4 dB of level error and 3.8 to 6.4 dB of shape error over eighty-one segments in three files. Reading `05_unvoiced` and `05b_unvoiced2` together has just settled most of its law: the band's width is linear in the bandwidth byte at about 38.6 Hz a step where the engine has it exponential over nine octaves, it is an absolute width in hertz rather than a fraction of the period (the two takes are two octaves apart and give the same width to within the scatter), its total power goes as the reciprocal of that width, the skirt *widens* it where the engine narrows it, and the resonance does nothing at all below byte 4 and is a pure tone at 5.

What none of that can reach is the top half of the bandwidth range. Every unvoiced segment ever recorded puts the band at a 1 kHz centre, and from bandwidth 40 up the band's lower edge is at DC. Both takes stop widening at about 1750 Hz and both plateau in level at the same bandwidth, which is what a band folding on itself looks like and not a law. So the clamp, the width above byte 40 and the level above it are all being read through a fold.

This file puts the centre at 8 kHz, where nothing folds until the band is 16 kHz wide:

* bandwidth 0 to 96 in steps of 4 at 8 kHz, skirt 0, resonance 0. The width law and where it stops.
* the skirt, 0 to 7, at 8 kHz, once at bandwidth 20 and once at 60. Whether the skirt scales the width or adds to it, which nothing has asked.
* the resonance, 0 to 7, at 8 kHz, bandwidth 20. Where the tone comes in.
* bandwidth at six settings at a 4 kHz centre, so "a width in hertz" is read at two centres rather than fitted at one.

It will sound like a slow sweep of band-limited hiss, quite high up. That is right.

## `14_sens`: the filter EG, and AM sensitivity against its own depth

34 segments, 3.1 minutes. Twenty-four are one sine operator under the LFO at note 60, ten are a comb through the filter at note 24.

**The filter EG.** The whole capture set has four filter EG segments and reading their envelopes turned up something bigger than a constant: the engine's four are identical to a hundredth of a decibel, so its filter EG does not move the corner at all, while your unit sweeps 14 to 15 dB at depth bytes 0 and 1. That part is ours to fix and needs no recording. What does need one is the law the chip runs once the path is reconnected, because your 0919 register retake put the EG on VOP3-1's side: nothing in the coefficient region moves while a filter EG sweeps, and the staged cutoff word matches the firmware's own conversion to the integer, so the chip runs the envelope off the shape the CPU hands it. Four segments cannot fit that. This file gives it a depth sweep at six settings and a time sweep at four, at note 24 on the same comb and the same probe performance `06_filter` uses.

**AM sensitivity.** `07_modulation_1` sweeps it at LFO amplitude depth 99 and nothing else, and one value of a variable is exactly what produced two confidently wrong answers for the formant window in a day. The engine is about 18 % too deep across the sweep, which is one number if the law is a scaling and a curve if it is not. Three depths separate the two. Twenty-four segments, four seconds each.

## Two register runs, if you have ten minutes on the rig

Neither is on the critical path and both are short.

**`python captures/sweep.py gate smoke skirt`**, for the formant form alone. The 0919 run gave `frmt` a ratio-mode patch and recorded silence, eighty decibels under the other six forms: a formant whose centre is the fundamental and whose window is 1.3 ms long puts out nothing. It uses `formbw`'s fixed 1 kHz patch now. Two minutes, and it is the last of the seven forms without a measured skirt.

**`python captures/sweep.py gate smoke filtcoef`**, through the coefficient pair write. The first attempt passed a slot number to `FUN_0000B5E2` as though it were a register and wrote the front panel latch instead, which is why the LCD flashed through the whole sweep; the firmware ships a coefficient as register 0 for the slot and register 11 for the value, and the run does that now. It would take `LADDER_K`, `CUT_COEF_FS` and `RESO_COMP` off the INFERRED list. Lower priority than it was, since `06_filter_1` now sits at 0.76 dB of shape error, which says the ladder reading is already close.

## What we are not asking for

The unvoiced law at a second fundamental, the detune table, the spectral skirt on the six non-formant forms, the key code law as far as an audio recording can take it, and the Fseq level glide are all measured and closed. The "1" forms' window, all1 and all2's geometry once the skirt opens, and the formant's own window family are all open and all three have their data recorded already; they are modelling work here, not another session on the unit.
