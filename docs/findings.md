# Findings

The research log. Newest first. One entry per thing learned, dated, with the evidence that settled it.

This file is history. It records what was found and how. It does not track open work, which lives on the board, and it does not hold the current state of what is known, which lives in [STATUS.md](../STATUS.md). When an entry here settles a constant, the constant itself belongs in `src/fs1r/chips/cal.h` with the entry named beside it.

Entries that have a dedicated working document (`aeg.md`, `skirt.md`, `noise.md`, `detune.md`, `formant.md`, `filter.md`) are summarized here and linked, not duplicated.

---

## 2026-09-21, the filter and the noise formant both come off the inferred list

`15_filter` is the first recording with a source that can see a filter. `06_filter` never could: its source is a single 32.7 Hz partial, so its eleven readings had nothing above the corner to attenuate. The corner is `17.4 * 2^(byte / 12)`, 0.098 octaves rms over thirteen points, where the one-pole reading of the coefficient had put byte 64 at 3751 Hz against the unit's 713. The resonance runs to a 19.9 dB peak where the engine topped out at 8.5, and the feedback goes as the cube of resonance table A. `RESO_COMP` is measured at zero rather than guessed. `docs/filter.md`.

`13_unvoiced3` settles the noise formant chain: the band sits at an 8 kHz centre where it does not fold at DC, and the corner, its clamp, the skirt, the level and the resonance are all measured. `docs/noise.md`.

## 2026-09-20, an Fseq frame does not cut the grain it lands on

rgwan reported clicks before 00:06 in Vokodrone that his unit does not make. The intro from 2.0 to 5.5 s is part 4 alone, eight formant operators under a preset Fseq. A grain carries the level it was fired with, so a frame that lands mid-grain takes effect on the next one rather than chopping the one in flight.

What the demo cannot separate is whether the chip latches per grain or slews the level register on a 3 to 5 ms time constant, which fits the intro nearly as well. The lever is the grain rate, one period of the fundamental, which is what `12_fseqlevel` was built to drive.

The first take of that file came back silent, which found an invented fallback: eleven of fourteen segments recorded at the digital floor and three as steady noise, because the performance left the Fseq unassigned and the engine had been substituting something the hardware does not.

Take 2 came back on 2026-09-21 and the grain latch was too slow. The same take's three sine segments also caught a separate fault: a ratio operator handed an Fseq frequency runs off the end of the register, one line at 23982 Hz at notes 36, 60 and 84 alike, which is pitch word 32767 to a tenth.

## 2026-09-19, the spectral skirt multiplies the window exponent

The grain window is `sin^p`, and the skirt multiplies p rather than stepping it: `p = 2 * 2^skirt` on all/odd/res and `2 * sqrt(2)^skirt` on the formant, where the engine had `2 * (skirt + 1)` for everything.

The proof needs no fit. `res2` puts its grain in exactly one period under a carrier on partial 9, high enough that no line folds back, so the lines it produces are the window's own Fourier coefficients, and a `sin^(2n)` window's coefficients are the binomial row. The unit gives 1, 2, 1 at skirt 0, then 1, 4, 6, 4, 1 at skirt 1, then the sin^8 and sin^16 rows, 0.00 to 0.35 dB rms against the exact coefficients over skirts 0 to 5. The engine lands inside 0.9 dB over all eight settings of both.

This also closes what separates all1 from all2, odd1 from odd2 and res1 from res2, which had stood as "identical at every setting measured". They are identical at skirt 0 and four to five times apart by skirt 7, and every segment of the capture set sat at skirt 0. `docs/skirt.md`.

## 2026-09-19, the amplitude EG's shape, and four seconds of Full Tines

The 0918 pass measured the rate table and stopped there, so everything the rates are used for was still a DX7 guess. Three were wrong.

Full Tines lost four seconds of its tail. The song holds the sustain pedal down and the hardware rings for four seconds after the last note off where ours rang for one. It was never the pedal and never the rate curve: the engine's key rate scaling was wrong by 11 to 16 chip steps at every note, always too fast, which is 7 to 16 times the decay time. With the law measured off `02_envelope_3`, the engine's last audible frame moves from 14.02 s to 17.57 s against the unit's 18.28, and the song's envelope correlation from 0.838 to 0.976, from the worst of the fifteen to above the median. 0.7 s of tail is left.

The key code law is a saturating table rather than a line: ten key codes four apart bound it to one value each, and it stops at plus or minus 13 at both ends. `docs/aeg.md`.

## 2026-09-19, the modulation index, and the alignment fault that got it wrong twice

One cycle of phase deviation at full modulator level is the DX7-lineage guess. Three is what the fifteen songs ask for. Mean absolute band error: 8.3 dB at one cycle, 6.9 at two, 6.3 at three, 6.4 at four. Raising the feedback alone to the same depth recovers only a third of it, so it is the operator-to-operator index and not the feedback path.

The sideband sweep that was meant to settle it gave 4.0, and that was wrong too. The analyzer had been lining each sweep up on where its tone begins, and `sweep.py` brings its note up 1.55 s before the first step writes anything, so every spectrum was scored against its neighbour's register value. A ladder is shift invariant in slope, so `LEVEL_DB` came out right and nothing looked wrong. Anchored properly the answer is **3.369**, and a second take agrees. `03_fm` settles it independently and was never being read: its 25 spectra sit 4.46 dB out in band shape at 4.0 and 0.25 dB at 3.369.

This is the lesson behind the rule that a number fitted against one recording is not measured. Three conclusions in two days were written up with a residual quoted and then turned out to be wrong, all failing the same way.

## 2026-09-19, the filter is a ladder, and the demo cannot tell

`FUN_0000C3D0` sends two resonance coefficients per channel, not one. With `r = 2^(-raw/16)`, table 0x374B24 is `A = 1 - r` and 0x374C24 is `B = max(0, 0.5 - 2r^2)`, quadratic in the same damping and zeroed for HPF and BEF, and `FUN_0000CA44` gives only LPF18 and LPF12 a tap mix and a doubled input. One shared cutoff with a feedback term and an input-side term is a ladder, so the three lowpasses became taps off one 4-pole cascade instead of three separate topologies, and the invented "second pole pair runs flat at 0.7" is gone.

Against the recording it is a wash, 5.55 dB where the old model scored 5.50. Two settings inside it did get settled: spending table B on the passband reads as a flat +4 dB across every band, so `RESO_COMP` is 0, and the textbook self-oscillation point `LADDER_K` 4 oversharpens the peak. The structure is kept because it is what the firmware's coefficients describe, not because the demo prefers it.

## 2026-09-19, two scoring faults that were hiding the real numbers

`tools/analyze_capture.py --compare` scores spectrum shape now, the octave bands with each segment's own level divided out. That is what caught the modulation index, and nothing had been reading it. It put numbers on three open items at once: `04_formant` at 6.3, 7.5 and 17.2 dB, `05_unvoiced` at 3.8 and 6.6, and `06_filter` at 14.7 dB on both files, where those same two files matched to 0.00 dB in level.

`captures/demo/render` was stale and `tools/demo_scores.txt` with it. Fourteen of the fifteen files predated the engine and came out 3.9 to 14.4 dB quieter when re-rendered, so the 5.50 dB line described a build that no longer existed. Re-rendered and the log restarted from a measured before and after.

## 2026-09-18, the first capture set, and two tables read out of the EPROM

rgwan recorded the whole capture set, 21 files in one 46.7 minute take off the digital output board, and ran the debug monitor over it reading the voice image back for all 492 segments.

**The frequency EG depth was over twice too strong at mid settings.** The sysex byte reaches register 0x60/0x68 through a table at EPROM 0x35B2E0, now extracted as `FEGLVL`, and it is far from linear.

**The pan table index was one step left of the hardware's.** The voice image carries twice the part's pan byte at +0x2E and `FUN_00025BC8` indexes at `pan >> 1`, so the index is the part byte itself.

Measured and now in the engine: the output path's fixed gain, `cal::OUT_GAIN` 0.14992, agreed to 0.005 dB by nine segments, and the channel accumulator's hard clip, recovered identically from four and eight stacked carriers.

Confirmed as they stood: the EG rate model to 0.6% over rates 16 to 33 across 38 measured rates, `EG_LEVEL_DB` 1.5 to 1.5039 over forty steps, `CARRIER_DB` 1.5 straight over sixteen steps.

## 2026-09-18, the extracted demo MIDIs ran 4 % fast

Most of what was holding the envelope correlations down. The demo player's timer ISR sets its next compare from the running count rather than from a fixed period (flash 0x0002F28C), so every period carries the interrupt's own entry cost. The recording measures that at 4 %: song to song the distance in rgwan's take is 1.040 times the nominal length, on all fifteen, with no reload gap left over. `tools/extract_demo.py` now writes a 520000 us quarter, 10.4 ms per delay unit.

## 2026-09-16, five CPU-side spots that were read but not certain

All five were marked "assumed" because the firmware had been read but not far enough. Tracing them settled each one.

**The controller matrix was wrong in five ways at once.** The source bit order (PB, CAT and PAT sit at bits 6 to 8, with FC, BC, MC3, MW and MC4 after them, not at the end), the source values (the knobs and MIDI controls are bipolar `(v - 64) * 2`, not raw), and three more.

**Frequency bias** (destination 36) was routed but never applied: `(int16)(scaled * sense * 0x10) >> 2` on the operator's frequency word.

**The part EG offsets** reach T1, T2, **T3** and T4, not T1, T2 and T4. `FUN_00019414` walks the part's bytes 0x1A, 0x1B, 0x1B, 0x1C. The hold rate takes its +4 only when it is below 0x3F.

**The Fseq pitch offset** is exact, constant for constant, against `FUN_000127FC`. The formant tracking term that used to sit alongside it was an invention and is gone: nothing in the firmware shifts a frame's formant frequencies, which is the point of an Fseq.

**MIDI clock** weights each tick by 25, 50, 100, 200 or 400 for speed words 0 to 4 (`FUN_0001ACB6`), so the 1/4 to 4/1 ratios were right, and the free-running `VELW[adjust] * 84 * 1000 / ratio + 2884` is confirmed instruction for instruction.

## 2026-09-16, four engine faults found by driving Vokodrone against the unit

Written as the v0.3.2 release notes, which were never published in full. The shipped release body carries only the title and the SHA-256 lines, so this is the only copy of the working.

The FS1R plays fifteen demo songs out of its own EPROM, and `tools/extract_demo.py` pulls that byte stream out as standard MIDI files, so the hardware and the engine are driven by one sequence and can be lined up sample for sample. Song 1, "Vokodrone", is the demo's hardest case on purpose: four parts, a formant sequence on part 4, and a foot controller and a mod wheel driving six controller sets for fifty seconds, 1123 and 901 messages of them. It was the worst of the fifteen against the recording, bright by 8 to 15 dB above 320 Hz and out by 19 dB over its closing phrase. Isolating its parts put the fault in the engine rather than in a calibration constant, and the firmware names each one.

**An Fseq that had played out blasted its operators at full level.** The operators a formant sequence drives had their own level forced to zero attenuation on the assumption that the frame would overwrite it, and when the sequence stopped advancing nothing did. The frame writer is gated on the mode byte and the part, not on the sequence still running, so the registers hold the last frame; only the start delay gates it, by clearing the track masks while the delay counts down. Any patch whose Fseq reaches its end and stops was jumping to full volume on its sequenced operators.

**The Fseq retrigger test looked at every part.** "First note" means the Fseq part's own held-note count, which the firmware keeps per part and tests for 1. The engine scanned all 32 channels instead, so any other part holding a note stopped the sequence restarting and it ran on out of step with the keys.

**The frequency bias controller detuned operators the hardware leaves alone, and then froze.** The firmware adds the frequency bias word only to formant and fixed operators; the ratio branch adds nothing, and the unvoiced one is zeroed outside normal mode. The engine added it everywhere, so a mod wheel swept Vokodrone's bass operators by up to an octave and a half that the hardware never moves. Worse, the whole controller matrix was read once at note-on, so the frequency bias, both bandwidth biases and the amplitude EG bias were frozen for the life of a note, while the firmware re-runs the destination handler whenever one of its sources moves and rebuilds the per-part arrays. Vokodrone holds a thirty-second chord under a moving foot controller. All of that now runs on the 192 Hz tick, and the Fseq frame's own frequency words carry the bias as the firmware adds it.

**Controller sets do not compose.** Every "direct" destination handler stores a per-part byte rather than adding to one, and the eight sets are walked in order, so two sets pointed at one destination overwrite each other rather than summing, exactly as for the destinations that edit a part byte.

**What it measured.** Across all fifteen demo songs the mean absolute per-octave-band error against the unit went from 6.28 to 5.50 dB and the median envelope correlation from 0.956 to 0.965. In the median over the songs, every band from 640 Hz up sat within a decibel of the hardware. Vokodrone itself went from +8 to +15 dB bright to inside 5 dB in every band, from 0.956 to 0.978 correlation, with its per-second level tracking the unit to 1.5 dB across the song. What was left concentrated in four songs and in the top octave, where the engine is still dark because it models neither the phase truncation nor the level quantization of a fixed-point chip.

**What it did not settle.** Calibration against a measurement rig. The demo recording is real hardware and moved the modulation index, the resonance curve, the filter's cutoff reading and the four faults above, but it never holds a note still, so the chip-side models (EG shape, formant window, filter, effect algorithms) stayed inferred. The capture kit added in v0.3.0 exists to settle them.

The v0.3.2 downloads and their SHA-256 lines are on the [release itself](https://github.com/musicastudio/FSVR/releases/tag/v0.3.2). The release predates the FSVR rename, so its artifacts carry the `FS1R.emu` name.
