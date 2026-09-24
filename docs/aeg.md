# The amplitude EG, read off the hardware

The 0918 capture measured the EG's rate table and declared the model right, and it was right about the rates. What it did not measure was everything the rates are *used for*: the shape of a rise, the length of the hold and the key scaling that shortens every segment as you go up the keyboard. Those three were still DX7 guesses, and two of them were wrong by more than an order of magnitude. Demo song 2, Full Tines, is where it showed: 0.838 envelope correlation, the second worst of the fifteen, with a tail four seconds shorter than the unit's.

This is the working, over two passes. The first reads `captures/hardware/02_envelope_[123].wav`, rgwan's recording of the three envelope request files. The second reads `captures/hardware/10_envelope2.wav`, recorded on 2026-09-19 to answer what the first could not, and it moved more than the EG: the key code law turned out to be a saturating table rather than a line, the attack turned out to aim at the top of the scale rather than at its own target, and the pan sweep nobody asked for measured the pan key scaling and uncovered a 4.24 dB error in the filter loop. Both are read against `captures/requests/manifest.json`. The register values behind them are not in question: `FS1R.unlock/captures/2026-09-19/images/02_envelope/` holds the voice image the unit was actually playing for every one of these hundred segments, so what is measured here is the chip and nothing else.

## What was already right

**The decay is linear in dB and `rate_secs` gives its slope.** Thirty-eight of the sixty-four rates were recovered in the 0918 pass and sit at 0.9945 of the model over rates 16 to 33, rising to 1.03 over 37 to 47. The second ratio is the measurement, not the chip: the fit switches to the 1 ms analytic envelope for fast decays and that estimator reads the top of each millisecond, which flattens a steep slope. The first is 0.55% and stands unexplained. Neither is worth a constant.

**`EG_LEVEL_DB` is 1.5.** `eglevel-0` through `eglevel-40` read −28.114 to −88.271 dB, which is 1.5039 dB a step over forty steps. `eglevel-44` and beyond sit near the tap's own floor, around −100 dB, and should be ignored; `eglevel-52` is 4.5 dB out for that reason and for no other.

**The release is the decay, at T4's rate.** Six release segments from rate 24 to 63 measure within 2.5% of `rate_secs`, and the curve lies on the engine's to 1 dB once the two are aligned on note off. The unit answers a note off about one 10 ms envelope step before we do, which is MIDI latency and the 192.3 Hz tick, not the EG.

## The attack was four times too slow and started 150 dB too low

The engine modelled a rising segment as an exponential in dB aiming `EG_OVERSHOOT` past its target with a time constant of `EG_ATTACK_K * rate_secs`, starting from wherever the EG already was, which after a note on is the bottom of the scale at −200 dB. Against the hardware that is not close. At chip rate 24 the unit is 42 dB below full after 400 ms and the engine is 107 dB below it; the two curves differ by 45 dB rms over the whole segment.

Three things were wrong and only one of them was the shape.

**The time constant is a sixteenth of a full traverse, not a quarter.** Fitting `tau / rate_secs` per segment over rates 36 to 44, where the 1 ms envelope resolves the whole rise inside the 400 ms window, gives 0.0664, 0.0678 and 0.0627. A joint fit over all eight usable rates gives 0.0615. The DX7's EGS reaches the same number from the other side: its attack increment is `((17 << 24) - level) >> 24` times the decay's, which is an exponential approach with a time constant of `2^24 / inc` frames against the decay's `2^28 / inc`, exactly one sixteenth. `EG_ATTACK_K` is 0.0625.

**The rise starts 53 dB below full, not at silence.** One millisecond into an attack from silence at rate 32 the unit is already 52.5 dB below its peak, and at rate 24 it is 55 dB below. A joint fit with the time constant fixed puts the floor at −53.2 dB. This is the DX7's jump target under another name: the EGS forces the level counter to 1716 of 4096 before a rising segment, which is 55.8 dB below full. `EG_ATTACK_FLOOR` is −53.2, and a rising segment that starts below it starts there instead, or at its target if the target is lower still.

**The overshoot is 3.8 dB, not 6.** With the time constant pinned at a sixteenth, fitting the floor and the overshoot together over 381 envelope points gives −53.2 and 3.78 dB, at 0.70 dB rms. The DX7's own 17/16 of full scale is 6 dB and costs 1.5 dB rms, so the chip's approach flattens a little more than the EGS near the top. The floor and the overshoot trade off against each other when every attack runs to full level, which every one of these does, so this pair is the weakest number here. **`10_envelope2` settled it, and found the aiming wrong as well; see "The attack aims at the top of the scale" below.** The constants below are the first pass's and the ones in `namespace cal` are the second's.

A discrete simulation of the EGS staircase, where the multiplier steps down in integers from 17 to 1, fits worse than the plain exponential at 2.3 dB rms. The chip is not running the DX7's arithmetic, only something very like its curve.

After the change the eleven attack segments lie within a decibel rms of the hardware on the 10 ms envelope, down from 45, and inside four on the 1 ms one, which is where the jump itself lands and where a millisecond of MIDI timing is worth tens of decibels.

## The key rate scaling was wrong at every note

> **Corrected 2026-09-19.** This section first said the old model "had no effect on anything, ever", and that is false. What follows is the corrected account; the measurement below it never depended on the claim and did not change.

Register 0x50 is the operator's EG time scaling, 0 to 7, and register 0xC0 is the key code, `(pitchWord >> 8) + 10`, which moves by one for every three semitones. The chip combines them and shortens every rate as the note rises. The engine's model was `(tscale * clamp(keycode - 80, 0, 31)) >> 3`, taken from the DX7's `note / 3 - 7`.

`docs/ymp706_registers.md` glossed register 0xC0 as "`(pitchWord >> 8) + 10` = note/3 + 10". The formula is right and the gloss is out by 63: the pitch word is 0x3FAA at note 0, not 0, so the key code is note/3 + **73**, and at middle C it is 93, not 30. Reading the gloss instead of the number made `clamp(93 - 80, 0, 31)` look like `clamp(30 - 80, 0, 31)`, which is zero, and the conclusion "dead code" followed. The engine's own debug print said `C0 93` in the same session.

What the old model actually did, at time scaling 7:

| note | 0 | 24 | 36 | 48 | 60 | 84 | 108 | 127 |
|---|---|---|---|---|---|---|---|---|
| key code | 73 | 81 | 85 | 89 | 93 | 101 | 109 | 116 |
| old rate offset | 0 | 0 | +4 | +7 | +11 | +18 | +25 | +27 |
| measured | −11 | −11 | −8 | −4 | −1 | +3 | +10 | +11 |

So it was not dead, it was wrong by 11 to 16 chip steps at every note on the keyboard and always in the same direction. A chip rate step is a quarter of an octave of decay time, so that is between 6.7 and 16 times too fast, everywhere. Which is still exactly why Full Tines lost its tail, and is a worse bug than the one first described, but the mechanism written here was not the mechanism.

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

The awkward part is that those three x values do not sit on a line. The key code moves by 8 between each pair of notes, so a law linear in the key code would give −10, −2, +6, and +6 is ruled out: it puts tscale 3 at note 84 two rate steps up where the recording says one, and tscale 7 five steps up where the recording says three. Three notes cannot say whether that is a kink at middle C, a dead band around it, or a ceiling further up. The engine took the piecewise reading, one rate step per key code step below key code 93 and three quarters of one above. **`10_envelope2`'s twenty `tkey` segments settled it and it is neither a kink nor a dead band; see "The key code law is a table" below.**

This is the change that fixed Full Tines. The song's decays and releases run on operators with a nonzero time scaling, and the old model ran every one of them 7 to 16 times too fast. The engine's last audible frame moves from 14.02 s to 17.57 s against the unit's 18.28, and the envelope correlation from 0.838 to 0.976.

## The hold is half a traverse plus a lag the recording cannot place better than a few milliseconds

The hold rate register is `((99 - T) * 0xA4) >> 8` then +4, capped at 0x3E, with 0x3F left alone; the firmware's own conversion, confirmed byte for byte in the register image. What the chip does with it was a guess: the engine held for a full traverse at that rate.

Seven hold settings are measured now, three from `02_envelope_3` and four from `10_envelope2`, spanning 19 ms to 3.65 s. Onsets at sample resolution, each against a segment in its own file that starts immediately:

| hold | register | half a traverse | measured | left over |
|---|---|---|---|---|
| 10 | 61 | 4.27 ms | 19.00 ms | 14.7 ms |
| 20 | 54 | 14.22 ms | 25.54 ms | 11.3 ms |
| 30 | 48 | 42.67 ms | 44.88 ms | 2.2 ms |
| 40 | 41 | 136.53 ms | 150.15 ms | 13.6 ms |
| 50 | 35 | 390.10 ms | 400.58 ms | 10.5 ms |
| 60 | 28 | 1365.33 ms | 1363.29 ms | −2.0 ms |
| 70 | 22 | 3640.89 ms | 3647.44 ms | 6.6 ms |

A free fit of a fraction and a fixed term over the five slowest lands on 0.4998, so the fraction is exactly a half and nothing else is worth fitting. The lag is then the mean of what the seven leave over, 8.1 ms, and its six-millisecond scatter is what two note-ons quantised to the 192.3 Hz tick cost; 5.2 ms each, and the reference is a note-on too. A recording cannot place it better than that. What it is remains unexplained: the likeliest reading is still that 0x3F's special case in the firmware is the chip's too, that 0x3F skips the hold stage, and that entering the stage at all costs a fixed few frames.

Settings 80 and 99 hold for longer than a five-second note and never sound, which is the right answer and is what the unit does.

Whether the key rate scaling reaches the hold is still untested: every hold segment leaves the time scaling at 0. The engine applies it, on the reasoning that the hold register sits in the same per-operator stripe as the four rates.

## The key code law is a table, and it saturates

`10_envelope2` plays the same decay at time scaling 7 and 3 across ten notes, one per octave from 12 to 120, which is ten key codes four apart. Every one of the twenty slopes lands on the `(4 + (q & 3)) << (q >> 2)` ladder to within 0.6 %, so the chip's rate comes out as an integer, and the two settings together bound the key offset to a single value at nine of the ten:

| key code | 77 | 81 | 85 | 89 | 93 | 97 | 101 | 105 | 109 | 113 |
|---|---|---|---|---|---|---|---|---|---|---|
| note | 12 | 24 | 36 | 48 | 60 | 72 | 84 | 96 | 108 | 120 |
| tscale 7 | −11 | −11 | −8 | −4 | −1 | +1 | +3 | +7 | +10 | +11 |
| tscale 3 | −4 | −4 | −3 | −1 | 0 | 0 | +1 | +3 | +4 | +4 |
| key offset | −13 | −13 | −10 | −5 | −2 | +2 | +4 | +8 or +9 | +12 | +13 |

Two things fall out. It **saturates at ±13**: notes 12 and 24 give the same offset, and note 120 adds only one step over note 108. And it is **not linear**, not even piecewise linear in any tidy way: the step from key code 85 to 89 is five where the step from 89 to 93 is three, and a search over every `trunc((a * c0 + b) / c)` with `a` and `c` under 32 returns nothing that fits all ten. The three-point fit this replaces read the shape right in the middle and was wrong at both ends by three rate steps, which is an octave and a half of decay time.

So the engine carries the table and interpolates between its samples. Four key codes is three semitones, so the interpolation is a guess over a small gap, and the ends are measured rather than extrapolated. What would settle the rest is register 0xC0 driven away from the note's own pitch, which is `FS1R.unlock`'s experiment 8.

## The attack aims at the top of the scale, not at its own target

Every attack in `02_envelope_2` runs from silence to full, and with only those the shape is ambiguous: an exponential aimed a little past the target and one aimed at the top of the scale and clamped at the target are the same curve when the target *is* the top. `attackto-70` separates them. Its target is 21 dB down, and the unit climbs to it at the same speed a climb to full does, reaching it in about 75 ms at chip rate 32 where an approach aimed at the target needs 190 ms and the engine was taking 400. Six decibels of error at the moment the note speaks, on every operator whose L1 is not 99, which is most of them.

The DX7's EGS is the same shape and says so plainly: its rising increment is `((17 << 24) - level) >> 24`, the distance from the top of the scale, and the segment simply stops when the level reaches the target. The engine now does `cur += (EG_OVERSHOOT - cur) * rate` and clamps.

Refitting the three attack constants over ten segments that reach three different targets from three different levels, 780 envelope points, gives `EG_ATTACK_K` 0.0648 against the 1/16 it is pinned at, `EG_OVERSHOOT` 3.94 and `EG_ATTACK_FLOOR` −54.48, at 0.85 dB rms. The constants barely moved; the aiming is the whole change.

`attackfrom-50` and `attackfrom-20` settle the floor's nature at the same time. The first starts its rise at L4 = 36 dB down and stays there, the second starts at 58.5 dB down and the unit is already at 52.7 one millisecond in. So the floor is a level the chip jumps to from anywhere below it, not merely where a rise from silence happens to begin, which is what the engine already assumed and now has a measurement for.

## The pan was in front of everything

`10_envelope2` moves the pan hard across the keyboard, and rgwan asked whether that is the unit misbehaving. It is not: the unit is doing exactly what the patch asks. Every file in `captures/requests/` is built on `fs1r_patch.init_performance`, which leaves the performance part's PAN SCALING byte at 0. The Data List gives that parameter as 0 to 100, so 0 is not neutral, it is the extreme, and the FS1R pans by key as hard as it can.

That turned out to be worth having, because it measures the law. The performance's part pan is centre, so the index starts at 64, and inverting the recording's channel balance through the firmware's own pan tables gives the index at each note:

| note | 12 | 24 | 36 | 48 | 60 | 72 | 84 | 96 | 108 | 120 |
|---|---|---|---|---|---|---|---|---|---|---|
| index | 127 | 112 | 96 | 80 | 64 | 48 | 32 | 16 | 0 | 0 |

which is `64 − 4 * (note − 60) / 3`, hard right at note 12 and hard left from note 108 up, matching every note to within half a decibel and reproducing both ends of the table exactly. That is 64 index steps per 48 semitones at byte 0. The engine had 50, so every pan-scaled patch was 28 % shy of the unit.

Two things came out of the correction that are bigger than the pan itself.

**The filter loop's insertion loss was 4.24 dB wrong, and two errors had been cancelling.** Both `06_filter` files play at note 24, where the pan sits at index 112, so those segments are panned hard right. `tools/analyze_capture.py` measured the left channel alone, and the engine's own pan law happened to be shy by almost exactly the amount that hid the difference. With the analyzer reading the pan-independent level and the engine's pan law measured, the hardware reads 4.236 dB above the engine on every filter type, every resonance from 0 to 100, every input gain and every cutoff from 48 up. `cal::FLT_LOSS` goes from 0.3190 to 0.5196, a 5.69 dB loop loss rather than 9.93, and the two filter files land at a median difference of 0.01 and 0.00 dB.

**"Level varies with note" was never a level.** `docs/captures/capture_0918.md` records `ratio-note24` through `ratio-note96` varying by 14.6 dB on the hardware and 9.1 dB in the engine and calls it incidental but unexplained. It is this pan, seen through one channel, and the gap between the two numbers is the 28 %. `tools/analyze_capture.py` now takes the pan out of every measure but the one that asks for it: the two channels carry the same signal at two gains under a constant-power law, so the pair's total power is the signal's wherever it sits in the image, and scaling the louder channel up to that leaves a real waveform at a level that does not move. A centred segment comes back exactly as its left channel did, so every constant measured before this existed still reads the same.

The request files are left as they are. Regenerating them with the byte centred would invalidate the recordings that exist, the analyzer takes the pan out, and a sweep in every file is a free check on the pan law rather than a nuisance.

## Where it stands

`tools/analyze_capture.py --compare` reports envelope shape as well as level: the two curves aligned on their own onsets, and the rms dB between them.

| | before the EG work | after `02_envelope` | after `10_envelope2` |
|---|---|---|---|
| `02_envelope_1..3` shape, mean rms | 5.45 dB | 0.58 dB | 0.57 dB |
| `10_envelope2` level, median | | 0.82 dB | 0.47 dB |
| `10_envelope2` shape, mean rms | | 3.37 dB | 0.81 dB |
| `06_filter_1` level, median | 0.23 dB | 0.23 dB | 0.01 dB |
| `06_filter_2` level, median | 0.23 dB | 0.23 dB | 0.00 dB |
| demo envelope correlation, median | 0.955 | 0.971 | 0.974 |
| demo band tilt, mean | 3.10 dB | 2.89 dB | 2.76 dB |

`04_formant`, `05_unvoiced` and `09_effects` did not move; they are the bandwidth level laws and the zeroed effect blocks, both open elsewhere.

Two notes on reading the demo numbers. The band tilt is each song's per-octave error with its own median band removed, because `captures/FS1R DEMO.flac` is one take through whatever gain rgwan's converter sat at, where the capture set was measured off the digital tap; the recording's absolute level is not the engine's to match, and `tools/demo_probe.py score` reports both.

## The key code table at every semitone, and the hold does not scale

`21_keycode` (recorded 2026-09-24, `FS1R.unlock/captures/2026-09-24-5`) plays the same decay, nominal rate 34 at time scaling 7, at every note from 12 to 120. Each of the 109 slopes lands on the `(4 + (q & 3)) << (q >> 2)` ladder, 0.06 of fit error over the lot, so every note names its rate outright, and with `10_envelope2`'s time-scaling-3 points the offset `x` in `trunc(tscale * x / 8)` is pinned at every key code from 77 to 113:

| key code | 77–81 | 82–83 | 84–85 | 86–87 | 88–89 | 90–91 | 92–94 | 95–96 | 97–99 | 100–101 | 102–103 | 104–105 | 106–107 | 108–109 | 110–113 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| x | −13 | −12 | −10 | −8 | −5 | −4 | −2 | −1/+1 | +2 | +4 | +5 | +8 | +10 | +12 | +13 |

It saturates at ±13 exactly where the ten-point table had guessed, and between the samples it is not the interpolation the engine carried: 86–87 read −8 (the drum's −8 confirmed), 88–89 read −5 where the line gave −6, 104–105 read +8 where the line gave +9. Symmetric about 95.5; the one ambiguity is 95–96, which time scaling 7 and 3 both read as a zero step and could be −1, 0 or +1. `EG_KEYOFF` is now the 37-entry table. Twelve of the 109 notes had been on the wrong rate step, an eighth to a fifth of the decay time each.

**The hold does not scale.** The same take holds 135 ms at note 12 and 135 ms at note 120, ±2 ms, where the engine had scaled the hold with the key code and held note 12 for 920 ms. The scaling reaches the four rate registers and not the hold's. `21_keycode` 7.99 / 2.43 → **4.44 / 0.57**; what is left of its level is the ten-second segments' RMS window against decays that now match to the step.

## What is still open

* **The hold's fixed lag.** 8.1 ms with six milliseconds of scatter that is the 192.3 Hz tick. A register sweep does not fix that either; only a trace of the chip's own writes would, which is the SH-2 core item.
* **The 0.55 % the decay rates sit under `rate_secs`** over rates 16 to 33, unexplained and too small to model.
