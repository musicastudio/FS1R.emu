# The amplitude EG, read off the hardware

The 0918 capture measured the EG's rate table and declared the model right, and it was right about the rates. What it did not measure was everything the rates are *used for*: the shape of a rise, the length of the hold and the key scaling that shortens every segment as you go up the keyboard. Those three were still DX7 guesses, and two of them were wrong by more than an order of magnitude. Demo song 2, Full Tines, is where it showed: 0.838 envelope correlation, the second worst of the fifteen, with a tail four seconds shorter than the unit's.

This is the working. The numbers come from `captures/hardware/02_envelope_[123].wav`, rgwan's recording of the three envelope request files, read against `captures/requests/manifest.json`. The register values behind them are not in question: `FS1R.unlock/captures/2026-09-19/images/02_envelope/` holds the voice image the unit was actually playing for every one of these hundred segments, so what is measured here is the chip and nothing else.

## What was already right

**The decay is linear in dB and `rate_secs` gives its slope.** Thirty-eight of the sixty-four rates were recovered in the 0918 pass and sit at 0.9945 of the model over rates 16 to 33, rising to 1.03 over 37 to 47. The second ratio is the measurement, not the chip: the fit switches to the 1 ms analytic envelope for fast decays and that estimator reads the top of each millisecond, which flattens a steep slope. The first is 0.55% and stands unexplained. Neither is worth a constant.

**`EG_LEVEL_DB` is 1.5.** `eglevel-0` through `eglevel-40` read −28.114 to −88.271 dB, which is 1.5039 dB a step over forty steps. `eglevel-44` and beyond sit near the tap's own floor, around −100 dB, and should be ignored; `eglevel-52` is 4.5 dB out for that reason and for no other.

**The release is the decay, at T4's rate.** Six release segments from rate 24 to 63 measure within 2.5% of `rate_secs`, and the curve lies on the engine's to 1 dB once the two are aligned on note off. The unit answers a note off about one 10 ms envelope step before we do, which is MIDI latency and the 192.3 Hz tick, not the EG.

## The attack was four times too slow and started 150 dB too low

The engine modelled a rising segment as an exponential in dB aiming `EG_OVERSHOOT` past its target with a time constant of `EG_ATTACK_K * rate_secs`, starting from wherever the EG already was, which after a note on is the bottom of the scale at −200 dB. Against the hardware that is not close. At chip rate 24 the unit is 42 dB below full after 400 ms and the engine is 107 dB below it; the two curves differ by 45 dB rms over the whole segment.

Three things were wrong and only one of them was the shape.

**The time constant is a sixteenth of a full traverse, not a quarter.** Fitting `tau / rate_secs` per segment over rates 36 to 44, where the 1 ms envelope resolves the whole rise inside the 400 ms window, gives 0.0664, 0.0678 and 0.0627. A joint fit over all eight usable rates gives 0.0615. The DX7's EGS reaches the same number from the other side: its attack increment is `((17 << 24) - level) >> 24` times the decay's, which is an exponential approach with a time constant of `2^24 / inc` frames against the decay's `2^28 / inc`, exactly one sixteenth. `EG_ATTACK_K` is 0.0625.

**The rise starts 53 dB below full, not at silence.** One millisecond into an attack from silence at rate 32 the unit is already 52.5 dB below its peak, and at rate 24 it is 55 dB below. A joint fit with the time constant fixed puts the floor at −53.2 dB. This is the DX7's jump target under another name: the EGS forces the level counter to 1716 of 4096 before a rising segment, which is 55.8 dB below full. `EG_ATTACK_FLOOR` is −53.2, and a rising segment that starts below it starts there instead, or at its target if the target is lower still.

**The overshoot is 3.8 dB, not 6.** With the time constant pinned at a sixteenth, fitting the floor and the overshoot together over 381 envelope points gives −53.2 and 3.78 dB, at 0.70 dB rms. The DX7's own 17/16 of full scale is 6 dB and costs 1.5 dB rms, so the chip's approach flattens a little more than the EGS near the top. The floor and the overshoot trade off against each other when every attack runs to full level, which every one of these does, so this pair is the weakest number here. `10_envelope2`'s `attackto-70` and `attackto-40` separate them.

A discrete simulation of the EGS staircase, where the multiplier steps down in integers from 17 to 1, fits worse than the plain exponential at 2.3 dB rms. The chip is not running the DX7's arithmetic, only something very like its curve.

After the change the eleven attack segments lie within a decibel rms of the hardware on the 10 ms envelope, down from 45, and inside four on the 1 ms one, which is where the jump itself lands and where a millisecond of MIDI timing is worth tens of decibels.

## The key rate scaling did nothing at all

Register 0x50 is the operator's EG time scaling, 0 to 7, and register 0xC0 is the key code, `(pitchWord >> 8) + 10`, which moves by one for every three semitones. The chip combines them and shortens every rate as the note rises. The engine's model was `(tscale * clamp(keycode - 80, 0, 31)) >> 3`, taken from the DX7's `note / 3 - 7`, and the pivot was wrong by more than the keyboard is wide: key code 80 is above the top note, so the clamp returned zero for every note and the rate scaling had no effect on anything, ever.

The 24 `tscale` segments of `02_envelope_3` settle the law. Each one decays at nominal rate 34 and the slope recovers the chip's actual rate exactly, landing on the `(4 + (q & 3)) << (q >> 2)` ladder to three decimal places:

| tscale | note 36 (key code 85) | note 60 (93) | note 84 (101) |
|---|---|---|---|
| 0 | 34 | 34 | 34 |
| 1 | 33 | 34 | 34 |
| 2 | 32 | 34 | 35 |
| 3 | 31 | 34 | 35 |
| 4 | 29 | 33 | 36 |
| 5 | 28 | 33 | 36 |
| 6 | 27 | 33 | 37 |
| 7 | 26 | 33 | 37 |

Each column is `trunc(tscale * x / 8)` added to the rate, for a single x per note, and there is exactly one x that fits all eight rows: −10 at note 36, −2 at note 60 and +4 at note 84. The division truncates toward zero rather than flooring, which is what separates note 60's row, where tscale 1 to 3 give nothing and 4 to 7 give one step, from the flooring version that would give one step from tscale 1 up.

The awkward part is that those three x values do not sit on a line. The key code moves by 8 between each pair of notes, so a law linear in the key code would give −10, −2, +6, and +6 is ruled out: it puts tscale 3 at note 84 two rate steps up where the recording says one, and tscale 7 five steps up where the recording says three. Three notes cannot say whether that is a kink at middle C, a dead band around it, or a ceiling further up. The engine takes the piecewise reading, one rate step per key code step below key code 93 and three quarters of one above, and `10_envelope2`'s twenty `tkey` segments across ten notes are what settle it.

This is the change that fixed Full Tines. The song's decays and releases run on operators with a nonzero time scaling, and with the scaling dead every note below middle C released at its unscaled rate instead of the slower one the unit uses. The engine's last audible frame moves from 14.02 s to 17.57 s against the unit's 18.28, and the envelope correlation from 0.838 to 0.976.

## The hold is half a traverse plus eleven milliseconds

The hold rate register is `((99 - T) * 0xA4) >> 8` then +4, capped at 0x3E, with 0x3F left alone; the firmware's own conversion, confirmed byte for byte in the register image. What the chip does with it was a guess: the engine held for a full traverse at that rate.

Measured at sample resolution off the recording, against a `hold-0` that has no hold at all, the onsets are 25.54 ms at register 54, 150.15 ms at 41 and 1363.29 ms at 28. Registers 16 and 4 hold longer than the five-second note and never sound, which is the right answer and is also what the unit does.

No pure fraction of `rate_secs` fits: the three would need 0.898, 0.550 and 0.499 of a traverse. Fitting a fraction and a fixed term together on relative error lands on 0.5007 and 8.5 frames of 64 samples, so half a traverse plus 11.4 ms, and that pair holds all three within 1.4%. Where the fixed term comes from is not known. It is not note-on latency, because `hold-0` is the reference and starts immediately; the most likely reading is that the firmware's special case for 0x3F is the chip's too, that 0x3F skips the hold stage entirely, and that entering the stage at all costs a fixed eight or nine frames. `10_envelope2` adds four hold values between the measured ones to separate the fraction from the lag.

Whether the key rate scaling reaches the hold is untested: every hold segment leaves the time scaling at 0. The engine applies it, on the reasoning that the hold register sits in the same per-operator stripe as the four rates.

## Where it stands

`tools/analyze_capture.py --compare` now reports envelope shape as well as level: the two curves aligned on their own onsets, and the rms dB between them. Over the hundred segments of `02_envelope_1` to `_3`:

| | before | after |
|---|---|---|
| envelope shape, mean rms | 5.45 dB | 0.58 dB |
| envelope shape, median rms | 0.58 dB | 0.38 dB |
| `02_envelope_3` level, median | 7.31 dB | 0.53 dB |
| demo envelope correlation, median | 0.955 | 0.971 |
| demo envelope correlation, worst | 0.750 | 0.879 |
| demo band tilt, mean | 3.10 dB | 2.89 dB |

Nothing else in the capture set moved. `04_formant`, `05_unvoiced` and `09_effects` are unchanged to the decimal; `06_filter_2`'s filter EG segments move by about a decibel on curves that are already 13 dB out, which is the open filter EG item and not this one.

Two notes on reading the demo numbers. The band tilt is each song's per-octave error with its own median band removed, because `captures/FS1R DEMO.flac` is one take through whatever gain rgwan's converter sat at, where the capture set was measured off the digital tap; the recording's absolute level is not the engine's to match, and `tools/demo_probe.py score` reports both. And `captures/demo/render/` had been left over from a build that predated `cal::OUT_GAIN`, which is why the old renders appeared to sit 10 dB closer than they were.

## What is still open

All of it goes through `captures/requests/10_envelope2.mid`, 28 segments and 2.8 minutes, which needs a recording the same way the first three did.

* **The key code law away from three notes.** `tkey7` and `tkey3` at ten notes from 12 to 120. This is the one that matters: it is the only piece of the EG that varies across the keyboard and it is fitted through three points.
* **The hold's fraction against its fixed lag.** `hold-10`, `hold-30`, `hold-50`, `hold-70`.
* **The overshoot against the attack floor.** `attackto-70` and `attackto-40` rise to a target part way up, where the two stop trading off against each other.
* **Whether the attack floor is a jump or a starting point.** `attackfrom-50` starts above it and `attackfrom-20` below it. If the chip jumps from any level below the floor, the second will start where the first does.

Two things the audio set cannot answer, and which `FS1R.unlock`'s register harness can. Whether the rate scaling reaches the hold register, and whether the key code the chip scales by is register 0xC0 itself or something derived from the pitch word behind it. Both need register 0xC0 driven directly while a note sounds, which is `sweep.py`'s shape exactly; `FS1R.unlock/docs/unknowns.md` has the experiment.
