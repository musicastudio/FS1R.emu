# FS1R.emu plugin guide

The plugin is a controller for the engine, not a second synthesiser. Everything you move becomes a
sysex parameter change into `fs1r::Device`, the engine echoes it back, and the echo is what moves the
control. That is how a hardware editor works, and it means the plugin and the console always agree.

## Building

```
git submodule update --init --recursive
cmake -B build/plugin -S . -DFS1R_BUILD_PLUGIN=ON
cmake --build build/plugin --config Release
```

VST3, CLAP and a standalone land in `bin/VST3`, `bin/CLAP` and `bin/Standalone` (`build.bat plugin`
runs those two commands). Without `-DFS1R_BUILD_PLUGIN=ON` the same CMake build produces only the
engine library, the console and the self checks, and needs no submodules.

## The standalone's audio device

**Options > Audio/MIDI Settings**. With the default **Windows Audio** device type JUCE runs at the
driver's own period, 10 ms on most machines, and that is the only entry in the buffer size list.
**Windows Audio (Exclusive Mode)** and **DirectSound** let you choose the size: exclusive mode takes
the device away from every other program while the standalone runs, DirectSound adds some latency of
its own. The choice is remembered in `%APPDATA%\FS1R.emu\FS1R.settings`.

## The EPROM image

Press **EPROM...** and point it at a 2 MB v1.20 dump. Until you do, the plugin runs the default
performance and voice; the 1408 preset voices and 360 performances come out of that image and are not
shipped. `presets/index.csv` next to the plugin, or in any folder above it (so a checkout's own `presets/`
serves `bin/Standalone/`), names and categorises them when it is present.

## Parts

Four parts, each with its own receive channel, bank and program. The **Part 1-4** buttons choose which
part the Part, Voice and Operator parameters address, exactly as the front panel edits one part at a
time. Switching parts re-reads the engine rather than sending anything, so nothing is disturbed.

Program change and bank select reach the engine as MIDI: bank MSB 63, LSB 0-11 for voices (Int,
PrA..PrK) on a part's receive channel, LSB 64-67 for performances on the performance channel. The
system's receive switches (System / bank select receive sw, program change receive sw) gate both.

## Parameters

893 parameters, generated from the Data List tables by `tools/gen_parameters.py`. Bytes that pack two
or three fields (operator key sync and transpose, the sense nibbles, the formant control destinations)
are split into separate parameters; the plugin merges them back into the byte before sending. Values
that span two sysex bytes (Fseq speed ratio, the loop points, the controller source bitmaps, every
effect parameter word) are sent as one 14-bit parameter change.

The **Effects** group's per-type words are named positionally because their meaning changes with the
selected effect type. The Effect Parameter List in the Data List says which is which for each of the
17 reverb, 29 variation and 41 insertion types.

Right-click any parameter for **MIDI learn**, to clear a mapping, or to reset to the Data List default.
The FS1R's own controller sets keep working alongside: a learned CC still reaches the engine, so a
controller routed through the performance's control matrix does both jobs.

## Patches and state

**Load .syx** takes an FS1R voice, performance or Fseq bulk dump, or a DX7 VCED file (an ACED preceding
a VCED is folded in, the way the hardware does it). **Save .syx** writes the whole device as bulk dumps:
system, performance, four voices and the Fseq.

Host state is those same bytes, so saving a session stores exactly what the hardware would have held.
A sysex message sent to the plugin's MIDI input is treated the same way, which is how an external
editor drives it.

## What is modelled and what is read

The CPU side is the firmware: velocity curves, level key scaling, pitch and portamento, the pitch EG,
LFO1 and LFO2, part levels, mono handling, performances, Fseq playback, pan. The tone generator and the
effect DSP are models, because no register documentation or instruction set exists for either chip;
every constant that models them is gathered in `namespace cal` in `src/fs1r_lib.cpp`, so calibrating
against a recording of real hardware is one table edit. `docs/ymp706_registers.md` marks which is which
line by line.
