# The noise formant, read off the hardware

The unvoiced operator was the last part of the engine with no measurement anywhere in it. A cascade of `1 + skirt` one-poles whose cutoff spanned nine octaves from 20 Hz, ring modulated up to the centre, with a level term that held the RMS constant: a reading of the patent, fitted against nothing. It read 7.1 to 7.4 dB out in level over eighty-one segments and 3.8 to 6.4 dB out in band shape, the largest gap left in the engine.

`13_unvoiced3`, recorded 2026-09-21, settles it. Every unvoiced segment recorded before it put the band at a 1 kHz centre, and from bandwidth 40 up the band's lower edge sits at DC, so both earlier takes stop widening at about 1750 Hz and both plateau in level at the same bandwidth. That is a band folding on itself, not a law, and it hid the top half of the range. At an 8 kHz centre nothing folds until the band is 16 kHz wide.

All three laws below are read rather than modelled.

## The corner is linear in the bandwidth register, and it clamps

Fitting a two one-pole band to each of the twenty-five bandwidths recovers a corner that is flat in `fc / register`:

| register | 10 | 15 | 20 | 25 | 30 | 36 | 41 |
|---|---|---|---|---|---|---|---|
| fitted corner, Hz | 203 | 263 | 342 | 418 | 500 | 624 | 733 |
| Hz per register | 20.3 | 17.6 | 17.1 | 16.7 | 16.7 | 17.3 | 17.9 |

So `fc = 17 Hz * register`, a straight line through the origin. `NOISE_BASE_HZ * 2^(register / 127 * NOISE_OCT)` gave 20 Hz at register 0 and 10 kHz at the top and was the wrong shape over the whole range.

**It stops at register 77.** Every spectrum from sysex bandwidth 60 up is the same band at the same level: 29.6 to 29.75 dB over ten settings, the same widths, the same shape. Sysex 60 is register 77.

**And it is an absolute width in hertz**, which the two earlier takes had already said without being read. `05_unvoiced_1` is at note 60 and `05b_unvoiced2` at note 36, two octaves apart, and the width does not move: 108 Hz against 102 at bandwidth 4, 1509 against 1389 at 40, 1781 against 1819 at 64. The same question the formant window fell into twice, answered the same way, by a second value of the variable.

## The skirt multiplies the corner

The engine gave the skirt a cascade of `1 + skirt` one-poles, which makes the band **narrower** as the skirt opens. The unit makes it wider. Fitted corner at bandwidth 20, over the eight settings:

| skirt | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| corner, Hz | 418 | 553 | 690 | 951 | 1235 | 1736 | 2442 | 3367 |

That is 8.06 over seven steps, so 1.35 a step. Half-power width follows it, 662 Hz to 1775 at a 1 kHz centre where the engine went 940 down to 243.

**The skirt also moves the level, and by a different law.** At bandwidth 60 the skirt lifts the total 29.94 to 42.19 dB over its eight settings; at bandwidth 20 it does nothing at all, 43.64 to 43.81. Both fall out of one rule: each skirt step reads like a register 5.5 lower on the level table below. At register 77 that walks back up the falling part of the table and is worth twelve decibels; at register 25 the table is already flat there and the skirt cannot move it.

## The level is a table

Total output against the bandwidth register, with the band's own RMS normalised out:

| register | 0 | 5 | 10 | 15 | 20 | 25 | 30 | 36 | 41 | 46 | 51 | 56 | 61 | 67 | 72 | 77+ |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dB | -19.9 | -0.30 | -0.07 | 0.00 | -0.36 | -1.11 | -1.77 | -2.77 | -3.63 | -4.69 | -6.07 | -7.65 | -9.41 | -12.17 | -14.24 | -15.14 |

Flat to a tenth of a decibel from register 5 to 20, then fifteen decibels of fall, then a floor. Nothing analytic fits both ends: a power law in the corner is monotone where this is flat and then steep, and the engine's constant-RMS normalisation is the flat part alone. So the engine carries the measurement on a five-register grid and interpolates, the way it already carries `FRMDET`, `FEGLVL` and `EG_KEYOFF`.

**Register 0 is not on that curve.** The unit puts the noise twenty decibels under the flat region there, where the engine sounded a full band.

## The resonance is a threshold

Voice byte `res` adds a carrier beside the band. Measured at bandwidth 20, an 8 kHz centre:

| res | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| half-power width, Hz | 674 | 598 | 715 | 668 | 381 | 64 | 64 | 64 |
| total, dB | 43.76 | 43.87 | 43.78 | 43.69 | 43.95 | 44.60 | 45.74 | 46.87 |

Settings 0 to 3 are the same spectrum to the byte. 4 narrows it. 5, 6 and 7 are a tone with the noise under it, 64 Hz being the analysis floor. Reading the rises as `1 + d^2` against a band at unit RMS gives the carrier amplitude directly: 0, 0, 0, 0, 0.21, 0.46, 0.76, 1.02. The engine's `res / 7` ramp had the tone taking over from setting 2 and added twelve decibels across the range where the unit adds three.

## What the demo settled that the capture set could not

Bandwidth register 0 silences the noise. Whether it also takes the resonance carrier down with it is not in any recording: every segment the set has at bandwidth 0 is at resonance 0 too.

Demo song 6, Kalimba, answers it. Its patch runs six unvoiced operators and two of them sit at bandwidth 0 with resonance 5 and 7 at levels 99. Taking the carrier down with the noise costs that song **5.88 dB of band tilt**; leaving it at the table's own level costs **1.94**, and nothing else in the fifteen moves by a digit. The capture set does not move either, to every digit, which is what says the demo is reading the one thing the set is blind to rather than absorbing an error.

## Where it stands

| file | level before | level after | shape before | shape after |
|---|---|---|---|---|
| `05_unvoiced_1` | 7.40 | **0.67** | 3.77 | 3.31 |
| `05_unvoiced_2` | 2.86 | **0.94** | 6.36 | 3.65 |
| `05b_unvoiced2` | 7.13 | **0.55** | 4.06 | 3.16 |
| `13_unvoiced3` | 6.15 | **0.61** | 2.96 | **1.23** |

The level is settled. What is left is band shape at a 1 kHz centre, 3.2 to 3.7 dB against 1.23 at 8 kHz, and it is one thing: **the unit carries a wideband pedestal the two-pole cascade rolls off too hard.** Band by band on `ubw-64`, hardware minus engine, with each side's own level divided out:

| octave | 125-250 | 250-500 | 500-1k | 1k-2k | 2k-4k | 4k-8k | 8k-16k |
|---|---|---|---|---|---|---|---|
| dB | -1.81 | -1.66 | +0.52 | +0.01 | -0.57 | +4.22 | +10.08 |

The core matches inside 0.6 dB and everything above 4 kHz is short. The width measurements say the same thing from the other side: the half-power width saturates near 1500 Hz while the twenty-decibel width keeps opening past 9 kHz, which is a narrow core on a broad pedestal and not any `1/(1 + (f/fc)^2)^N`. That is the same energy STATUS.md's "top octave" item has been chasing, and a file that sweeps the bandwidth at an 8 kHz centre with the analysis reaching to Nyquist is what would model it.

## Reproducing this

```
python tools/make_capture_0921.py
python tools/make_capture_set.py --render
python tools/analyze_capture.py captures/hardware/13_unvoiced3.wav --compare
```
