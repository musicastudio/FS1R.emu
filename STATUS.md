# Status

What is known about the FS1R, what is modelled, and what nobody knows. This file is the reference the engine is written against. It is not a task list, and it is not a history.

Three words carry every claim below.

- **KNOWN** is read from the firmware, its tables or the board. It needs no calibration and a disagreement with the hardware is a bug.
- **INFERRED** is modelled, because the two custom chips have no public documentation. Every inferred constant lives in one place, `src/fs1r/chips/cal.h`, so calibrating against a recording is a table edit rather than a hunt through the engine.
- **UNKNOWN** is what no file, photo or recording tells us yet.

Moving a thing from INFERRED to KNOWN is the work. [docs/findings.md](docs/findings.md) records each move and the evidence that made it. [docs/fidelity_plan.md](docs/fidelity_plan.md) ranks what is still open by how far off it is.

## Where the numbers stand

Against rgwan's digital recording of the built-in demo, which the unit plays from its own EPROM so the same byte stream drives both: envelope correlation 0.819 to 0.992, median 0.978. Mean band tilt 2.52 dB, and per band from 40 Hz to 10 kHz, 2.4, 1.5, 1.3, 1.6, 1.2, 1.5, 2.0, 4.0 and 7.2 dB. The engine sits about 6 dB under the unit on the demo, median over the fifteen songs.

Against rgwan's 2026-09-22 take of the same demo with the effects and the filter stripped, the engine sits 2.8 dB under the unit in the median band, flat across the octaves: four songs at zero, Ana-Unison at -8. The effects are the largest single number in the set at 17 to 29 dB over eighty-four segments.

## KNOWN

- **Board.** SH7044 (HD64F7044F28, SH-2, 28 MHz, 256 KB internal flash) with a 2 MB M27C160 EPROM, 512 KB DRAM at 0x01000000, a 1 Mbit battery-backed NVRAM on CS1 (0x400000, user banks). Two YMP706-F tone generators at 0xC00000 and 0xC00400 (16 channels each, 8-bit write-only bus, PBUSY handshake). Two YSS236-F DSPs on CS2 with a 16-bit register block each, VOP3-2 (effects) at 0x800000 and VOP3-1 (the per-voice filter) at 0x800200, status at 0x800242. LED and LCD-contrast latch at 0x800100. Two Sanyo LC78834M 18-bit stereo DACs. Sources: the studiorepair.com FS1R gallery, rgwan's dumps, the firmware's address usage.
- **What is not on the bus.** 0x0140xxxx was listed for weeks as a second external device or NVRAM and is neither: the NVRAM is the 1 Mbit part on CS1, and 0x01000000-0x01FFFFFF is the SH7044's own DRAM area (rgwan, 2026-09-16). No code references the address, the EPROM words that read like pointers to it form no table, and anything that did reach it would alias onto 0x0100xxxx. `docs/research.md` 2.1.
- **Clocks and audio path** (rgwan, 2026-09-15, from the service manual and his board). One 24.576 MHz crystal runs the audio side: the main DAC is the I2S master at 512 Fs, exactly 48 kHz, and clocks the slave DAC, both VOP3s and both YMP706s. The CPU has its own 7 MHz crystal with the PLL at 4x, so the 28 MHz the SCI divisor implied is confirmed and the tick is not locked to the sample clock. FS1A's dry and send busses feed FS1B, which sums both chips and feeds VOP3-2; VOP3-2's SDO1 drives the main DAC and SDO3 the slave DAC, which is the INDIVIDUAL OUTPUT pair. VOP3-1 sits in a channel-level loop off both tone generators. The master volume is an analogue pot after the main DAC. Diagram and consequences in `docs/research.md` 2.0.1.
- **CS2 decode** (rgwan, 2026-09-16, from the schematic). Partial decoding on A8 and A9 alone: `VOP3_SELN = CS2N | A8`, `VOP3-1N = ~A9 | VOP3_SELN`, `VOP3-2N = A9 | VOP3_SELN`, `IC25_26_CLK = ~A8 | CS2N | WRL`. So VOP3-2 sits at 0x800000, VOP3-1 at 0x800200 and the 74HC374 pair at 0x800100, where IC26 drives LED0-7 off the low byte and IC25 drives CONTA/CONTB/CONTC off D8-10 into IC29, the LCD contrast DAC. Nothing above A9 is decoded, so each repeats every 0x400. IC31's WA/WD pins are open, so VOP3-1 has no external DRAM and cannot run a reverb. The firmware has a separate driver for each chip, `FUN_0000B5E2` and `FUN_00039648`; `docs/research.md` section 8.
- **The two VOP3s.** The YSS236 is Yamaha's "VOP3", the same programmable DSP that is the synthesis engine of the AN1x, AN200 and PLG150-AN and the vocal harmony processor of the PSR-9000. The FS1R has two, each with its own driver in the firmware and its own program uploaded at boot: VOP3-1 the filter at 0x800200 (FUN_0000BC8C, extracted into `docs/vop3_microcode.md` and `docs/vop3/` with the register each word goes to) and VOP3-2 the effects at 0x800000 (FUN_0003A064 and FUN_0003C9C4, images at EPROM 0x372204 and 0x373604). Both programs live in the EPROM, not in the chips.
- **Engine, CPU side.** All 88 algorithms, the DX7 conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1 and LFO2, portamento, mono modes with priorities and legato, note and velocity limits, part volume/expression/balance, pan, performances, Fseq frame timing and playback. The 192.3 Hz tick (MTU2), the Fseq frame timer (CMT1), the YMP706 register map and every conversion table. See `docs/ymp706_registers.md`, and, since 2026-09-18, the voice image checked byte for byte against a running unit.
- **Controller sets.** The whole matrix, read out of FUN_00014DDC, FUN_00014AD0 and FUN_000191C0: the source bit order, the bipolar and raw source conversions, the clamped sum, the three scalers and which of the 48 destinations uses each, the per-part gate, and the destinations that are not a plain offset (pitch bias, amplitude EG bias, frequency bias, bandwidth, the LFO depths, Fseq speed, formant scratch). `docs/ymp706_registers.md` has the tables; `tools/ghidra_ctrl_dests.py` regenerates the handler decompilations.
- **Data.** Voice (608 bytes), performance (400), Fseq and system bulk layouts, the whole sysex parameter map, the 1408 preset voices, 384 performances and 90 Fseqs in the EPROM, the DX7 VCED and ACED maps.
- **Effect parameter encoding.** Read back out of the preset performances and checked against every documented default: frequency index `20 * 2^(i/6)` Hz, gain `v - 64` dB, Q `v/10`, delay words in 0.1 ms, threshold `v - 127` dB, the XG LFO frequency and reverb time curves.

## INFERRED

- **YMP706 EG shape and rate scaling.** Measured 2026-09-19 off four envelope recordings: the rate law, the 1.5 dB level step and the 1.5 dB carrier correction step all stand, and the 8-bit level register is 0.376287 dB, a halving every sixteen steps. The attack's shape, its floor, the hold's length and the key rate scaling were all wrong and are now measured. `docs/aeg.md` is the working. What is left is the key code law between its ten measured points, three semitones apart, and the hold's fixed 8 ms lag, whose six-millisecond scatter is the 192.3 Hz tick rather than the chip. Both want register 0xC0 driven directly.
- **Modulation and sensitivity.** Per-op pitch and frequency modulation sensitivity scaling; feedback `0.5 * 2^(fb - 7)`. The modulation index is measured, 3.369 cycles at full level, off two sideband sweeps and confirmed by `03_fm`'s own spectra. Amplitude sensitivity is measured and disagrees with the model. Detune is settled and needs no constant at all: it is the EPROM's own key scaled `FRMDET`, measured over six octaves, and the engine reads the table rather than modelling it. See `docs/captures/capture_0918.md` and `docs/detune.md`.
- **Frequency EG range and timing.**
- **The formant window** (bandwidth and skirt) and the harmonic forms `all/odd/res`. Patent model, `docs/research.md` section 4. The noise formant chain came off this list on 2026-09-21 and was re-read on 2026-09-22: two digital one-poles with different coefficients, both measured against the register and the skirt on two files at three centres, the second pole's floor being what had read as a pedestal, and a two-sample mean on the unvoiced output. The resonance carrier's law against the register is the one thing left in it. `docs/noise.md`.
- **The per-voice filter's response.** The CPU side is read from the firmware, including the cutoff and both resonance conversions, so what is left is how VOP3-1 reads a coefficient. Measured 2026-09-21 by `15_filter`, the first recording with a source that can see a filter: the corner is `17.4 * 2^(byte / 12)`, 0.098 octaves rms over thirteen points, where the one-pole reading of the coefficient had put byte 64 at 3751 Hz against the unit's 713. The resonance runs to a 19.9 dB peak where the engine topped out at 8.5, and the feedback goes as the cube of resonance table A. `RESO_COMP` is measured at zero rather than guessed. The three lowpasses are taps off one 4-pole ladder, and HPF, BPF and BEF a 2-pole state-variable filter, which is what the unit measures: 24, 18 and 12 dB per octave on the lowpasses and 12 on the HPF, to a decibel. `docs/filter.md`.
- **The effect algorithms**, still modelled from the Data List. `09_effects` puts that at 17 to 29 dB, the largest single gap in the set.

## UNKNOWN

- **YMP706.** Which write starts a note (0xFC/FD is written before the registers load) and the exact response to every register above. No register documentation and no die shot; the chip is too rare for one. The pan path came off this list when FUN_00025BC8 and its siblings were read out, and the filter came off it for good: the firmware sends every filter parameter to VOP3-1 over the 0x800200 block, so this chip only switches the channel loop out to it.
- **YSS236 / VOP3.** The instruction set. VOP3-1's program and coefficient tables are extracted into `docs/vop3/`, but nobody has decoded the format, here or anywhere else, and it is a research project on the scale of a full DSP core. Three things about it are settled: the pinout is read, `docs/vop3_pinout.md` out of the AN200 service manual, which bounds the machine at 128 CPU registers, 8 serial inputs and 8 outputs and a bank of external DRAM per chip; the addressing is settled, so the upload already extracted is the filter chip's; and a filter is a smaller and more predictable thing to decode first than a reverb.
- **Firmware odds and ends.** What distinguishes VOP3-1's two program variants, since FUN_0000B010 loads variant 0 on the normal boot and FUN_003C2038, the diagnostic entry, loads variant 1 alongside VOP3-2's image 0. How the filter EG reaches the chip is settled (2026-09-23): the CPU steps it segment by segment through registers 0x2B to 0x2D and 0x21, on the VOP3-1 status interrupt, `FUN_0000D050` / `FUN_0000CB6C`. The 0x650000 access in the SCI transmit path is no longer on this list: rgwan confirmed it on hardware on 2026-09-19 as the NVRAM at offset 0x10000, aliased, since the 1 Mbit part decodes A0-A16 and 0x650000 minus 0x400000 is 0x250000, whose low 17 bits are 0x10000. A poke at 0x410000 reads back at 0x650000. It is the second user bank, not a device.
- **Hardware behaviour, chip side.** The CPU side of the note-on path is checked byte for byte, 240,476 checks over 802 segments with no mismatch, so what is left under this heading is how VOP3-1 reads a filter coefficient and what the effect blocks do. The register sessions that settled the CPU side are indexed in [docs/captures/sessions.md](docs/captures/sessions.md); their data is in a private companion repo. rgwan owns the unit and the digital-out board. The tap is the main DAC's I2S input, after both effect DSPs and before the analogue volume pot, so a recording's absolute level is comparable and the effect blocks are always in the path.

## Open work

Grouped by what blocks each item rather than by subsystem. This file is the list of record. The maintainers mirror it on a private project board, which follows this file rather than the other way round, so a change belongs here first.

**Ready, needs no hardware**

- The effects against hardware, 17 to 29 dB over three files and eighty-four segments. The largest number in the set
- The drum's tonal half, 2 to 3 dB hot on every hit and 4 dB in its carriers' band, with the modulation shallower than the unit's in the first 6 ms. Three things it needs are unmeasured, and `tools/make_capture_tonal.py` writes the files: the velocity law per sensitivity, a decay to a level that is not silence, and a modulator on a modulator
- The bass part's formants at fundamentals of 80 to 120 Hz, an octave out either side: the formant window family, at a note the capture set never played
- AM sensitivity at deep modulation. Three LFO depths recorded, the law is a saturation and not a scaling
- The "1" forms and all1/all2's geometry, data already recorded in `docs/skirt.md`
- The formant's window family, same data
- The silent-segment scoring artifact: `10_envelope2`'s two hold segments are silence on both sides scored as a 30 dB disagreement
- The constants that rest on one dataset. Score spectrum shape and envelope shape as well as level, and read every width in hertz as well as in partials

**Blocked on a hardware session**

- The chip's reading of the filter EG rate word, its depth scale, and LFO2's rate: `FEG_RATE_K`, `FEG_DEPTH_BYTES`, `LFO2_INC_K` in `cal.h`, all guesses. FS1R.unlock `capture3 flteg` / `lfo2` and `16_fltmod` (findings 2026-09-23)
- Whether the chip latches per grain or slews the level register on a 3 to 5 ms constant. The lever is the grain rate
- The per-channel arrays at 0x0103B384 and 0x01044C, which the voice image does not carry
- The effects, against a register session rather than against the Data List

**Open research**

- Run the real firmware on an SH-2 core. `tools/ghidra_ctrl_dests.py` shows the pattern for reaching code Ghidra never turned into functions
- VOP3 instruction set decode, then run the real program. The only route to bit-exact effects

**Parked**

- YMP706 emulation. Not possible without register semantics or a die, and the chip is too rare for a die shot. It stays a model
- Engine thread with a lookahead buffer, only if a host's buffer is ever the limit rather than the engine
- Skin files. Only worth it if a second panel ever needs skinning

**Reported**

- [Intel Macs are not supported](https://github.com/musicastudio/FSVR/issues/3)
- [Audio output is stuttery/choppy](https://github.com/musicastudio/FSVR/issues/1)
