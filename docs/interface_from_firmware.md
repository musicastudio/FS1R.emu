# The FS1R interface, read out of the firmware

The panel in `plugin/PanelView.cpp` was built from the owner's manual and from the manual's own drawing
of the front panel. The manual is prose, though, and prose leaves gaps: it does not say what order the
CURSOR buttons walk, it lists the icon labels in a different order in two different places, and it
never says which MIDI messages the unit answers. All of that is in the v1.20 EPROM as data, and this
note is what came out of it.

Everything below is checkable. `tools/check_interface.py` reads the screen tables back out and fails
the build's test step if the panel's stop lists and the firmware ever drift apart; it skips with a
warning when the EPROM image is not present, since that image is not in this repository.

Where the tables only say what a screen contains and not how it is laid out, the second source is
footage of a real v1.20 unit: <https://www.youtube.com/watch?v=ic4TUvlUxtI>, 55 seconds of someone
stepping through the Preset C performances. Section 5 is what that settled, including two things the
owner's manual gets wrong.

## 1. The screen tables

The display screens are a run of fixed 40-byte records. Each record is one cursor stop, and it holds
the two lines the display shows while the cursor is on it:

| bytes | what |
|---|---|
| 0-18 | the upper line, 19 characters |
| 19-38 | the lower line, 20 characters |
| 39 | NUL |

The upper line is one character short because the display's top-left cell is not in the template: the
firmware draws the performance or voice **edit mark** there, which is why the manual keeps mentioning
one ("The performance edit mark will appear when any performance edit operation is performed", page
22). So the text area is two lines of twenty, which is what `LcdView` models.

Reading the records is what settles the line split. Take the MIDI View record for Bank:

```
 MIDI     Bank    =Bn 20     =
|<------ upper 19 ------>|<----- lower 20 ----->|
```

Split anywhere else and `Bn 20` is cut in half. Split there and both lines read cleanly: the upper line
is the MIDI View header with the parameter name, the lower line is the message.

Four tables sit next to each other, two per mode, one screen table and one MIDI View table, and the
two tables of a mode are index-for-index parallel:

| table | address | records |
|---|---|---|
| PLAY (PART = ALL) screens | 0x392FEC | 8 |
| PLAY MIDI View | 0x39312C | 8 |
| PART ASSIGN screens | 0x39328C | 12 |
| PART ASSIGN MIDI View | 0x39346C | 12 |

To dump them again:

```bash
python tools/check_interface.py
```

## 2. The cursor stops

### PLAY, with PART = ALL (8 stops)

This is the screen the unit powers up on, and the one every demo of the FS1R is filmed in.

| # | upper line | lower line | MIDI View |
|---|---|---|---|
| 0 | `Pfm Ch` | `=` | - |
| 1 | *(blank)* | `Perform` | `Bn 20` |
| 2 | *(blank)* | `Perform` | `Cn` |
| 3 | `Pfm Vol` | `=` | `Bn 07` |
| 4 | `Pfm Pan` | `=` | `Bn 0A` |
| 5 | `Rev Rtn` | `=` | - |
| 6 | `Var Rtn` | `=` | - |
| 7 | `PfmNSft` | `=` | - |

Stops 1 and 2 are the performance's bank and program number. Their upper line is blank because the
display puts the bank and program pair there itself, with the solid and hollow pointers that say which
half the cursor is on; the lower line names what is being selected, `Perform`.

### PART ASSIGN, PART = 01 to 04 (12 stops)

| # | upper line | lower line | MIDI View | our field |
|---|---|---|---|---|
| 0 | `Rcv Ch` | `=   -` | - | MIDI icon |
| 1 | `Rcv Max` | `=   -` | - | MIDI icon, parts 1 and 2 only |
| 2 | *(blank)* | `Voice` | `Bn 20` | BANK/PGM# icon |
| 3 | *(blank)* | `Voice` | `Cn` | BANK/PGM# icon |
| 4 | `Volume` | `=` | `Bn 07` | VOL |
| 5 | `Pan` | `=` | `Bn 0A` | PAN |
| 6 | `RevSend` | `=` | `Bn 5B` | REV |
| 7 | `VarSend` | `=` | `Bn 5D` | VAR |
| 8 | `InsEfSw` | `=` | - | INS |
| 9 | `Dry Lvl` | `=` | - | **no icon** |
| 10 | `Filter` | `=` | `Bn 4A` | FLT |
| 11 | `NoteSft` | `=` | - | KEY |

Three things fall out of this that the manual does not say plainly.

**The cursor order is not the silkscreen order.** The labels under the glass read PART MIDI BANK/PGM#
VOL FLT PAN REV VAR INS KEY, but the cursor walks Volume, Pan, RevSend, VarSend, InsEfSw, Dry Lvl,
Filter, NoteSft. The pointer therefore runs left to right as far as INS, jumps back to FLT, then
forward to KEY. The manual's own parameter descriptions on pages 25 and 26 are in exactly the table's
order, which is the confirmation; the figure caption on page 24 lists the labels around the drawing
instead and reads differently.

**Twelve stops, ten icons.** `Rcv Ch` and `Rcv Max` share the MIDI icon, `Bank` and `Pgm#` share
BANK/PGM#, and `Dry Lvl` has no icon at all. On `Dry Lvl` the strip shows no pointer and the stop is
named on the upper line only.

**PART is not a cursor stop.** The part number has an icon but no record: the part moves with the PART
buttons, or with knob 1 when both knob mode buttons are dark.

## 3. The MIDI View, and the controllers behind it

The MIDI View ([ENTER] pressed twice) shows the **channel message** that sets the selected parameter,
not a system exclusive parameter change. Seven of the twelve PART ASSIGN stops have one; the other
five records are blank, and the display shows nothing for them.

That table also names controller numbers, and they can be checked against the firmware's own per-CC
dispatch table in the SH7044 flash at **0x3DA04**: 128 four-byte handler pointers, one per controller,
with `0x151C0` as the ignore stub. Twenty-five entries are not the stub:

| CC | handler | writes part byte | what |
|---|---|---|---|
| 0 | 0x15C4A | - | bank select MSB |
| 5 | 0x16078 | 0x25 | portamento time |
| 6 | 0x1609A | - | data entry MSB |
| 7 | 0x16144 | 0x0B | volume |
| 10 | 0x161F2 | 0x0E | pan |
| 11 | 0x16312 | - | expression |
| 32 | 0x16346 | - | bank select LSB |
| 38 | 0x16754 | - | data entry LSB - **an `rts` stub, ignored** |
| 64 | 0x16758 | - | sustain |
| 65 | 0x16798 | 0x24 | portamento switch |
| **71** | 0x1537A | **0x19** | filter resonance |
| **72** | 0x15250 | **0x1C** | EG release time |
| **73** | 0x151C4 | **0x1A** | EG attack time |
| **74** | 0x1539C | **0x18** | filter cutoff |
| 91 | 0x15336 | 0x13 | reverb send |
| **93** | 0x15358 | **0x12** | variation send |
| 98, 99, 100, 101 | | | NRPN and RPN select |
| 120, 121, 123, 126, 127 | | | mode messages |

The five sound-controller handlers are all the same eight instructions, and the immediate loaded into
r6 is the address low they write to:

```
mov  #0x1a,r6          ; the part parameter, here EG ATTACK TIME
mov.b @r1,r4           ; the part number
add  0x30,r4           ; 0x30 + part: the part's own sysex address high
jsr  @r1               ; write parameter (0x30+part, 0x00, r6) = value
```

Two of those are the panel's own ATTACK and RELEASE knobs, which is a useful cross-check: the knobs
write part bytes 0x1A and 0x1C, and CC 73 and 72 write the same two.

## 4. The PLAY screen as the hardware draws it

The tables give the stops; the video gives the layout. Every frame of it looks like this:

```
 C056 Hit
        Perform>PrC>056
 ALL  01  [En]  VOL  (pan)  ))  ))     + 0
```

- **Upper line**: the performance's bank letter, its three digit program number, a space, then its
  name. `C052 Ensemble`, `C067 Dark Clar`, `C072 HyperFuzz`. Left aligned.
- **Lower line**: `Perform` followed directly by the bank and program pair, right aligned to the last
  cell. The first pointer is hollow and the second solid, so the cursor is on the program number.
- **Strip**: `ALL` under PART, the performance channel under MIDI, and the performance's **category**
  under BANK/PGM# - `En`, `Br`, `Vo`, `Rd`, `Ld`, `Ba` across the clip, which are the same two-letter
  codes the voice categories use. Then the performance volume, pan and the two returns, and the note
  shift under KEY. No FLT and no INS, exactly as the eight stops predict.

Two corrections to the owner's manual fall out of it.

**The selected icon is drawn in reverse video, not with a pointer above it.** The manual says on pages
22 and 25 that "a small triangular pointer appears above the icon corresponding to the selected
parameter". On a v1.20 unit the selected field is a filled block with the glyphs knocked out of it,
and there is no triangle anywhere on the screen in any frame of the clip. The panel drew the manual's
version until this was checked.

**BANK/PGM# does not show a number in PLAY mode.** It shows the performance's category. The number is
already on both text lines, so the icon carries the one thing that is not.

This is also the answer to why the demos change patches with nothing obviously selected: the screen
comes up with the cursor on the program number half of the pair, so VALUE steps performances one at a
time without anything being selected first. Pressing a PART button leaves for PART ASSIGN, where the
same VALUE buttons step the selected part's voice instead - which is the behaviour that looked wrong.

## 5. What this changed here

Fixed:

- **PART = ALL, the PLAY screen, now exists** with its own eight stops, its own strip and the layout
  above. VALUE on its bank and program pair steps the performance - a whole bank of 128 on the bank
  half, one at a time on the program half.
- **The selected icon is reverse video**, not a pointer and a box.
- **CC 93, not 94, is the variation send.** The engine had 94, which is the XG celeste depth. Both the
  MIDI View record (`Bn 5D`) and the handler table say 93.
- **CC 71, 72, 73 and 74 were not handled at all.** They are now, on the part bytes above.
- **The cursor order.** `Filter` was sixth; it is second to last. `Rcv Max` and `Dry Lvl` were missing
  entirely, and `Part` was a stop the hardware does not have.
- **`Rcv Max` is skipped on parts 3 and 4**, as the manual says on page 25.
- **The `Filter` stop is the cutoff offset, not the filter switch.** It is part byte 0x18, which is
  what `Bn 4A` writes; the panel was pointing at the on/off switch at byte 0x07 and drawing it as one.
- **The MIDI View shows the channel message**, `Bn 20 = 3` rather than an invented sysex string, and
  shows nothing for the five stops whose records are blank.
- `Ins Sw` is spelled `InsEfSw`, as the display spells it.

Still different from the hardware, on purpose or for want of room:

- **The upper line carries the performance name and the stop name together.** The hardware puts only
  the stop name there, at columns 10 to 18, and shows the value after the `=` on the lower line. Ours
  needs the lower line for the part's voice, so the value is shown transiently instead. On the PLAY
  screen, where the hardware's lower line is free, the layout matches.
- **PART = ALL is reached with the PART buttons or [EXIT]**, not by pressing both PART buttons at once
  as page 22 describes, because one mouse cannot press two buttons. PART - from part 1 goes to ALL and
  either PART button leaves it, so the two buttons walk ALL, 01, 02, 03, 04.
- **The REV and VAR send icons and the KEY field are approximations** of the hardware's shapes.
- **On the Bank and Pgm# stops the hardware blanks the upper line** and writes the bank and program
  pair there with the solid and hollow pointers. Ours keeps the name on the right and puts the bank or
  the program number in the strip field, because the pair does not fit beside the performance name.
- **The volume knob is moved right** of where the drawing has it, which is a deliberate cosmetic
  choice and the only one of these that is not about space.
