# Findings

The research log. Newest first. One entry per thing learned, dated, with the evidence that settled it.

This file is history. It records what was found and how. It does not track open work, which lives on the board, and it does not hold the current state of what is known, which lives in [STATUS.md](../STATUS.md). When an entry here settles a constant, the constant itself belongs in `src/fs1r/chips/cal.h` with the entry named beside it.

Entries that have a dedicated working document (`aeg.md`, `skirt.md`, `noise.md`, `detune.md`, `formant.md`, `filter.md`) are summarized here and linked, not duplicated.

---

## 2026-09-23, a part whose voice bank is off receives nothing

rgwan: A011 Sho plays parts 1 and 2 on the unit and all four in FSVR, with parts 3 and 4 sounding
their InitEP voices. The performance's parts 3 and 4 have Voice Bank = off (part byte 1 = 0) and
part 4 a receive channel of 16 (byte 4 = 0x10, "pfm"). The engine took the receive channel as the
whole of whether a part listens, and loaded whatever voice was in the part's buffer. The owner's
manual, page 63: "When Voice Bank = off no MIDI data will be received for the corresponding part."
`Part::rcv()` now reads as off when the bank is off, which is the one place `part_listens` and
`partActive` both go through; the panel shows "off" for the voice. Seven of the 384 ROM performances
have a part like this: Sho, Replicant, SuperPad, Strat 7II, Ice Score, Angel Bells, Ensemble.

## 2026-09-23, the filter EG's rate law off the CPU's own stage word: exponential, doubling every 15.5 rate words

`fs1r_capture_session3.py flteg` rerun after the gate fix, twelve notes, the CPU's stage word at
0x0106ADEC sampled every 26 ms. The attack segment, L4 = 0 to L1 = 100 (a swing of 512 level units),
at times 10 to 80:

| time | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 |
|---|---|---|---|---|---|---|---|---|
| rate word | 119 | 134 | 149 | 164 | 179 | 194 | 209 | 224 |
| attack, s | 0.052 | 0.078 | 0.129 | 0.233 | 0.418 | 0.806 | 1.565 | 2.994 |

`t = 1.33e-4 * 2^(word / 15.5) + 0.024 s` to 1.2% rms, the 24 ms being the sampler's own offset. Time
0 (word 11) and 99 (word 253) fall outside the sampler: the first ends inside the first sample and the
second had not ended at six seconds, 11 s by the law. With the asymptote half a swing past the end the
segment covers two thirds of the way to it, ln 3 time constants, so the chip's per-tick approach is
`42.9 * 2^(-word / 15.5)` at the 192.3 Hz tick (`cal::FEG_RATE_K`, was a 0.67 guess on a `/ 32` law).

**Exponential, not a ramp.** Time 40 at L1 = 25 (a swing of 128) took 0.232 s against 0.233 at L1 =
100 (a swing of 512): the time does not depend on the swing, which is what an exponential aimed
past a target proportional to the swing gives and a linear ramp cannot.

**A flat segment is a fixed 0.78 s.** Stage 2 in every note is L1 to L2 = L1, no swing, and it held
0.71 to 0.86 s at every rate word from 11 to 224, so the CPU's +2/+3 words are not what the chip
times; it takes 0.78 s over a zero swing regardless (`cal::FEG_FLAT_S`). And time 40 at L1 = 50, an
end word of exactly 0, skipped its attack entirely: the level was already at the word.

**LFO2 never touches the staged cutoff word.** The `lfo2` run sampled the part's slot at 0x01068F78
under LFO1 filter depth 99, under LFO2 depth 99, and under neither, 115 samples each: no slot moved
in any of the three. LFO1's filter term is the CPU's and goes out by a different path (the `+0x28`
offset word FUN_0002E6CC refreshes, not the staged coefficient), and LFO2 is the chip's own, which
`20_fltmod` measured the rate of. The engine had both right in structure.

`06_filter_2` envelopes 2.38 to **2.52**, `14_sens` 3.21 to **3.14**; what is left in both is the
patch's amplitude onset (the unit's first 100 ms sit 10 to 30 dB above the engine's on every filter
EG segment, EG depth or not), not the filter EG, which now opens at the same millisecond. No GUESS
constants remain in `cal.h`.

## 2026-09-23, session 3's take: the filter EG depth and LFO2's rate measured, three files already inside 0.1 dB

rgwan ran `fs1r_capture_session3.py` and recorded the six outstanding request files as one take
(`FS1R.unlock/captures/2026-09-23/16_vel_to_20_flt.flac`). `tools/split_take.py` cuts a take like that
into one WAV per file by each file's own marker bursts; all six landed with their spans to the hop.

**Three of the six needed nothing.** `16_velocity_1/2` (63 segments, the velocity law at seven amplitude
sensitivities) sit at 0.06 dB median level error, so `vel_att` as read from the firmware is the chip's.
`17_egdecay` (a decay to a level that is not silence) 0.09 dB level, 0.43 dB envelope. `18_fmchain`
(the second and third links of a modulator chain) 0.05 dB level, 0.15 dB shape: every link carries the
same index. The drum's tonal half was not any of those.

**The filter EG depth scale is 110 cutoff bytes** at full depth and full level (`cal::FEG_DEPTH_BYTES`,
was a 96 guess). Six held-level segments at cutoff 96: the corner reads 13.1, 25.5 and 52 bytes down
for depth words -8, -16 and -32 at level 250, and 52 down for level -128 at depth 63, so the shift is
linear in both and 1.15x what 96 gave. Per octave band the closed corners now sit within 2 dB of the
unit's down to its own -72 dB leakage floor. `06_filter_2` envelopes 2.47 to **2.38**.

**LFO2 is a 16-bit phase stepped at 3000 Hz.** Six speeds; the modulation fundamental off the envelope
spectrum is 0.95, 1.91, 2.70, 4.76, 10.64 Hz for `LFO2SPD` words 20, 40, 60, 104, 232, which is
`word * 3000 / 65536` within 4% at every one, 3000 being 48 kHz / 16, one step per 16 frames.
`cal::LFO2_INC_K` carries it; the engine's guess had been 192.3 Hz ticks, six times slow. Speed 127
(word 556) reads 43.2 Hz where the law gives 25.4, and is left unexplained: it is one point, past the
table's last measured step, and a sweep over 100 to 127 would say whether the word or the chip bends
there. `20_fltmod`'s LFO envelopes 19.5 to 9.2 dB; what is left in them is the waveform's shape against
the analyzer's alignment, not the rate.

**The drum, `19_drums`.** Note 60 hits match to 1 dB at every velocity and the noise-off / voiced-off
halves separately. Note 41 is 2.6 to 3.0 dB quiet in the engine at every velocity, and the envelope
says why: its voiced half decays about 10 dB further than the unit's by 300 ms while note 60's tracks
the unit to the decibel. Note 41 is key code 86, between the measured 85 and 89 of the key-code table
in `aeg.md`, so the interpolation there is the suspect, and `10_envelope2`'s `tkey` grid at one note
per octave never played it. Open.

## 2026-09-23, AM sensitivity: the chip's AM word is seven bits, and the sensitivity weights are eighths

Off `14_sens` as recorded, no new take. The CPU's AM word is `EGBIAS[amd * (127 - lfo) >> 8]`, 0 to 255,
and the engine applied it as `regAM * LEVEL_DB * ams / 7`, which is why it matched the unit at LFO
depths 33 and 66 (swings of 18 and 49 steps) and ran 20% deep at 99 (a swing of 224). Per-cycle
peak-to-trough on both sides through the same 2 ms estimator, in dB, eight sensitivities:

| depth | | ams 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|---|
| 33 | unit | 0.4 | 1.2 | 2.0 | 3.5 | 4.2 | 5.1 | 5.9 | 6.7 |
| 66 | unit | 0.4 | 2.6 | 4.6 | 9.1 | 11.3 | 13.5 | 15.8 | 18.1 |
| 99 | unit | 0.4 | 7.8 | 13.8 | 23.6 | 29.6 | 35.5 | 40.9 | 46.1 |
| 99 | engine, `ams / 7` | 0.4 | 6.9 | 13.7 | 20.6 | 27.5 | 34.3 | 41.2 | 48.0 |
| 99 | engine, now | 0.4 | 6.0 | 11.9 | 23.8 | 29.7 | 35.6 | 41.6 | 47.6 |

Two things fall out. The 66 row divided by its 49-step swing is 1, 2, 4, 5, 6, 7, 8 eighths to the
step, not sevenths: the sensitivity is a 3-bit shift-and-add, {0, 1, 2, 4, 5, 6, 7, 8} / 8, and the
jump from 2 to 3 (4.6 to 9.1 dB) is the unit's, not noise. And the 99 row tops out at 46 dB where
224 steps would be 85: the chip's AM word is seven bits, 127 steps, 47.8 dB, and the CPU's 8-bit
table runs off the end of it. `am_att()` in `internal.h`; the linear `ams / 7` is gone. What is left
is 1.8 dB at sensitivities 1 and 2 at depth 99, where the unit modulates a little deeper than
`127 / 8` and `127 / 4` allow, inside the estimator's smear on a trough that steep. `07_modulation_1`
envelopes 2.27 to **2.21**, `14_sens` 3.28 to **3.24** (its number is the filter EG's).

**Three small ones off the same pass.** The feedback gain: `03_fm_2`'s six clean feedback levels put
the second harmonic a steady 0.5 dB under the unit, so `cal::FEEDBACK` is 0.53, and levels 1 to 6 now
land to 0.1 dB. The Fseq MIDI-clock rate table was marked inferred and is the firmware's:
`FUN_0001ACB6` scales the clocks since the last frame by 1/4, 1/2, 1, 2, 4 for speed words 0 to 4.
And the analyzer's silent-segment fault: a float render's residue after an EG has run to its floor sits
at -105 to -115 dB through the output stage, above the -120 gate, so `hold-50`/`hold-70` in
`10_envelope2` and `hold-60`/`hold-80` in `02_envelope_3` scored 30 to 38 dB of disagreement on
silence. `SILENT_DB` is -100, 70 dB under anything that plays; those four rows are gone.

## 2026-09-23, the firmware audit: the filter EG is the CPU's, LFO2 and the Fseq delay were not what the engine had

A pass over the engine against the flash with Ghidra's decompiler, readonly-folding the EPROM so the
tables resolve, and with `FUN_00203000` given its real signature (the signed divide, `r1 / r0`, which the
cached decompilations had lost so every `/ 889` and `/ 507` in the filter path read as a call). Nothing
here is from a recording; each item names the function that settles it. The engine edits are in
`vop3_filter.h`, `notes.cpp`, `fseq.cpp` and `cal.h`; the scores that moved are
`06_filter_2` envelopes 2.68 to **2.47**, `14_sens` 3.34 to **3.28**, `08_panlevel_1` shape 1.71 to
**1.60**, nothing else changed.

**The filter EG is stepped by the CPU, one segment at a time.** `FUN_0000D050` (note on) writes the
segment to VOP3-1 through the register primitive `FUN_00039648(idx, word)` = `*(u16*)(0x800000 + 2 * idx)`:
register 0x2B is the rate, `0x374F4E[time]` (`FEGRATE`, 100 words, 0x0FFF down to 0x0016), 0x2C is the
end level `(L - 50) * 256 / 50`, 0x2D is the asymptote, the end level plus **half the swing again**, so
the chip aims past the target and the CPU cuts the segment when the chip's status word at 0x800242
reports the crossing; 0x21 starts it. `FUN_0000CB6C` runs on that interrupt, advances the channel's stage
word at 0x0106ADEC (1 attack, 2, 3, 4 hold, 5 release from `FUN_0000D7B0`, whose release aims at L4 with
the same law) and loads the next segment. When the swing is zero the asymptote is the target plus 3 (plus
2 on the end word), so a flat segment still ends. The engine's `StepEG` had four fixed time constants
off the amplitude EG's rate table and a `FEG_STEP_K` fraction; it now has the firmware's structure, and
the one thing left inferred is the chip's reading of the rate word (`cal::FEG_RATE_K`, a two-point
guess, marked). FS1R.unlock's `capture3.py flteg` reads that off the stage word directly, and whether
the chip is exponential or a ramp with it.

**Everything else on the filter path was a different formula.** All in `FUN_0000D050` / `FUN_0000E170`:

* cutoff key scaling is `(ks - 64) * (note - breakpoint) * 16 / 507`, the engine had `/ 64`;
* resonance velocity is `sens * (vel - 127) * 116 / 889` for a positive sensitivity and `vel * 116 / 889`
  scaled by the sensitivity for a negative one, the engine had `(sens * (vel - 64)) >> 4`;
* the EG depth velocity is multiplicative, `depth * sens * (vel - 127) / 889` added to the depth, the
  engine added a velocity term to the level;
* the EG depth word is `0x120 * depth` (register 0x28); what the chip does with it against the level is
  inside VOP3-1 (`cal::FEG_DEPTH_BYTES`, a guess, see `20_fltmod`).

**LFO2 is not the CPU's.** `FUN_0000D050` and `FUN_0000E2A0` hand VOP3-1 a waveform (`FUN_0000C1AC`, the
0x20-per-step field `ymp706_registers.md` had as the filter type) and a speed word from `0x374A04`
(`FUN_0000C130`, `LFO2SPD`, 128 words), and nothing on the CPU side adds an LFO2 term to the cutoff
coefficient. The engine had run LFO2 as a copy of LFO1 with the same speed table; it now uses
`LFO2SPD` and the chip's rate per word is `cal::LFO2_INC_K`, a guess. The filter type reaches the chip
another way: `FUN_0000C280` / `FUN_0000C604` patch the per-channel microcode jump. `capture3.py lfo2`
confirms the cutoff word holds still under LFO2, and `20_fltmod` records the rate.

**The voice's Formant and FM control** (`FUN_00017454`, handlers at 0x3DE04): the amount is
`clamp(bias(src) * bias(dep) * 2 >> 7)` like every other controller, and the destinations store
`-2 * amt` as a level offset, `amt << 5` as a frequency word offset, and `amt` into the per-op bandwidth
offset that `FUN_0001F6BC` scales by the op's own `BWBIAS` as destination 37 does. The engine had a quarter,
an eighth and a quarter of those, and `cal::VCTRL_FREQ` for a scale that is a shift in the flash.
Removed.

**The Fseq start delay** is `0x35BD3E[byte]` (`FSEQDLY`, 0 to 346) frames of a fixed 7000-count CMT1
period, 8 ms, counted down by `FUN_0001A47A`, and the real frame rate starts when it runs out: 2.77 s
at 99. `cal::FSEQ_DELAY_S = 1.0` was wrong in unit and value. Removed.

**The pan LFO depth** goes through `eb86()` (`FUN_00019362` case 0x29 stores it at image +0x1F, 1 at
byte 0); the engine used the raw byte, 22% shallow at the top.

**The reverb's dimensions are not a tap pattern.** `FUN_0039B09C` loads the block: coefficient rows at
`0x363600 + 0x364F10[type] * 0x128`, 0x5E coefficient words to the slots at `0x35E02C` and 0x36 delay
words, in samples times `0x7AE1 / 65536`, to the slots at `0x35E0E8`, and `0x36FEB4` is the eight-row
width/height/depth table the room types index. The engine's early reflections are an invented six-tap
spread (`run_reverb`, marked INFERRED). Not changed here: it is a rebuild of the block from the
microcode, `vop3_2_microcode.md` territory, and the 09_effects files are the ones to score it with.

**Still inferred, and what settles each**, in `FS1R.unlock/captures/capture3.py` (reads only) and
`captures/requests/20_fltmod.mid` (one recording, 2.3 min):

| constant | what | how |
|---|---|---|
| `FEG_RATE_K` | chip time per filter EG rate word, and exponential vs ramp | `fs1r_capture_session3 flteg`: the stage word through 14 notes |
| `FEG_DEPTH_BYTES` | cutoff bytes per EG level per depth word | `20_fltmod` held-level segments, cutoff parked at 96 |
| `LFO2_INC_K` | LFO2 phase per tick per speed word | `20_fltmod` lfo2 segments; `capture3 lfo2` proves it is the chip's |

## 2026-09-22, the noise band is two unequal poles, the drum is not the noise, and the effects-off demo

rgwan recorded the fifteen demo songs with the effects and the filter stripped (`captures/demo_nofilterfx`), plus Vokodrone's bass and drum parts on their own. Against the whole take the engine sits 2.8 dB under the unit in the median band, flat across the octaves, with four songs at zero and Ana-Unison at -8; the top octave, +3.3 dB bright on Vokodrone, is the one band the day's work moved, to -0.6.

**The snare is the two sine carriers, not the noise.** `tools/compare_isolated.py` lines the drum take up against a render of channel 3 alone and scores each note by band. The drum part reads 1.5 to 3.3 dB hot on every note and the snare 4 dB hot in the 100 to 200 Hz band, which is operators 6 and 8, sines at 143 Hz under a three-deep modulator chain and a feedback operator. Muting every unvoiced operator costs the engine's snare half a decibel. Time-frequency slices say where: for the first 6 ms the unit's carrier line is 12 dB down where the engine's is already clear, so the unit's modulation is deeper early and decays through the first 10 ms, and the engine's modulator, whose EG drops 99 to 80 at rate 51, is already down. Nothing in the capture set measures a decay to a level that is not silence, the velocity law at any sensitivity but 0, or a modulator on a modulator, and `tools/make_capture_tonal.py` writes the four files that do, the fourth being the drum voice itself one hit at a time. FS1R.unlock `fs1r_capture_session3.py record` plays them.

**The noise band is two digital one-poles with different coefficients**, and the "pedestal" of 2026-09-21 is the second pole's own floor: a digital one-pole passes `(a / (2 - a))^2` at Nyquist, which is white, sits at the same level at any centre and rises with the register twelve decibels an octave. Fitting both poles free reproduces every one of forty-one segments to 0.6 to 0.9 dB rms over 200 Hz to 23 kHz, and the 1 kHz take gives the same coefficients as the 8 kHz one at every register they share. The peak falls one `LEVEL_DB` per register from 25 up. The skirt is two controls rather than a register shift. The unvoiced output is also averaged two samples at a time, which the fixed sines of `01_reference` do not show and every wideband noise segment does. Band shape over 100 Hz to 22 kHz: `13_unvoiced3` 2.23 to **0.61**, `05_unvoiced_1` 2.65 to **1.19**, `15_filter`'s source 2.16 to **0.52**. `docs/noise.md`, `tools/fit_noise_band.py`.

**The bass part** is 2.2 dB quiet at every velocity and its formants sit in the wrong octaves, -5 to -8 dB at 160 to 320 Hz and +5 to +10 at 640 to 2560 Hz on notes 40 to 57, where the window is shorter than the period. That is the formant window family, item 8 of `docs/fidelity_plan.md`, seen at a fundamental the capture set never played.

**Two scoring faults, still open.** `tools/analyze_capture.py`'s shape number on the noise files is carried by its sixth-octave bands below 100 Hz, one or two FFT bins each at the recording's floor, which is why it moved 3.3 to 3.1 where the same files moved 2.65 to 1.19 on bands within 40 dB of the peak. And `tools/regress.py` needs `bin/fs1r_emu.exe`, so it cannot run outside Windows.

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
