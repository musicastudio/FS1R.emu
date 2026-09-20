# What we need from a real FS1R

For Zhiyuan Wan (rgwan), who offered to record bit-exact signals off his unit and its digital output board.

## Why

The engine reproduces the firmware's own logic: the note-on path, the tick pipeline, the parameter conversions and the tables all come out of the decompiled ROM, so a patch is interpreted the way the hardware interprets it. That part is settled, and the firmware cannot settle any more of it.

What the firmware never says is what the YMP706 does with the register values it is handed. Nothing in the ROM says how many dB a level step is, how long a rate takes, what shape an envelope traverses, what a bandwidth of 40 does to a formant, or what the filter's cutoff byte means in Hz. Those are modelled from the DX7 lineage and from Yamaha's formant patent, and every one of them is a guess that happens to sound plausible. They are collected in one place, `namespace cal` in `src/fs1r_lib.cpp`, so that calibrating against a recording is a table edit rather than a rewrite.

A recording of the right test tones turns each of those guesses into a measurement. That is the whole ask.

## The recordings

`captures/requests/` holds 22 Standard MIDI Files and a manifest. Each file sets up every patch itself with sysex, so the unit needs no preparation and nothing is written to its memory: only the current performance and voice buffers change, and a power cycle clears them.

**Procedure.** Play one MIDI file into the FS1R's MIDI IN from any player or DAW that sends sysex faithfully. Record the digital output for the whole file. Save one WAV per MIDI file, named after it, so `01_reference_1.mid` becomes `01_reference_1.wav`. Start the recorder before playback and stop it afterwards: the files open and close with three short beeps that we align to, so trimming, latency and clock drift are all handled at our end. Nothing else needs to be exact.

**Before the first file**, set MIDI Device Number to 1 and Receive Exclusive on, and leave the system settings at their defaults (master tune centre, velocity curve "thru", master note shift 0). If your device number cannot be 1, tell us the number and we will regenerate the files for it. Do not touch the panel while a file is playing.

**The built-in check.** Every file starts with three short 1 kHz beeps about a second in. If you hear those, the sysex is being received and the rest of the file will work. If you hear nothing at all, stop: something about the device number or the sysex path is wrong and we will fix the files rather than waste your time.

**Format.** 48 kHz, 24-bit or 32-bit, straight off the digital tap, no dither, no sample rate conversion, no processing of any kind. If the analogue outputs are all that is convenient, record those instead and say so: it is still worth having, just with the converters and the output stage in the way.

**Order.** The files are numbered by value. Please do `01_reference_1` and `01_reference_2` first and send just those two, so we can confirm the alignment and the levels look right before you spend more time. After that, 02 (the envelope), 03 (modulation index and feedback) and 04 (the formant window) carry most of the remaining value. 05 to 08 are worth having when you get to them. 09 is the effects and is genuinely optional: those run on the YSS236 and we cannot make them exact from a recording anyway.

`captures/requests/README.md` lists every file with its length and what it settles. Total playback across all 22 is about 49 minutes.

### 2026-09-19: one new file, `10_envelope2`

Your 0918 recording of 02 settled the amplitude EG's rates and then showed that three other things about it were wrong, two of them badly: the attack was four times too slow and the key rate scaling ran every note 7 to 16 times too fast. `docs/aeg.md` is the working and the engine has all three now. What the three envelope files cannot finish is in `10_envelope2.mid`, 28 segments and 2.8 minutes, recorded exactly like the others and in no hurry. It is worth more than 05 to 09 if you are choosing.

Most of it is one question. The chip shortens every EG rate as the note rises, and how much it shortens by is fitted through three notes, which do not sit on a straight line. Twenty of the segments play the same decay at two time-scaling settings across ten notes from the bottom of the keyboard to the top, which settles it outright.

### 2026-09-20: `12_fseqlevel`, one file, 1.3 minutes

You reported that demo song 1 "Vokodrone" clicks all the way through its opening on the engine and does not on your unit. It is the Fseq, and the engine is fixed for the part of it the recording settles; this file is for the part it cannot.

An Fseq frame rewrites all sixteen level registers of a channel at once, and the frames are a vocoder analysis, so adjacent frames sit 13 to 80 dB apart. The CPU writes them raw, so whatever keeps a real unit from clicking its way through an Fseq happens inside the YMP706. Your demo recording says something does: Vokodrone's intro is part 4 alone running a 35.6 Hz Fseq, and averaging the 5 kHz-and-up envelope over its 120 frame boundaries puts your unit 1.2 dB above its own floor at the boundary where the engine sat 12.3 dB above its.

Two models fit. Either the operator carries the level it was fired with, so a write lands on the next grain and the grain window, which is zero at both ends, swallows the step; or the level register itself slews on a time constant. Latching per grain brings the engine to 1.2 dB, your number to two digits, and costs no constant, so that is what ships. A 3 to 5 ms slew fits almost as well and the demo cannot choose, because everything in it runs at one frame rate over a narrow range of notes.

**The lever is the grain rate.** A grain is one period of the fundamental. Per-grain latching gives a transition whose width tracks the note and ignores the frame rate; a slewed register gives one whose width is the same milliseconds whatever the note and whatever the frame rate. `12_fseqlevel.mid` plays one formant operator on preset Fseq 34 "RndArp4" at notes 36, 48, 60, 72, 84 and 96, then the same note 60 at 16, 65 and 176 frames a second. Five octaves and an eleven-fold frame rate separate the two outright.

**The second question is the operators that have no grain.** A sine operator and the unvoiced (noise formant) operator take the same frame level through the same register and neither has a window to hide a step in. If the chip latches per grain they should step where the formant does not. Three segments are the sine and three the noise, at notes 36, 60 and 84. That is the 0.85 dB the intro has left after the fix.

Fourteen segments, 1.3 minutes, recorded exactly like the rest of the set. The whole file is one voice at a time with no effects and no filter, and it peaks around -21 dBFS because the Fseq's own levels are low; do not ride the gain, the analysis is frame-synchronous and reads the transitions off the waveform.

### 2026-09-20: `11_detune`, recorded and answered the same day

Both files came back, and so did a `sweep.py detune` session, and between them they closed detune outright. It is the EPROM's own key scaled table and the engine reads it now rather than modelling anything: `docs/detune.md`. Nothing further is needed on this one. What follows is the request as it went out.

### 2026-09-20: two more, `11_detune`

Your 0918 recording settled detune as well, and the engine had it 65% too wide: the unit runs about 1.21 cents per step near zero rising to 2.7 at the ends, not the flat 2.0 the DX7 lineage suggested. That is fixed. It matters more than a pitch error sounds, because operators detuned against each other beat at the difference between them, so the wrong width is heard as a warble at the wrong rate. Demo song 2 "Full Tines" went out of here with a vibrato your unit does not have, which is how it was found.

`11_detune_1.mid` and `11_detune_2.mid`, 51 segments and under four minutes together, close what the 0918 take could not. It stepped detune by three, so sixteen of the thirty-one settings were never played; and it played note 60 only, which cannot separate a fixed frequency offset from a fixed ratio, the two being the same number at one note and an octave apart four octaves down. File 1 plays every step at note 60 and file 2 plays the four ends at notes 36, 48, 72, 84 and 96.

**If the debug monitor is easier**, `FS1R.unlock/captures/sweep.py detune` is the same measurement as a parameter change with a retrigger, about the same length, and it reads voice byte 7 back off the unit before it records anything so a run that is not landing stops instead of filling a file. Run it as `gate smoke detune` like the others. Either one answers it; no need for both.

**One more, free-form:** a minute or two of you playing a few of your favourite preset performances normally, recorded the same way, with a note of which performances they were. That is the sanity check that the whole engine sounds like the instrument rather than merely measuring correctly.

## A logic analyzer capture, if you ever have one hooked up

Worth more per minute of your time than all the audio, and no MIDI files needed from us.

The YMP706 bus is 10 address lines, 8 data lines, the chip enables and PBUSY. A capture of chip 0 at 10 MSa/s or better, covering power-up through the first sound, plus a couple of notes on one simple patch, would let us check the entire CPU-side rewrite against the real register stream, byte for byte. It would also answer the one thing about the interface we still cannot read: which write actually starts a note, since the firmware writes the key-on mask at 0xFC/0xFD before it loads the registers.

If your analyzer has only 16 channels, D0-D7 plus A0-A7 is already most of the value; the registers above 0xFF become ambiguous but the per-operator stripes do not. You used a DSLogic for the UART dump, which is fine for this.

Four captures would be plenty: boot to first sound, one note on a plain sine patch, one note with the filter switched on, and an Fseq playing.

## Questions about the hardware, answered

Zhiyuan answered 1 to 5 on 2026-09-15 and 6 and 7 on 2026-09-16. They are folded into `docs/research.md` sections 2 and 8 and the KNOWN list in `TODO.md`; what is still open is at the end.

1. **The tap sits after both effect DSPs and before the master volume**, which is an analogue pot. It is taken from the main DAC's I2S input, so a recording is exactly what the effect DSP hands the converter and does not depend on where the volume knob sits. Every file in `captures/requests/` therefore runs through the effect DSP, which is why all three effect blocks are set to No Effect and the master EQ flat. Any fixed gain or truncation left in that path shows up as one constant offset that the reference file measures.
2. **One 24.576 MHz crystal clocks the whole audio side.** The main LC78834 is the I2S master at 512 Fs, so exactly 48 kHz, and it generates BCLK and LRCLK for the slave DAC, both VOP3s and both YMP706s. The tone generators are locked to the same word clock, so the engine's 48 kHz is the hardware's own rate.
3. **The CPU is 28 MHz**, a 7 MHz crystal with the PLL at 4x. The SCI divisor inference was right, so the 192.3 Hz tick, the LFOs and the envelope timing rest on a measured number. The CPU and the audio side run off separate crystals, so the tick is not locked to the sample clock on hardware; the drift is crystal tolerance, tens of ppm, and nothing in the engine depends on the difference.
4. **The VOP3 pinout is in the AN200 service manual**, which is typeset rather than scanned like the FS1R's. Fetched and transcribed into `docs/vop3_pinout.md`: 128 CPU registers on CA0-CA6, 8 serial inputs and 8 outputs, and a bank of external DRAM per chip. A die shot of the VOP3 is possible if it ever becomes necessary; the YMP706 is too rare for one.
5. **Summed in the digital domain, in the tone generators themselves.** FS1A (IC10) sends its dry and send busses, DOUT0/1 and SOUT0/1, into FS1B's (IC11) DIN0/1 and SIN0/1; FS1B sums both chips and sends four wires to VOP3-2 (IC12) on SI0-3. VOP3-2's SDO1 drives the main DAC and SDO3 the slave DAC, which is the INDIVIDUAL OUTPUT pair: the owner's manual says the VOLUME control does not affect those jacks, which is the same analogue pot in answer 1. That maps straight onto the chip's two pan register pairs, 0x22C/D main and 0x22E/F individual.

**And a sixth thing we had not thought to ask.** VOP3-1 (IC31) sits in a channel-level loop off both tone generators: SI1/SI3 from FS1B's CHOUT0/1, SI5/SI7 from FS1A's CHOUT0/1, SO3/SO7 back into FS1B's CHIN0/1. Zhiyuan reads that as VOP3-1 being the per-voice filter and VOP3-2 the effects, and the firmware agrees: YMP706 register 0x270 goes from 10 to 11 the moment any part switches its filter on, which is the tone generator switching that channel loop in, and the coefficients the CPU computes every tick go out over the 0x800200 block rather than the tone generator bus. Answers 6 and 7 below settle it: 0x800200 is VOP3-1, and IC31 has no external memory to run an effect with.

**The two questions the pinout raised** were answered on 2026-09-16, and both came back clean.

6. **IC31's WA/WD pins are left open in the schematic.** VOP3-1 has no external DRAM, so it cannot be running a reverb or a delay, and the filter reading is settled from the diagram without measuring anything.
7. **The two chips are decoded on A8 and A9 of CS2**, partial decoding to save gates:

   ```
   VOP3_SELN   = CS2N | A8
   VOP3-1N     = ~A9 | VOP3_SELN     VOP3-1 on A8 = 0, A9 = 1   -> 0x800200
   VOP3-2N     =  A9 | VOP3_SELN     VOP3-2 on A8 = 0, A9 = 0   -> 0x800000
   IC25_26_CLK = ~A8 | CS2N | WRL    the 74HC374 pair on A8 = 1 -> 0x800100
   ```

   IC26 latches the low byte as LED0-7 and IC25 latches D8-10 as CONTA/CONTB/CONTC, which drive IC29 as the LCD contrast DAC. Nothing above A9 is decoded, so each device repeats every 0x400 through CS2.

Searching CS2 for addresses that match that decode found the second driver straight away: `FUN_00039648(reg, value)` writes `0x800000 + 2*reg` and carries 18 wrappers, a near copy of the 0x800200 driver, with its own boot init (`FUN_0003A064`) and its own 512-step program upload (`FUN_0003C9C4`) out of EPROM 0x372204 and 0x373604. So we had not misread the bus, we had only found half of it.

It also turns the chip assignment the right way up. 0x800200 is VOP3-1, and everything that block does is filter work: 16 channels initialised at boot, cutoff with key scaling, resonance, the per-slope mix coefficient. What we had documented as the effect DSP interface, and extracted as the effect microcode, is the filter chip's. The real effect microcode is the pair of images `FUN_0003C9C4` uploads, which nobody has looked at yet. Details in `docs/research.md` section 8.

Still open: the logic analyzer capture above, and the recordings.

## What happens to it

The measurements go straight into `namespace cal` and the engine stops being a model of those parts. `tools/analyze_capture.py` already does the work: it aligns a recording to the manifest, measures every segment, fits the constants it can fit, and diffs the result against our own render of the same MIDI file. We have run the whole loop against the engine's renders, so the moment real WAVs arrive there is no tooling to write. Recovering a known constant from a synthetic render currently lands within a fraction of a percent, so the accuracy limit will be the hardware, not the method.

Everything is GPL-3 and your work is credited in `NOTICE.md` and the README. The dumps this project is built on are yours.

## Where you could take the project

You asked about joining the reversing and design side. Three things are open, and all three are more your ground than ours:

**The VOP3 instruction set.** The YSS236 is Yamaha's VOP3, the same DSP as the AN1x synthesis engine. The FS1R uploads its whole program and coefficient tables at boot, and we have extracted the upload with the destination register of every word: `docs/vop3_microcode.md` and `docs/vop3/`. Nobody has decoded the encoding. That is the only route to exact effects, and it would be useful well beyond this project. Your answer about the addressing gives a smaller way in than the effect programs: the 0x800200 upload we already extracted is VOP3-1's, so it is the filter program, and a filter is a far smaller thing to decode than a reverb and one whose output we can predict, since a cutoff sweep on a known filter type is something either of us can check against.

**A YMP706 driver.** `docs/ymp706_registers.md` documents the bus, the register map and every CPU-side conversion. What is missing is the chip's response, which is what the recordings above start to pin down. If a hardware-level driver for the chip is ever worth writing, by you or by anyone else, that document plus the captures is the foundation for it.

**Running the firmware.** gearmulator has an SH-2 core with the SH7040-family peripherals. Running the real flash and EPROM on it and logging the YMP706 writes would verify our rewrite of the CPU side completely, and would catch anything the port missed. `tools/ghidra_ctrl_dests.py` shows the trick for reaching the code Ghidra never turns into functions, which was most of what made the flash hard to read.
