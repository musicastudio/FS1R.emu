# FS1R.emu TODO

Goal: a virtual instrument (VST3/CLAP plugin plus standalone) whose GUI is the FS1R front panel, a per-synth engine library with a `synthLib::Device`-style interface (process a block of audio, take MIDI in, give MIDI out, get/set state as sysex), a console test harness on top of it, and a JUCE plugin layer that never models synthesis. The plugin only moves parameter values in and out of the engine as MIDI, exactly as a hardware editor would, and draws a skin (RML/RCSS plus PNG strips, one folder per skin) bound to a JSON parameter description list.

We rewrite the FS1R's control logic in C++ from the firmware and model the two custom chips, because nobody has register semantics for the YMP706 or an instruction set for the YSS236. See Tier 4 for where processor emulation still fits.

## What we know

The scope below rests on three lists. KNOWN is read from the firmware, its tables or the board. INFERRED is modelled from the DX7 lineage, the formant patent and the Data List, and marked INFERRED in `src/fs1r_emu.cpp`. UNKNOWN is what no file or photo tells us.

### KNOWN

- **Board.** SH7044 (HD64F7044F28, SH-2, 28 MHz, 256 KB internal flash) with a 2 MB M27C160 EPROM, 512 KB DRAM at 0x01000000, battery SRAM on CS1 (0x400000, user banks). Two YMP706-F tone generators at 0xC00000 and 0xC00400 (16 channels each, 8-bit write-only bus, PBUSY handshake). Two YSS236-F effect DSPs behind one 16-bit register block at 0x800200 (status at 0x800242). Panel and LED latch at 0x800100. Two Sanyo LC78834M 18-bit stereo DACs; rgwan's digital-out board measures the DAC clock at 48 kHz. Sources: the studiorepair.com FS1R gallery, rgwan's dumps, the firmware's address usage.
- **Effect DSP.** The YSS236 is Yamaha's "VOP3", the same programmable DSP that is the synthesis engine of the AN1x, AN200 and PLG150-AN and the vocal harmony processor of the PSR-9000. In the FS1R the CPU uploads its program and coefficients at boot (FUN_0000BC8C): register 0 selects an address (bit 15 selects a second space), registers 6-10 take five 16-bit words per step for 512 steps from EPROM 0x375E1A (alternate set 0x37721A), register 0xB one word per step from 0x37541A, register 0xC one byte per step from 0x375C1A, registers 0x16 and 0x24-0x2A fifteen per-bus values, register 1 reset, register 5 mode 0x1004. An effect type change (FUN_0000D050) mutes the block, patches program steps through FUN_0000B600 from a table indexed by type (0x374F2E, 0x378629-0x378642) and unmutes. So the effect algorithms live in about 6 KB of VOP3 microcode in the EPROM, not in the chip.
- **Engine, CPU side.** All 88 algorithms, the DX7 conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1, portamento, mono modes with priorities and legato, note and velocity limits, part volume/expression/balance, performances, controller sets, Fseq frame timing and playback. The 192.3 Hz tick (MTU2), the Fseq frame timer (CMT1), the YMP706 register map and every conversion table. See `docs/ymp706_registers.md`.
- **Data.** Voice (608 bytes), performance (400), Fseq and system bulk layouts, the 1408 preset voices, 360 performances and 90 Fseqs in the EPROM, the DX7 VCED to FS1R map.

### INFERRED

- YMP706 EG timing and shape, dB per level step (0.375 dB level register, 1.5 dB EG level), rate scaling.
- Per-op pitch, amplitude and frequency modulation sensitivity scaling; feedback `0.5 * 2^(fb - 7)`; modulation index one cycle at full level; 2 cents per detune step on non-formant ops.
- Frequency EG range and timing.
- The formant window (bandwidth and skirt), the harmonic forms `all/odd/res`, the noise formant chain. Patent model, see `docs/research.md` section 4.
- The per-voice filter response, the effect algorithms and the pan law, once implemented: modelled from the Data List, not from the chips.

### UNKNOWN

- **YMP706.** Which write starts a note (0xFC/FD is written before the registers load), what the chip does with the pan bytes 0x22A-0x22F and the pan tables, the filter registers, the exact response to every register above. No register documentation, no die shot, no MAME driver.
- **YSS236 / VOP3.** The instruction set. The 512 x 80-bit program and its coefficient tables are in the EPROM, but nobody has decoded the format; MAME's AN1x driver is a skeleton that maps the chip onto an unemulated stub. Decoding it is a research project of the size MAME spent on the MU100's MEG.
- **Firmware odds and ends.** FUN_0020315C, which selects between the two effect table variants (values 0-3); the 583 pointer words into 0x0140xxxx that no code references; the 0x650000 access in the SCI transmit path.
- **Hardware behaviour.** Nothing has been measured on a real unit yet: no recordings, no register captures. rgwan owns a unit and the digital-out board.

Tiers are ordered. Each tier is usable on its own; nothing in a later tier is needed to ship the one before it.

## Tier 0: finish the engine (missing firmware features)

The parts of `fs1r_emu.exe` that the README lists as not implemented or INFERRED. All of these live in `src/fs1r_emu.cpp`.

- [ ] **Per-voice filter.** Types LPF24/LPF18/LPF12/HPF/BPF/BEF, cutoff, resonance, filter EG (levels, times, velocity and key scaling), input gain, cutoff key and velocity scaling, part filter switch (register 0x270 = 11). Chip-side, so a model, marked INFERRED. Data List filter parameters and manual pages 62-70.
- [ ] **LFO2.** Filter only: speed, waveform, phase, filter depth. The firmware's tick already computes LFO1; LFO2 is the same stepper feeding the filter cutoff.
- [ ] **Pan.** The 0x22A-0x22F path: part pan, voice pan LFO depth, the four level bytes from the pan tables at 0x35C0EB/0x35C16B (extract with `tools/extract_tables.py`), performance output routing. Ends the mono output.
- [ ] **Effects.** The 112 effect bytes per performance: reverb (17 types), variation (29 types), insertion, EQ, the send levels (SENDTAB). Model each type from the Effect Parameter List in `docs/FS1R_DataList_text.txt`; they are Yamaha's XG-era algorithms and their parameters are fully documented. The firmware's parameter-to-coefficient conversions (FUN_0000C36C to FUN_0000C6C0, the tables at 0x3750xx-0x3786xx) give the exact parameter scaling even though the algorithms themselves are VOP3 microcode.
- [ ] **Fseq gaps.** Scratch mode (formant sequence position from a controller), MIDI clock sync of the frame timer, loop modes, Fseq sysex bulk load and the Fseq voice and level knobs.
- [ ] **Remaining MIDI.** RPN 0-2 (bend range, fine and coarse tune), bank select, sysex dump requests answered (so an editor or the plugin can read state back), system sysex, MIDI clock and active sensing.
- [ ] **Voice edit coverage.** Check every parameter in the voice, performance and part sysex tables against the parameter-change handlers; anything that reaches the engine only through a bulk dump should also work as a single parameter change.
- [ ] **Confirm the INFERRED constants.** EG rate timing and shape, dB per level step, per-op modulation sensitivity scaling, feedback and modulation index, formant window and noise formant. Needs recordings of a real unit (Tier 4); until then keep them as named constants in one place so calibration is a table edit.

## Tier 1: engine library and test console

Restructure without changing behaviour so the plugin has something to link.

- [ ] **Split the source.** `src/fs1r_emu.cpp` becomes `fs1rLib` (decoders, note and tick pipeline, operator engine, effects, no Windows calls) plus `fs1rTestConsole` (the current command line: WinMM MIDI in, waveOut, offline render). Same exe name and options as today.
- [ ] **Device interface.** One class with `process(audioOut, numSamples, midiIn, midiOut)`, `sendMidi(bytes)`, `getState()/setState()` as sysex bulk dumps (system + performance + 4 voices + Fseq), a fixed internal sample rate (the hardware's 48 kHz) and a resampler to the host rate. Real-time safe: no allocation in `process`.
- [ ] **MIDI out.** Answer dump requests and echo parameter changes on the engine's MIDI output. This is how the GUI learns the state; the plugin never reads engine internals.
- [ ] **CMake build** next to `build.bat`, MSVC and clang, so JUCE can be added as a submodule later.
- [ ] **Render regression.** `tools/check_wav.py` over a fixed preset list and a stored reference of pitch, harmonics and envelope numbers, run after every engine change. One script, no framework.

## Tier 2: plugin (JUCE)

`jucePluginLib` layer: the plugin is a controller for the engine.

- [ ] **JUCE as a submodule**, CMake targets for VST3, CLAP (clap-juce-extensions) and standalone. Windows first; macOS AU and Linux LV2 once builds exist.
- [ ] **Parameter descriptions.** A JSON list like `parameterDescriptions_*.json` (name, min, max, default, bipolar, discrete, value-to-text) generated by a script from the Data List tables in `docs/FS1R_DataList_text.txt`: voice common (112), 8 operators x 62, performance common, 4 parts, system. Each entry carries its sysex address so the plugin can send it.
- [ ] **Parameter binding.** Every host-automatable parameter is a sysex parameter change into the engine; engine echoes drive the controls back. Host state is the engine's bulk dump.
- [ ] **Patch manager.** Load and save `.syx` voices, performances and Fseqs, the 1408 ROM presets in `presets/`, DX7 VCED/ACED files (the firmware's converter already exists). Browser with bank and category.
- [ ] **Multi-part.** Four parts with receive channels, program change and bank select, one stereo out (individual outs later if wanted).
- [ ] **MIDI learn** for CC to parameter, and the FS1R's own controller sets exposed as they are.

## Tier 3: GUI, the FS1R panel

Skin the plugin as the hardware: a 1U rack front panel with a 2x40 LCD, the four knobs, part buttons, edit, play and utility buttons and the data dial. The panel alone cannot reach 600 parameters comfortably, so the panel is reproduced 1:1 and expanded editor pages in the same style sit under it, switched with tabs.

- [ ] **Assets.** Photograph or measure a real panel; draw the background, knob strips, button states, LED states and the LCD font as PNG. Fonts for the LCD (DSEG-style) and panel labels.
- [ ] **Skin files.** RML/RCSS layout plus a JSON style sheet per skin, one folder, assets listed for embedding (`addSkin` / `assets.cmake` pattern).
- [ ] **LCD.** The 2x40 display shows the engine's play and edit screens using our own strings in the FS1R layout (patch name, part, category, edited parameter and value). Not the firmware's menu system.
- [ ] **Editor pages.** Operator grid (8 voiced + 8 unvoiced: form, frequency, EG, level scaling), algorithm diagram for the 88 algorithms from `src/fs1r_algorithms.h`, formant and noise pages with bandwidth and skirt curves, LFO and pitch EG, filter, performance and part pages, effects, Fseq display with the frame data.
- [ ] **Knobs and controller sets.** The four panel knobs drive the performance's knob assignments exactly as on the hardware.

## Tier 4: fidelity and verification

- [ ] **Hardware recordings.** Record a fixed note set from a real FS1R (rgwan has one and a bit-exact digital-out board; the yamahamusicians thread is the contact) and compare with `tools/check_wav.py` renders. Calibrate the INFERRED constants against them.
- [ ] **Run the real firmware on an SH-2 core.** gearmulator has an SH-2 core with the SH7040-family peripherals (`source/cpu/sh2`: MTU, CMT, SCI, INTC, BSC, DMAC, ports) used by their 88emu. Running the SH7044 flash and the EPROM on it and logging YMP706 and YSS236 register writes gives a reference for every register value our rewrite computes, and finds events the port missed. Research strategy 3 in `docs/research.md`. This verifies the CPU side; it does not replace it.
- [ ] **Extract the VOP3 microcode.** Pull the program (0x375E1A, 0x37721A), coefficient tables (0x37541A, 0x375C1A, 0x37501A) and the per-type patch tables (0x374F2E, 0x378629-0x37863B, 0x37861A) out of the EPROM with `tools/extract_tables.py` into `docs/`, with the register each word goes to. Reference material only; nothing in the plugin depends on it.
- [ ] **VOP3 emulation (stretch).** Decode the instruction set from the microcode plus hardware recordings of each effect type, then run the real program. This is the only route to bit-exact effects and it is open research; the modelled effects from Tier 0 ship regardless.
- [ ] **YMP706.** No emulation is possible without register semantics or a die. It stays a model. Revisit if the MAME driver rgwan is working on publishes anything.

## Tier 5: release

- [ ] **Licence file.** None in the repo yet. GPL-3 matches gearmulator and JUCE's GPL terms. Note that `presets/` and the ROM tables in `src/` are extracted from Yamaha's EPROM.
- [ ] **CI.** GitHub Actions build of the console exe and the plugin per push, zip artifacts, tagged releases behind the README download link.
- [ ] **Docs.** Keep `docs/ymp706_registers.md` as the engine reference; add a short plugin user guide when the GUI ships.
