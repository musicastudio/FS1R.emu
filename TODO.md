# FS1R.emu TODO

Goal: a virtual instrument (VST3/CLAP plus standalone) whose GUI is the FS1R front panel, a per-synth engine library with a `synthLib::Device`-style interface (process a block of audio, take MIDI in, give MIDI out, get/set state as sysex), a console test harness on top of it, and a JUCE plugin layer that never models synthesis. The plugin only moves parameter values in and out of the engine as MIDI, exactly as a hardware editor would, and draws a skin bound to a JSON parameter description list.

We rewrite the FS1R's control logic in C++ from the firmware and model the two custom chips, because nobody has register semantics for the YMP706 or an instruction set for the YSS236. See Tier 4 for where processor emulation still fits.

Tiers 0 to 3 and 5 are done. What is left is in Tier 4, and all of it waits on hardware or on research nobody has finished: recordings of a real unit to calibrate against, an SH-2 core to verify the rewrite, and a decode of the VOP3 instruction set. Tier 3's remaining line is artwork, not code.

## What we know

The scope below rests on three lists. KNOWN is read from the firmware, its tables or the board. INFERRED is modelled from the DX7 lineage, the formant patent and the Data List, and gathered in `namespace cal` in `src/fs1r_lib.cpp`. UNKNOWN is what no file or photo tells us.

### KNOWN

- **Board.** SH7044 (HD64F7044F28, SH-2, 28 MHz, 256 KB internal flash) with a 2 MB M27C160 EPROM, 512 KB DRAM at 0x01000000, battery SRAM on CS1 (0x400000, user banks). Two YMP706-F tone generators at 0xC00000 and 0xC00400 (16 channels each, 8-bit write-only bus, PBUSY handshake). Two YSS236-F effect DSPs behind one 16-bit register block at 0x800200 (status at 0x800242). Panel and LED latch at 0x800100. Two Sanyo LC78834M 18-bit stereo DACs; rgwan's digital-out board measures the DAC clock at 48 kHz. Sources: the studiorepair.com FS1R gallery, rgwan's dumps, the firmware's address usage.
- **Effect DSP.** The YSS236 is Yamaha's "VOP3", the same programmable DSP that is the synthesis engine of the AN1x, AN200 and PLG150-AN and the vocal harmony processor of the PSR-9000. In the FS1R the CPU uploads its program and coefficients at boot (FUN_0000BC8C). The whole upload is now extracted: see `docs/vop3_microcode.md` and `docs/vop3/`, with the register each word goes to. The algorithms live in about 6 KB of VOP3 microcode in the EPROM, not in the chip.
- **Engine, CPU side.** All 88 algorithms, the DX7 conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1 and LFO2, portamento, mono modes with priorities and legato, note and velocity limits, part volume/expression/balance, pan, performances, controller sets, the voice Formant and FM control matrix, Fseq frame timing and playback. The 192.3 Hz tick (MTU2), the Fseq frame timer (CMT1), the YMP706 register map and every conversion table. See `docs/ymp706_registers.md`.
- **Data.** Voice (608 bytes), performance (400), Fseq and system bulk layouts, the whole sysex parameter map, the 1408 preset voices, 360 performances and 90 Fseqs in the EPROM, the DX7 VCED and ACED maps.
- **Effect parameter encoding.** Read back out of the 360 preset performances and checked against every documented default: frequency index `20 * 2^(i/6)` Hz, gain `v - 64` dB, Q `v/10`, delay words in 0.1 ms, threshold `v - 127` dB, the XG LFO frequency and reverb time curves.

### INFERRED

Every chip-side constant is now in one place, `namespace cal` in `src/fs1r_lib.cpp`, so calibrating against a recording is a table edit.

- YMP706 EG timing and shape, dB per level step (0.375 dB level register, 1.5 dB EG level), rate scaling.
- Per-op pitch, amplitude and frequency modulation sensitivity scaling; feedback `0.5 * 2^(fb - 7)`; modulation index one cycle at full level; 2 cents per detune step on non-formant ops.
- Frequency EG range and timing.
- The formant window (bandwidth and skirt), the harmonic forms `all/odd/res`, the noise formant chain. Patent model, see `docs/research.md` section 4.
- The per-voice filter response and the effect algorithms: modelled from the Data List, not from the chips.

### UNKNOWN

- **YMP706.** Which write starts a note (0xFC/FD is written before the registers load), the filter registers, the exact response to every register above. No register documentation, no die shot, no MAME driver. The pan path is no longer on this list: FUN_00025BC8 and its siblings read it out.
- **YSS236 / VOP3.** The instruction set. The program and its coefficient tables are extracted into `docs/vop3/`, but nobody has decoded the format; MAME's AN1x driver is a skeleton that maps the chip onto an unemulated stub. Decoding it is a research project of the size MAME spent on the MU100's MEG.
- **Firmware odds and ends.** FUN_0020315C, which selects between the two effect program variants (values 0-3); the 583 pointer words into 0x0140xxxx that no code references; the 0x650000 access in the SCI transmit path.
- **Hardware behaviour.** Nothing has been measured on a real unit yet: no recordings, no register captures. rgwan owns a unit and the digital-out board.

## Tier 0: finish the engine (missing firmware features)

- [x] **Per-voice filter.** LPF24/LPF18/LPF12/HPF/BPF/BEF, cutoff, resonance, filter EG, velocity and key scaling, input gain, cutoff key and velocity scaling, the part filter switch. Chip-side, so a model, marked INFERRED. Coefficients are refreshed on the 192.3 Hz tick, which is when the CPU would write them.
- [x] **LFO2.** Filter only: speed, waveform, phase, key sync, filter depth, with the part offset and both controller destinations.
- [x] **Pan.** The 0x22A-0x22F path read out of the firmware: part pan including random, key-position pan scaling, the LFO1 pan depth, performance pan and the Panpot controller, through the EPROM's own pan tables at 0.375 dB per step. Ends the mono output.
- [x] **Effects.** Reverb (17 types), variation (29), insertion (41) and the master EQ in the XG topology, with the part dry/send levels and the insertion switch. `src/fs1r_effects.h`; `tools/test_effects.cpp` checks the decoding and sweeps every type.
- [x] **Fseq gaps.** Scratch mode from controller destination 47, MIDI clock sync, oneway and round loop modes with the direction the loop points imply, the start offset, the performance start delay, bulk load and the voice Fseq switches.
- [x] **Remaining MIDI.** RPN 0-2, the ten NRPN 01 xx part parameters, bank select and program change in both modes, dump requests answered with checksummed bulk dumps, parameter requests answered, the full 76-byte system block, MIDI clock, active sensing, mono/poly.
- [x] **Voice edit coverage.** Every address in tables 1 to 4 is writable as a single parameter change, including the 112 effect bytes and the Fseq header. The voice's Formant and FM control matrix was the last unimplemented block. `fs1r_emu -selftest` checks each address class.
- [ ] **Confirm the INFERRED constants.** Needs recordings of a real unit (Tier 4). Until then they are named constants in `namespace cal`, so calibration is one table edit.

## Tier 1: engine library and test console

- [x] **Split the source.** `src/fs1r_lib.{h,cpp}` (the engine, no Windows, no host, no GUI) plus `src/fs1r_console.cpp` (WinMM MIDI in and out, waveOut, offline render). Same exe name and options.
- [x] **Device interface.** `fs1r::Device`: `process` into float buffers, `sendMidi`, `nextMidiOut`, `getState`/`setState` as bulk dumps. The engine runs at the hardware's 48 kHz whatever the host rate, and resamples on the way out.
- [x] **MIDI out.** Dump and parameter requests answered, parameter changes echoed. The console's `-o` opens a MIDI out for them.
- [x] **CMake build** next to `build.bat`, with ctest running both self checks.
- [x] **Render regression.** `tools/regress.py` over twelve fixed cases against `tools/regress_ref.json`: pitch, harmonic peaks, envelope, stereo width, centroid.

## Tier 2: plugin (JUCE)

- [x] **JUCE as a submodule** with clap-juce-extensions; VST3, CLAP and standalone build from `-DFS1R_BUILD_PLUGIN=ON`.
- [x] **Parameter descriptions.** `tools/gen_parameters.py` generates `plugin/parameterDescriptions_fs1r.json`: 893 parameters with their sysex addresses, the packed-byte bit layouts, the 14-bit pairs and the runs the printed tables abbreviate. Part, voice and operator blocks are described once and marked partRelative.
- [x] **Parameter binding.** Every parameter change is a sysex parameter change into the engine; the engine's echo moves the controls. Host state is the engine's bulk dump.
- [x] **Patch manager.** `.syx` voices, performances and Fseqs, the EPROM banks, DX7 VCED and ACED, save as one `.syx`. The browser reads `presets/index.csv` for names and categories when it is there.
- [x] **Multi-part.** Four parts with receive channels, program change and bank select, one stereo out.
- [x] **MIDI learn** for CC to parameter on the right-click menu, alongside the FS1R's own controller sets.

## Tier 3: GUI, the FS1R panel

- [ ] **Assets.** Photograph or measure a real panel; draw the background, knob strips, button states, LED states and the LCD font as PNG. **The one thing left that is not code.** The panel is currently drawn as vectors from the owner's manual's front panel description, which needs no assets and survives resizing; a photographed skin would replace the paint methods, not the wiring.
- [ ] **Skin files.** RML/RCSS layout plus a JSON style sheet per skin, assets listed for embedding. Waits on the assets above; adding RmlUi before there is anything to skin buys nothing.
- [x] **LCD.** A real 2x40 dot-matrix display drawn from a 5x7 font, showing our own strings in the FS1R layout: the play screen, and the edited parameter and value for two seconds after a touch.
- [x] **Editor pages.** Eleven pages switched with tabs and reachable from the panel's mode buttons: operators, unvoiced, algorithm (the diagram for all 88, straight from the connection table), formant with the spectral form drawn, LFO and pitch EG, filter, performance, part, effects, Fseq with the eight voiced tracks and the playback position, system, and a searchable flat list.
- [x] **Knobs and controller sets.** The four panel knobs drive the part's tone offsets in TONE mode and the system's KN1-4 control numbers in the other, so the value travels through the performance's control matrix exactly as on the hardware.

## Tier 4: fidelity and verification

- [ ] **Hardware recordings.** Record a fixed note set from a real FS1R (rgwan has one and a bit-exact digital-out board; the yamahamusicians thread is the contact) and compare with `tools/regress.py` renders. Calibrate `namespace cal` against them. **Blocked: nobody here has a unit.**
- [ ] **Run the real firmware on an SH-2 core.** gearmulator has an SH-2 core with the SH7040-family peripherals (`source/cpu/sh2`: MTU, CMT, SCI, INTC, BSC, DMAC, ports) used by their 88emu. Running the SH7044 flash and the EPROM on it and logging YMP706 and YSS236 register writes gives a reference for every register value our rewrite computes, and finds events the port missed. Research strategy 3 in `docs/research.md`. This verifies the CPU side; it does not replace it.
- [x] **Extract the VOP3 microcode.** `tools/extract_vop3.py` into `docs/vop3_microcode.md` and `docs/vop3/`, with the register each word goes to, read from the boot upload rather than guessed. Reference material only; nothing depends on it.
- [ ] **VOP3 emulation (stretch).** Decode the instruction set from the microcode plus hardware recordings of each effect type, then run the real program. The only route to bit-exact effects and open research; the modelled effects ship regardless.
- [ ] **YMP706.** No emulation is possible without register semantics or a die. It stays a model. Revisit if the MAME driver rgwan is working on publishes anything.

## Tier 5: release

- [x] **Licence file.** GPL-3 in `LICENSE`; `NOTICE.md` separates our own work from the tables and presets extracted from Yamaha's EPROM and names the tool that regenerates each.
- [x] **CI.** GitHub Actions builds the engine and its self checks on Windows, Linux and macOS, builds all three plugin formats on each, and attaches zipped artifacts to tagged releases.
- [x] **Docs.** `docs/ymp706_registers.md` stays the engine reference; `docs/plugin_guide.md` is the plugin user guide; `docs/vop3_microcode.md` is the effect DSP reference.
