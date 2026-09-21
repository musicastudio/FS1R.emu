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

**Neither the bandwidth nor the resonance touches these four, and that is by design.** The first version of this section gave them the formant's window law, because bandwidth 0 was the only setting the capture set had for them and borrowing the formant's curve was the only available guess. Two register sweeps and the manual settle it. **Voice byte 6 is two parameters sharing a byte, not one.** The front panel names it *bandwidth* on the formant and *resonance* on res1 and res2, and those are separate controls: bandwidth reaches register 0x218 and only the formant reads it, resonance reaches register 0x230, which the Data List calls the "freq. ratio of band spectrum", and only res1 and res2 read that. all1, all2, odd1 and odd2 read **neither**. Stepping both registers through all hundred values left those four flat to the byte, width and peak both, which is the unit behaving correctly rather than a sweep missing its target. Their window is a constant because the parameter that would open it does not exist on them.

Their shape control is the **skirt**, voice byte 5, which rgwan confirmed from the front panel moves every waveform except sine and which `sweep.py skirt` then measured on all of them. [skirt.md](skirt.md) is that working: the window is `sin^p` with p doubling per skirt step, and the skirt is also what separates all1 from all2 and odd1 from odd2, since the two are identical at skirt 0 and four to five times apart by skirt 7.

One thing had to change with it. The formant restarts its carrier at every grain, which is what holds its peak still while the fundamental moves under it, and that is right for a formant. On the odd forms the grains come twice a period, so restarting puts half a cycle of alternation into the train and lands the whole group on the *even* partials, which is what the first attempt did. Half a cycle back on alternate grains is the same carrier running on, and the group returns to partials 1, 3, 5.

All four forms then land on the right partial with the peak 0.0 to 0.5 dB off. What is left is a level: the unit puts all1 and all2 5.67 dB under the engine's grain sum and odd1 and odd2 2.15 dB under, and the 3.5 dB between them is the doubled grain rate carrying its power. Taking the root of the rate out leaves one constant, `cal::FORM_LEVEL` 0.520, fitted to four segments. Byte 6 varying is not what would test it, since these four do not read either of the two parameters that byte carries; the skirt is, and every segment in the capture set sits at skirt 0.

Spectrum error on the four form segments: **25.4 dB before, 0.3 dB after**. On `06_filter_1`'s twenty all1 segments, 29.3 dB before and under 1 after.

## res1 and res2 put a three-line group on harmonic byte + 1

The resonant forms were the biggest number left outside the effects, 16.9 dB over nineteen segments, and nothing about them had been modelled. Sweeping register 0x230 through all hundred values settles them completely.

**The peak lands on partial `byte + 1`. At every one of the hundred values, on both forms, at two fundamentals.** The engine had `fc = fop * (1 + ratio * 31 / 99)`, which tops out at 32 times the fundamental where the unit reaches 100.

**The group is three lines and nothing else**: the partials either side of the peak are 6 dB down and the next ones are 90 dB down, at every setting. That is exactly what a `sin^2` window exactly one grain period long gives, and it is a stronger constraint than any fit, because the Hann kernel is precisely 1, 0.5, 0 at offsets of 0, 1 and 2 over the sample spacing. So the resonant forms' window is one period, where all1 and all2's is two.

**And res1 and res2 are the same thing.** Identical peak partial, identical level, identical sidebands at every setting. Whatever distinguishes them is not in the spectrum of a held note.

That third fact tidied the level constant as well. `cal::FORM_LEVEL` had an ad-hoc root-of-the-grain-rate factor bolted on to explain why the odd forms sit 3.5 dB above the all forms. With the grain sum normalised by its own window length, that 3.5 dB falls out of the odd forms retriggering twice a period under a window half as long, the factor is gone, and one constant puts all six non-formant forms within **0.14 dB** of the unit at once.

`04_formant_3` goes from 3.05 dB of level error and 16.89 dB of spectrum error to **0.30 and 1.03**.

## Which moved the filter loss again

`cal::FLT_LOSS` has now been measured three times and it was wrong twice for reasons outside the filter.

| | loss | why it read that |
|---|---|---|
| 2026-09-18 | 9.93 dB | both filter files play at note 24, where the request set's pan scaling throws the sound hard right, and the left channel alone was being measured against an engine whose pan law was 28 % shy |
| 2026-09-19, morning | 5.69 dB | the pan corrected, but the source was still an all1 operator the engine was giving a whole harmonic series, so the loss was absorbing that too |
| 2026-09-19, now | **1.23 dB** | the source measured |

Each reading was internally consistent and each was wrong. A constant fitted on a file whose source model is wrong absorbs the source's error, and nothing in the fit can tell. The guard against it is the one this session added: score the shape as well as the level, because a gain error and a spectrum error look identical in a level and completely different in a spectrum.

At 1.23 dB the two filter files sit at 0.01 and 0.00 dB in level and 0.76 and 0.24 dB in shape, and `06_filter_2`'s filter EG envelopes come down from 15.68 dB to 5.43 without being touched.

## The level register glides, 2026-09-20 and 2026-09-21

Demo song 1 "Vokodrone" clicked its way through its own opening, once every 28 ms, where rgwan's recording of the same bytes does not. The intro from 2.0 to 5.5 s is part 4 alone, the vocal patch "ShoobyVoic": eight formant operators, no feedback, no FM, and preset Fseq "L&G MayI" running at 35.6 frames a second. An Fseq frame rewrites all sixteen level registers and all sixteen frequency words of the channel at once, and the frames are a vocoder analysis, so adjacent frames sit 13 to 80 dB apart. `FUN_00012a32` writes them raw, frame level shifted left one, with nothing between one frame and the next.

Applied per sample those writes cut the grain in flight. Operator 1's output stepped from +0.1490 to +0.0332 between two samples at 2.1685 s, and went to zero and back at 2.3348 and 2.4178, both mid-window: a step of a third of the operator's peak with no envelope in front of it, which is the click.

**The first measurement.** Take the 5 kHz-and-up envelope, average it over the 120 frame boundaries in the intro and read the peak against each take's own median. The recording is **+1.20 dB** at the boundary, which is nothing. The engine was **+12.3 dB**. Latching the level and the carrier frequency to the grain that follows the write brought it to +1.19 with the unvoiced operators muted, and that is what shipped on 2026-09-20.

**It was too slow, and `12_fseqlevel` says so.** That file drives one formant operator from preset Fseq 34 "RndArp4" at six notes over five octaves and then at 16, 65 and 176 frames a second, which is a far harder case than the demo: the frames are 15 ms and the grain at a low fundamental is tens of milliseconds, so a level latched to the grain arrives one or two frames late. Mean octave band error against the unit over the eight formant segments:

| what the level does between frames | 12_fseqlevel | Vokodrone boundary |
|---|---|---|
| hardware | 0 | **+1.20 dB** |
| steps, as it did before any of this | 9.7 dB | +12.3 dB |
| latched to the grain | 10.6 dB | +2.05 dB |
| glides, 0.5 ms | 5.8 dB | |
| glides, 1.2 ms | 4.8 dB | |
| **glides, 1.6 ms** | **4.74 dB** | **+2.71 dB** |
| glides, 3 ms | 5.3 dB | +2.95 dB |
| glides, 6 ms | 6.8 dB | |

A latched level is worse than no smoothing at all on the file built to measure it, and a glide beats both while still holding the demo's frame boundary. It is one mechanism and one constant, `cal::LEVEL_SLEW_MS`, where the latch needed the grain to be the right length and it is not. The minimum is flat from 1.2 to 2.0 ms, so the last digit is not measured. The carrier frequency stays latched to the grain, which is the same statement as the carrier phase reset the chip already does, and it is worth 0.4 dB on the demo boundary.

The glide starts at the new note's own level rather than gliding from the last note's, or it rounds every attack. With that, the whole hardware capture set is unchanged to every digit: nothing in it moves a level fast enough to tell.

## A ratio operator handed an Fseq frequency runs off the end of the register, 2026-09-21

`12_fseqlevel`'s three sine segments came back from the unit as one line at **23982 Hz** at notes 36, 60 and 84 alike, which is pitch word 32767 to a tenth. The engine sang at 275 Hz.

A frame's frequency word goes into the operator's own frequency register and does not stand in for the whole register chain, so a ratio operator still has the channel pitch added on top. The word is an absolute formant centre, around 26566 here, and the channel pitch is another 15742 to 19524, so the sum is 39632 to 45056 at every frame of every one of the three notes and the 16 bit register saturates. That is why all three notes give the same line. `word_hz` clamps now, and the Fseq branch adds the channel pitch for an operator that is neither a formant nor fixed. Those three segments go from 42 dB of band error to 5, and the residual is a flat offset on an inaudible tone.

An Fseq switch on a ratio operator is a nonsense patch and the unit's answer is Nyquist. It is in the file because a sine operator has no grain to hide a level step in, which was the second question the file asked; the answer is that it never gets to sing.

## What `12_fseqlevel` leaves open

* **The unvoiced operator**, 11 to 12 dB dark above 640 Hz at all three notes, and the same before and after any of this. *(The bandwidth law is measured now, [noise.md](noise.md), and `12_fseqlevel` goes from 52.4 to 3.16 dB in level once the renderer is handed the EPROM it always needed. Whether this segment is still dark is a re-read.)* That is the noise formant's own bandwidth law, the `NOISE_*` constants that `05_unvoiced` is for, and it swamps anything the file could say about the unvoiced level register. Slewing that one only makes it darker, so it is left stepping.
* **Sixteen frames a second**, `frmt-rate200`, 17 dB bright from 1 to 5 kHz and flat below. No slew setting touches it, so it is not the level path. Nothing else in the set holds one formant frame for 63 ms.
* **The file's own lever was blunted.** `init_performance` leaves formant pitch mode at 0, so the Fseq's pitch track drives the fundamental over 8.6 octaves and the note I chose only shifts it. The note was meant to set the grain rate on its own. It still separated the models, since the frame rate varies too, but a rerun with performance byte 0x23 at 1 would read cleaner.


## Where it stands

| | before | after |
|---|---|---|
| `04_formant_1` level / shape | 4.32 / 6.32 dB | **0.06 / 0.77 dB** |
| `04_formant_2` level / shape | 5.61 / 7.48 dB | **1.13 / 3.12 dB** |
| `04_formant_3` level / shape | 3.05 / 16.89 dB | **0.30 / 1.03 dB** |
| `06_filter_1` / `_2` shape | 14.72 / 14.76 dB | **0.76 / 0.24 dB** |
| `06_filter_2` filter EG envelopes | 15.68 dB | **5.43 dB** |
| demo band tilt, mean | 2.75 dB | **2.55 dB** |
| demo envelope correlation, median | 0.9755 | **0.9775** |

The demo is a wash on the last step of this, 2.52 dB of tilt against 2.57, and the capture set is not. The measurement wins: the window being a time rather than a fraction of the period is settled by three separate tests and the demo has fifteen songs of modelled effects in front of it.

`05_unvoiced`, `07_modulation`, `08_panlevel` and the envelope files did not move.

One song went the other way. Ana-Unison's band tilt improves from 4.26 to 2.76 dB and its envelope correlation falls from 0.872 to 0.823, which is the worst of the fifteen either way. It is a unison patch and the forms it leans on now beat differently against each other; that is worth a look and it is not a reason to keep a spectrum that is measurably wrong.

## What is still open

* **The skirt** now has its own working, [skirt.md](skirt.md), and it is mostly settled: the grain window is `sin^p` and the skirt **multiplies** p rather than stepping it, doubling on all/odd/res and going by `sqrt(2)` on the formant. That is exact on odd2 and res2, where the unit's line amplitudes are binomial rows read straight off a table. What is left there is the "1" forms' window, which is no `sin^p` at any exponent, and the 2.5 dB the formant keeps at the best exponent of the family.
* **`05_unvoiced`**, 3.8 and 6.4 dB, the noise formant's own bandwidth law and a separate model. **The units question is answered, 2026-09-21: it is an absolute width in hertz.** `05b_unvoiced2` repeats the sweep at note 36 and the width does not move, 108 Hz against 102 at bandwidth 4 and 1781 against 1819 at 64, over two octaves of fundamental. The law with it: the width is linear in the byte at 38.6 Hz a step, the total power goes as its reciprocal so `NOISE_BW_POW` is 1.0, the skirt widens the band where the engine narrows it, and the resonance does nothing below byte 4. **Closed 2026-09-21** by `13_unvoiced3`, which puts the band at an 8 kHz centre where nothing folds: the corner clamps at register 77, the skirt multiplies it rather than adding poles, the level is a table and the resonance is a threshold. 3.8 and 6.4 dB of band error become 3.3 and 3.7, and the level 7.40 and 2.86 dB become 0.67 and 0.94. What is left there is a wideband pedestal above 4 kHz that no cascade of one-poles produces. [noise.md](noise.md).
* ~~**What separates res1 from res2, and all1 from all2.**~~ **Closed 2026-09-19: it is the skirt.** Each pair is identical at skirt 0 and diverges as the skirt opens, the "2" of each ending up four to five times wider by skirt 7. Every segment in the capture set sat at skirt 0, which is why nineteen of them could not tell the pairs apart. [skirt.md](skirt.md) has the widths.
* **all1 and all2's geometry once the skirt opens.** The engine builds them as two overlapping grains under a carrier on the fundamental, a window two periods long, which is what makes skirt 0 one partial with nothing within 88 dB exactly as the unit has it. Above skirt 1 the engine's group slides off the fundamental and peaks near the fourth partial where the unit's stays on the first, and it is 36 to 66 dB out. This moves both members of the pair the same way, so it is the construction and not the window law.
