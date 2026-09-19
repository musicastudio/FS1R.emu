# How the FS1R handles Note On, Control Change and Aftertouch

Read out of the `FS1R_DISASM` disassembly against the v1.20 flash image. Every address is a CPU address: below 0x00040000 is the SH7044 internal flash, 0x00200000 and up is the external EPROM, 0x00400000 is the NVRAM and 0x01000000 is DRAM. Names are Ghidra's, from `FS1R_GHIDRA_ANALYSIS/decomp.db`.

Two things are worth knowing before the trees, because they are not what the MIDI spec would lead you to expect.

**Both kinds of aftertouch are Control Change.** Channel aftertouch and polyphonic aftertouch each rewrite their own data bytes into the Control Change shape and jump into `FUN_00014AD0`, using controller numbers 0xD0 and 0xA0, which no real Control Change can carry. Polyphonic aftertouch throws the key number away doing it, so the FS1R treats it as a channel wide controller and cannot respond per key.

**Nothing on the MIDI path runs in the interrupt.** The receive interrupt does one byte into a ring and returns. The parser, the dispatch and the whole engine run in a task off the main loop.

## Front end, common to all three

```
SCI0 RXI interrupt  FUN_0002F6E8            reads SSR 0xFFFF81A4 and RDR 0xFFFF81A5, clears RDRF
  `-- FUN_0002F760                          active sensing and clock timing, then:
        |-- FUN_0002F894(2, byte)           push into MIDI IN ring 0x010179BE, 0x8000 bytes
        |                                     read index 0x0101F9BE, write index 0x0101F9C0
        `-- FUN_00009F74(2, 1)              wake the MIDI task

FUN_0003D004                                the MIDI task's loop
  |-- FUN_0003CDC0                          drain ring 2
  |     |-- FUN_0002EE92(2, buf)              one byte out
  |     |-- FUN_0003CD7A / FUN_0003CD6E       running status bookkeeping for MIDI thru
  |     |-- FUN_0003D7DC                      the thru filter, for 0xBn, 0xDn and 0xEn
  |     `-- FUN_0001486A(byte)                into the parser
  `-- FUN_0003CF94                          drain ring 3, the internal source
        |-- FUN_0002EE92(3, buf)              ring 0x0101F9C4, 0x1000 bytes, indices 0x010209C4/C6
        `-- FUN_0001486A(byte)                same parser, filled by FUN_0002E9E4
```

`FUN_0002F682` is the SCI0 receive error interrupt and recovers the byte the same way. `FUN_0002F5AC` touches the SCI and the timers together and is the third handler on the same channel; I did not pin down which condition it serves.

### The parser

```
FUN_0001486A(byte)
  |-- byte >= 0xF9                          real time: set bits in 0x01045EF8, no dispatch
  |-- byte >= 0x80                          status: 0x0102A1D3 = 0, 0x0102A1D4 = status
  |                                           0xF0 arms the sysex collector, 0xF7 disarms it
  `-- byte < 0x80                           data
        |-- running status 0xF?             jump PTR_LAB_0003DE44[status & 0x0F], 8 entries
        |                                     [0] 0x00017E16 is the sysex byte collector
        |                                     [1]..[7] all 0x00015C46, the ignore stub
        `-- otherwise                       FUN_00014942
              |                               0x0102A1D5 = data 1, 0x0102A1D6 = data 2
              |                               two byte messages, (status & 0x60) == 0x40,
              |                               that is 0xCn and 0xDn, dispatch on data 1
              |                               0x0102A1EE and 0x0102A400 = status & 0x0F
              `-- jump PTR_LAB_0003D9E4[(status & 0xF0 - 0x80) >> 4]
```

`PTR_LAB_0003D9E4`, flash 0x0003D9E4:

| status | target | what |
|---|---|---|
| 0x8n | 0x000149A8 | Note Off |
| 0x9n | 0x000149FC | Note On |
| 0xAn | 0x00015B2E | Polyphonic Aftertouch |
| 0xBn | 0x00014AD0 | Control Change |
| 0xCn | 0x000159A8 | Program Change |
| 0xDn | 0x00015B1E | Channel Aftertouch |
| 0xEn | 0x00015B80 | Pitch Bend |
| (8th) | 0x00015C46 | unreachable, the ignore stub |

None of the seven is a function Ghidra found, because each is reached only through this table.

## Note On

```
0x000149FC                                  note = 0x0102A1D5, velocity = 0x0102A1D6
  |-- velocity == 0
  |     `-- 0x0102A402 = note ; FUN_00010A52          a Note Off, below
  `-- velocity != 0
        |-- 0x0102A402 = note, 0x0102A404 = velocity
        |-- 0x00014810                                 the system note filter, NVRAM 0x00400011
        |                                                1 keeps odd notes, 2 keeps even, 0 keeps all
        `-- FUN_00010B48                                the note on, if the filter said yes
```

```
FUN_00010B48
  |-- 0x01045EF8 &= 0xFFFC                  hold off the real time clock bits
  |-- FUN_0003414A                          enter the critical section
  |     |-- FUN_00009F40
  |     `-- FUN_00009FD4
  |
  |-- for each part in 0x010287BC[channel]  the receive channel's part mask, low bit first
  |     |-- FUN_00014512                    point the part at 0x010000C0 + part * 0x34
  |     |                                   and its voice at 0x01000190 + part * 0x260
  |     |     `-- FUN_00010DC4              the pitch: transposes, shift, detune, master tune,
  |     |                                     NOTETAB and the key factor
  |     |-- note limit   part[0x0F], part[0x10]     high below low means outside the gap
  |     |-- velocity limit part[0x2A], part[0x2B]   same rule
  |     |-- velocity curve
  |     |     v' = clamp(((part[0x0C] * VELCURVE[NVRAM 0x0040000E][v]) >> 6)
  |     |                 + (part[0x0D] - 0x40) * 2, 1, 127)
  |     |     the curve table is at 0x0035B71E, 0x80 bytes a curve
  |     |
  |     |-- part[5] == 0, mono
  |     |     |-- FUN_00010F3C
  |     |     `-- FUN_000112C0              held note list at 0x01028298, count 0x010288FC
  |     |           |                       capped at 8, priority from part[6]
  |     |           |-- FUN_0000F7DC / FUN_0000FCFA / FUN_0000FA5C   channel allocation
  |     |           |     `-- FUN_0000B1B6
  |     |           |-- FUN_000119D0        already sounding: retune, do not retrigger
  |     |           |-- FUN_00023000, FUN_0002A1F0, FUN_000233E0, FUN_00020302
  |     |           |-- FUN_0000EC74 / FUN_0000EC88                  the event queue to the driver
  |     |           |-- FUN_00011A86, FUN_0000FFFA, FUN_00011B10
  |     |           |-- FUN_000125F8        velocity to the operator tables, VELW at 0x0035BA1E
  |     |           `-- FUN_0002A47C
  |     `-- part[5] != 0, poly
  |           `-- FUN_0001228C              note count 0x010288DC capped at 0x20
  |                 |                       same allocation, event and velocity calls
  |                 `-- FUN_000124FE
  |
  |     `-- FUN_0000B0E8(channel, part, 0x0103937A, velocity)     if the part actually sounded
  |           |-- FUN_00009FD4(7) / FUN_00009FBC(7)               the tone generator lock
  |           `-- FUN_0000D050(channel, part)                     key on
  |
  |-- FUN_00010F8C                          the channel bookkeeping after the whole mask
  |     `-- FUN_000238C0
  `-- FUN_00034144                          leave the critical section
        `-- FUN_00009FBC
```

Note Off, for completeness, since Note On with velocity 0 lands there:

```
FUN_00010A52
  `-- for each part in 0x010287BC[channel]
        |-- FUN_00014512
        |-- part[5] == 0 -> FUN_0001100E    mono
        |-- part[5] != 0 -> FUN_00011E40    poly
        `-- FUN_0000B188(channel)
              `-- FUN_0000D7B0(channel & 0x0F)    key off, under the same lock
```

## Control Change

```
FUN_00014AD0                                cc = 0x0102A1D5, value = 0x0102A1D6
  |-- 0x01021BF8 != 0                       edit lock: the whole handler is skipped
  |-- save the current part 0x0102A408, set it to 0
  |-- mask = 0x010287BC[channel]
  |-- cc == NVRAM 0x0040001F  ->  FUN_00016056
  |
  |-- cc < 0x80                             real controllers only, so no aftertouch here
  |     |-- order = 0x0003DCC4[cc]
  |     |-- order == 0, per part
  |     |     `-- for each part in mask
  |     |           |-- FUN_00020834        part parameters  -> 0x010000C0 + part * 0x34
  |     |           |-- FUN_0001E4F2        voice parameters -> 0x01000190 + part * 0x260
  |     |           |-- FUN_0001E4D6        the driver's own part context
  |     |           |     |-- FUN_0000ECCE(0x102) / FUN_0000ECE6(part)
  |     |           |     `-- FUN_00024730(part)
  |     |           `-- jump PTR_LAB_0003DA04[cc]
  |     `-- order != 0, once
  |           |-- jump PTR_LAB_0003DA04[cc] first
  |           `-- then the part loop, context only
  |     and inside either loop:
  |           cc == NVRAM 0x00400020 -> FUN_00030796(part + 0x30, 0, 0x1D, value, 0)
  |           cc == NVRAM 0x00400021 -> FUN_00030796(part + 0x30, 0, 0x1E, value, 0)
  |
  |-- the controller matrix, for every cc including 0xA0 and 0xD0
  |     |-- match cc against the 14 source slots at 0x01029218
  |     |     store into 0x0102A23C + channel * 0x1C + slot * 2
  |     |     slots 6 to 10 and 12 keep the raw signed byte, the rest keep (value - 0x40) * 2
  |     |     with +0x7E promoted to +0x7F so the top of the range is reachable
  |     `-- for each part in mask
  |           |-- FUN_00020834 / FUN_0001E4F2 / FUN_0001E4D6
  |           `-- FUN_00014DDC(matched slot mask)
  `-- restore 0x0102A408 and its context
```

`PTR_LAB_0003DA04` at flash 0x0003DA04 has 128 entries and 103 of them are 0x000151C0, the ignore stub. The 25 the FS1R acts on:

| cc | handler | order | cc | handler | order |
|---|---|---|---|---|---|
| 0 bank MSB | 0x00015C4A | once | 73 attack | 0x000151C4 | per part |
| 5 portamento time | 0x00016078 | per part | 74 brightness | 0x0001539C | per part |
| 6 data entry MSB | 0x0001609A | per part | 91 reverb send | 0x00015336 | per part |
| 7 volume | 0x00016144 | once | 93 variation send | 0x00015358 | per part |
| 10 pan | 0x000161F2 | once | 98 NRPN LSB | 0x00015402 | once |
| 11 expression | 0x00016312 | per part | 99 NRPN MSB | 0x00015416 | once |
| 32 bank LSB | 0x00016346 | once | 100 RPN LSB | 0x000155EC | once |
| 38 data entry LSB | 0x00016754 | per part | 101 RPN MSB | 0x00015600 | once |
| 64 sustain | 0x00016758 | per part | 120 all sound off | 0x00015464 | once |
| 65 portamento | 0x00016798 | per part | 121 reset controllers | 0x00015546 | per part |
| 71 resonance | 0x0001537A | per part | 123 all notes off | 0x000155AA | per part |
| 72 release | 0x00015250 | per part | 126 mono | 0x000155B0 | per part |
| | | | 127 poly | 0x000155CE | per part |

The controller names are the standard MIDI assignments for those numbers, not something the firmware states.

## Aftertouch

Both kinds are four instructions and a jump.

```
Channel Aftertouch, 0x00015B1E
  0x0102A1D6 = 0x0102A1D5      the pressure becomes the controller value
  0x0102A1D5 = 0xD0            and the controller number is 0xD0
  jmp FUN_00014AD0

Polyphonic Aftertouch, 0x00015B2E
  0x0102A1D5 = 0xA0            the key number is overwritten, the pressure stays in 0x0102A1D6
  jmp FUN_00014AD0
```

0xA0 and 0xD0 both fail the `cc < 0x80` test, so the jump table and the two NVRAM assign slots are skipped entirely and only the controller matrix runs. That is the whole of the FS1R's aftertouch response: whatever the performance has routed 0xA0 or 0xD0 to in its eight controller sets, and nothing else.

## The controller matrix

This is the tail of every Control Change and the whole of both aftertouches.

```
FUN_00014DDC(matched slot mask)
  `-- for set in 0..7                       the performance's eight controller sets
        |-- sources = 0x01000030[set*2] * 0x80 + 0x01000031[set*2]      14 bits, two 7 bit bytes
        |-- skip unless the mask and sources overlap
        |-- sum the stored values of every source in the set,
        |     from 0x0102A23C + channel * 0x1C, clamped to -0x80..0x7F at each step
        |-- dest = 0x01000040[set], acted on when 1 <= dest < 0x30
        |-- depth = 0x01000048[set] - 0x40
        |-- 0x0035C4C4[dest] == 0          global destinations, 0x00 to 0x10 and 0x2E, 0x2F
        |     `-- jump PTR_LAB_0003DC04[dest](sum, depth)
        `-- 0x0035C4C4[dest] != 0          per part destinations, 0x11 to 0x2D
              |-- FUN_0000F7A8(part, 0x01000028[set])      (1 << part) & the set's part switch,
              |                                              performance common 0x28 + set
              `-- jump PTR_LAB_0003DC04[dest](sum, depth)  only if this part is switched on
  `-- if anything set 0x010294D6
        |-- FUN_0001B334
        `-- FUN_0001D056                    push the result at the chip
```

`PTR_LAB_0003DC04` at flash 0x0003DC04 has 48 destinations, all distinct, 0x00016904 to 0x00017D7C: off, the fourteen insertion effect parameters and the two insertion sends, then volume, pan, the two sends, the filter, the envelope times, the pitch EG, the voiced and unvoiced balance, formant, FM, the four biases, the two bandwidths, the LFOs and the Fseq speed. `tools/ghidra_ctrl_dests.py` names every one and decompiles it, which is the only way to read them: nothing but the pointer table reaches those addresses, so Ghidra never makes functions there.

## Addresses used above

| address | what |
|---|---|
| 0x0102A1D3 | how many data bytes of the current message have arrived |
| 0x0102A1D4 | running status |
| 0x0102A1D5, 0x0102A1D6 | data 1 and data 2 |
| 0x0102A1EE, 0x0102A400 | the channel of the message |
| 0x0102A402, 0x0102A404 | the note and the velocity being acted on |
| 0x0102A408 | the part the firmware is working on |
| 0x010287BC | 16 words, the part mask listening on each MIDI channel |
| 0x010000C0 | part parameters, 0x34 bytes a part |
| 0x01000190 | voice parameters, 0x260 bytes a part |
| 0x01029218 | 14 words, the controller source numbers the performance assigns |
| 0x0102A23C | 14 words a channel, stride 0x1C, the current value of each source |
| 0x01000028, 0x01000030, 0x01000040, 0x01000048 | the eight controller sets: part switch, source mask, destination, depth |
| 0x0003D9E4 | channel voice dispatch, 8 longs |
| 0x0003DA04 | Control Change dispatch, 128 longs |
| 0x0003DC04 | controller matrix destinations, 48 longs |
| 0x0003DCC4 | 128 words, whether a controller runs once or once per part |
| 0x0003DE44 | system message dispatch, 8 longs |
| 0x0035B71E | velocity curves, 0x80 bytes each |
| 0x0035BA1E | VELW, the velocity to operator level table |
| 0x0035C4C4 | 48 words, whether a destination is per part and so needs the part switch |
| 0x0040000E | system velocity curve |
| 0x00400011 | system note filter, 0 all, 1 odd, 2 even |
| 0x0040001F, 0x00400020, 0x00400021 | the system's three assignable controller numbers |

## What is read rather than measured

All of this is static reading of the disassembly, none of it was run on a unit. Three points carry more weight than the rest and are worth checking on hardware with the debug monitor in [`FS1R.unlock`](https://github.com/musicastudio/FS1R.unlock) before anything is built on them.

- That part byte 5 selects mono and poly the way this says. `FUN_000112C0` keeps a held note list and retunes without retriggering, which is mono behaviour, and [`ymp706_registers.md`](ymp706_registers.md) reads it the same way from the same firmware. Neither is a measurement.
- That polyphonic aftertouch really discards the key. The instruction that overwrites 0x0102A1D5 is plain enough, but the conclusion is worth one listening test: hold two keys, press one harder, and see whether anything is per key.
- Which of `FUN_0002F5AC`, `FUN_0002F682` and `FUN_0002F6E8` is wired to which SCI0 vector. The vector table goes through a trampoline array at flash 0x00009B2C that I did not follow. All three read RDR and reach `FUN_0002F760`, so the path is the same whichever fires.

The controller names in the Control Change table are the standard MIDI assignments, put there to make the table readable. The firmware only has numbers.
