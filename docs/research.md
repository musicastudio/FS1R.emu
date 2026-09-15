# Yamaha FS1R emulation: research notes

Started 2026-09-12. Goal: a Windows softsynth (exe with MIDI input first, VST later) that behaves like the FS1R.

## 1. What already exists

**rgwan/fs1r_firmware_RE** (github, last update 2026-07-21). Contents and what they are actually good for:

| item | what it is | useful for |
|---|---|---|
| `dmps/fs1r_sh7044_flash_1.20_256k.bin` | full 256 KB internal flash of the SH7044, v1.20, dumped through a hooked UART | CPU-side firmware (loader + the "timing sensitive" part: SCI/MIDI interrupt handlers live here) |
| `dmps/Yamaha FS1R v1.1/v1.20 EPROM Firmware.bin` | 2 MB external EPROM (M27C160). **Byte-swapped**: every 16-bit word is little-endian in the file. Swap pairs and `[0x1C8000:0x200000]` equals flash `[0x8000:0x40000]` exactly | main firmware, preset voices, performances, Fseqs, tables |
| `dmps/plg150-dx/PLG150-DX.BIN` | 1 MB ROM of the PLG150-DX board (SH7043 in ROM-less mode + the same YMP706 + YSS233) | a second, simpler firmware that drives the same tone generator chip |
| `fs1r_gdr` | Ghidra project (SH-2A language, owner "Zhiyuan"; patch OWNER in `fs1r.rep/project.prp` to open it) | 26 named functions: boot, loader, flash erase/write, version compare, LCD_send, p_printlcd, test mode entry, MIDI test, UART send, factory reset. Nothing about the tone generator |
| `la/` | DSLogic capture of the UART dump | nothing for us |
| `doc/plg150-dx-sm.pdf` | PLG150-DX service manual | block diagram, parts list (HD6437043E00F, YMP706-F, YSS233-F, XS516/XS720/XS936/XS992, 2x M5M51008 SRAM) |

**Forum thread** (yamahamusicians.com, 11 posts, Aug 2025 to Jun 2026). Author's stated goal is a MAME driver. Last status (2026-06-09): "I'm analyzing firmware/FS chip, preparing for emulating it in MAME". Nothing about the chip's register interface has been published. The thread contains no synthesis information.

**MAME** has SH7042/SH7043 CPU peripherals (`src/devices/cpu/sh/sh7042.cpp`, used by the MU128/MU2000/PSR540 drivers) and reverse-engineered Yamaha AWM chips (SWP00/20/30). No FS1R driver, no YMP706. So the CPU side of a hardware emulation is largely available under BSD-3; the tone generator is not.

**Other**: the YMP706 is "8 operator 32 ch FS sound generator", QFP128, only used in FS1R and PLG150-DX (undocumentedsoundchips). No register documentation anywhere public.

Conclusion: the reverse engineering that exists covers booting, flashing and repairing the unit. The synthesis chip, which is all that matters for a softsynth, is untouched.

## 2. Hardware summary

- CPU Hitachi SH7044 (HD64F7044F28), SH-2 core, 256 KB flash at 0x00000000, 4 KB RAM at 0xFFFFF000 (reset SP = 0xFFFFFFFC), mode pins 1010. Reset vector -> 0x400.
- Address map (SH7040 series): flash 0x0-0x3FFFF; EPROM 0x200000-0x3FFFFF; CS1 0x400000, CS2 0x800000, CS3 0xC00000 (4 MB each, board peripherals: YMP706, effect DSP, SRAM/NVRAM, LCD, panel); peripheral registers 0xFFFF8000+ (SCI0 0xFFFF81A0, SCI1 0xFFFF81B0, MTU 0xFFFF8200+, flash control FLMCR at 0xFFFF8580).
- Vector table at 0x0 and a second copy at 0x8000 (the loader hands over to it).
- Version tag string at flash 0x3FC00: `#A0281 ver1.20 1998.09.10-01`.
- Two YMP706-F tone generators ("FS"), two YSS236-F effect DSPs (Yamaha "VOP3": also the synthesis engine of the AN1x/AN200/PLG150-AN and the vocal harmony processor of the PSR-9000; the PLG150-DX uses the smaller YSS233 instead), two Sanyo LC78834M 18-bit stereo DACs. Chip identification from the studiorepair.com FS1R gallery; rgwan's digital-out board reports the DAC word clock at 48 kHz.

### 2.0 YMP706 pinout (PLG150-DX service manual p.13, "FS1-AB AWM Tone Generator & Digital Filter")

- CPU side: A0-A9 (10 address lines), D0-D7 (8-bit data, input only: the chip is write-only apart from status), CE0L + CE1 chip enables, WEL write enable, ICL initial clear, PBUSY busy output, CLK clock in, HCLK sync clock out.
- Audio side: SYW/SYWI/SYWD sync signals, CHIN0/1 and CHOUT0/1 channel data (serial link between chips), DIN0/1 + DOUT0/1 "dry" in/out, SIN0/1 + SOUT0/1 "send" in/out. So the chip also carries the per-voice digital filter and mixes dry/send busses in the serial audio domain; the effects DSP (YSS233 on the PLG150-DX) sits on those serial busses.
- In the FS1R the flash driver code addresses two identical register banks, 0xC00000-0xC003FF and 0xC00400-0xC007FF, which is consistent with two YMP706s (32 notes = 2 x 16) selected by A10.

### 2.1 Board map from the firmware (v1.20)

Boot: reset vector 0x400 clears on-chip RAM, calls the BSC/PFC init at 0x44C, then the EPROM code (0x3AEF22, 0x39B050), the flash-upgrade check (0x3B03F0) and the normal entry (0x3B0108).

BSC init (0x44C): BCR1=0x2005, BCR2=0xC00C, WCR1=0x5222, WCR2=0xC534, DCR=0, RTCSR=0xD2, RTCNT=6 (a DRAM refresh setup: there is DRAM on the board), PFC PAIOR/PACR/PBCR/PCCR/PDCR/PECR set up, SCI0 SMR=0, BRR=0x1B (MIDI 31250 baud at 28.7 MHz), A/D control. Normal entry (0x3B0108): MTU channels 0-2 timers, CMT0, INTC priorities IPRA..IPRE, then RAM variables at 0x0101xxxx/0x0102xxxx.

| range | what | evidence |
|---|---|---|
| 0x00000000-0x0003FFFF | SH7044 flash | vectors, ISRs (SCI RX at 0x2F6E8, TX 0x2F930) |
| 0x00200000-0x003FFFFF | EPROM | main code 0x200000-0x22FFFF and 0x390000-0x3C7FFF, data in between |
| 0x00800100 | panel/LED latch on CS2 (16-bit, bit 9 set with every write; also pulsed by the flash-upgrade code) | FUN_003C04B8, FUN_003B0588 |
| 0x00800200-0x0080024F | YSS236 effect DSP host interface on CS2: 16-bit registers at 0x800200 + 2n, status at 0x800242 (bit 4 ready, bits 0-3 index) | FUN_0000B5E2 and its wrappers, see section 8 |
| 0x00C00000-0x00C007FF | the two YMP706s on CS3 (0xC00000 and 0xC00400) | `docs/ymp706_registers.md` |
| 0x01000000-0x0107FFFF | 512 KB DRAM work RAM | literal pools (0x0100xxxx-0x0106xxxx) and the entry code |
| 0x0140xxxx | second external device or SRAM/NVRAM (583 pointer words in the EPROM code) | to be identified |

### 2.2 ROM data

| EPROM address (CPU view) | contents |
|---|---|
| 0x230091 | 1152 preset voices (banks **PrC-PrK**) stored as DX7 VCED voices, 155 bytes each with the 10-char name first. The firmware converts them with its DX7 -> FS1R conversion at load time |
| 0x25C280 | 256 preset voices (**PrA, PrB**) as native 608-byte voices, byte-identical to the sysex bulk layout |

The bank order is the manual's own, page 21: PRESET A and B hold 128 FS1R voices each and PRESET C through K the nine banks of DX-series voices. An earlier reading had the part's BANK NUMBER byte mapped the other way round (2-10 to the DX7 block, 11-12 to the native one), which made every factory performance load the wrong voices - "FundaBass" asks for bank 2 program 41, and native voice 41 is "FundaBass" while DX7 voice 41 is "E.Piano 15". Across all 384 performances the corrected reading puts 439 parts on FS1R voices against 48, matches the performance's own category on 71% of them against 10%, and names 94 performances after their own part 1 voice against none.
| 0x20A000 | 384 preset performances, 400 bytes each, byte-identical to the sysex bulk layout. The three preset banks of 128 in order A, B, C: "Zap !" (PrA 001), "Sweepy Voice" (PrB 001) at entry 128, "UprightPiano" (PrC 001) at entry 256, ending at "Drum Kit 2" (PrC 128) with 0xFF fill after 0x22F800. Each block matches one of the Data List's performance lists entry for entry (127, 124 and 125 of 128 names identical in order; the rest are OCR damage in the text conversion). Preset C is confirmed independently of the booklet by the manual's own description of it, page 21 - the G50 guitar controller bank, "the maximum MIDI receive channel for these voices is 6, and the pitch bend range is -12 ... +12" - and entries 256-383 are the only block whose parts stop at channel A6 and whose bend range is 76/52 throughout. INTERNAL is battery-backed user RAM and is not in the image; the Data List prints its factory contents, which are a re-ordered selection from the presets, as the manual says on page 21. An earlier reading of this table started it at 0x20C580 and made it 360 entries, which silently dropped PrA 001-024 and mislabelled every bank |
| 0x283000 | preset Fseq 11-90, 6432 bytes each: a 32-byte header and 128 50-byte frames |
| 0x300A00 | preset Fseq 1-10, 25632 bytes each: the same header and 512 frames |
| 0x280140 area | Fseq voices "FseqBase01..14" (native format, forms = frmt) |

`tools/extract_presets.py` writes all of them to `presets/` as .syx files plus `presets/index.csv`.

A 512-frame Fseq bulk is 25632 data bytes, which the dump's 14-bit byte count cannot express. That is
why the Data List says "FSeq Bulk does not interpret Byte Count" (3.2.1): the header's own frame count
field (byte 0x1B, frames = 128 * (n + 1)) is what says how long the dump is, and the engine's loader
reads it there.

## 3. FS1R data model (from the Data List)

Bulk/sysex layout is fully documented and is the natural patch format for the softsynth:

- Voice = 112 common bytes + 8 x 62 op bytes (35 voiced + 27 unvoiced) = 608 bytes. Address 40-43 00 00 (part edit buffers) or 51 00 nn (internal).
- Performance = 80 common + 112 effect + 4 x 52 part = 400 bytes.
- Fseq = header + 128/256/384/512 frames, each frame: pitch (2), 8 voiced freq (2 each), 8 voiced level, 8 unvoiced freq (2 each), 8 unvoiced level.
- System = 76 bytes.
- The FS1R also converts DX7 VCED/ACED dumps into native voices, so the firmware contains a DX7 -> FS1R algorithm map.

Full tables: `docs/FS1R_DataList_text.txt` pages 37-42. Parameter behaviour: `docs/FS1R_OwnersManual_text.txt` pages 62-70.

## 4. Synthesis model (what the "FS" chip most likely computes)

Source: Yamaha patent US 5,610,354 (Koyama, Nishimoto; priority 1995-01-12), full text in `docs/US5610354_formant_synth_patent.txt`. It describes exactly the FS1R operator set: FM operators with feedback ("calculation units 20A/20B"), a formant unit ("20C") and noise formant units ("20D/20E"), all connectable through selectable routing (the 88 algorithms).

Formant operator (spectral form `frmt`):
- A window generator runs at the fundamental FP (the note pitch). The window shape is `sin^(2*SKT)(x/2)`: SKT is the Skirt parameter.
- A carrier sine runs at the formant centre FC (Coarse/Fine/Transpose/Detune; ratio or fixed) and is phase-reset at every window cycle (pitch synchronous).
- Output = window x carrier. Window time width is set by BW; two window generators offset by half a period allow the window to be up to two fundamental periods long (long window = narrow bandwidth). This is FOF-style formant synthesis.
- Modulation inputs (from other operators) add to the carrier phase and/or window phase, which is how FM and formants combine in one algorithm.

Harmonic forms (`all1/all2/odd1/odd2/res1/res2`) come from the same machinery with preset window widths ("1" = broad band, "2" = narrow band): `all` = window pulse train at the fundamental, `odd` = alternating-sign pulses at twice the rate (odd harmonics only), `res` = carrier locked to the harmonic number given by Resonance. `sine` is the classic FM operator. This mapping is inferred, not proven; it is the first thing to verify against recordings of the real unit.

Unvoiced operator: white noise -> low-pass (Skirt = NSK) -> add DC (Resonance = NRS, makes a peak at the centre) -> correlation stage (Bandwidth = NBW) -> ring-modulated by a sine at the formant frequency NF (or by the voiced operator's carrier for `linkFF`, or the fundamental for `linkFO`). Result: band-pass noise centred at NF with an optional pure-tone peak.

Everything else (EGs, level scaling, LFOs, pitch EG, filter, Fseq playback, effects) is conventional Yamaha and the exact curves are tables in the firmware.

## 5. Strategy

Two roads, and the plan uses both:

1. **Behavioural softsynth now** (this repo, C++, WinMM MIDI in + waveOut): implement the data model of section 3 with the synthesis model of section 4, load real FS1R voices, play from MIDI. Accuracy is limited by the guesses in section 4.
2. **Firmware analysis to replace the guesses** (`FS1R_DISASM`, Ghidra SH-2 via `tools/build_fs1r_ghidra.py`): find the YMP706 register block in CS1-CS3, the parameter -> register conversion tables (EG rates, level curves, frequency tables, window width/skirt tables, algorithm connection table for the 88 algorithms, DX7 map), and the preset voice/performance/Fseq data. These tables go straight into the softsynth.
3. Later, if exactness is wanted: run the real firmware on an SH-2 interpreter (MAME's SH7042 peripherals are a template) and drive the same synthesis engine from captured YMP706 register writes. That is the "true emulator" and also the author's MAME goal; it needs register semantics that only measurement of a real unit can confirm.

## 6. Sources

- https://github.com/rgwan/fs1r_firmware_RE
- http://studiorepair.com/gallery/Yamaha/FS1R/ (board photos with every chip identified)
- https://github.com/mamedev/mame/blob/master/src/mame/yamaha/yman1x.cpp (AN1x skeleton: names the YSS236-F as VOP3, no emulation)
- https://github.com/rgwan/completed-fs1r (18-bit I2S digital out board)
- https://yamahamusicians.com/forum/threads/im-trying-to-emulating-an-fs1r.23211/
- https://sites.google.com/site/undocumentedsoundchips/yamaha/ymp706
- https://patents.google.com/patent/US5610354A/en
- Data List: https://usa.yamaha.com/files/download/other_assets/4/317954/FS1RE2.PDF
- Owner's manual: https://usa.yamaha.com/files/download/other_assets/5/333335/FS1RE1.PDF
- MAME SH7042: https://github.com/mamedev/mame/blob/master/src/devices/cpu/sh/sh7042.cpp

## 7. Firmware findings (2026-09-13)

- The tone generator driver lives in the SH7044 flash (0x22000-0x2E000). It is event driven: FUN_000224F8(id) then
  FUN_0002251E(value). Sysex parameter handlers (jump table 0x3DE64, case = 0x11 + parameter offset, operator
  parameters at 0x70 + n) convert voice bytes into chip register writes. Register map, events and every CPU-side
  formula: `docs/ymp706_registers.md`.
- Two YMP706 chips (0xC00000 and 0xC00400), 16 channels each, channel select at 0x3FF/0x7FF, PBUSY polled on port A
  before every byte. Per-operator registers are 8-byte stripes, 16-bit values are hi at reg and lo at reg+8.
- The 88-algorithm table is at EPROM 0x37C0DC (`src/fs1r_algorithms.h`), decoded as a bus model. Algorithms 9-40 are
  DX7 algorithms 1-32.
- Frequency words are log2 pitch, 1024 per octave, 440 Hz at 26861. Ratio mode adds the channel pitch inside the
  chip; fixed and formant frequencies are absolute with CPU-side key tracking and velocity.
- The CPU does far more than expected: the pitch EG, LFO1, portamento, bend, velocity, key scaling, EG bias, part
  levels and Fseq frame stepping are all software, refreshed into the chip on a 192.3 Hz timer (MTU2). Their tables
  are extracted into `src/fs1r_rom_tables.h`. CPU clock 28 MHz (SCI divisor 27 = 31250 baud).
- Chip-side only: EG timing and shapes, dB per level step, per-op modulation sensitivity scaling, the formant window
  and noise formant, the filter, effects. Those are modelled with DX7 and patent priors and marked INFERRED.

## 8. Effect DSP (2026-09-14)

The effects run on two YSS236-F chips. The firmware drives them through one 16-bit register block at 0x800200 (`FUN_0000B5E2(reg, value)` writes `0x800200 + 2*reg`; `FUN_0000BC56/BC68` read the status word at 0x800242). Register 0 is an address latch (bit 15 selects a second address space), the other registers are data ports into the DSP's internal memories:

| register | written by | contents |
|---|---|---|
| 1 | init | reset/run (1 at the start of init, 0 at the end) |
| 5 | init | mode word 0x1004 |
| 6-10 | FUN_0000B600, init | five 16-bit words per program step, 512 steps: the VOP3 program. Boot image at EPROM 0x375E1A (alternate 0x37721A), 5 x 1024 bytes, copied to DRAM 0x01069978 and patched per effect type |
| 0xB | FUN_0000B6A4 | one word per step from 0x37541A (alternate 0x37581A), DRAM copy 0x01068F78 |
| 0xC | FUN_0000B788/B7EE | one byte per step from 0x375C1A, DRAM copy 0x01069628; 0x0E is the mute value used while a type changes |
| 0x16 | FUN_0000B85E | 15 per-bus words from 0x37861A (address = bus + 1) |
| 0x17 | init | 0x23 during the upload, 0 after |
| 0x24-0x2A | FUN_0000B8EC..BA84 | 15 per-bus values each (levels, sends, pans; defaults 0x15, 0, 0x1000) |

The step addresses written before each data word come from the list at 0x37501A. `FUN_0000BC8C(variant)` does the whole upload at boot; `FUN_0020315C` returns the variant (0-3) that picks between the table sets and is not yet understood. An effect type change (`FUN_0000D050(block, part)`, types listed at 0x374F2E) writes 0x0E to the block's steps, waits, patches the program words through `FUN_0000B600` with the per-type table (0x378629/0x37862F/0x378635/0x37863B, 0x378642), then restores. Parameter edits go through `FUN_0000C36C..C6C0` into the 0xB/0xC ports.

Consequences: the effect algorithms are about 6 KB of VOP3 microcode in the EPROM, not fixed functions in the chip. MAME has no VOP3 core (`yman1x.cpp` is a skeleton that maps the chip onto an unemulated stub) and no instruction set description exists in public, so exact effects would need the ISA reverse engineered from this microcode plus recordings. The plan models the effects from the Data List and keeps the firmware's parameter-to-coefficient conversions and the extracted microcode as reference (TODO.md, Tier 0 and Tier 4).
