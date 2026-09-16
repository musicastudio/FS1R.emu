# FS1R.emu TODO

Goal: a virtual instrument (VST3/CLAP plus standalone) whose GUI is the FS1R front panel, a per-synth engine library with a `synthLib::Device`-style interface (process a block of audio, take MIDI in, give MIDI out, get/set state as sysex), a console test harness on top of it, and a JUCE plugin layer that never models synthesis. The plugin only moves parameter values in and out of the engine as MIDI, exactly as a hardware editor would, and draws a skin bound to a JSON parameter description list.

We rewrite the FS1R's control logic in C++ from the firmware and model the two custom chips, because nobody has register semantics for the YMP706 or an instruction set for the YSS236. See Tier 4 for where processor emulation still fits.

Tiers 0 to 3 and 5 are done, and the CPU-side spots that were read from the firmware but not certain have now been settled from the disassembly (Tier 0, "confirm what was assumed"). What is left is in Tier 4, and all of it waits on hardware or on research nobody has finished: recordings of a real unit to calibrate the chip models against, an SH-2 core to verify the rewrite, and a decode of the VOP3 instruction set. Tier 3's remaining line is artwork, not code.

## What we know

The scope below rests on three lists. KNOWN is read from the firmware, its tables or the board. INFERRED is modelled from the DX7 lineage, the formant patent and the Data List, and gathered in `namespace cal` in `src/fs1r_lib.cpp`. UNKNOWN is what no file or photo tells us.

### KNOWN

- **Board.** SH7044 (HD64F7044F28, SH-2, 28 MHz, 256 KB internal flash) with a 2 MB M27C160 EPROM, 512 KB DRAM at 0x01000000, battery SRAM on CS1 (0x400000, user banks). Two YMP706-F tone generators at 0xC00000 and 0xC00400 (16 channels each, 8-bit write-only bus, PBUSY handshake). Two YSS236-F effect DSPs behind one 16-bit register block at 0x800200 (status at 0x800242). Panel and LED latch at 0x800100. Two Sanyo LC78834M 18-bit stereo DACs; rgwan's digital-out board measures the DAC clock at 48 kHz. Sources: the studiorepair.com FS1R gallery, rgwan's dumps, the firmware's address usage.
- **Effect DSP.** The YSS236 is Yamaha's "VOP3", the same programmable DSP that is the synthesis engine of the AN1x, AN200 and PLG150-AN and the vocal harmony processor of the PSR-9000. In the FS1R the CPU uploads its program and coefficients at boot (FUN_0000BC8C). The whole upload is now extracted: see `docs/vop3_microcode.md` and `docs/vop3/`, with the register each word goes to. The algorithms live in about 6 KB of VOP3 microcode in the EPROM, not in the chip.
- **Engine, CPU side.** All 88 algorithms, the DX7 conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1 and LFO2, portamento, mono modes with priorities and legato, note and velocity limits, part volume/expression/balance, pan, performances, Fseq frame timing and playback. The 192.3 Hz tick (MTU2), the Fseq frame timer (CMT1), the YMP706 register map and every conversion table. See `docs/ymp706_registers.md`.
- **Controller sets.** The whole matrix, read out of FUN_00014DDC, FUN_00014AD0 and FUN_000191C0: the source bit order, the bipolar and raw source conversions, the clamped sum, the three scalers and which of the 48 destinations uses each, the per-part gate, and the destinations that are not a plain offset (pitch bias, amplitude EG bias, frequency bias, bandwidth, the LFO depths, Fseq speed, formant scratch). `docs/ymp706_registers.md` has the tables; `tools/ghidra_ctrl_dests.py` regenerates the handler decompilations.
- **Data.** Voice (608 bytes), performance (400), Fseq and system bulk layouts, the whole sysex parameter map, the 1408 preset voices, 384 performances and 90 Fseqs in the EPROM, the DX7 VCED and ACED maps.
- **Effect parameter encoding.** Read back out of the preset performances and checked against every documented default: frequency index `20 * 2^(i/6)` Hz, gain `v - 64` dB, Q `v/10`, delay words in 0.1 ms, threshold `v - 127` dB, the XG LFO frequency and reverb time curves.

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
- **Hardware behaviour.** No register captures, and no recordings of the capture set. There is now one piece of real audio: rgwan's digital recording of the built-in demo, `captures/FS1R DEMO.flac`, which the unit plays from its own EPROM. That fixes the timing and catches gross errors but measures nothing on its own, because the demo never holds a note still. rgwan owns the unit and the digital-out board.

## Tier 0: finish the engine (missing firmware features)

- [x] **Per-voice filter.** LPF24/LPF18/LPF12/HPF/BPF/BEF, cutoff, resonance, filter EG, velocity and key scaling, input gain, cutoff key and velocity scaling, the part filter switch. Chip-side, so a model, marked INFERRED. Coefficients are refreshed on the 192.3 Hz tick, which is when the CPU would write them.
- [x] **LFO2.** Filter only: speed, waveform, phase, key sync, filter depth, with the part offset and both controller destinations.
- [x] **Pan.** The 0x22A-0x22F path read out of the firmware: part pan including random, key-position pan scaling, the LFO1 pan depth, performance pan and the Panpot controller, through the EPROM's own pan tables at 0.375 dB per step. Ends the mono output.
- [x] **Effects.** Reverb (17 types), variation (29), insertion (41) and the master EQ in the XG topology, with the part dry/send levels and the insertion switch. `src/fs1r_effects.h`; `tools/test_effects.cpp` checks the decoding and sweeps every type.
- [x] **Fseq gaps.** Scratch mode from controller destination 47, MIDI clock sync, oneway and round loop modes with the direction the loop points imply, the start offset, the performance start delay, bulk load and the voice Fseq switches.
- [x] **Remaining MIDI.** RPN 0-2, the ten NRPN 01 xx part parameters, bank select and program change in both modes, dump requests answered with checksummed bulk dumps, parameter requests answered, the full 76-byte system block, MIDI clock, active sensing, mono/poly.
- [x] **Voice edit coverage.** Every address in tables 1 to 4 is writable as a single parameter change, including the 112 effect bytes and the Fseq header. The voice's Formant and FM control matrix was the last unimplemented block. `fs1r_emu -selftest` checks each address class.
- [ ] **Confirm the INFERRED constants.** Needs recordings of a real unit (Tier 4). Until then they are named constants in `namespace cal`, so calibration is one table edit. This is now the only item in tier 0, and it is the only one that cannot be done from the firmware.
- [x] **Settle what was read but not certain.** Five CPU-side spots were marked "assumed" because the firmware had been read but not far enough. All are now traced and corrected:
  - **The controller matrix** was wrong in five ways at once: the source bit order (PB, CAT and PAT sit at bits 6-8, with FC, BC, MC3, MW and MC4 after them, not at the end), the source values (the knobs and MIDI controls are bipolar `(v - 64) * 2`, not raw), the gain (`>> 6` with a bias on each positive operand, not `>> 6` of the raw product), the missing clamps, and the assumption of a single uniform scaler where the firmware has three and picks by destination. Destinations 17-33 edit a part byte rather than contributing an offset, exactly as the manual's "overwrites the edit buffer" note says.
  - **Frequency bias** (destination 36) was routed but never applied: `(int16)(scaled * sense * 0x10) >> 2` on the operator's frequency word.
  - **The part EG offsets** reach T1, T2, **T3** and T4, not T1, T2 and T4: FUN_00019414 walks the part's bytes 0x1A, 0x1B, 0x1B, 0x1C. The hold rate takes its +4 only when it is below 0x3F.
  - **The Fseq pitch offset** is confirmed exact, constant for constant, against FUN_000127FC. The formant tracking term that used to sit alongside it was an invention and is gone: nothing in the firmware shifts a frame's formant frequencies, which is the point of the format.
  - **MIDI clock** weights each tick by 25, 50, 100, 200 or 400 for speed words 0 to 4 (FUN_0001ACB6), so the 1/4 to 4/1 ratios were right; the free-running `VELW[adjust] * 84 * 1000 / ratio + 2884` is confirmed instruction for instruction.
- [x] **Check the formant operator against the firmware's own formula.** `tools/check_formant.py` builds a voice with one formant operator at a known setting, works out where TRANS, FRMDET and the ratio tables say that formant belongs, and measures where it lands across three octaves. The fundamental tracks the key to within 0.25 %, the formant stays put while it does, and transposing a 800 Hz formant up twelve semitones lands on the 1.6 kHz setting exactly. The tolerance widens with f0 / formant because a formant is only locatable to about half the harmonic spacing. This checks the frequency, which is read from the firmware; the window shape around it is still a model from the patent.
- [x] **Cover it in the regression.** The suite could not see any of the above: it never moved a controller and always played the Fseq's own assigned note. `-cc num=val` sends control changes before an offline render, and seven cases now bracket the controller matrix and the Fseq away from centre. The self check asserts the three scalers, the source conversions, the part gate and the global destinations.

## Tier 1: engine library and test console

- [x] **Split the source.** `src/fs1r_lib.{h,cpp}` (the engine, no Windows, no host, no GUI) plus `src/fs1r_console.cpp` (WinMM MIDI in and out, waveOut, offline render). Same exe name and options.
- [x] **Device interface.** `fs1r::Device`: `process` into float buffers, `sendMidi`, `nextMidiOut`, `getState`/`setState` as bulk dumps. The engine runs at the hardware's 48 kHz whatever the host rate, and resamples on the way out.
- [x] **MIDI out.** Dump and parameter requests answered, parameter changes echoed. The console's `-o` opens a MIDI out for them.
- [x] **CMake build** next to `build.bat`, with ctest running both self checks.
- [x] **Render regression.** `tools/regress.py` over twelve fixed cases against `tools/regress_ref.json`: pitch, harmonic peaks, envelope, stereo width, centroid.
- [x] **Engine cost.** The per-operator frequency and level maths (a dozen `pow()` calls per operator per sample) now runs every 16 samples in `refresh_ctl`, the per-sample dB-to-linear path is a table, and an operator's own pitch EG stays per sample while it moves. One note on the four-part "Everybody" went from 30% of a core to 9%; 16 notes from 254% to 61%. The next step if 32-voice loads still glitch is the sine and window lookups (`floor()` in `fsin`), not the buffer.

## Tier 2: plugin (JUCE)

- [x] **JUCE as a submodule** with clap-juce-extensions; VST3, CLAP and standalone build from `-DFS1R_BUILD_PLUGIN=ON`.
- [x] **Parameter descriptions.** `tools/gen_parameters.py` generates `plugin/parameterDescriptions_fs1r.json`: 893 parameters with their sysex addresses, the packed-byte bit layouts, the 14-bit pairs and the runs the printed tables abbreviate. Part, voice and operator blocks are described once and marked partRelative.
- [x] **Parameter binding.** Every parameter change is a sysex parameter change into the engine; the engine's echo moves the controls. Host state is the engine's bulk dump.
- [x] **Patch manager.** The 1408 factory voices, the 384 factory performances and the 90 preset formant sequences are packed into the binary by `tools/make_presets_blob.py`, so the browser works with no EPROM image: category, bank and voice as the hardware's [SEARCH] does, and a performance loads its four part voices and the Fseq it plays. An EPROM image still loads and is read instead. `.syx` import adds a file's voices as a browsable Usr bank; save writes the whole device as one `.syx`.
- [x] **Multi-part.** Four parts with receive channels, program change and bank select, one stereo out.
- [x] **MIDI learn** for CC to parameter on the right-click menu, alongside the FS1R's own controller sets.
- [ ] **Engine thread with a lookahead buffer**, if the host's buffer is ever the limit rather than the engine: JUCE's shared-mode WASAPI is fixed at the driver period (10 ms) and only exclusive mode and DirectSound offer other sizes (see the plugin guide). A ring buffer between an engine thread and `processBlock` with a user-set depth would make the size a plugin setting in every host, at the cost of that much latency. Not needed since the engine cost drop above.

## Tier 3: GUI, the FS1R panel

- [x] **Assets.** The panel is the owner's manual's own front panel drawing: page 14 of `docs/FS1RE1.pdf` converted to vectors, stripped of the conversion's leftover clip paths by `tools/make_panel_svg.py` and embedded as `plugin/fs1r_panel.svg`. `PanelView.cpp` places every live control over it in the drawing's own user units, rotates the drawing's own knob caps, and lights its LED wells; `tools/check_panel.py` reads the shapes back out of the SVG and fails `build.bat test` if the two ever drift apart. It needs no bitmaps and survives resizing.
- [ ] **Skin files.** RML/RCSS layout plus a JSON style sheet per skin, assets listed for embedding. Only worth it if a second panel ever needs skinning; one SVG plus a coordinate table is less machinery than a skin engine.
- [x] **LCD.** The graphic display on a 120x37 dot grid: two text lines in a 5x7 font, then the icon strip along the bottom lined up with the PART MIDI BANK/PGM# VOL FLT PAN REV VAR INS KEY silkscreen, with the selected field framed and marked by the triangular pointer the owner's manual describes on page 22. CURSOR walks the strip, VALUE edits it, and a parameter touched on any editor page shows its name and value for two seconds.
- [x] **Editor pages.** Eleven pages switched with tabs and reachable from the panel's mode buttons: operators, unvoiced, algorithm (the diagram for all 88, straight from the connection table), formant with the spectral form drawn, LFO and pitch EG, filter, performance, part, effects, Fseq with the eight voiced tracks and the playback position, system, and a searchable flat list.
- [x] **Knobs and controller sets.** The two knob mode buttons give the four knobs the hardware's three states (owner's manual page 15): upper lit drives the part's ATTACK, RELEASE, FORMANT and FM offsets, lower lit sends the system's KN1-4 control numbers so the value travels through the performance's control matrix, and both dark makes them part/op, group, cursor and value.

## Tier 4: fidelity and verification

- [ ] **Hardware recordings.** Calibrate `namespace cal` against a real FS1R. **The capture kit is built and waiting on the recordings**: rgwan has offered to record them off his unit and its digital-out board.
  - `tools/make_capture_set.py` writes `captures/requests/`: 21 Standard MIDI Files, 576 segments, 46 minutes, each file setting up its own patches by sysex and opening and closing with a marker so a recording aligns itself. `captures/requests/README.md` lists what each file settles; `docs/hardware_capture_request.md` is the note that went with them, and also asks for a logic analyzer capture of the YMP706 bus and answers about the board.
  - `tools/analyze_capture.py` aligns a recording to the manifest, measures every segment, fits the constants it can, and diffs the result against our own render of the same file. `fs1r_emu -smf file.mid -w out.wav` renders a request file through the engine, which is how the comparison is made and how the patches were checked before anyone was asked to record them.
  - The whole loop is tested against engine renders: it recovers LEVEL_DB as 0.374 where the engine's value is 0.375, DETUNE_CENTS as 2.004 where it is 2.0, and the EG rate table across 37 rates to within a few percent. The accuracy limit will be the hardware, not the method.
- [ ] **The demo song, end to end.** The FS1R plays 15 demo songs out of its own EPROM, so the same byte stream drives the hardware and the engine and no capture rig is needed. `tools/extract_demo.py` decodes the player's event format (flash 0x0002E8B8) into `captures/demo/NN_Name.mid`, and `tools/check_demo.py` finds each render inside rgwan's recording of the whole demo and reports the alignment, the envelope correlation and the per-octave level difference. First pass: all 15 line up in order with 1 to 5 s of reload pause between them, envelope correlation 0.72 to 0.96, and the engine sits about 5 dB low overall with a high-band tilt. Three results are defects, not calibration:
  - **Song 10 "Accordion" is silent** in our render, peak 0.005 against a full-level original. Something in that performance produces nothing.
  - **Songs 4, 6 and 9 lose 20 to 54 dB above 5 kHz**, which the other twelve do not, so it is a patch feature and not a global filter error.
  - **Song 1 peaks at 0.98** and is the loudest by a factor of three, which is either the gain staging or a missing part attenuation.

- [ ] **Two defects in the effects model**, found by driving all 87 types with their factory parameter sets (`captures/engine/09_effects_*.wav`): variation type 15 and insertion type 7 produce silence, and one variation type runs away until it is pinned against the limiter. The engine no longer emits NaN or silences itself when that happens (a runaway block is cleared and muted for that sample rather than poisoning the master EQ), but the types themselves are still wrong.
- [ ] **Run the real firmware on an SH-2 core.** Still worth doing, and cheaper than it was: `tools/ghidra_ctrl_dests.py` shows the pattern for reaching code Ghidra never turned into functions, which is most of what makes the flash hard to read. gearmulator has an SH-2 core with the SH7040-family peripherals (`source/cpu/sh2`: MTU, CMT, SCI, INTC, BSC, DMAC, ports) used by their 88emu. Running the SH7044 flash and the EPROM on it and logging YMP706 and YSS236 register writes gives a reference for every register value our rewrite computes, and finds events the port missed. Research strategy 3 in `docs/research.md`. This verifies the CPU side; it does not replace it.
- [x] **Extract the VOP3 microcode.** `tools/extract_vop3.py` into `docs/vop3_microcode.md` and `docs/vop3/`, with the register each word goes to, read from the boot upload rather than guessed. Reference material only; nothing depends on it.
- [ ] **VOP3 emulation (stretch).** Decode the instruction set from the microcode plus hardware recordings of each effect type, then run the real program. The only route to bit-exact effects and open research; the modelled effects ship regardless.
- [ ] **YMP706.** No emulation is possible without register semantics or a die. It stays a model. Revisit if the MAME driver rgwan is working on publishes anything.

## Tier 5: release

- [x] **Licence file.** GPL-3 in `LICENSE`; `NOTICE.md` separates our own work from the tables and presets extracted from Yamaha's EPROM and names the tool that regenerates each.
- [x] **CI.** GitHub Actions builds the engine and its self checks on Windows, Linux and macOS, builds all three plugin formats on each, and attaches zipped artifacts to tagged releases.
- [x] **Docs.** `docs/ymp706_registers.md` stays the engine reference; `docs/plugin_guide.md` is the plugin user guide; `docs/vop3_microcode.md` is the effect DSP reference.
