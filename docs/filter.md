# The per-voice filter, read off the hardware

The filter runs on VOP3-1, not on the YMP706. The CPU side is read from the firmware, including the cutoff word and both resonance tables, and the 2026-09-19 register retake confirmed the staged cutoff matches `0xC0D + 0xA9 * cut` to the integer. What the chip makes of those numbers was the model, and until 2026-09-21 nothing had measured any of it.

## Why nothing had measured it

`06_filter_1` and `_2` have been in the capture set since the beginning and neither of them measures a filter. Their source is `sine(form=ALL1)` at note 24, which the formant work established the unit gives as **one partial at 32.7 Hz**, the second harmonic ninety-five decibels down. That is four octaves below any corner the filter reaches, so on rgwan's 0918 recording:

* the eleven resonance segments read **−34.906 to −34.908 dB** across the whole 0 to 100 range
* the three lowpass slopes and the band-elimination type read identically
* of sixteen cutoff bytes, only 0, 8 and 16 attenuate anything at all

The file's 0.45 dB of band error was a sine passing through an open filter and agreeing with itself. `LADDER_K`, `RESO_COMP`, `RESO_Q0` and `RESO_PER_OCT` had never been measured by anything in either repo.

`15_filter` fixes the source. `13_unvoiced3` had just measured one on the unit that has energy everywhere: an unvoiced operator at bandwidth 60 and skirt 7 puts its half-power band from 21 Hz to 16.4 kHz. Two `filtoff` segments carry the same source with the part's filter switch off, one at each end, so every response here is a ratio to them and whatever our model of the source gets wrong divides out. The two references agree to **0.034 dB** on the unit, so nothing drifted under the sweep.

## The corner doubles every twelve cutoff bytes

Half-power point against the cutoff byte, LPF24, resonance 0:

| byte | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Hz | 52 | 68 | 100 | 160 | 274 | 434 | 713 | 1079 | 1802 | 2597 | 4437 | 7447 | 11289 |

```
fc = 17.4 * 2^(byte / 12)        Hz
```

to **0.098 octaves rms over thirteen points**, so the whole cutoff range spans 10.6 octaves, 17 Hz to 26 kHz. Bytes 0 and 8 sit above the line because the corner there is below what the measurement can resolve, not because the law bends.

The engine had `28.5 * 2^(0.110 * byte)`, which put byte 64 at 3751 Hz where the unit is at 713. That was fitted the day before on `06_filter_1`, whose three usable points could fix a slope and not a scale, and before that the coefficient had been read as a one-pole at 48 kHz, which put byte 0 at 755 Hz. The one-pole reading could never have worked at both ends: `-log(1 - a)` spans 14:1 over the whole byte range and the unit spans 1500:1.

## The slopes are 24, 18 and 12 dB per octave, exactly

Per-octave response at cutoff 64, resonance 0, past the corner:

| type | measured slope |
|---|---|
| LPF24 | −15.4, −20.6, −22.3 dB/octave |
| LPF18 | −13.2, −17.1, −17.2 |
| LPF12 | −12.0, −12.1, −12.0 |
| HPF | +10.5, +13.4, +12.7 (so 12 dB/octave) |
| BPF | 6 dB/octave each side, centred near 350 Hz |
| BEF | a notch near 250 to 500 Hz, 5.6 dB deep in octave bands |

Four poles, three poles and two poles for the lowpasses, and two poles for HPF and BPF. The engine's ladder taps and its state-variable filter were already the right shapes; only the corner was wrong.

## The resonance runs right up to the edge

This is what nothing had ever seen. Peak lift over the passband at cutoff 64:

| resonance | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| hardware, dB | 0.82 | 3.04 | 6.32 | 8.99 | 11.58 | 13.50 | 16.19 | 17.15 | 17.97 | 18.47 | 19.89 |

The engine topped out at **8.5 dB**, because `LADDER_K` was 2 against a self-oscillation point of 4 and the demo, which cannot see a resonance peak either, had been the only thing asking.

Driving the engine's own ladder with noise gives a map from feedback to peak lift, and pushing these eleven measurements back through it asks for `k` = 0.00, 0.91, 1.70, 2.07, 2.46, 2.70, 3.04, 3.13, 3.20, 3.25, 3.37. Against resonance table A:

| | reso 20 | 40 | 60 | 80 | 100 | spread |
|---|---|---|---|---|---|---|
| `k / A` | 2.15 | 2.70 | 3.15 | 3.25 | 3.39 | 1.24 |
| `k / A^2` | 2.72 | 2.96 | 3.28 | 3.30 | 3.42 | 0.70 |
| **`k / A^3`** | **3.45** | **3.25** | **3.40** | **3.36** | **3.44** | **0.20** |

So the feedback goes as the **cube** of table A. The cube is empirical; the CPU side is not in question, since the register retake matched A to the integer. Swept through the engine, `LADDER_K` lands at **3.65**, where the eleven lifts come back at 0.45 dB rms with the mean at −0.04, against 0.98 dB at 3.50 and 1.12 at 3.80.

**`RESO_COMP` is measured at zero now**, where the demo had only guessed it. The octaves below the corner read 0.2, −0.9, 0.0 dB at resonance 0 and 0.0, −0.1, 0.1 at resonance 60, so the passband the peak does not reach does not move at all, and the ladder's own `(1 + k)` normalisation is the whole story.

## The filter EG's segment rate, and its depth

`14_sens` sweeps the filter EG over six depths and four times, where the whole set had four segments. Two things come out.

**The depth is right once the corner is.** The unit's closed plateau on that patch is −57 to −58 dB and the engine's is −57.5 to −58.1, with the byte clamped at 0 exactly as the firmware clamps it. No fitted floor is needed and `CUT_BYTE_MIN` is 0.

**The segment rate was nearly three times too slow.** At time 60 the unit is back in its passband inside a second and the engine had not recovered by the end of a four second note. `StepEG`'s time constant was a quarter of a full traverse; `cal::FEG_STEP_K` is **0.09**, where the envelope error over `06_filter_2`'s four segments bottoms at 2.68 dB against 6.31 at 0.25. It is flat from 0.06 to 0.12, so the last digit is not measured. The amplitude EG's own rising constant is a sixteenth, which is the same order.

## Where it stands

| | before | after |
|---|---|---|
| corner at cutoff byte 64 | 3751 Hz | **713 Hz**, against the unit's 713 |
| peak lift at resonance 100 | 8.5 dB | **19.9 dB**, against the unit's 19.9 |
| `06_filter_2` filter EG envelopes | 5.43 dB | **2.68 dB** |
| `06_filter_1` band shape | 0.76 dB | 0.44 dB |
| `14_sens` envelopes | 3.79 dB | **3.34 dB** |
| `15_filter` level / shape | 5.9 / 14.6 dB | **2.39 / 4.11 dB** |

## What is left

* **`15_filter`'s own 2.39 dB**, concentrated at the extremes: the unit reads 3 to 7 dB above the engine at the highest resonances and the widest cutoffs. Whatever that is, it is not the corner and not the peak height, both of which now land on the measurement.
* **The stopband floor is the recording's, not the chip's.** Every hardware response bottoms out at −70 to −77 dB relative to the reference, and reading it against the source's own level per band shows a constant absolute floor rather than a leak proportional to the input. The engine runs on down to −180, so the octave-band metric scores bands that are measuring a noise floor on one side and arithmetic on the other. That is the same class as the silent-segment artifact already open in `analyze_capture.py`.
* **The demo disagrees**, and the capture set wins. Mean band tilt goes 2.45 to 2.80, and the two songs that move most, Kalimba and Fat Line, are the two whose patches sit hardest on the filter: a corner two octaves lower changes them a great deal. Against that, the corner is thirteen clean points on a source that can see it, with a filter-off reference, at 0.098 octaves rms, and the demo carries an effect model now measured at 17 to 29 dB against real impulse responses. Where they disagree the capture set is the reference.

## Reproducing this

```
python tools/make_capture_filter.py
python tools/make_capture_set.py --render
python tools/analyze_capture.py captures/hardware/15_filter.wav --compare
```
