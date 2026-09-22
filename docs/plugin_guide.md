# FSVR plugin guide

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
its own. The choice is remembered in `%APPDATA%\FSVR\FSVR.settings`.

## Voices

The bar under the panel runs performance first, then the voice browser, which is the order the two are
chosen in: a performance names the four voices, so it is the wider choice and the one made first.

The 1408 factory voices are built into the binary, so the browser works out of the box. Pick a
**category** and a **bank** to narrow the list, then the voice, exactly as the hardware's **[SEARCH]**
does (owner's manual page 27); the panel's [SEARCH] button jumps to the category box. Picking a voice
sets the selected part's bank and program number, and that is what actually loads it: moving BANK or
PGM# on the panel, or automating those two parameters from the host, does the same thing. The banks
are the hardware's, PrA to PrK. "off" and "Int" name no preset - Int is the unit's own writable bank,
which this has no store for - so the part keeps whatever voice it is holding.

## Performances and Fseqs

The 384 factory performances are built in too: the EPROM's three preset banks of 128, A, B and C.
Picking one loads it with the four part voices it names and the formant sequence it plays, which is
what the hardware does from its own ROM. INTERNAL is the user's own battery-backed bank on the
hardware, so there is nothing to browse for it; a MIDI bank select for Internal lands on Preset A,
which is what the unit ships holding.

Both boxes follow the engine rather than only driving it, so stepping a performance with the panel's
VALUE buttons moves the performance box, and a part's voice changing moves the voice box.

Loading anything - a performance, a voice, an Fseq, an imported .syx - silences what is sounding
first. A patch arriving under held notes would otherwise leave the engine's channels running on a
voice that no longer exists. The effect tails are left to ring out, as they are on the hardware.

Patches are named the way the display names them, a bank letter and a three digit program number:
**A070 Venus** is Preset A performance 070, **A042 FundaBass** is voice 042 of Preset A, **U001** is
the first voice of an imported .syx. The panel's own BANK field still reads `Int`, `PrA`..`PrK` as the
hardware prints it there.

All 90 preset formant sequences are built in. A performance names the one it wants, so there is no
separate Fseq browser: the **Fseq** page's FSEQ PART, FSEQ bank and FSEQ number parameters are the
selection, exactly as on the hardware, and moving any of them loads it.

`tools/make_presets_blob.py` packs `presets/` into `plugin/fs1r_presets.syx`, `plugin/fs1r_presets.csv`,
`plugin/fs1r_performances.syx` and `plugin/fs1r_fseqs.syx`; all are committed, and
`tools/check_presets.py` checks them in the build's test step.

## The EPROM image

Press **EPROM...** and point it at a 2 MB v1.20 dump. Nothing needs it any more - the voices,
performances and Fseqs it holds are all bundled, and the built-in copies were extracted from it.
Loading one only changes where they are read from.

## The panel

The panel is page 14 of the owner's manual, with live controls placed over the drawing. Three departures
from the hardware: the logo reads FSVR, in the manual drawing's own letters with a V drawn to match the
1 it replaces (`docs/fsvr.svg`, dropped in by `tools/make_panel_svg.py`); the volume knob sits midway
between the panel's left edge and the display rather than hard against the edge; and the BANK/PGM# strip
shows the bank while the cursor is on it and the program number the rest of the time, because there is
only room for three characters under that label.

The four controller knobs are the selected part's own ATTACK, RELEASE, FORMANT and FM parameters, so
they follow the part: switching part or loading a performance moves them.

The panel opens on the hardware's PLAY screen, PART = ALL: the performance's code and name across the
top, `Perform` and its bank and program pair along the bottom right, and a strip that belongs to the
performance rather than to a part - the category under BANK/PGM#, no FLT and no INS. The cursor starts
on the program number, so VALUE steps performances straight away, which is how the unit is used in
every demo of it. The PART buttons walk ALL, 01, 02, 03, 04 and [EXIT] comes back to ALL.

In PART ASSIGN the CURSOR buttons walk the twelve stops in the firmware's own order, which is not the
order the labels are printed in: Rcv Ch, Rcv Max, Bank, Pgm#, Volume, Pan, RevSend, VarSend, InsEfSw,
Dry Lvl, Filter, NoteSft. Rcv Max is skipped on parts 3 and 4 and Dry Lvl has no icon, both as on the
hardware. [ENTER] twice is the MIDI View, which shows the channel message that sets the selected stop.
`docs/interface_from_firmware.md` is where all of that was read out of, and `tools/check_interface.py`
keeps the panel and the firmware's tables from drifting apart.

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

**Import .syx** takes an FS1R voice, performance or Fseq bulk dump, or a DX7 VCED file (an ACED preceding
a VCED is folded in, the way the hardware does it). Every voice in the file joins the browser as the
**Usr** bank, so a bank file is browsed the same way the factory voices are; a performance or Fseq dump
goes straight into the engine. **Save .syx** writes the whole device as bulk dumps: system, performance,
four voices and the Fseq.

Host state is those same bytes, so saving a session stores exactly what the hardware would have held.
A sysex message sent to the plugin's MIDI input is treated the same way, which is how an external
editor drives it.

## What is modelled and what is read

The CPU side is the firmware: velocity curves, level key scaling, pitch and portamento, the pitch EG,
LFO1 and LFO2, part levels, mono handling, performances, Fseq playback, pan. The tone generator and the
effect DSP are models, because no register documentation or instruction set exists for either chip;
every constant that models them is gathered in `src/fs1r/chips/cal.h`, so calibrating
against a recording of real hardware is one table edit. `docs/ymp706_registers.md` marks which is which
line by line.
