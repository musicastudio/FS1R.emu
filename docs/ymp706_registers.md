# YMP706 ("FS") register interface and the CPU-side engine, from FS1R firmware v1.20

Source: flash code 0x22194 (chip init), 0x224F8 (event setter), 0x2251E (event handler), 0x22B20, 0x27178/0x27284
(write helpers), ROM tables listed at the end. Addresses are CPU addresses on CS3; the chip has A0-A9 and an 8-bit
write-only data bus. Everything in this file is read from the firmware or its ROM tables unless a line says INFERRED.
The same split is marked in `src/fs1r_emu.cpp`: comments saying INFERRED are chip-side assumptions, everything else
is firmware behaviour.

## Bus protocol

- Two chips: chip 0 at 0xC00000-0xC003FF (PBUSY on port A bit 8, PADR 0xFFFF8382 & 0x100), chip 1 at 0xC00400-0xC007FF
  (PBUSY on port A bit 5). Channels 0-15 live on chip 0, 16-31 on chip 1. 32 channels = the 32-note polyphony.
- Before every single byte write the CPU spins until PBUSY is low.
- Register 0x3FF (0x7FF on chip 1) = channel select, 0-15. All per-channel registers below refer to the selected channel.
- 16-bit values are written as two bytes: high byte at reg, low byte at reg+8.
- Per-operator registers are 8-byte stripes: reg + op (op 0-7).

## Register map (per chip, offsets)

| offset | scope | contents |
|---|---|---|
| 0x00-0x18 | channel, op (voiced) | EG levels L1-L4 = `LEVTAB[L] >> 1` (6-bit attenuation, image +0x40..+0x5F) |
| 0x20-0x38 | channel, op | EG times T1-T4 as rates `((99-T)*0xA4)>>8` (0..63), part EG attack/decay/release offsets added to T (image +0x60..+0x7F) |
| 0x40 | channel, op | EG hold: same rate formula then +4, capped 0x3E. 0x3F means no hold. The chip holds for half a traverse at that rate plus 11.4 ms, measured; `docs/aeg.md` |
| 0x48 | channel, op | level attenuation, 8-bit: `min(255, levelOffset + egBias) | flags`, refreshed every tick (see Levels) |
| 0x50 | channel, op | EG time scaling 0..7. The chip adds `trunc(tscale * keyoff / 8)` to every rate, where keyoff comes off register 0xC0; measured, `docs/aeg.md` |
| 0x58 | channel, op | `fms << 3 | ams` (freq mod sense only when the op is fixed) |
| 0x60-0x78 | channel, op | frequency EG init level, attack level (`FEGLVL[v]`, 0..255 with 128 the centre, image +0x98/+0xA0) and attack, decay time as rates (image +0xA8/+0xB0) |
| 0x80 | channel, op | detune as sign-magnitude, low nibble the amount and 0xF0 the sign: `d - 15` for d >= 15, `~d` below (image +0xB8) |
| 0x90/0x98 | channel, op | 16-bit operator frequency word (see Frequency) |
| 0xA0/0xA8 | channel | 16-bit LFO frequency modulation word (see LFO), 0xA8 written with the low byte |
| 0xB0 | channel | LFO amplitude modulation attenuation 0..255 (see LFO); 0xB8 always 0 |
| 0xC0 | channel | key code `(pitchWord >> 8) + 10`, which is **note/3 + 73**, not note/3 + 10 as this line said until 2026-09-19: the pitch word is 0x3FAA at note 0, not 0. That gloss is what made the engine's old rate scaling look like dead code when it was merely wrong. Measured at ten key codes: a table, not a line, saturating at +-13; `docs/aeg.md` |
| 0xC8 | chip | init 2 |
| 0xF8/0xF9 | channel | 16-bit LFO pitch modulation word, signed, +-0x7FF (see LFO) |
| 0xFA/0xFB | chip | channel bit mask written at note off (release: EG stage 4) |
| 0xFC/0xFD | chip | channel bit mask written at the start of note on, init 0xFFFF (damp / restart) |
| 0x100-0x1C8 | same as 0x00-0xC8 for the unvoiced operators | image +0xC0..+0x13F, 0x148 level, 0x190/0x198 frequency |
| 0x200/0x208 | channel, op | 16-bit algorithm word (`src/fs1r_algorithms.h`) with carrier level correction 0..15 in bits 4-7 of the low byte |
| 0x210 | channel, op | form (bits 0-2), skirt (3-5), fixed (6) |
| 0x218 | channel, op | voiced bandwidth `clamp(bw + ctrl * BWBIAS[bias], 0, 99)`, bit 7 passed through |
| 0x220 | channel, op | pitch mod sense 0..7 |
| 0x228 / 0x229 | channel | voiced / unvoiced part level: `min(255, VNBAL[index] + 0x10)` (see Part levels) |
| 0x22A / 0x22B | channel | `~(((255 - pan) * s) >> 7)` and `(pan * s) >> 7`, s = image +0x3D (+1 when non-zero) |
| 0x22C / 0x22D | channel | `min(255, image[+0x3E] + PANL[pan >> 1])` and the same with PANR: the main pair |
| 0x22E / 0x22F | channel | the same with image[+0x3F] and a second pan source: the individual out pair, which reaches the slave DAC and the INDIVIDUAL OUTPUT jacks |
| 0x230/0x238 | channel, op | frmt ops: `0x1243 + TRANS[transpose] + FRMDET[detune][band]`; other forms: the raw byte 6 |
| 0x240/0x248 | channel | 16-bit channel pitch word (see Pitch) |
| 0x260 | channel | feedback level 0..7 |
| 0x268 | channel | image +0x181 (init 3) |
| 0x270 | chip | 10, or 11 when any part has its filter on: on the board this switches the CHOUT/CHIN loop to VOP3-1 in (see `docs/research.md` 2.0.1) |
| 0x300 | channel, op | unvoiced `(mode + 1) << 6 | res << 3 | skirt`, so the three modes are 1, 2, 3 on the chip; linkFF demoted to 0x80 when the voiced op is not frmt |
| 0x308 | channel, op | unvoiced bandwidth `clamp((bw*2*0xA5)>>8 + ctrl * BWBIAS[bias], 0, 127)` |
| 0x310/0x318 | channel, op | unvoiced transpose word `0x1243 + TRANS[transpose]` |
| 0x3FF | chip | channel select |

## Events

`FUN_000224F8(id)` selects an event, `FUN_0002251E(value)` feeds it bytes (16-bit values high byte first).

- 0x000-0x021: per-op sysex byte to register through tables 0x35C558 (image offset) / 0x35C59C (register).
- 0x100-0x1DF chip 0 channel events, 0x250-0x2D3 the same for chip 1: 0x100 select channel, 0x102 select voice image,
  0x103 channel + image, 0x105 0xF8/F9, 0x107 key off mask (sets PEG stage 4, release rate/level), 0x109 0xFC/FD,
  0x10B-0x112 voiced level offsets (8 bytes, writes 0x48 on the 8th), 0x113-0x11A unvoiced, 0x11B-0x122 voiced bandwidth
  offsets, 0x123-0x12A unvoiced, 0x12B-0x13A frequency words, 0x13B-0x14A 0x230 words, 0x14B-0x15A unvoiced frequency,
  0x15B-0x16A algorithm words, 0x16B-0x17A unvoiced transpose, 0x17B channel base pitch, 0x17D/0x17E part levels,
  0x17F feedback, 0x181 note-on finish (PEG init, 0xC0), 0x183 0x270, 0x1CC key factor, 0x1D0-0x1DF bandwidth bases.
- 0x200-0x24F voice-image events: 0x205 EG bias controller (recomputes image +0x1C0/+0x1C8), 0x206 bend word,
  0x20B/0x20D portamento target/start pitch, 0x211 pan, 0x212-0x215 level bytes, 0x216 Fseq pitch offset,
  0x218 velocity (also copies the EG time registers), 0x220 Fseq level velocity, 0x222-0x224 pan scalers,
  0x225 pan offset, 0x226 pitch refresh, 0x228/0x22A bend offset, 0x22C clear bend, 0x230-0x23F EG bias sensitivities,
  0x240-0x24F bandwidth bases.
- 0x300-0x32B: stored in the image header through table 0x35C7E4: pitch EG levels (words at +0x0E..+0x17), pitch EG
  times (+0x06..+0x0D), LFO speed increment (+0x02), LFO delay increment (+0x04), LFO wave (+0x19), key sync (+0x1A),
  PMD (+0x1C), AMD (+0x1D), FMD (+0x1E), pan LFO depth (+0x1F), PEG velocity sens (+0x18), range (+0x1B), time scaling
  (+0x21), portamento rate (+0x2A), portamento switch (+0x3B).
- 0xF00/0xF04: note-on image copy (27 groups of 8 bytes + 2 bytes, tables 0x35C6AC / 0x35C6EC / 0x35C768), 0xF01 the
  algorithm words, 0xF08-0xF0D LFO waveform helpers.

## The voice image, measured

The driver keeps sixteen 512-byte voice images per tone generator at 0x01039384, `image_base + n * 0x200`, and a
channel plays one of them. The image is the CPU's decoded copy of the voice, so the register table above quotes
offsets into it. rgwan read all sixteen back off a running unit for each of the 492 segments of the audio capture
set on 2026-09-18, with the exact voice and performance bulk that produced each one, which places every byte that
moves and turns the conversions into measurements rather than readings of the code.

**The image is the same under every note and every velocity.** That is the structural thing to know about it. The
level offsets, the frequency words, the pitch word and the bandwidth registers are all note or velocity dependent
and none of them is here; they live in the per-channel arrays at 0x0103B384, 0x01044C44 and 0x01045544. Nothing to
do with the filter is here either, since that is VOP3-1's and sits at 0x0106AExx.

Rows the capture set never moved are marked "event list": those come from the 0x300-0x32B event table above and
are where the firmware puts them, but nothing here has checked the value.

| offset | contents |
|---|---|
| +0x02 | LFO1 speed increment, 16-bit: `2 * v * (v < 160 ? 11 : 11 + (v - 160) / 4)` for `v = eb86(speed)`, 22 when v is 0 |
| +0x04 | LFO1 delay increment, 16-bit: `((16 + (q & 15)) << 10) >> (7 - (q >> 4))`, `q = 99 - delay` |
| +0x06..+0x0D | pitch EG times (event list) |
| +0x0E..+0x17 | pitch EG levels, words (event list) |
| +0x18 | PEG velocity sensitivity (event list) |
| +0x19 | LFO1 waveform |
| +0x1A | LFO1 key sync |
| +0x1B | PEG range (event list) |
| +0x1C / +0x1D / +0x1E | PMD / AMD / FMD, each `eb86(v)` |
| +0x1F | pan LFO depth, `eb86(v)`, reading back as 1 rather than 0 at depth 0 |
| +0x21 | PEG time scaling (event list) |
| +0x2A | portamento rate (event list) |
| +0x2E | pan, twice the part byte, 254 raised to 255. The table index is `pan >> 1`, so it is the part byte itself |
| +0x3B | portamento switch (event list) |
| +0x3D / +0x3E / +0x3F | the three output pair scalers, registers 0x22A/B, 0x22C/D and 0x22E/F |
| +0x40..+0x5F | EG levels L1-L4, `LEVTAB[L] >> 1` |
| +0x60..+0x7F | EG rates T1-T4, `((99 - T)*0xA4)>>8` with the part's EG offsets already in T |
| +0x80..+0x87 | EG hold rate, the same conversion then +4 capped 0x3E, and 0x3F left alone |
| +0x88..+0x8F | EG time scaling |
| +0x90..+0x97 | `fms << 3 \| ams` |
| +0x98..+0x9F | frequency EG init level, `FEGLVL[v]` |
| +0xA0..+0xA7 | frequency EG attack level, `FEGLVL[v]` |
| +0xA8..+0xAF | frequency EG attack rate, the EG rate conversion |
| +0xB0..+0xB7 | frequency EG decay rate |
| +0xB8..+0xBF | detune, sign-magnitude |
| +0xC0..+0x137 | the unvoiced operators, the same eight stripes in the same order |
| +0x140..+0x147 | pitch mod sense |
| +0x148 / +0x149 | voiced / unvoiced part level, registers 0x228/0x229 |
| +0x150 / +0x158 | register 0x230 word, hi and lo |
| +0x160..+0x167 | register 0x300, `(mode + 1) << 6 \| res << 3 \| skirt` |
| +0x180 | feedback level |
| +0x181 | register 0x268, 1 in every segment captured |
| +0x190 / +0x198 | algorithm word, hi and lo |
| +0x1A0..+0x1A7 | register 0x210, form, skirt and fixed |
| +0x1C0 / +0x1C8 | EG bias, voiced and unvoiced (event 0x205, event list) |

`FS1R.unlock/captures/image_check.py` walks a session and checks every one of those against the conversion, which
is 147,548 checks on the 2026-09-19 set, all of them passing, with nothing unexplained left over. Six came back
different from what this file said:

- the frequency EG levels go through `FEGLVL` rather than being linear in the sysex byte, and half the displayed
  depth is only a fifth of the register offset,
- the frequency EG times are rate-converted like the amplitude EG, not the raw bytes,
- the unvoiced mode field is one higher on the chip than in the voice,
- the detune register is sign-magnitude, not the raw 0..30 byte,
- `VNBAL[index] + 0x10` saturates instead of wrapping,
- and the pan index is the part's pan byte, not one below it, which the Pan section below sets out.

Everything else held exactly, including the two LFO1 increments, both EG level and rate conversions, the hold
rate's +4, the part level chain through volume, expression and balance, and the whole unvoiced half. Two things
the set cannot speak to: every segment leaves the part's EG offsets at 64, so the decay offset reaching T2 and T3
both is still FUN_00019414's word rather than the unit's, and the pitch EG never moves.

**The algorithm word.** The connection table's two bytes per operator become `hi = t0 >> 1`, `lo = (t1 << 1) | (t0 & 1)`,
with the carrier level correction in bits 4-7 of the low byte, and an input select of 0 promoted to 1 on the way.
Only three distinct `t0` values appear in the capture set, so the promotion is the least tested line here.

**Which pair carries the sound.** In all 492 segments +0x3D reads 0x7F and +0x3E and +0x3F read 0xFF, which mutes
the 0x22C/D and 0x22E/F pairs and leaves 0x22A/0x22B as the pair the note comes out of. Those patches send nothing
to either effect block, so the reading is that 0x22A/0x22B is the dry pair and the other two are the sends; the
capture set never moved a send level, so that is an observation rather than a measurement.

## Note on (FUN_00010B48, FUN_000112C0, FUN_000125F8)

1. Part filters: note limit low/high and velocity limit low/high (a high below low means "outside the gap").
2. Velocity: `v' = clamp(((depth * VELCURVES[sysCurve][v]) >> 6) + (offset - 64) * 2, 1, 127)` with the part's velocity
   sense depth/offset and the system velocity curve (thru, soft 1, soft 2, wide, hard).
3. Mono parts keep a held-note list and pick the sounding note by priority (last, top, bottom, first); with a note
   already sounding the channel is retuned without retriggering (FUN_000119D0). Poly parts take a free channel.
4. Pitch (FUN_00010DC4): note' = note + system transpose + performance shift + part shift + voice shift, each step
   clamped to 0..127. `pitch = NOTETAB[note'] + detune + (masterTune - 64)` where detune =
   `(partDetune - 64) * (15 - (note' >> 3)) / 15` (at least +-1). Key factor = `KEYFACT[note'] >> 2`.
5. Velocity tables: `VELW[v']`, `VELW[127 - v']`.
6. Per operator (FUN_00012A32, FUN_00012FCC, FUN_00012E2C, FUN_0001E838, FUN_000135D8, FUN_0001386A, FUN_00013BC6):
   - voiced level offset = `velAtt + keyScale`, capped 255, where `velAtt = (15 - 2s) + (s * 32 * VELW[x]) >> 8`
     (s = sens+1 and x = v' for positive sensitivity, s = 7 - raw and x = 127 - v' for negative) and keyScale =
     `2 * clamp(LEVTAB[level] +- (curve[dist] * eb86(depth)) >> 8, 0, 127)`; dist is in key groups
     `(KEYFACT[n] >> 2) - 3`, break point = sysex value + 21, curves KSEXP for -EXP/+EXP and KSLIN for -LIN/+LIN,
     "-" curves add attenuation, "+" curves remove it.
   - unvoiced level offset = `clamp(min(255, 2 * LEVTAB[level] + velAtt) - (lks * 64 * (n - 60)) >> 8, 0, 255)`.
   - EG bias: `|sens| * EGBIAS[ctrl or 255 - ctrl] >> 3`, added to the level register every tick.
   - frequency word: ratio ops `0x1243 + COARSE[c] + FINE[f]` (chip adds the channel pitch); fixed and frmt ops
     `8 * (c * 128 + f) + 0x28ED + keytrack + fvs` with `keytrack = (eb70(notescale) + 1) * (pitch - 0x53AA) / 128`
     and `fvs = +-((v' - 64) * FVSTAB[sens]) >> 3`. Unvoiced: same as fixed, saturated at 0x7F00.
   - formant transpose word for frmt ops: `0x1243 + TRANS[transpose] + FRMDET[detune band][pitch band]`, pitch band
     from `(pitch >> 8) + 10` (0 below 0x50, `(x ^ 0x10) & 0x1F` up to 0x70, else 31), detune below 15 negated.
6b. EG times (FUN_00019414): the part's EG bytes 0x1A, 0x1B, 0x1B, 0x1C are walked against the voice's
   T1-T4, so the **decay offset reaches both T2 and T3**; each is `clamp((offset - 0x40) + T, 0, 99)`
   before the rate conversion. The hold rate gets +4 capped at 0x3E only when it is below 0x3F.
7. Pitch EG (FUN_00026C5A): levels `(PEGLVL[L] - 128) << 7`, times `PEGTIME[T]`; part PEG offsets apply to L0, L4,
   T1, T4. velP = 128 when the velocity sensitivity is 0, else `v' + 1` (the graded PEGVEL table is computed by event
   0x218 and then overwritten). Level = `(word * velP >> 7) >> (range ? range + 1 : 0)`, rate =
   `min(254, (PEGTIME * (velP + 1) >> 7) + min(255, tscale * keyfact))`, 255 stays 255 (instant). Stage 0 runs L0 to
   L1 at T1 (skipped when T1 is instant), then L2 at T2, L3 at T3, hold, key off: L4 at T4, hold.
8. Portamento (FUN_000124FE): rate `PEGTIME[(time * 100) >> 7]`; fingered mode glides only when another key is held.
9. Fseq trigger (FUN_0000FFFA) when the performance's Fseq part is this part: on the first note, or every note.

## Tick (MTU channel 2, TGRA compare every 0x238B counts at clock/16 = 5.20 ms, 192.3 Hz)

Per channel (FUN_00028674):

- LFO1 (FUN_00028828): delay counter += delayInc, saturating at 0xFFFF; once saturated the fade counter += delayInc
  (same increment). `delayInc = ((16 + (q & 15)) << 10) >> (7 - (q >> 4))`, q = 99 - delay (with the part offset).
  Phase (16-bit) += speedInc, `speedInc = 2 * v * (v < 160 ? 11 : 11 + (v - 160) / 4)`, v = eb86(speed + part offset)
  + 2 * controller, v = 0 gives 22. Waveforms from the phase: tri, saw down, saw up, square, sine (table SINE64), S&H
  (random walk in SHTAB on every wrap). Speed 0 = 0.065 Hz, 99 = 50.9 Hz, delay 99 = 2.7 s plus a 2.7 s fade.
- Pitch EG stepper (FUN_00028C8E): the level moves linearly by `rate` per tick toward the stage target.
- Portamento (FUN_00029048): step = `(1 + |diff| >> 10) * rate` toward the target.

Registers refreshed from those (FUN_0002C798, FUN_0002C214, FUN_0002AD72, FUN_0002B458):

- 0x240 pitch = `portamentoPitch + (peg >> 2) + bendWord + fseqPitch`.
- 0xF8 LFO pitch = `(lfo * ((PMD' * fadeHi) >> 8 & 0xFF)) >> 4`, capped 0x7FF. PMD' = eb86(pmd + part offset) +
  controller, fadeHi = fade >> 8. 1024 units per octave, so PMD 99 gives about +-1 octave before the per-op pms.
- 0xA0 LFO frequency mod: same with FMD'.
- 0xB0 LFO amplitude: `EGBIAS[(((AMD' * clamp(fadeHi + ctrl)) >> 8) * (127 - lfo)) >> 8]`.
- 0x48 level = `clamp(levelOffset + egBias, 0, 255) | flags`, Fseq ops use the frame level `<< 1`, scaled by the
  Fseq level velocity `~((~level) * k >> 8)` with k from VELW of the VELCURVE-mapped velocity.
- 0x218 bandwidth = `clamp(base + ctrlOffset & 0x7F, 0, 99) | bit 7`.
- Bend word (FUN_00020194): `BENDTAB[|range|] * sign * (bend >> 3) >> 7`, range high for up, low for down, bend =
  (MSB - 64) * 16.

## Controller sets (FUN_00014DDC, FUN_00014AD0, FUN_000191C0)

The performance's eight controller sets live at common 0x28-0x4F: part switch (0x28 + n), a 14-bit
source bitmap (0x30 + 2n high, 0x31 + 2n low), destination (0x40 + n) and depth (0x48 + n).

**Sources.** FUN_000191C0 fills the source table in this bit order, bit 0 first:

| bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| source | KN1 | KN2 | KN3 | KN4 | MC1 | MC2 | PB | CAT | PAT | FC | BC | MC3 | MW | MC4 |
| from | sys 0x16 | 0x17 | 0x18 | 0x19 | 0x1A | 0x1B | status E0 | D0 | A0 | 0x1E | 0x1F | 0x1C | CC1 | 0x1D |

The four knob slots are disabled (control number 0xF7) when the system's knob receive switch (0x14) is
off. FUN_00014AD0 stores each arriving value per receive channel, 14 signed 16-bit slots per channel:
the knobs and MIDI controls bipolar as `(v - 64) * 2` with 126 raised to 127, the physical controllers
(PB, CAT, PAT, FC, BC, MW, mask 0x17C0) as the raw value. Both end up in -128..127.

**Sum.** For a set whose destination matches, walk the bitmap from bit 0 up, adding each enabled
source and clamping to -128..127 after every addition.

**Scaling.** The sum and `depth - 0x40` go to a per-destination handler from the pointer table at flash
0x3DC04. Three scalers, with `v' = v + (v > 0)` and `d' = d + (d > 0)`:

| helper | formula | destinations |
|---|---|---|
| FUN_00016F9C | `clamp((v' * d' * 2) >> 6, -128, 127)` | 1-16, 34-47: a signed offset straight to the tone generator |
| FUN_00016F58 | `clamp(base + ((v' * d' * 2) >> 7), 0, 127)` | 18, 21-33: edits a part byte |
| FUN_00017032 | `clamp(base + adj((v' * d' * 2) >> 6), 0, 127)`, adj taking one off a positive result | 17, 19, 20: volume and the two sends |

`base` is the stored part byte, and the handler writes the result back as a parameter change. That is
exactly the split the owner's manual describes: destinations 17-33 "overwrite the edit buffer" and show
the edit mark, 1-16 and 34-47 "directly control the tone generator without affecting" it.

**Part switch.** The table at EPROM 0x35C4C4 flags destinations 17-45 as per-part; only those consult
the set's part switch. Destinations 1-16 (the insertion parameters and its two sends) and 46-47 (the
Fseq) are performance-wide.

**Destinations that are not a plain offset:**

- **34 pitch bias** (ctrlDest_34): the depth is the range in semitones, not a scaler.
  `word = sign(depth) * BENDTAB[min(|depth|, 24)] * sum >> 7`, matching the manual's "if Vcn depth is
  set to +2 then the maximum control value is +two semitones".
- **35 amplitude EG bias** (ctrlDest_35): one per-part value `~((|scaled| + T[min(|depth|, 31)]) * 2)`
  capped at 0xFF, T being the table at flash 0x3DDC4 (127, 119, 115, ... 3, 0). Event 0x205 then scales
  it by each operator's own EG bias sense.
- **36 frequency bias** (FUN_0001B334): per operator, sense = `((op[0x1F] & 0x78) >> 3) - 7`, offset =
  `(int16)(scaled * sense * 0x10) >> 2` added to the operator's frequency word.
- **37/38 bandwidth** (ctrlDest_37, ctrlDest_38): `clamp((scaled * BWBIAS[bias nibble]) >> 3, -128, 127)`.
- **39 LFO1 pitch mod** (ctrlDest_39): the offset is `2 * scaled`, 254 raised to 255.
- **43 LFO1 speed** (ctrlDest_43): `clamp(eb86(voice speed + part offset) + 2 * scaled, 0, 255)`.
- **46 Fseq speed** (ctrlDest_46): `clamp(ratio + ((scaled * 0x1324) >> 7), 100, 5000)`.
- **47 formant scratch** (ctrlDest_47): the scaled signed byte is the transport position, and it only
  applies when the performance's Fseq play mode is scratch.

## Part levels (FUN_00020044)

`u = ((volume + 1) * expression) >> 8`; voiced index = unvoiced index = u + 1; balance below 64 scales the unvoiced
index by `(u + 2) * bal >> 6`, above 64 the voiced index by `(u + 2) * (128 - bal) >> 6`; registers 0x228/0x229 =
`min(255, VNBAL[index] + 0x10)`. Expression is 0..254 (CC11 * 2), initial 254. The saturation is not cosmetic:
balance 0 lands on `VNBAL[0] = 255`, and wrapping there would turn a muted unvoiced half into a loud one.
Measured against the volume, expression and balance sweeps in the 2026-09-19 voice images.

## Fseq playback (FUN_0000FFFA, FUN_0001A59E, FUN_0001E838)

Frame timer: CMT1 (FUN_0001A59E), `CMCOR1 = VELW[speedAdjust] * 84 * 1000 / ratio + 2884` counts at
clock/32, confirmed instruction for instruction. A ratio below 100 means MIDI clock instead: CMT1 is
parked at a fixed 7000 counts and FUN_0001ACB6 advances the sequence from the clock count, weighting
each tick by 25, 50, 100, 200 or 400 for speed words 0 to 4, so the five settings are 1/4, 1/2, 1/1,
2/1 and 4/1 of the base rate. (ratio in 0.1 %,
100..5000; 100 % with adjust 64 gives 7.9 ms per frame). Tempo velocity: `ratio -= (ratio - 100) * sens * (127 - v') /
7 / 127`. Frames are 50 bytes: pitch hi/lo, 8 voiced formant frequency hi, 8 lo, 8 voiced levels, 8 unvoiced hi, 8 lo,
8 unvoiced levels; words are `hi * 256 + lo * 2`. An operator with its Fseq switch on reads track `fseqtrk` of the
frame instead of its own frequency and level. The fundamental follows `framePitch - (NOTETAB[noteAssign] + 0x1243) +
(tuning - 63)` unless the performance's formant pitch mode is 1; FUN_000127FC confirms that expression
to the constant. Nothing shifts the frame's own formant frequencies, which is the point of the format:
the formants stay where the sequence put them while the fundamental follows the key. Loop start/end and start offset come from the
performance, the end of valid data from the Fseq header. Preset Fseqs live in the EPROM at 0x300A00 (1-10, 512 frames,
25632 bytes each) and 0x283000 (11-90, 128 frames, 6432 bytes each), header 32 bytes then frames.

## Performances

360 entries of 400 bytes at EPROM 0xC580 (PrA, PrB, then the factory internal set): 80 common + 112 effect + 4 x 52
part, sysex layout. Part bank 2..12 = PrA..PrK (PrA and PrB are the native banks, PrC-PrK the DX7-format ones).

## INFERRED (chip side, not in any file)

- Level register step 0.375 dB (LEVTAB is the DX7 0.75 dB curve and the chip gets 2 x LEVTAB), EG level step 1.5 dB.
- EG timing: rate 0..63 timed like the DX7 EGS (Dexed increments), rate scaling `tscale * (keycode - 80) >> 3`.
- Per-op pms as the DX7 pitch mod sensitivity curve, ams and fms as linear fractions of the channel words.
- Feedback `0.5 * 2^(fb - 7)`, modulation index 1 cycle at full level, detune 2 cents per step on non-formant ops.
- Frequency EG range +-4 octaves, timed like the amplitude EG.
- The formant window (bandwidth and skirt), the harmonic forms, the noise formant: patent model, see research.md.

## The per-voice filter (VOP3-1, not this chip)

Read out of the firmware on 2026-09-15, and it settles what rgwan's board wiring suggested: the filter is
not a YMP706 feature. Register 0x270 only switches the tone generator's channel loop out to VOP3-1, and
every filter parameter goes over VOP3-1's own register block at 0x800200 (the chip select is settled:
`docs/research.md` section 8).

**The path.** A parameter change lands in `FUN_0000B2E4(address, part)`, which compares the address against
the filter's own bytes and sets one of four per-part dirty flags at 0x01068F14/18/1C/20, then posts
`FUN_00009FA4(7, 3)`. The groups are voice 0x54 (type) alone; 0x55, 0x56, 0x57, 0x59, 0x5B, 0x5C, 0x5D with
part 0x18 and 0x19; 0x5A with part 0x18, 0x19, 0x2E, 0x2F; and 0x56, 0x64 with part 0x1F. Controller
destinations write their own values first: `FUN_0000B3F4` (from ctrlDest_42) into 0x0106AE6C, `FUN_0000B40E`
into 0x0106AE74 and `FUN_0000B428` into 0x0106AE7C, one 16-bit word per part.

**The channels.** VOP3-1 carries 16 filter channels. The table at 0x0106AD8C maps each to a part, and
`FUN_0000DB3C(part)` and `FUN_0000E2A0(part)` walk all 16 and update the ones that match, so a part's filter
is sent once per channel it owns. The per-channel note sits at 0x0106ADAD.

**Cutoff** (`FUN_0000C36C`): `coef = clamp(0xC0D + 0xA9 * cutoff + ks * (note - breakpoint), 0xC0D, 0x6000)`,
written into the VOP3 coefficient memory at 0x01068F78 through the per-channel step address at 0x00374E44.
`cutoff` is `voice[0x57] + part[0x18] - 0x40` plus the LFO and controller terms, clamped 0..127 first. So the
byte is **linear in the coefficient**, spanning 8:1, not linear in octaves.

**Resonance** (`FUN_0000C3D0`): `voice[0x55] + (part[0x19] - 0x40) * 2`, note the doubled part offset, plus
`(voice[0x56] - 7)` times a velocity term, clamped 0..0x74 (116). The raw value indexes two tables and both go
into the coefficient memory. With `r = 2^(-raw/16)` the damping, 0x00374B24 is `A = 1 - r` and 0x00374C24 is
`B = max(0, 0.5 - 2r^2)`, quadratic in the same damping: zero while `r >= 0.5`, then rising to 0.5. Filter types
3 and 5 (HPF and BEF) write zero instead of B. The two land in different regions of the coefficient memory
(`docs/research.md` section 8): A in a run of four adjacent slots, one per channel of a block, and B alongside
that channel's two input-gain slots, which is where a feedforward term would sit.

**Type** (`FUN_0000C1AC`): 0..6 become 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xE0 in bits 5-7 of the channel's
register word. **Input gain** goes through `FUN_0000C576` and the table at 0x00374D24.

`FUN_0000CA44` adds a mix coefficient per type: LPF18 gets +0x4000 and a unity input scaler, LPF12 gets
-0x4000 with the same scaler, and every other type gets 0 with a half scaler, which is what a tapped ladder
needs to turn one cascade into 24, 18 and 12 dB slopes.

What the chip does with the coefficients is still the unknown part, but their shape narrows it. One cutoff
coefficient, one resonance coefficient, one input-side coefficient per channel, and three lowpass slopes taken
off the same thing by FUN_0000CA44's tap mix, is a ladder and not three separate topologies. `namespace cal`
reads it that way now: the cutoff coefficient is a one-pole `a = 1 - e^(-2 pi f / fs)`, which puts byte 0 at
755 Hz and byte 127 at 10.6 kHz, resonance table A scales the ladder's feedback (`LADDER_K`) and table B is
available as passband compensation (`RESO_COMP`). HPF, BPF and BEF are separate chip modes and keep the 2-pole
reading. Those constants are the calibration targets; the formulas around them are the firmware's.

## Pan (FUN_00025BC8, FUN_00025C12, FUN_00025C5C, events 0x211 and 0x222-0x225)

`pan = clamp(chan[+0x19] + panOffset, 0, 255)`, index `pan >> 1` into two 128-byte tables at 0x35C0EB and
0x35C16B, which are one curve read forwards and backwards. Read as 0.375 dB attenuations they give a
constant-power law: 0 dB at one end, silence at the other, -3 dB on both sides at the centre. Each output
pair has its own scaler byte added to the attenuation and clamped at 255, and the individual-out pair takes
its pan from a different source than the channel's own. Event 0x211 also folds in the part's random pan.

`chan[+0x19]` is the per-channel block at 0x0103B384, not the voice image: FUN_00025D84 walks the 32 channels
and reads `+0x19` of each, adds the pan offset, clamps to 0..255 and halves for the index. The voice image feeds
that byte from its own +0x2E, which is twice the part's pan byte, so the table index is the part byte itself and
not one below it. Everything else in the sum, pan scaling, the pan LFO and the performance pan, is added in the
0..255 domain, which is half a table step per unit. Pan scaling is measured now: `10_envelope2` leaves that byte
at 0 and sweeps the pan across the keyboard, and the index comes out as `64 - 4 * (note - 60) / 3`, hard right at
note 12 and hard left from note 108, which is the whole 128 of one side in the 0..255 domain at byte 0 and matches
all ten notes and both ends of the table. `docs/aeg.md`. The pan LFO and the performance pan are still added a
whole index step at a time and no recording has asked them anything, since every request file centres both.

## Still unknown

Which write starts a note (0xFC/FD is written before the registers are loaded), the effect algorithms, and everything the
chip does with the register values listed above.

The filter is no longer on this list, and never belonged on it. The board wires VOP3-1 as a channel-level insert loop off
both tone generators and 0x270 is the switch that puts it in the path, so the cutoff and resonance the CPU computes every tick
arrive over VOP3-1's register block at 0x800200 rather than this bus. Those are the writes traced above, and the chip at that
address is confirmed: rgwan read the CS2 decode off the schematic on 2026-09-16 and A9 = 1 selects VOP3-1. What the chip does
with a coefficient is still the model. `docs/research.md` 2.0.1 has the wiring and section 8 the addressing.

## ROM tables (generated into src/fs1r_rom_tables.h by tools/extract_tables.py)

PANL 0x35C0EB, PANR 0x35C16B, LEVTAB 0x35B4A8, FEGLVL 0x35B2E0, PEGLVL 0x35B50C, PEGTIME 0x35B5D6, VELW 0x35BA1E, VELCURVE 0x35B99E, VELCURVES 0x35B71E, EGBIAS
0x35CD24, KSEXP 0x35C3EB, KSLIN 0x35C413, KEYFACT 0x35C43B, NOTETAB 0x35B346, COARSE 0x35BC32, FINE 0x35BC72, TRANS
0x35B446, BENDTAB 0x35BBD0, SINE64 0x35CC94, SHTAB 0x35CF24, FVSTAB 0x35C4BC, BWBIAS 0x35C533, FRMDET 0x35BE06, VNBAL
0x35BB1E, PEGVEL 0x35C870, SENDTAB 0x35C006, algorithms 0x37C0DC.
