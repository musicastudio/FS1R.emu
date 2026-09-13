# FS1R.emu

A Windows emulation of the Yamaha FS1R: four parts, 32 channels, MIDI in, performances and Fseq playback, no effects or filter yet.

Download the latest build: **https://github.com/musicastudio/FS1R.emu/releases/latest** (`fs1r_emu.exe`, no installer, no dependencies).

## Thanks

This project would not exist without **[rgwan/fs1r_firmware_RE](https://github.com/rgwan/fs1r_firmware_RE)** and its author, Zhiyuan Wan. That project dumped the SH7044's 256 KB internal flash through a hooked UART, dumped the 2 MB external EPROM, dumped the PLG150-DX ROM, captured the board's UART traffic, and published a Ghidra project with the boot, loader, flash and LCD code already named. Every byte of firmware and every preset this emulator reads comes from those dumps. This project is a synth engine that runs the firmware's logic directly, but it stands entirely on that groundwork. That work is written up in the [yamahamusicians.com thread](https://yamahamusicians.com/forum/threads/im-trying-to-emulating-an-fs1r.23211/).

## Background

The FS1R (1998) is Yamaha's formant-shaping FM synth: 8 operators per voice, 32 channels, four parts, and a second oscillator type that generates a formant with a shaped spectral window instead of a sine. Its tone generator is the YMP706, a QFP128 part used in exactly two products, the FS1R and the PLG150-DX board. There is no public register documentation for it, and MAME has no driver.

So the engine here is reconstructed from the firmware rather than from the chip. `tools/build_fs1r_ghidra.py` imports the EPROM into Ghidra as SH-2 and decompiles it; the note-on path, the tick pipeline, the parameter conversion tables and the 88-algorithm routing table are read straight out of that code and ported to C++. Where the firmware only writes a register value and the chip's response is unknown, the behaviour is inferred from the DX7 lineage and from Yamaha's formant synthesis patent (US5610354), and those places are marked INFERRED in the source.

The practical result: patches, performances and Fseqs load and play with the same parameter interpretation the hardware uses, because the same code computes them. The parts that live inside the YMP706 (filter, effects, the exact EG shape) are approximations or missing.

See `docs/research.md` for what is known about the hardware and `docs/ymp706_registers.md` for the tone generator interface and the CPU-side engine lifted from the firmware.

## Build and run

```bat
build.bat
fs1r_emu.exe -l                                  list MIDI inputs
fs1r_emu.exe                                     asks for a MIDI input once, remembers it in fs1r_emu.ini
fs1r_emu.exe -m 0 -v presets\native\000_Ballad_EP.syx
fs1r_emu.exe -v presets\dx7\004_Pianotone1.syx   DX7-format presets are converted like the firmware does
fs1r_emu.exe -r ..\FS1R_DISASM\roms\fs1r_v120_eprom_cpuview.bin -p 128    ROM voice 0-255 native, 256-1407 DX7 banks
fs1r_emu.exe -r ..\FS1R_DISASM\roms\fs1r_v120_eprom_cpuview.bin -P 0      ROM performance 0-359 with its voices and Fseq
fs1r_emu.exe -r ... -P 0 -f 29                   override the Fseq with preset Fseq 1-90
fs1r_emu.exe -v presets\native\128_BagPipe.syx -w test.wav -n 60 -d 3    offline render, no devices needed
python tools\check_wav.py test.wav 60            pitch, harmonics and envelope of a render
```

Options: `-m` MIDI input, `-c` force all parts onto one MIDI channel (default: each part listens on its receive channel, part 1 of a fresh unit on channel 1), `-v` sysex file with FS1R voice / performance / Fseq bulk dumps or a DX7 VCED dump (`-p` picks the n-th dump in the file), `-r` 2 MB EPROM image with `-p` voice, `-P` performance, `-f` Fseq, `-g` output gain. `FS1R_DEBUG=1` prints the computed register values at every note on.

MIDI: notes, bend, aftertouch, CC1 mod wheel, CC2 breath, CC4 foot, CC5/65 portamento, CC7 volume, CC11 expression, CC16-19 knobs, CC13/20-22 MIDI controls, CC64 sustain, CC120/121/123, program change (with `-r`), FS1R bulk dumps and parameter changes (performance common, part, voice, system). The performance's controller sets route the sources to the destinations that exist in the engine. Needs MSVC (build.bat uses the VS 2022 Professional vcvars64).

The ROM image the `-r` examples use is rgwan's EPROM dump with its 16-bit words byte-swapped into CPU order, kept outside this repo in `../FS1R_DISASM/roms/`. The emulator does not ship it.

## Layout

- `src/fs1r_emu.cpp` the whole synth (MIDI in, waveOut, voice/performance/Fseq decoders, the firmware's note and tick pipeline, the operator engine)
- `src/fs1r_rom_tables.h` conversion tables pulled from the EPROM by `tools/extract_tables.py`
- `src/fs1r_algorithms.h` the 88-algorithm routing table from the EPROM
- `tools/build_fs1r_ghidra.py` imports and decompiles the firmware into `../FS1R_DISASM` with pyghidra (SH-2); `tools/decomp.py`, `tools/ghidra_disasm.py`, `tools/ghidra_switches.py`, `tools/ghidra_handlers.py` query it
- `tools/extract_presets.py` pulls the 1408 preset voices out of the EPROM image into `presets/`
- `docs/` research notes, register map and engine description, data list and manual text, the formant patent

## Status

From the firmware and its tables: all 88 algorithms, the DX7 conversion, frequency words, EG rate and level conversion, velocity curves and attenuation, level key scaling, EG bias, note shifts, part detune, master tune, bend, the software pitch EG, LFO1 (speed, delay, fade, waveforms, pitch/amplitude/frequency depths), portamento, mono modes with priorities and legato, note and velocity limits, part volume/expression/balance, performances, controller sets, Fseq frame timing and playback.

Inferred from the DX7 lineage and the patent, marked INFERRED in the source and listed in `docs/ymp706_registers.md`: EG timing and shape, dB per level step, per-op modulation sensitivity scaling, feedback and modulation index, the formant window and noise formant models.

Not implemented: the per-voice filter, effects, pan, Fseq scratch mode, LFO2 (filter only).
