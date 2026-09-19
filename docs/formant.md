# The formant window and the harmonic forms, read off the hardware

The voiced operator is what makes an FS1R an FS1R, and until 2026-09-19 all of it was a model from Yamaha's patent with no measurement behind it. `04_formant_1` to `_3` are seventy-five segments recorded on 2026-09-18 that show the spectrum of the thing directly, and nothing had ever read them: `tools/analyze_capture.py --compare` reported each segment's level and its loudest peak and stopped there, so a window of the wrong width scored the same as one of the right width as long as the two had the same total energy.

Adding spectrum shape to the comparison, the octave bands with each segment's own level divided out, put numbers on it at once. This is the working behind what those numbers moved.

## What went looking for the filter and found this instead

The spectrum-shape metric flagged `06_filter_1` and `06_filter_2` at 14.7 dB each, on two files that matched the unit to 0.00 dB in level. The obvious reading was that the filter's gain was right and its response was not, which would have been `CUT_COEF_FS` and `LADDER_K` finally showing themselves.

It was not the filter. Both files drive the loop with `sine(form=ALL1)`, a ratio-1 operator on spectral form all1 at bandwidth 0, and at note 24 that is a 32.9 Hz fundamental. On the unit it is **one partial**: the second harmonic sits at −128 dB, ninety-five decibels down, which is nothing. The engine gave it a full harmonic series, partials 2 to 7 within 23 dB of the fundamental. The 14.7 dB was the engine inventing a spectrum, and the filter had nothing to do with it.

Worth saying plainly because it is the third time in two days that a number has turned out to be something else standing in front of the thing it was named after.

## The bandwidth does nothing at all below 44

`04_formant_1` sweeps the formant's bandwidth byte from 0 to 96 in steps of four, at a 1 kHz formant over a 261.8 Hz fundamental. Register 0x218 is the byte itself, clamped 0 to 99, so what the chip is handed is not in question.

The first eleven bandwidths give **identical spectra**. Bytes 0, 4, 8 ... 40 all read −79.9, −69.2, −45.6, −25.9, −56.9 on partials 1 to 5, to a tenth of a decibel. The window does not begin to open until byte 44, and from there it widens steadily:

| bandwidth | 0-40 | 44 | 48 | 52 | 56 | 64 | 72 | 80 | 88 | 96 |
|---|---|---|---|---|---|---|---|---|---|---|
| partials in the group | 1.01 | 1.03 | 1.17 | 1.67 | 2.19 | 4.30 | 5.53 | 8.13 | 16.3 | 35.9 |
| peak, dB | −25.9 | −26.5 | −28.7 | −32.2 | −34.5 | −40.5 | −44.2 | −48.2 | −58.6 | −71.3 |

The engine had the window opening from byte 0 and halving every 20 steps. Rendering the sweep and scoring it against the recording over a grid of both parameters lands on a knee at 44 and a halving every 8:

```
window length = 2 * 2^(-max(0, bw - 44) / 8) periods
```

Spectrum error over the 25 segments: **11.86 dB before, 1.99 dB after**, and the level error 4.32 dB before, 0.06 after. The knee also explains what `docs/capture_0918.md` recorded as a puzzle, that "the formant level is independent of bandwidth up to bw 40, then falls away": the level is flat there because the window is.

`cal::FRMT_NORM` stays at 0. With the knee in place the peak follows the window's own normalisation and needs no help.

## all1, all2, odd1 and odd2 are the same window, not a pulse train

The engine generated these four as a fixed quarter-period window at DC, retriggered every period, with the odd forms alternating sign. That is a pulse train: a full harmonic series, the same one at every bandwidth, because the bandwidth never entered into it.

The unit at bandwidth 0:

| | partial 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| all1, all2 | −31.1 | −119.0 | −108.1 | −119.1 | −119.3 |
| odd1, odd2 | −27.6 | −115.4 | −37.2 | −115.5 | −113.2 |

So all1 and all2 are one partial and odd1 and odd2 are two, the first and the third. These are groups that start at the operator's own frequency and reach upward as the bandwidth opens, which is the same construction the formant is, with the carrier at `fop` instead of at the formant frequency and the window length taken from the bandwidth. Retriggering at twice `fop` is what leaves only the odd partials: a grain train at that spacing under a carrier at `fop` puts its lines at `fop`, `3 fop`, `5 fop` and nowhere else.

One thing had to change with it. The formant restarts its carrier at every grain, which is what holds its peak still while the fundamental moves under it, and that is right for a formant. On the odd forms the grains come twice a period, so restarting puts half a cycle of alternation into the train and lands the whole group on the *even* partials, which is what the first attempt did. Half a cycle back on alternate grains is the same carrier running on, and the group returns to partials 1, 3, 5.

All four forms then land on the right partial with the peak 0.0 to 0.5 dB off. What is left is a level: the unit puts all1 and all2 5.67 dB under the engine's grain sum and odd1 and odd2 2.15 dB under, and the 3.5 dB between them is the doubled grain rate carrying its power. Taking the root of the rate out leaves one constant, `cal::FORM_LEVEL` 0.520, fitted to four segments at one bandwidth. **One point per form is not a law.** A bandwidth sweep on the harmonic forms is what would replace it, and the capture set does not have one.

Spectrum error on the four form segments: **25.4 dB before, 0.3 dB after**. On `06_filter_1`'s twenty all1 segments, 29.3 dB before and under 1 after.

## Which moved the filter loss again

`cal::FLT_LOSS` has now been measured three times and it was wrong twice for reasons outside the filter.

| | loss | why it read that |
|---|---|---|
| 2026-09-18 | 9.93 dB | both filter files play at note 24, where the request set's pan scaling throws the sound hard right, and the left channel alone was being measured against an engine whose pan law was 28 % shy |
| 2026-09-19, morning | 5.69 dB | the pan corrected, but the source was still an all1 operator the engine was giving a whole harmonic series, so the loss was absorbing that too |
| 2026-09-19, now | **1.23 dB** | the source measured |

Each reading was internally consistent and each was wrong. A constant fitted on a file whose source model is wrong absorbs the source's error, and nothing in the fit can tell. The guard against it is the one this session added: score the shape as well as the level, because a gain error and a spectrum error look identical in a level and completely different in a spectrum.

At 1.23 dB the two filter files sit at 0.01 and 0.00 dB in level and 0.76 and 0.24 dB in shape, and `06_filter_2`'s filter EG envelopes come down from 15.68 dB to 5.43 without being touched.

## Where it stands

| | before | after |
|---|---|---|
| `04_formant_1` level / shape | 4.32 / 6.32 dB | **0.06 / 0.85 dB** |
| `04_formant_2` level / shape | 5.61 / 7.48 dB | **1.29 / 3.18 dB** |
| `04_formant_3` shape, the four form segments | 25.4 dB | **0.3 dB** |
| `06_filter_1` / `_2` shape | 14.72 / 14.76 dB | **0.76 / 0.24 dB** |
| `06_filter_2` filter EG envelopes | 15.68 dB | **5.43 dB** |
| demo band tilt, mean | 2.75 dB | **2.52 dB** |
| demo envelope correlation, median | 0.9755 | **0.9775** |

`05_unvoiced`, `07_modulation`, `08_panlevel` and the envelope files did not move.

One song went the other way. Ana-Unison's band tilt improves from 4.26 to 2.90 dB and its envelope correlation falls from 0.872 to 0.819, which is the worst of the fifteen either way. It is a unison patch and the forms it leans on now beat differently against each other; that is worth a look and it is not a reason to keep a spectrum that is measurably wrong.

## What is still open

* **`res1` and `res2`**, the two resonant forms, at 16.9 dB over nineteen segments and untouched here. They spend the bandwidth byte on a resonance that moves a peak up the series instead of on a window, and `04_formant_3` sweeps it in ten steps for each. That is the biggest number left outside the effects.
* **The skirt**, at 3.18 dB over `04_formant_2`'s sixteen segments, down from 7.48 but not right. The unit's skirts *rise* with the parameter, 45 dB of difference at the twelfth partial between skirt 0 and skirt 7, and `sin^(2(skirt+1))` does not reproduce that shape at either bandwidth. A better window family would, and the data to choose one is already recorded.
* **A bandwidth sweep on the harmonic forms.** `cal::FORM_LEVEL` and the window law those forms share with the formant both rest on a single bandwidth. Twenty segments of all1 and odd1 across the bandwidth range would settle both, and would say whether all1 and all2 differ at all: at bandwidth 0 they are identical to a tenth of a decibel, and the engine now treats them as the same thing.
* **`05_unvoiced`**, 3.8 and 6.6 dB, which is the noise formant's own bandwidth law and a separate model.
