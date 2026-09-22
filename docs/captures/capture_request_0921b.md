# Capture request: the filter, properly this time

One file, `captures/requests/15_filter.mid`, 53 segments, **3.8 minutes**. Same procedure as everything else: play it into the unit, record the digital output for the whole file, save one WAV of the same name next to the rest. It opens and closes with the usual three 1 kHz bursts so it aligns itself.

## Why, because the existing filter files are a dead letter

`06_filter_1` and `_2` have been in the set since the beginning and neither of them measures a filter. Their source is `sine(form=ALL1)` at note 24, and the 2026-09-19 formant work established that the unit gives that **one partial at 32.7 Hz**, with the second harmonic ninety-five decibels down. That is four octaves below any corner the filter reaches, so on your 0918 recording:

* the **eleven resonance segments read −34.906 to −34.908 dB** across the whole 0 to 100 range, flat to a millidecibel
* the **three lowpass slopes read identically**, and so does the band-elimination type
* of **sixteen cutoff bytes only 0, 8 and 16** attenuate anything at all

So `LADDER_K`, `RESO_COMP`, `RESO_Q0` and `RESO_PER_OCT` have never been measured by anything in either repo, and the `CUT_HZ0` and `CUT_OCT` fitted on 2026-09-21 rest on three points. `06_filter_1`'s 0.45 dB of band error is a sine passing through an open filter and agreeing with itself.

## What is different

The source. `13_unvoiced3` just measured one on your unit that has energy everywhere: an unvoiced operator at bandwidth 60 and skirt 7 puts its half-power band from **21 Hz to 16.4 kHz**. One spectrum of that through the filter traces the whole response, where the old source gave one number.

Rendered through the engine as a check before sending it, the reference segment carries energy in every octave from 15 Hz to 20 kHz, and cutoff byte 0 falls from the passband to the noise floor over seven octaves at a clean 24 dB per octave. That is the shape the old file could not see at all.

## What is in it

| segments | what |
|---|---|
| `filtoff-a`, `filtoff-b` | the same source with the part's filter switch **off**, one at each end. Every response is the ratio to these, so whatever our model of the source gets wrong divides out, and the drift between the two says whether anything moved during the file |
| `wcut-0` to `wcut-120`, 16 | LPF24, resonance 0, the cutoff byte every 8. The corner law over its whole range |
| `wreso-0` to `wreso-100`, 11 | LPF24 at cutoff 64, resonance every 10. The peak, which nothing has ever seen |
| `wreso-cut32-*`, 4 | the same peak an octave and a half down, where the source is denser |
| `wtype-*`, 6 | all six types at cutoff 64, resonance 0. The slopes |
| `wtypeq-*`, 6 | all six at resonance 60. The firmware zeroes resonance table B for HPF and BEF, and this is where that shows |
| `whpf-cut*`, `wbpf-cut*`, 8 | HPF and BPF at four cutoffs each: whether the corner law is the lowpass's |

It will sound like wide band-limited hiss being swept by a filter. Some segments are quiet, down around −65 dB, which is deliberate: that is cutoff byte 0 doing its job and it sits eighty decibels above a 24-bit floor.

## Still outstanding from the last request

`python captures/sweep.py gate smoke skirt` on the unlock rig, for the formant form alone. The 2026-09-19 run gave `frmt` a ratio-mode patch and recorded silence; it uses `formbw`'s fixed 1 kHz patch now. Two minutes, and it is the last of the seven spectral forms without a measured skirt.

## What we do not need

Everything else open is modelling with its data already recorded. The effects are 17 to 29 dB out over eighty-four real impulse responses from the 0921 take. The unvoiced pedestal is measured: at bandwidth 64 your unit carries a flat 7.3 dB floor from 62 Hz to 2 kHz that the engine has none of, and none at all at bandwidth 20. The `sin^p` window's "1" forms, all1 and all2's geometry and the formant's window family all have every spectrum tabulated in `docs/skirt.md`. AM sensitivity has three LFO depths now, which is enough to show it saturates rather than scales.
