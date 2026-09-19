# The formant window and the harmonic forms, read off the hardware

The voiced operator is what makes an FS1R an FS1R, and until 2026-09-19 all of it was a model from Yamaha's patent with no measurement behind it. `04_formant_1` to `_3` are seventy-five segments recorded on 2026-09-18 that show the spectrum of the thing directly, and nothing had ever read them: `tools/analyze_capture.py --compare` reported each segment's level and its loudest peak and stopped there, so a window of the wrong width scored the same as one of the right width as long as the two had the same total energy.

Adding spectrum shape to the comparison, the octave bands with each segment's own level divided out, put numbers on it at once. This is the working behind what those numbers moved.

## What went looking for the filter and found this instead

The spectrum-shape metric flagged `06_filter_1` and `06_filter_2` at 14.7 dB each, on two files that matched the unit to 0.00 dB in level. The obvious reading was that the filter's gain was right and its response was not, which would have been `CUT_COEF_FS` and `LADDER_K` finally showing themselves.

It was not the filter. Both files drive the loop with `sine(form=ALL1)`, a ratio-1 operator on spectral form all1 at bandwidth 0, and at note 24 that is a 32.9 Hz fundamental. On the unit it is **one partial**: the second harmonic sits at −128 dB, ninety-five decibels down, which is nothing. The engine gave it a full harmonic series, partials 2 to 7 within 23 dB of the fundamental. The 14.7 dB was the engine inventing a spectrum, and the filter had nothing to do with it.

Worth saying plainly because it is the third time in two days that a number has turned out to be something else standing in front of the thing it was named after.

## The bandwidth is a time, and both fitted knees were that law in the wrong units

> **Corrected 2026-09-19, the same day.** This section first said the window "does nothing at all below byte 44" and halves every eight steps above it, fitted against `04_formant_1`. A register sweep at a different note then said the knee was at 27. Neither is a knee. The window is a fixed **time** and the engine was holding it at a fixed fraction of the fundamental's period, so fitting that wrong shape against note 60 put the break at 44 and against note 36 at 27 — sixteen bandwidth steps apart, which is exactly the two doublings between the two fundamentals. What follows is the corrected account.

The decisive measurement is the same bandwidth byte at two notes. `04_formant_1` sweeps it at note 60 and `FS1R.unlock`'s `formbw` register sweep at note 36, and the group they produce is the same width in **hertz** and four times as many **partials**:

| bandwidth | 56 | 64 | 72 | 80 | 88 |
|---|---|---|---|---|---|
| note 60, partials | 2.19 | 4.30 | 5.53 | 8.13 | 16.34 |
| note 36, partials | 8.54 | 16.33 | 23.08 | 33.77 | 63.79 |
| note 60, Hz | 573 | 1125 | 1448 | 2128 | 4278 |
| note 36, Hz | 559 | 1068 | 1510 | 2209 | 4172 |

The capture set has its own test for this and it was never read: `f0-note36` through `f0-note72` play the same 1 kHz formant at bandwidth 40 under four fundamentals. The hardware gives 3.05 partials at note 36 and 1.62 at note 48, which is 199 Hz and 212 Hz. A window held at a fixed fraction of the period would have given 3.05 partials at both.

So the law is a bandwidth in hertz, which is what the parameter is called and what a formant's bandwidth ought to be:

```
window length = 1 / (FRMT_BW_HZ0 * 2^(bw / FRMT_BW_DB))   seconds
              capped at FRMT_WL_MAX periods of the fundamental
```

`FRMT_BW_DB` is 8 and was right in both earlier readings: the width doubles every eight bandwidth steps over the whole range, on both notes. `FRMT_BW_HZ0` is 3.1 Hz, fitted by rendering `04_formant_1`'s twenty-five bandwidths and scoring the spectra; the engine then tracks the `f0-note` series to within 13 % at the two notes that can resolve it. The cap at two periods is the engine's own grain-slot limit, and it binds only where the group is narrower than one partial at either note recorded, so nothing here can see it and it is INFERRED.

Spectrum error on `04_formant_1`: **11.86 dB before any of this, 0.77 dB now**, and its level error 4.32 dB before and 0.06 now.

The lesson is worth more than the constant. Both wrong answers were obtained by fitting a model against one dataset until the error stopped falling, and both were confidently written up with the residual quoted. What caught it was a second dataset at a different fundamental, and the thing that made the disagreement readable was expressing the measurement in Hz as well as in partials. A fit that only ever sees one value of a variable cannot tell you whether the model depends on it.

## all1, all2, odd1 and odd2 are the same window, not a pulse train

The engine generated these four as a fixed quarter-period window at DC, retriggered every period, with the odd forms alternating sign. That is a pulse train: a full harmonic series, the same one at every bandwidth, because the bandwidth never entered into it.

The unit at bandwidth 0:

| | partial 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| all1, all2 | −31.1 | −119.0 | −108.1 | −119.1 | −119.3 |
| odd1, odd2 | −27.6 | −115.4 | −37.2 | −115.5 | −113.2 |

So all1 and all2 are one partial and odd1 and odd2 are two, the first and the third. These are groups that start at the operator's own frequency, which is the same construction the formant is, with the carrier at `fop` instead of at the formant frequency. Retriggering at twice `fop` is what leaves only the odd partials: a grain train at that spacing under a carrier at `fop` puts its lines at `fop`, `3 fop`, `5 fop` and nowhere else.

**The bandwidth has nothing to do with it, and the engine had that wrong too.** The first version of this section gave the four forms the formant's window law, because bandwidth 0 was the only setting the capture set had for them and borrowing the formant's curve was the only available guess. The register sweep settles it: stepping register 0x218 through all hundred values on each form in turn moves the formant's window and leaves every one of the other six flat to the byte, width and peak both. Voice byte 6 does reach those forms, but as register **0x230**, which the Data List calls the "freq. ratio of band spectrum" and which the unit's own voice image confirms carries the raw byte for them and the formant transpose word for the formant. So their window is a constant, and what byte 6 does to them is unmeasured: the sweep meant to settle it wrote the formant's register.

One thing had to change with it. The formant restarts its carrier at every grain, which is what holds its peak still while the fundamental moves under it, and that is right for a formant. On the odd forms the grains come twice a period, so restarting puts half a cycle of alternation into the train and lands the whole group on the *even* partials, which is what the first attempt did. Half a cycle back on alternate grains is the same carrier running on, and the group returns to partials 1, 3, 5.

All four forms then land on the right partial with the peak 0.0 to 0.5 dB off. What is left is a level: the unit puts all1 and all2 5.67 dB under the engine's grain sum and odd1 and odd2 2.15 dB under, and the 3.5 dB between them is the doubled grain rate carrying its power. Taking the root of the rate out leaves one constant, `cal::FORM_LEVEL` 0.520, fitted to four segments at one setting of byte 6. **One point per form is not a law**, and the sweep that was meant to give the rest of them went to the wrong register.

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
| `04_formant_1` level / shape | 4.32 / 6.32 dB | **0.06 / 0.77 dB** |
| `04_formant_2` level / shape | 5.61 / 7.48 dB | **1.13 / 3.12 dB** |
| `04_formant_3` shape, the four form segments | 25.4 dB | **0.3 dB** |
| `06_filter_1` / `_2` shape | 14.72 / 14.76 dB | **0.76 / 0.24 dB** |
| `06_filter_2` filter EG envelopes | 15.68 dB | **5.43 dB** |
| demo band tilt, mean | 2.75 dB | **2.57 dB** |
| demo envelope correlation, median | 0.9755 | **0.9775** |

The demo is a wash on the last step of this, 2.52 dB of tilt against 2.57, and the capture set is not. The measurement wins: the window being a time rather than a fraction of the period is settled by three separate tests and the demo has fifteen songs of modelled effects in front of it.

`05_unvoiced`, `07_modulation`, `08_panlevel` and the envelope files did not move.

One song went the other way. Ana-Unison's band tilt improves from 4.26 to 2.76 dB and its envelope correlation falls from 0.872 to 0.823, which is the worst of the fifteen either way. It is a unison patch and the forms it leans on now beat differently against each other; that is worth a look and it is not a reason to keep a spectrum that is measurably wrong.

## What is still open

* **`res1` and `res2`**, the two resonant forms, at 16.9 dB over nineteen segments and untouched here. They spend byte 6 on a resonance that moves a peak up the series instead of on a window, they take it through register 0x230 like the other non-formant forms, and `04_formant_3` sweeps it in ten steps for each. That is the biggest number left outside the effects.
* **The skirt**, at 3.18 dB over `04_formant_2`'s sixteen segments, down from 7.48 but not right. The unit's skirts *rise* with the parameter, 45 dB of difference at the twelfth partial between skirt 0 and skirt 7, and `sin^(2(skirt+1))` does not reproduce that shape at either bandwidth. A better window family would, and the data to choose one is already recorded.
* **What voice byte 6 does to the six forms that are not the formant.** It reaches them as register 0x230 and the sweep wrote 0x218, so this is still open and `cal::FORM_LEVEL` and their fixed window still rest on one setting of it. It would also say whether all1 and all2 differ at all: at byte 6 = 0 they are identical to a tenth of a decibel and the engine treats them as the same thing. `FS1R.unlock/docs/unknowns.md` experiment 9, rewritten for the right register.
* **`05_unvoiced`**, 3.8 and 6.6 dB, which is the noise formant's own bandwidth law and a separate model.
