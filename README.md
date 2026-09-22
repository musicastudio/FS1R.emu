# FSVR

![The FSVR standalone running](docs/standalone.png)

**FSVR**, Formant Synthesizer Virtual Rack, is a software reconstruction of the Yamaha FS1R as a plugin and a console synth: its firmware logic rewritten in C++ from the decompiled ROM, four parts, 32 channels, the filter, both LFOs, pan, the three effect blocks, performances and Fseq playback. VST3, CLAP and standalone, with the front panel as the GUI.

Download the latest build: **https://github.com/musicastudio/FSVR/releases/latest** (no installer, no dependencies).

Licensed GPL-3, see `LICENSE` and `NOTICE.md`.

**Discord:** https://discord.gg/6sXu3GmkNm

This project is still in development, still not fully accurate to hardware.

## Demos

### FS1R DEMO SONG 2: Full Tines

**FS1R Hardware Recording**

https://github.com/user-attachments/assets/fc235492-d053-4abb-af04-8e1041c32d46

**FSVR**

https://github.com/user-attachments/assets/30774094-75f1-4819-8b73-ce0a620f2f7c

### FS1R DEMO SONG 1: Vokodrone

**FS1R Hardware Recording**

https://github.com/user-attachments/assets/8afa466b-e7c4-4427-a058-392120f1a15d

**FSVR**

https://github.com/user-attachments/assets/43fdbb3e-3b65-424d-8f08-f2e8452ab288

## Background and History

In 2025 Zhiyuan Wan ([rgwan](https://github.com/rgwan/)) created the **[rgwan/fs1r_firmware_RE](https://github.com/rgwan/fs1r_firmware_RE)** repository to study the hardware and software on the Yamaha FS1R synthesizer. That project extracted the SH7044's 256 KB internal flash through a hooked UART, dumped the 2 MB external EPROM, dumped the PLG150-DX ROM, captured the board's UART traffic, and published a Ghidra project with the boot, loader, flash and LCD code already named. 

The progress of this work was shared on the [yamahamusicians.com forum](https://yamahamusicians.com/forum/threads/im-trying-to-emulating-an-fs1r.23211/).

Just over a year later in September 2026, James Hansen ([jameshansen](https://github.com/jameshansen/)) created this repository, after analyzing and attempting to create the FS1R in software based on the material in rgwan's repository using a custom approach where the firmware files were decompiled with [Ghidra](https://github.com/NationalSecurityAgency/ghidra) into a database that can be read and explored by a coding AI agent.

Today, jameshansen and rgwan are collaborating on this project to improve the accuracy of the engine and bring it as close to the FS1R as possible.

AI coding agents and frontier LLMs have played a large role in making this project possible. The initial recreation was created using Claude Fable 5.1, which much of the ongoing analysis and engine improvements being assisted with Claude Opus 5. As part of the collaborative process, developers on the project can now use these tools via [Discord](https://discord.gg/6sXu3GmkNm).

Further hardware analysis is ongoing and planned. A patched firmware has been created to poke memory on a real FS1R to read registers and values, and taking die shots of the VOP3 is also being researched.

## FS1R vs FSVR

The FS1R (1998) is Yamaha's formant-shaping FM synth: 8 operators per voice, 32 channels, four parts, and a second oscillator type that generates a formant with a shaped spectral window instead of a sine. Its tone generator is the YMP706, a QFP128 part used in exactly two products, the FS1R and the PLG150-DX board. There is no public register documentation for it and no emulation of it anywhere.

FSVR is a rewrite of that logic, not an emulation of the processors on the board. The SH7044 is not emulated and its firmware does not run here. `tools/build_fs1r_ghidra.py` imports the EPROM into Ghidra as SH-2 and decompiles it, and the note-on path, the tick pipeline, the parameter conversion tables and the 88-algorithm routing table are read straight out of that code and ported to C++. The two custom chips cannot be treated that way, since nobody has register semantics for the YMP706 or an instruction set for the YSS236, so they are models, and every constant behind them sits in `src/fs1r/chips/cal.h` where calibrating against a recording of a real unit is one table edit.

Three words carry the status of each piece below. **Read** is lifted out of the decompiled firmware, its tables or the board, and needs no calibration. **Measured** is a model whose constants came off a recording of rgwan's unit rather than off a guess. **Modelled** is inferred from the DX7 lineage, Yamaha's formant synthesis patent (US5610354) or the Data List, and marked INFERRED in the source.

The practical result is that patches, performances and Fseqs load and play with the same parameter interpretation the hardware uses, because the same logic computes them. What still differs from a real unit lives inside the two custom chips.

### The processors

- **SH7044 CPU**, SH-2 at 28 MHz with 256 KB of internal flash. **Read, and verified against the unit.** The firmware's logic is rewritten in C++: the note-on path, the 192.3 Hz MTU2 tick, the CMT1 Fseq frame timer and every conversion table. rgwan ran a debug monitor over the whole capture set on 2026-09-18 and read the voice image back segment by segment, so the CPU side is checked byte for byte, 240,476 checks over 802 segments with no mismatch. Running the real firmware on an emulated SH-2 core (gearmulator has one with the SH7040 peripherals) stays on the list as a second opinion, not as a replacement.

- **YMP706-F tone generators**, two of them, 16 channels each on an 8-bit write-only bus. **CPU side read, chip side modelled and largely measured.** There is no register documentation and no die shot, and the chip is too rare for one, so it stays a model. Everything the CPU computes for it is read from the firmware: all 88 algorithms, the DX7 VCED and ACED conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1 and LFO2, portamento, the mono modes with priorities and legato, note and velocity limits, part volume, expression and balance, and pan. What the chip does with those values breaks down as follows.
  - **The amplitude EG.** Measured. The rate law holds to 0.6 % over 38 measured rates and the level register is 0.376287 dB a step, a halving every sixteen. The attack's shape, its floor, the hold's length and the key rate scaling were all DX7 guesses and all wrong until two envelope recordings settled them; shape error over a hundred segments went from 5.45 dB rms to 0.58. Left: the key code law between its ten measured points, and the hold's fixed 8 ms lag. [docs/aeg.md](docs/aeg.md).
  - **FM, feedback and the modulation index.** Measured. 3.369 cycles at full level, off two sideband sweeps and confirmed by `03_fm`'s own spectra; feedback is `0.5 * 2^(fb - 7)`.
  - **Detune.** Read, and it needs no constant at all. It is not a fixed step but the EPROM's own key scaled `FRMDET` table, matched over six octaves to half a cent, which took a vibrato off demo song 2 that the unit does not have. [docs/detune.md](docs/detune.md).
  - **The voiced formant window.** Measured where the geometry is known. The skirt law came out of the sweep exactly, without a fit: the window exponent doubles per skirt step, and `odd2` and `res2` land inside 0.9 dB over all eight settings. `odd1` and `res1` take the same law and are 10 to 14 dB out, because no `sin^p` reproduces them at any exponent, and `all1` and `all2` are 36 to 66 dB out at wide skirts, which is a two-period grain construction rather than an exponent. The formant form itself keeps 2.5 dB that no member of the family removes. [docs/skirt.md](docs/skirt.md).
  - **The noise formant**, the unvoiced operator. Measured, 2026-09-21. The width is linear in the bandwidth byte at 38.6 Hz a byte and absolute in hertz rather than relative to the fundamental, the level goes as the reciprocal of the width, the skirt widens the band where the model narrowed it, resonance is a threshold rather than a ramp, and bandwidth 0 is silence. Level error over the four unvoiced files fell from 2.9 to 7.4 dB down to 0.55 to 0.94. Left: everything above 4 kHz is 4 to 10 dB short. [docs/noise.md](docs/noise.md).
  - **Amplitude modulation sensitivity.** Measured and still modelled wrongly. Three LFO depths are recorded and the law is a saturation, not a scaling; `07_modulation_1` carries 2.27 dB of envelope error against the unit.
  - **Which write starts a note.** Unknown. 0xFC/0xFD goes out before the registers load and nothing says what the chip makes of it.

- **YSS236-F / VOP3-1, the per-voice filter**, at 0x800200. **CPU side read, response fitted.** Yamaha's VOP3 is the same programmable DSP that is the synthesis engine of the AN1x, AN200 and PLG150-AN; the FS1R has two, and this one sits in a channel-level loop off both tone generators. The CPU's whole side of it is read out of the firmware: sixteen filter channels mapped to parts, a dirty flag per parameter group, and closed-form conversions for cutoff, resonance, type and input gain. The response is the model, and the coefficient is not a one-pole, measured 2026-09-21: the corner goes as the byte, 28.5 Hz doubling every 9.1 bytes, fitted over six segments. `cal` runs the three lowpasses as one 4-pole ladder with resonance table A scaling the feedback, and HPF, BPF and BEF are separate chip modes that keep a 2-pole SVF reading. The filter EG runs on the chip rather than being streamed by the CPU, which is why `06_filter_2` still carries 3.36 dB of envelope error. `tools/extract_vop3.py` has the microcode out into `docs/vop3/`, register by register, but the instruction set is undecoded.

- **YSS236-F / VOP3-2, the effects**, at 0x800000. **Parameter encoding read, algorithms modelled. The largest gap in the engine.** Reverb (17 types), variation (29), insertion (41) and the master EQ in the XG topology, with the part dry and send levels and the insertion switch, in `src/fs1r/chips/vop3_effects.h`. The parameter *encoding* is read rather than guessed, recovered out of the 360 preset performances and matching every documented default. The 87 algorithms themselves are modelled from the Data List, and were measured against real impulse responses for the first time on 2026-09-21, at 16.9, 27.2 and 28.8 dB over eighty-four segments, in a set where most files agree with the unit inside half a decibel. `tools/extract_vop3_2.py` has that chip's two images out into `docs/vop3_2/`. Decoding the VOP3 instruction set is the only route to bit-exact effects and a bit-exact filter; it is open research on the scale of a full DSP core, and the modelled effects ship regardless.

- **The board around them.** Read. One 24.576 MHz crystal runs the audio side and the main Sanyo LC78834M DAC is the I2S master at exactly 48 kHz, so the engine runs at 48 kHz whatever the host rate and resamples on the way out. The recording tap is that DAC's I2S input, after both DSPs and before the analogue volume pot, which is what makes a recording's absolute level comparable. The output path's fixed gain, the hard clip on the channel accumulator and the filter loop's insertion loss are all numbers off that tap. `docs/research.md` 2.0.1 and section 8 have the clocking and the CS2 decode.

### The data and the control paths

All read from the firmware and its tables, and all finished.

- **Factory data.** The 1408 preset voices, 384 performances and 90 formant sequences out of the EPROM, plus the voice (608 bytes), performance (400), Fseq and system bulk layouts and the DX7 VCED and ACED maps. Extracted into `presets/` and built into the plugin, so the patch browser needs no EPROM image.
- **Fseq playback.** Frame timing on its own timer, every loop mode with the direction the loop points imply, the start offset, the performance start delay, scratch mode from controller destination 47, MIDI clock sync at all five speed ratios, and the voice Fseq switches.
- **Controller sets.** The whole matrix out of three firmware functions: the source bit order, the bipolar and raw source conversions, the clamped sum, the three scalers and which of the 48 destinations uses each, the per-part gate, and the destinations that edit a part byte instead of contributing an offset. The voice's own Formant and FM control matrix with it.
- **MIDI and sysex.** The whole parameter map in both directions, bulk dumps and parameter changes in and out, dump and parameter requests answered with checksummed replies, RPN 0-2, the ten NRPN 01 xx part parameters, bank select and program change in both performance and multi modes, MIDI clock and active sensing.
- **The panel.** The plugin's GUI is the front panel, and the cursor stops and controller set behind it are read out of the EPROM's own screen tables rather than off a photo. `docs/interface_from_firmware.md`.

### Where it stands

Against rgwan's digital recording of the built-in demo, which the unit plays from its own EPROM so the same byte stream drives both: median envelope correlation 0.978, worst 0.894, mean band tilt 2.8 dB. Against the 29 frozen capture files, seventeen agree with the unit inside half a decibel of level.

What is left, worst first: the effects, the unvoiced pedestal above 4 kHz, AM sensitivity at deep modulation, the `all1`/`all2` grain geometry and the formant's window family, and the last decibel of the filter EG. [docs/fidelity_plan.md](docs/fidelity_plan.md) ranks them with the numbers and says which are modelling work with their data already recorded and which are waiting on hardware. [STATUS.md](STATUS.md) carries the KNOWN / INFERRED / UNKNOWN lists and the open work. Tiers 0 to 3 and 5 are done, so the engine, the library and console split, the plugin in all three formats, the front panel GUI, the licence and CI are all in, and Tier 4 is fidelity and verification.

## Build and run

### Prebuilt

[The latest release](https://github.com/musicastudio/FSVR/releases/latest) has zipped VST3, CLAP and standalone builds for Windows, Linux and macOS. No installer, no dependencies.

Every push to `main` builds the same set through [GitHub Actions](https://github.com/musicastudio/FSVR/actions), so a build for an OS you do not own is always one Actions run away, including on a fork. `.github/workflows/build.yml` is three jobs:

- **engine**, on `windows-latest`, `ubuntu-latest` and `macos-latest`. Plain CMake with no submodules, then `ctest`, which runs the effect self check everywhere and the engine self check on Windows. Uploads `FSVR-console-windows`.
- **plugin**, on the same three. Checks out the submodules, installs the X11, ALSA and FreeType packages JUCE asks for on Linux, configures with `-DFS1R_BUILD_PLUGIN=ON`, and uploads `FSVR-plugin-windows`, `FSVR-plugin-linux` and `FSVR-plugin-macos`, each holding the VST3, the CLAP and the standalone for that OS.
- **release**, on a `v*` tag only. Zips every artifact and attaches it to a GitHub release.

### Building it

CMake everywhere, C++17. The engine, the console and the self checks need no submodules and no dependencies:

```bash
cmake -B build/cmake -S .                 # fs1rLib, test_effects, bin/render_capture, and bin/fs1r_emu on Windows
cmake --build build/cmake --config Release
ctest --test-dir build/cmake -C Release   # the self checks
```

The plugin needs the submodules and adds one flag:

```bash
git submodule update --init --recursive
cmake -B build/plugin -S . -DFS1R_BUILD_PLUGIN=ON
cmake --build build/plugin --config Release
```

VST3, CLAP and a standalone land in `bin/VST3`, `bin/CLAP` and `bin/Standalone`. The 1408 factory voices, the 384 factory performances and the 90 preset formant sequences are built into the plugin, so its patch browser needs no EPROM image. [docs/plugin_guide.md](docs/plugin_guide.md) is the user guide.

On Windows `build.bat` is the shortcut, MSVC x64 out of the VS 2022 Professional vcvars64:

```bat
build.bat            bin\fs1r_emu.exe, the console
build.bat test       build and run the self checks
build.bat plugin     the two plugin CMake commands above
```

Everything meant to be run lands in `bin/`; objects and the test binaries go to `build/`.

### The console

`bin/fs1r_emu` is the test harness on top of the engine library. It is Windows only, since it uses WinMM for MIDI and waveOut for audio, but its offline render path needs no devices at all.

```bat
fs1r_emu -l                                     list MIDI ports
fs1r_emu                                        asks for a MIDI input once, remembers it in bin\fs1r_emu.ini
fs1r_emu -m 0 -o 0 -v presets\native\000_Ballad_EP.syx
fs1r_emu -v presets\dx7\004_Pianotone1.syx      DX7-format presets, converted the way the firmware does
fs1r_emu -r <eprom.bin> -p 128                  ROM voice, 0-255 native and 256-1407 the DX7 banks
fs1r_emu -r <eprom.bin> -P 0                    ROM performance 0-383 with its voices and Fseq
fs1r_emu -r <eprom.bin> -P 0 -f 29              override the Fseq with preset Fseq 1-90
fs1r_emu -v presets\native\128_BagPipe.syx -w test.wav -n 60 -d 3     offline render, no devices needed
```

Options. `-m` MIDI input, `-o` MIDI output for dump and parameter replies, `-c` force all parts onto one MIDI channel (the default is each part on its own receive channel), `-v` a sysex file holding FS1R voice, performance or Fseq bulk dumps or a DX7 VCED dump, with `-p` picking the n-th dump in the file, `-r` a 2 MB EPROM image with `-p` voice, `-P` performance and `-f` Fseq, `-g` output gain, `-w` offline render with `-n` note, `-n2` a second note a third of the way in, `-cc num=val` control changes sent before the note, `-d` seconds, `-mono` part 1 mono with full-time portamento, `-selftest` the engine self check. `FS1R_DEBUG=1` prints the computed register values at every note on.

MIDI. Notes, bend, both aftertouches, the control numbers the system table assigns to KN1-4, MC1-4, FC, BC, Formant and FM, CC5/65 portamento, CC7 volume, CC10 pan, CC11 expression, CC64 sustain, CC91/94 sends, CC120/121/123/126/127, RPN 0-2, the ten NRPN 01 xx part parameters, bank select and program change in both performance and multi modes, MIDI clock, active sensing, FS1R bulk dumps and parameter changes across the whole map, and dump and parameter requests answered. The performance's controller sets route the sources to the destinations.

The EPROM image the `-r` examples take is rgwan's dump with its 16-bit words byte-swapped into CPU order, kept outside this repo in `../FS1R_DISASM/roms/`. FSVR does not ship it, and nothing but the `-r` paths needs it.

### Checking a change

```bash
python tools/check_wav.py test.wav 60   # pitch, harmonics and envelope of one render
python tools/regress.py                 # the whole fixed preset list against the stored reference
```

## Repo Layout

**Engine.** No Windows, no host, no GUI. This is what the plugin links. The tree says where a claim comes from, which is the thing to know before changing anything in it.

- `src/fs1r.h` the public header, `fs1r::Device`. The only one the plugin includes
- `src/fs1r/hardware.h` the FS1R's own numbers, read off the board. Facts, never tuned
- `src/fs1r/internal.h` the engine's shared declarations, including `struct Synth`

`src/fs1r/firmware/` is **KNOWN**, rewritten from the disassembly, and every claim in it cites a `FUN_` address. A disagreement with a real unit is a bug here, not a calibration.

- `patch.cpp` voice, performance and part data as the firmware decodes it, with the DX7 conversion
- `controllers.cpp` the eight controller sets, the three scalers and the 48 destinations
- `notes.cpp` note on and off, operator setup, pitch, portamento, and the 192.3 Hz tick
- `fseq.cpp` formant sequence playback; `midi.cpp` MIDI in and sysex in and out; `rom.cpp` EPROM and sysex loading
- `tables.h` the EPROM's conversion tables, by `tools/extract_tables.py`; `algorithms.h` the 88-algorithm routing table

`src/fs1r/chips/` is **INFERRED**, because neither custom chip has public register documentation. These are claims about the hardware, changed by measuring against a recording and never by taste.

- `cal.h` the calibration surface, every modelled constant in one place
- `ymp706.cpp` the tone generator: operators, envelopes, the formant window, and the render path
- `vop3_filter.h` VOP3-1's filter; `vop3_effects.h` VOP3-2's reverb, variation, insertion and master EQ

`src/fsvr/` is **ours**. Nothing in the hardware corresponds to any of it.

- `tuning.h` cost knobs, the control-rate decimation and the queue cap. Changing one must not change the output
- `device.cpp` `fs1r::Device`, host-rate resampling and state as bulk dumps
- `smf.h` Standard MIDI File reading; `selftest.cpp` the engine self check
- `src/console/main.cpp` the test console: WinMM MIDI in and out, waveOut, offline render

**Plugin.** The JUCE layer, which never models synthesis; it moves parameter values in and out of the engine as sysex, exactly as a hardware editor would.

- `plugin/PluginProcessor`, `PluginEditor`, `PanelView`, `EditorPages`, `PatchManager` and `ParameterDescriptions`
- `plugin/fs1r_panel.svg` the front panel artwork, generated by `tools/make_panel_svg.py`
- `plugin/fs1r_presets.syx` with its index `fs1r_presets.csv`, `fs1r_performances.syx` and `fs1r_fseqs.syx`, the bundled factory banks, packed from `presets/` by `tools/make_presets_blob.py`
- `plugin/parameterDescriptions_fs1r.json` the 893 parameters with their sysex addresses and bit layouts, generated by `tools/gen_parameters.py`
- `extern/` JUCE and clap-juce-extensions, as submodules

**Tools, firmware.**

- `tools/build_fs1r_ghidra.py` imports and decompiles the firmware into `../FS1R_DISASM` with pyghidra (SH-2); `decomp.py`, `ghidra_disasm.py`, `ghidra_switches.py`, `ghidra_handlers.py` and `ghidra_probe.py` query it
- `tools/ghidra_ctrl_dests.py` decompiles the 48 controller destination handlers, which Ghidra never turns into functions because only a pointer table reaches them
- `tools/extract_tables.py`, `extract_presets.py` and `extract_demo.py` pull the conversion tables, the 1408 voices, 384 performances and 90 Fseqs, and the demo songs out of the EPROM image
- `tools/extract_vop3.py` and `extract_vop3_2.py` pull the two VOP3 microcode uploads into `docs/vop3/` and `docs/vop3_2/`

**Tools, calibration.**

- `tools/make_capture_set.py` writes `captures/requests/`, the hardware capture kit: Standard MIDI Files that play a measurement into a real FS1R, each setting up its own patches by sysex and opening and closing with a marker so a recording aligns itself. Built out of `tools/fs1r_patch.py`; the other `make_capture_*.py` are the later additions to the set
- `tools/analyze_capture.py` aligns a recording to the manifest, measures every segment, fits the constants it can, and diffs the result against our own render of the same file. `bin/render_capture` makes that render, which is also how a request file is checked before anyone is asked to play it
- `tools/render_song.py`, `check_demo.py` and `demo_probe.py` play the extracted demo songs through the engine and score them against rgwan's recording; `demo_scores.txt` is the running ledger
- `tools/pitch_track.py`, `split_capture.py` and `analyze_unvoiced2.py` the per-measurement readers

**Tools, checks.**

- `tools/regress.py` renders the fixed preset list and diffs it against `regress_ref.json`: pitch, harmonic peaks, envelope, stereo width, centroid
- `tools/test_effects.cpp` decodes and sweeps every effect type, and `fs1r_emu -selftest` is the engine check. Both run under `ctest`
- `tools/check_wav.py`, `check_formant.py`, `check_presets.py`, `check_panel.py` and `check_interface.py`, the rest of what `build.bat test` runs

**Data.**

- `presets/` the factory voices, performances and Fseqs as `.syx` with `index.csv`, plus the DX7 bank
- `captures/requests/` the capture kit as MIDI with a README listing what each file settles, `captures/hardware/` the recordings that came back, `captures/analysis/` the per-segment numbers, `captures/engine/` our own renders of the same files

**Docs.**

- [docs/research.md](docs/research.md) what is known about the hardware, and [docs/ymp706_registers.md](docs/ymp706_registers.md) the tone generator interface and the CPU-side engine lifted from the firmware. These two are the reference.
- [docs/fidelity_plan.md](docs/fidelity_plan.md) the ranked read of where the engine stands against every recording; `capture_0918.md` and `hardware_capture_request.md` are the capture working
- `docs/aeg.md`, `formant.md`, `skirt.md`, `noise.md` and `detune.md`, one measurement each, the working behind the constants in `cal`
- `docs/midi_dispatch.md` how a Note On, a Control Change and both aftertouches get from the SCI0 interrupt to the tone generator; `interface_from_firmware.md` the panel's cursor stops and controller set, read out of the EPROM's own screen tables
- `docs/vop3_microcode.md`, `vop3_2_microcode.md` and `vop3_pinout.md` the VOP3 reference
- [docs/plugin_guide.md](docs/plugin_guide.md) the plugin user guide. The Data List and owner's manual text and the formant patent are here too.
- [STATUS.md](STATUS.md) what is known, what is modelled and what nobody knows, with the open work at the end
- [docs/findings.md](docs/findings.md) the research log, newest first, and the evidence behind each constant
