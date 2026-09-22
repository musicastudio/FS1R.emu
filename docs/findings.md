# Findings

The research log. Newest first. One entry per thing learned, dated, with the evidence that settled it.

This file is history. It records what was found and how. It does not track open work, which lives on the board, and it does not hold the current state of what is known, which lives in [STATUS.md](../STATUS.md). When an entry here settles a constant, the constant itself belongs in `src/fs1r/chips/cal.h` with the entry named beside it.

Entries that have a dedicated working document (`aeg.md`, `skirt.md`, `noise.md`, `detune.md`, `formant.md`, `filter.md`) are summarized here and linked, not duplicated.

---

## 2026-09-16, four engine faults found by driving Vokodrone against the unit

Written as the v0.3.2 release notes, which were never published in full. The shipped release body carries only the title and the SHA-256 lines, so this is the only copy of the working.

The FS1R plays fifteen demo songs out of its own EPROM, and `tools/extract_demo.py` pulls that byte stream out as standard MIDI files, so the hardware and the engine are driven by one sequence and can be lined up sample for sample. Song 1, "Vokodrone", is the demo's hardest case on purpose: four parts, a formant sequence on part 4, and a foot controller and a mod wheel driving six controller sets for fifty seconds, 1123 and 901 messages of them. It was the worst of the fifteen against the recording, bright by 8 to 15 dB above 320 Hz and out by 19 dB over its closing phrase. Isolating its parts put the fault in the engine rather than in a calibration constant, and the firmware names each one.

**An Fseq that had played out blasted its operators at full level.** The operators a formant sequence drives had their own level forced to zero attenuation on the assumption that the frame would overwrite it, and when the sequence stopped advancing nothing did. The frame writer is gated on the mode byte and the part, not on the sequence still running, so the registers hold the last frame; only the start delay gates it, by clearing the track masks while the delay counts down. Any patch whose Fseq reaches its end and stops was jumping to full volume on its sequenced operators.

**The Fseq retrigger test looked at every part.** "First note" means the Fseq part's own held-note count, which the firmware keeps per part and tests for 1. The engine scanned all 32 channels instead, so any other part holding a note stopped the sequence restarting and it ran on out of step with the keys.

**The frequency bias controller detuned operators the hardware leaves alone, and then froze.** The firmware adds the frequency bias word only to formant and fixed operators; the ratio branch adds nothing, and the unvoiced one is zeroed outside normal mode. The engine added it everywhere, so a mod wheel swept Vokodrone's bass operators by up to an octave and a half that the hardware never moves. Worse, the whole controller matrix was read once at note-on, so the frequency bias, both bandwidth biases and the amplitude EG bias were frozen for the life of a note, while the firmware re-runs the destination handler whenever one of its sources moves and rebuilds the per-part arrays. Vokodrone holds a thirty-second chord under a moving foot controller. All of that now runs on the 192 Hz tick, and the Fseq frame's own frequency words carry the bias as the firmware adds it.

**Controller sets do not compose.** Every "direct" destination handler stores a per-part byte rather than adding to one, and the eight sets are walked in order, so two sets pointed at one destination overwrite each other rather than summing, exactly as for the destinations that edit a part byte.

**What it measured.** Across all fifteen demo songs the mean absolute per-octave-band error against the unit went from 6.28 to 5.50 dB and the median envelope correlation from 0.956 to 0.965. In the median over the songs, every band from 640 Hz up sat within a decibel of the hardware. Vokodrone itself went from +8 to +15 dB bright to inside 5 dB in every band, from 0.956 to 0.978 correlation, with its per-second level tracking the unit to 1.5 dB across the song. What was left concentrated in four songs and in the top octave, where the engine is still dark because it models neither the phase truncation nor the level quantization of a fixed-point chip.

**What it did not settle.** Calibration against a measurement rig. The demo recording is real hardware and moved the modulation index, the resonance curve, the filter's cutoff reading and the four faults above, but it never holds a note still, so the chip-side models (EG shape, formant window, filter, effect algorithms) stayed inferred. The capture kit added in v0.3.0 exists to settle them.

The v0.3.2 downloads and their SHA-256 lines are on the [release itself](https://github.com/musicastudio/FSVR/releases/tag/v0.3.2). The release predates the FSVR rename, so its artifacts carry the `FS1R.emu` name.
