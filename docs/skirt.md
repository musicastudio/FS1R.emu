# The spectral skirt

What voice byte 5 does to a voiced operator, measured on rgwan's unit on 2026-09-19 with `FS1R.unlock`'s `sweep.py skirt`: eight settings on each of the seven non-sine forms, one held note at 36, one unbroken take of the digital output. The working for the rest of the formant chain is in [formant.md](formant.md); this is the skirt alone, because it turned out to be the whole of what shapes four of the seven forms and because it settles a question that had been open since the pairs were first measured.

## What it is

The skirt is bits 5 to 3 of voice byte 5, `[-msssnnn]`, sharing the byte with the oscillator's fixed/ratio bit and its Fseq track. It is not voice byte 6: that byte carries two other parameters, *bandwidth* on the formant and *resonance* on res1 and res2, and all1, all2, odd1 and odd2 read neither of them. So for those four the skirt is the only shape control there is, which rgwan confirmed from the front panel before the sweep was run.

Every form but sine responds to it. Sine has no grain train and nothing for a window to shape.

## The law: the exponent doubles

The grain window is `sin^p` and **the skirt multiplies p, it does not step it**:

```
p = 2 * 2^skirt          all1, all2, odd1, odd2, res1, res2     2, 4, 8, 16, 32, 64, 128, 256
p = 2 * sqrt(2)^skirt    frmt                                   2, 2.8, 4, 5.7, 8, 11, 16, 23
```

The engine had `p = 2 * (skirt + 1)`, which agrees at skirt 0 and skirt 1 and then falls behind exponentially: at skirt 7 it gave 16 where the unit wants 256.

**res2 proves it outright.** Its grain is one period long under a carrier on partial 9, far enough up that no line folds, so the amplitudes the unit produces *are* the window's own Fourier coefficients; and a `sin^(2n)` window's coefficients are the binomial row `C(2n, n-k)`. Against the exact rows, with nothing fitted:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| window | `sin^2` | `sin^4` | `sin^8` | `sin^16` | `sin^32` | `sin^64` |
| rms against the binomial row, dB | 0.00 | 0.00 | 0.01 | 0.13 | 0.15 | 0.35 |

Skirt 0 gives 0, -6.0, -6.0 and nothing beyond, which is the row 1, 2, 1. Skirt 1 gives 0, -3.5, -3.5, -15.6, -15.6, the row 1, 4, 6, 4, 1. Skirt 2 gives 0, -1.9, -8.0, -18.8, -36.9, the `sin^8` row to a hundredth of a decibel. Those are binomial coefficients read off a table, not a curve fitted until the error stopped falling.

odd2 carries the same window and does not show the rows directly, because its carrier sits on the fundamental and the lines below it fold back up and add: measured against the plain row it reads 1 to 3 dB high, and against the engine, which does the folding, 0.07 to 0.9 dB.

Fitting the exponent freely, through the engine itself so the table and its interpolation are included, returns the same numbers:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| res2, fitted p | 2 | 4 | 8 | 16 | 32 | 64 | 256 | 609 |
| odd2, fitted p | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 |
| engine rms, dB | 0.00 | 0.01 | 0.01 | 0.03 | 0.06 | 0.27 | 2.13 | 3.95 |

The last row is res2 against the engine after the change, over every partial the unit puts above its own noise floor. Skirts 0 to 5 are exact. Skirts 6 and 7 carry 2 to 4 dB, which is where the unit's own arithmetic floor at about -90 dB sits under the group's tail; the fitted p wanders there for the same reason.

The level follows from the window with nothing added. A `sin^p` window's energy goes as `1/sqrt(p)`, so doubling p costs 1.5 dB, and the unit's total level falls 1.3 to 1.5 dB per skirt step across the whole range. The engine's grain sum does that by itself and needs no normalisation term.

## The formant goes slower, and its family is still wrong

`04_formant_2` sweeps the skirt on the formant at two bandwidths, sixteen segments, and it wants `sqrt(2)` per step rather than 2. Fitted against those segments, skirt by skirt, with the level and the band shape weighted together:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| fitted log2 p | 1.00 | 1.50 | 2.00 | 2.25 | 3.00 | 3.50 | 4.25 | 4.25 |
| `2 * sqrt(2)^skirt` | 1.00 | 1.50 | 2.00 | 2.50 | 3.00 | 3.50 | 4.00 | 4.50 |

That closed form takes `04_formant_2` from 1.13 dB of level error and 3.12 dB of band error to **0.29 and 2.52**, and `04_formant_3` from 1.03 to 0.73 dB of band error.

The 2.52 dB that remains is the family, not the exponent. **No exponent of any `sin^p` window gets the formant's skirt closer than about 2.6 dB of band shape** at these bandwidths, which the fit above establishes by trying the whole range: the best member of the family is still that far out. Whatever the chip does to a formant grain as the skirt opens, it is not raising a sine to a power. That is the next thing worth a measurement, and the data to choose a replacement is already recorded.

Why the formant should differ at all is open. Its window length is a bandwidth the player sets, where every other form's is fixed at one or two grain periods, so a window generator with a fixed number of steps stretched over a variable time would behave differently. That is a guess and it is written here as one.

## What separates all1 from all2, odd1 from odd2, res1 from res2

This had been open in both repos since the pairs were first measured: *"Each pair is identical in the spectrum of a held note at every setting measured. Whatever the difference is, it is not there."* It was not there because every setting measured was skirt 0.

**The pairs are identical at skirt 0 and diverge as the skirt opens.** Equivalent group width, in partials, over the eight settings:

| form | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| all1 | 1.00 | 1.01 | 1.05 | 1.11 | 1.18 | 1.24 | 1.29 | 1.34 |
| all2 | 1.00 | 1.05 | 1.25 | 1.58 | 2.14 | 3.13 | 4.35 | 6.52 |
| odd1 | 1.13 | 1.20 | 1.26 | 1.35 | 1.38 | 1.55 | 1.53 | 1.70 |
| odd2 | 1.13 | 1.29 | 1.43 | 1.97 | 2.65 | 3.42 | 5.06 | 7.31 |
| res1 | 1.45 | 1.80 | 1.84 | 2.24 | 2.46 | 2.34 | 2.47 | 2.57 |
| res2 | 1.48 | 1.87 | 2.46 | 4.00 | 4.57 | 7.43 | 9.08 | 13.41 |

The "2" of each pair ends up four to five times wider than the "1". So the number in the name is the skirt's reach, and a patch that leaves the skirt at 0 cannot tell the two apart, which is why nineteen segments of capture set could not either.

**The "1" forms are not a `sin^p` window at any exponent.** Fitting one freely leaves 6 to 17 dB, against 0.01 dB for their partners. Their spectra have a narrow core that stops changing above skirt 4 and a broad pedestal whose level rises with the skirt, which reads as a spike sitting on a shoulder rather than as one bump. Reconstructing the window from the line magnitudes shows that directly for res1, where the recovered shape stops narrowing at about two thirds of the grain period while res2's keeps shrinking past a fifth of it. Phase retrieval does not fully converge on res1, so those reconstructions carry a sign ambiguity and are a lead rather than a result.

The engine gives the "1" forms their partners' law for now. On odd1 and res1 that settles at 10 to 14 dB out, against 26 to 92 dB under the old law. all1 stays 44 to 66 dB out, but that is the all-form geometry fault below rather than the skirt: all2 is 36 to 45 dB out in the same way and it is the form whose window law is otherwise exact.

## What the unit actually produced

Every number above comes from these. dB below each group's own peak, partials 1 to 14, note 36:

**all1**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | -69 | -70 | -89 | -75 | -91 | -81 | -95 | -82 | -94 | -88 | -94 | -86 | -93 |
| 1 | 0 | -18 | -28 | -76 | -44 | -80 | -54 | -87 | -60 | -85 | -66 | -87 | -70 | -92 |
| 2 | 0 | -13 | -21 | -32 | -37 | -48 | -47 | -56 | -54 | -63 | -60 | -67 | -63 | -71 |
| 3 | 0 | -10 | -17 | -23 | -28 | -35 | -39 | -45 | -47 | -52 | -53 | -57 | -57 | -61 |
| 4 | 0 | -8 | -15 | -19 | -23 | -27 | -31 | -35 | -38 | -43 | -45 | -48 | -50 | -53 |
| 5 | 0 | -7 | -14 | -17 | -20 | -23 | -25 | -28 | -31 | -34 | -36 | -39 | -41 | -44 |
| 6 | 0 | -7 | -13 | -15 | -18 | -20 | -22 | -24 | -26 | -28 | -30 | -32 | -34 | -36 |
| 7 | 0 | -6 | -12 | -15 | -17 | -19 | -21 | -22 | -24 | -25 | -27 | -28 | -29 | -31 |

**all2**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | -60 | -70 | -86 | -75 | -92 | -81 | -97 | -82 | -96 | -89 | -96 | -86 | -93 |
| 1 | 0 | -12 | -63 | -73 | -77 | -79 | -86 | -81 | -90 | -88 | -81 | -88 | -91 | -95 |
| 2 | 0 | -6 | -17 | -35 | -69 | -72 | -74 | -74 | -78 | -78 | -78 | -78 | -78 | -81 |
| 3 | 0 | -3 | -8 | -16 | -26 | -39 | -58 | -77 | -76 | -75 | -78 | -79 | -79 | -78 |
| 4 | 0 | -2 | -4 | -8 | -13 | -19 | -26 | -35 | -45 | -57 | -72 | -72 | -70 | -70 |
| 5 | 0 | -1 | -2 | -4 | -6 | -9 | -13 | -17 | -22 | -27 | -33 | -40 | -47 | -58 |
| 6 | 0 | -0 | -1 | -2 | -3 | -5 | -6 | -8 | -11 | -13 | -16 | -19 | -23 | -27 |
| 7 | 0 | -0 | -0 | -1 | -2 | -2 | -3 | -4 | -5 | -7 | -8 | -10 | -11 | -13 |

**odd1**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | -76 | -9 | -79 | -72 | -86 | -81 | -93 | -88 | -93 | -86 | -94 | -87 | -93 |
| 1 | 0 | -74 | -8 | -76 | -26 | -80 | -36 | -86 | -53 | -89 | -53 | -90 | -62 | -90 |
| 2 | 0 | -78 | -6 | -78 | -19 | -78 | -27 | -80 | -36 | -81 | -43 | -83 | -49 | -84 |
| 3 | 0 | -74 | -5 | -74 | -15 | -76 | -21 | -77 | -27 | -78 | -32 | -80 | -38 | -82 |
| 4 | 0 | -75 | -4 | -77 | -12 | -80 | -18 | -80 | -22 | -79 | -26 | -79 | -30 | -79 |
| 5 | 0 | -72 | -4 | -76 | -11 | -77 | -16 | -77 | -19 | -77 | -22 | -77 | -25 | -77 |
| 6 | 0 | -71 | -3 | -74 | -10 | -75 | -15 | -75 | -17 | -75 | -20 | -75 | -22 | -75 |
| 7 | 0 | -70 | -3 | -73 | -9 | -73 | -14 | -73 | -16 | -74 | -18 | -73 | -20 | -73 |

**odd2**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | -81 | -9 | -75 | -68 | -85 | -85 | -93 | -88 | -93 | -87 | -94 | -87 | -92 |
| 1 | 0 | -73 | -6 | -76 | -20 | -72 | -68 | -79 | -80 | -85 | -84 | -86 | -82 | -87 |
| 2 | 0 | -68 | -3 | -76 | -11 | -71 | -23 | -74 | -42 | -81 | -76 | -85 | -84 | -86 |
| 3 | 0 | -65 | -2 | -73 | -6 | -75 | -12 | -70 | -20 | -72 | -31 | -75 | -45 | -79 |
| 4 | 0 | -66 | -1 | -68 | -3 | -68 | -6 | -70 | -10 | -73 | -15 | -74 | -22 | -73 |
| 5 | 0 | -63 | -0 | -65 | -1 | -66 | -3 | -66 | -5 | -66 | -8 | -67 | -11 | -67 |
| 6 | 0 | -59 | -0 | -60 | -1 | -61 | -1 | -61 | -3 | -61 | -4 | -61 | -5 | -61 |
| 7 | -0 | -57 | 0 | -57 | -0 | -57 | -1 | -57 | -1 | -57 | -2 | -57 | -3 | -57 |

**res1**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | -86 | -85 | -86 | -82 | -85 | -80 | -73 | -6 | 0 | -6 | -73 | -81 | -90 | -93 |
| 1 | -77 | -61 | -84 | -49 | -79 | -32 | -23 | -5 | 0 | -5 | -23 | -32 | -79 | -49 |
| 2 | -61 | -52 | -52 | -41 | -36 | -25 | -16 | -4 | 0 | -4 | -16 | -25 | -36 | -41 |
| 3 | -46 | -41 | -38 | -31 | -26 | -20 | -13 | -3 | 0 | -3 | -13 | -20 | -26 | -31 |
| 4 | -35 | -32 | -29 | -25 | -22 | -18 | -11 | -3 | 0 | -3 | -11 | -18 | -22 | -26 |
| 5 | -27 | -26 | -24 | -22 | -19 | -16 | -10 | -2 | 0 | -2 | -10 | -16 | -19 | -22 |
| 6 | -23 | -22 | -21 | -19 | -17 | -15 | -9 | -2 | 0 | -2 | -9 | -15 | -18 | -20 |
| 7 | -20 | -20 | -19 | -18 | -16 | -14 | -8 | -2 | 0 | -2 | -9 | -15 | -17 | -20 |

**res2**, dB below the group's own peak, partials 1 to 14 (note 36, 65.41 Hz):

| skirt | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | -86 | -85 | -86 | -82 | -84 | -80 | -65 | -6 | 0 | -6 | -65 | -81 | -89 | -93 |
| 1 | -75 | -79 | -77 | -82 | -79 | -65 | -16 | -4 | 0 | -4 | -16 | -65 | -80 | -87 |
| 2 | -68 | -71 | -70 | -69 | -37 | -19 | -8 | -2 | 0 | -2 | -8 | -19 | -37 | -70 |
| 3 | -66 | -57 | -41 | -27 | -17 | -9 | -4 | -1 | 0 | -1 | -4 | -9 | -17 | -27 |
| 4 | -36 | -27 | -19 | -13 | -8 | -5 | -2 | -1 | 0 | -1 | -2 | -5 | -9 | -13 |
| 5 | -19 | -14 | -10 | -7 | -4 | -2 | -1 | -0 | 0 | -0 | -1 | -2 | -4 | -7 |
| 6 | -6 | -6 | -4 | -3 | -2 | -1 | -1 | -0 | 0 | -0 | -1 | -1 | -2 | -3 |
| 7 | -0 | -0 | -0 | -0 | -0 | -0 | 0 | -0 | -0 | -0 | -1 | -1 | -2 | -2 |


## What is still open

* **The "1" forms' window.** Measured in full above and not modelled. It is the single largest disagreement left in the voiced chain, 10 to 14 dB on three of the seven forms.
* **The window family on the formant.** 2.52 dB that no `sin^p` can remove.
* **all1 and all2's geometry.** The engine builds them as two overlapping grains, a window two periods long at a carrier on the fundamental, which is what makes skirt 0 a single partial with nothing within 88 dB, exactly as the unit has it. Once the skirt opens, the engine's group slides off the fundamental and peaks around the fourth partial where the unit's stays on the first, and it is 36 to 66 dB out. The unit's all2 at skirt 7 is flat from the fundamental to the twentieth partial within 4 dB. That is a geometry fault, not an exponent one: it moves both members of the pair the same way and it was there before the exponent changed.
* **The formant's own skirt sweep.** The 2026-09-19 run gave it a ratio-mode patch and recorded silence, eighty decibels under the other six: a formant whose centre is the fundamental and whose window is 1.3 ms long puts out nothing. `sweep.py skirt` uses `formbw`'s fixed 1 kHz patch for `frmt` now, and one rerun of two minutes gets the seventh form.
