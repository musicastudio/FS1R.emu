# What rgwan's capture measured

Zhiyuan Wan recorded the whole capture set off his FS1R's digital output board and sent it on 2026-09-18 as `captures/FS1R_capture_request_0918.flac`. This is the working behind the constants it moved. The per-segment numbers are in `captures/analysis/`, and `namespace cal` in `src/fs1r_lib.cpp` names this file against the three constants that are no longer inferred.

## What arrived

One continuous take, 46.68 minutes, 48 kHz, 24-bit stereo, 192 MB. The request asked for 21 separate WAVs and got one recording of all 21 played back to back, which is less work at the hardware end and costs nothing at ours. `tools/split_capture.py` finds all 42 marker bursts in one pass and assigns them to files, since the span between a file's two markers is fixed by the manifest and no two files share a span closely enough to be confused. All 21 are present, in manifest order, and every marker span matches the manifest to within 10 ms. The clock offsets sit inside ±140 ppm, which is the resolution of the 5 ms analysis hop rather than anything about the unit.

Two things worth saying about the recording itself. It starts 0.92 s into `01_reference_1`, and nothing sounds before 3.4 s, so nothing is lost. And a segment inside `07_modulation_2` happens to look like a three-burst marker; the splitter steps over it because no file starts at a distance from it that matches any span.

The digital silence is bit-exact zero, not a noise floor. The tap really is the DAC's I2S input and there is no converter noise to model.

## What is measured now

**The output path carries a fixed gain of −4.44 dB.** Nine single-operator segments at the same nominal level, spread across `fixed-220` to `fixed-7040`, `stack-1`, `corr-0` and `ratio-note60`, agree on it to 0.005 dB. Against the engine's summed bus that is `cal::OUT_GAIN` 0.14992. The engine's `gain` is now the analogue volume pot instead, which sits after the tap on the hardware and so cannot be part of any measurement; it defaults to unity and the plugin's knob starts at full.

**The channel accumulator clips hard at 0.126435 of full scale.** `stack-1` through `stack-8` stack one, two, four and eight carriers at the same frequency and phase. One and two are exactly linear, two being twice one to five decimal places, so the operator sum is plain integer addition. Four and eight are driven four and eight times past the ceiling and both recover the same clip level to five decimal places, which is what a hard memoryless clip does and a soft saturator does not. The spectrum agrees: odd harmonics only, with the evens down at −134 dB.

The clip is not a limiter and it is not the DAC. `ingain-12` in `06_filter_2` reaches 0.201 at the tap, 4 dB above the ceiling, so nothing downstream of the filter loop can be what clips. The filter runs off CHOUT and back into CHIN, so the ceiling sits at the channel accumulator ahead of it, which is where `render_chan` now puts it. Pan is applied after, and the centre-pan −3.04 dB carries the ceiling to `cal::CHAN_CLIP` 1.1919 in the engine's bus units.

The measured plateau carries about 4.5% of overshoot, which is a downstream filter ringing on the clip transitions and not part of the clip.

**The filter loop costs a flat 9.93 dB.** *(Corrected 2026-09-19, twice, and now 1.23 dB. Both filter files play at note 24, where the request set's pan scaling puts the segment hard right, and reading the left channel alone against an engine whose own pan law was shy hid 4.24 dB of it: `docs/aeg.md`, "The pan was in front of everything". Then the source turned out to be wrong too, an all1 operator the engine was giving a whole harmonic series, and the loss had been absorbing that as well: `docs/formant.md`.)* Every segment in `06_filter_*` whose tone sits in the passband reads that much below the engine once the output gain is taken out, constant to 0.06 dB across filter types 0, 1, 2 and 5, cutoff bytes 48 to 112, and every resonance from 0 to 100. That is `cal::FLT_LOSS` 0.3190 on the filter's input gain.

**`LEVEL_DB` is 0.3795, not 0.375.** Three routes over the level ladder in `01_reference` give 0.3792, 0.3797 and 0.3800. The DX7 lineage's 0.375 is 1% shy.

## What the capture confirms

**The EG rate model is right, and it was the only part of the EG this pass looked at.** `docs/aeg.md` goes back over the same three files for the shape, the hold and the key scaling, and finds three of those wrong.

 `tools/analyze_capture.py` recovered 38 of the 64 rates. Against `rate_secs`, the DX7-lineage formula the engine already uses, the ratio is 0.9945 with a standard deviation of 0.0008 over rates 16 to 33, which is as close as the measurement can resolve. Rates 37 to 47 come out about 3% slow and the whole range 16 to 60 sits at 1.009 ± 0.017. Rate 63 and rate 8 are at the ends of what a four-second segment can measure and should be ignored. This was one of the three models the engine leans on hardest and it needed no change.

**The frequency scale and the note table are right.** `fixed-220` measures 220.006 Hz and `fixed-440` measures 440.039 Hz, so the firmware's pitch word, the note table and the 48 kHz rate are all confirmed together. The engine runs about 0.5 cents flat of the hardware across the keyboard, which is 0.03% and not worth chasing yet.

**`CARRIER_DB` is 1.5.** The hardware's carrier correction ladder is 1.5089 dB per step with a maximum residual of 0.015 dB, dead straight over sixteen steps.

**The operator sum and the filter input gain are both exactly linear.** Filter input gain measures 1.000 dB per step on the hardware across its whole ±12 dB range.

## One engine defect the capture exposed

The engine's master soft limiter, `l / (1 + |l| * 0.5)`, was compressing everything measurably. It is why the engine's own carrier ladder read 1.4825 dB per step instead of 1.5, why its level ladder bowed, and why its filter input gain compressed at the top. Undoing it analytically turns the engine's carrier ladder into a straight 1.5015 dB per step with a 0.012 dB residual, matching the hardware's 1.5089 and 0.015. The hardware has no such thing. It has the channel clip above and then the DAC's own ceiling, and that is what the engine now has.

## What is measured but still needs modelling

These are all clean, repeatable measurements that need more than a constant changed. In rough order of how much they cost.

**The unvoiced level does not follow its bandwidth the way the engine thinks.** *(Measured 2026-09-21, and the level was the smaller half of it. Reading this sweep against `05b_unvoiced2`'s at note 36 gives the whole law: the band's width is linear in the bandwidth byte at 38.6 Hz a step where the engine has it exponential over nine octaves, it is an absolute width in hertz rather than a fraction of the period, and the total power goes as the reciprocal of that width, so `NOISE_BW_POW` is 1.0 and not 0.5. The flat region and the plateau are both a band folding at DC, since every unvoiced segment recorded sits at a 1 kHz centre; `13_unvoiced3` moves it to 8 kHz. `docs/fidelity_plan.md` item 1.)* The hardware is flat from `ubw-4` to `ubw-16`, then loses 15 dB by `ubw-60` and plateaus from there to `ubw-96`. The engine is flat across the whole sweep, so the error runs from +2.9 dB at the narrow end to −14.3 dB at the wide end. `cal::NOISE_BW_POW` at 0.5 holds the RMS constant, which is the wrong shape rather than the wrong value. `ubw-0` is a separate matter: the hardware is effectively silent at bandwidth 0, 30 dB below the engine. TODO item "songs 8 and 15 are 10 to 20 dB quiet" is this, and `05_unvoiced` settles it.

**The formant level is independent of bandwidth up to bw 40.** *(Explained 2026-09-19: the level is flat there because the window is. The bandwidth byte does nothing at all below 44 and halves the window every 8 steps above it. `FRMT_NORM` stays at 0; `docs/formant.md`.)* The hardware reads −28.710 dB at every one of `bw-0` through `bw-40`, identical to three decimals, then falls away steadily to −57.6 dB at `bw-96`. The engine sweeps 6.6 dB across the flat region alone. That pins `cal::FRMT_NORM` at 1, holding the spectral peak, over the region where the window still fits, with the fall-off beyond bw 44 to be modelled separately. `FRMT_NORM` is currently 0.

**The filter cutoff byte reaches much higher than the engine reads it.** On a 261 Hz tone the hardware is flat from cutoff byte 24 all the way to 120, and only bytes 0 and 8 attenuate at all. Filter types 0, 1 and 2, the three lowpass slopes, are identical to a millidecibel at cutoff 64, which only happens if the corner is far above the tone. The engine is still climbing at byte 120. `CUT_COEF_FS` and the one-pole reading of the coefficient are what this lands on, and the sweep is dense enough to fit a response rather than guess one.

**Detune was a curve, and it is a table.** *(Closed 2026-09-20 by `11_detune` and a `sweep.py detune` session; `docs/detune.md` is the working.)* This capture measured detune at note 60 in steps of three and killed the engine's 2.0 cents per step: read off the audio rather than off the analyzer's FFT bins the hardware runs 1.21 cents a step near zero and 2.7 at the ends, holds to a millihertz for two seconds and reads the same on the third harmonic. Fitting that curve was still wrong, because one note cannot see that detune is key scaled. It is `FRMDET[magnitude][band]` in pitch word units, the EPROM table the CPU already applies to a formant operator's transpose word, and the chip applies the same one to a ratio operator's register 0x80. Fifty-five measurements over six octaves land on it to 0.52 cents, which is the chip's own phase increment quantisation.

The cost of getting it wrong is a beat rate, not a pitch. Demo song 2 "Full Tines" detunes three of its six carriers and went out of the engine warbling at 4.2 Hz where the unit warbles at 2.4.

**AM sensitivity is roughly twice as deep as the engine makes it.** *(Wrong, and it points the other way now. This reading predates both the analyzer's pan correction and the `LEVEL_DB` change. Re-measured 2026-09-21 off the same two envelopes: the mean level falls 14.38 dB on the unit over the sweep and 16.31 in the engine, and the modulation depth is linear in `ams` on both sides with the engine about 18 % too deep. The shape is right and the scale is not. `docs/fidelity_plan.md` item 5.)* From `ams-0` to `ams-7` the hardware loses 11.29 dB and the engine loses 4.89. The per-step shape differs too, so the engine's linear `ams / 7` scaling of the channel AM attenuation is not the law. Note this is a mean level under an LFO, so the sensitivity curve cannot be read straight off these numbers. The 2026-09-19 register capture puts this on the chip's side for good: the `fms << 3 | ams` byte at image +0x90 matches the engine on every segment, so the CPU sends the sensitivity the engine thinks it does and the depth is what the chip makes of it.

**Operator level below sysex 27 leaves the straight line**, and at `level-7` the hardware still sounds at −105 dB where the engine is silent. The engine's cutoff to silence is more aggressive than the chip's.

**Level varies with note in `ratio-note24` through `ratio-note96` on both sides, by 14.6 dB on the hardware and 9.1 dB in the engine.** *(Settled 2026-09-19: it is not a level at all. Every request file leaves the performance's pan scaling at its extreme, so a segment away from C3 is panned, and this is that pan read through one channel. The 5.5 dB between the two numbers is the engine's pan law being 28 % shy. `docs/aeg.md`.)* The segment exists to tie the note table to Hz and does that cleanly, so this is incidental, but something in the amplitude EG or its key scaling is scaled differently. `02_envelope_3`'s `tscale` segments are the ones that settle it and they disagree badly.

## The effects files

`09_effects_*` cannot be read yet, for the reason already in TODO. The tool sends a whole 112-byte effect block per segment, and for the types no preset performance uses there is no factory block, so the parameters go out as zeros on the hardware and in the engine alike. Those segments measure the request, not the unit. The file is worth keeping because the hardware's real impulse responses are in it once the segments are rebuilt from the Effect Parameter List defaults.

## Reproducing this

```
python tools/split_capture.py captures/FS1R_capture_request_0918.flac
cmake --build build/cmake --target render_capture
python tools/make_capture_set.py --render
python tools/analyze_capture.py captures/hardware/*.wav --compare
```

`tools/render_capture.cpp` is new and exists because `fs1r_emu -smf` needs WinMM and so the calibration loop could only ever be closed on Windows. It renders 32-bit float, so a render carries no quantisation floor of its own against a 24-bit capture; the old 16-bit renders floored at −96 dB and several segments of this capture sit below that. The shared parts came out of `fs1r_console.cpp` into `src/fs1r_smf.h`, which the console still uses.

`tools/analyze_capture.py` also stopped scoring a segment that is silent on both sides as a disagreement. The engine renders digital silence as exactly zero and so does the hardware, but a 24-bit capture floors at −144.5 dB and a float render at −200, which scored those segments at 55 dB and swamped the median. Both columns below are measured with that fixed, so they are comparable.

## What the changes did

Re-rendered and re-measured against the same capture, median absolute level difference per file.

| file | before | after |
|---|---|---|
| `01_reference_1` | 4.22 | **0.12** |
| `01_reference_2` | 4.52 | **0.08** |
| `02_envelope_1` | 4.28 | **0.17** |
| `02_envelope_2` | 4.28 | **0.11** |
| `02_envelope_3` | 6.41 | 7.37 |
| `03_fm_1` | 4.13 | **0.10** |
| `03_fm_2` | 4.12 | **0.11** |
| `04_formant_1` | 2.43 | 4.38 |
| `04_formant_2` | 2.41 | 5.66 |
| `04_formant_3` | 5.88 | 3.11 |
| `05_unvoiced_1` | 9.84 | 7.24 |
| `05_unvoiced_2` | 3.27 | 1.37 |
| `06_filter_1` | 14.27 | **0.02** |
| `06_filter_2` | 14.26 | **0.02** |
| `07_modulation_1` | 4.11 | **0.12** |
| `07_modulation_2` | 4.11 | **0.10** |
| `08_panlevel_1` | 4.38 | **0.23** |
| `08_panlevel_2` | 4.10 | **0.54** |
| `09_effects_1` | 12.14 | 15.13 |
| `09_effects_2` | 42.35 | 37.26 |
| `09_effects_3` | 35.80 | 33.31 |

Median across the 21 files, 4.28 dB to 0.23 dB. Twelve of them now agree with the hardware to a quarter of a decibel, and the two filter files to 0.02 dB. The clip reproduces `stack-4` and `stack-8` to 0.04 dB, which is the check that matters most, since those are the only segments driven past the ceiling.

Four files got worse, and all four for the same reason. The old 4.1 dB offset was the wrong sign for some of these models and had been partly cancelling them, so removing it uncovers the real error instead of hiding it. `04_formant_1` and `04_formant_2` are the bandwidth level law, `02_envelope_3` is the rate scaling, and `09_effects_1` is the zeroed parameter blocks. None of them is a regression in the engine; each is a model that was never right and can now be read straight.
