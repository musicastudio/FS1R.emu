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
- Tone generator YMP706 ("FS"), effects DSP (YSS233 on the PLG150-DX; the FS1R has its own, TBD), 18-bit right-justified serial DAC.

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
| 0x00C00000-0x00C007FF | register block on CS3: the flash driver code addresses 0xC00000+0x0C8/0x1C8/0x22C/0x260/0x270/0x490/0x62C/0x3FF/0x7FF | literal pools of the flash code. Best candidate for the YMP706 |
| 0x01000000-0x0107FFFF | 512 KB DRAM work RAM | literal pools (0x0100xxxx-0x0106xxxx) and the entry code |
| 0x0140xxxx | second external device or SRAM/NVRAM (583 pointer words in the EPROM code) | to be identified |

### 2.2 ROM data

| EPROM address (CPU view) | contents |
|---|---|
| 0x230091 | 1152 preset voices (banks PrA-PrI) stored as DX7 VCED voices, 155 bytes each with the 10-char name first. The firmware converts them with its DX7 -> FS1R conversion at load time |
| 0x25C280 | 256 preset voices (PrJ, PrK) as native 608-byte voices, byte-identical to the sysex bulk layout |
| 0x20C580 / 0x22F350 | performance data ("Everybody", 0x40 pan bytes) |
| 0x280140 area | Fseq voices "FseqBase01..14" (native format, forms = frmt) |

`tools/extract_presets.py` writes all of them to `presets/` as .syx files plus `presets/index.csv`.

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
