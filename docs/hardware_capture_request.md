# What we need from a real FS1R

For Zhiyuan Wan (rgwan), who offered to record bit-exact signals off his unit and its digital output board.

## Why

The engine reproduces the firmware's own logic: the note-on path, the tick pipeline, the parameter conversions and the tables all come out of the decompiled ROM, so a patch is interpreted the way the hardware interprets it. That part is settled, and the firmware cannot settle any more of it.

What the firmware never says is what the YMP706 does with the register values it is handed. Nothing in the ROM says how many dB a level step is, how long a rate takes, what shape an envelope traverses, what a bandwidth of 40 does to a formant, or what the filter's cutoff byte means in Hz. Those are modelled from the DX7 lineage and from Yamaha's formant patent, and every one of them is a guess that happens to sound plausible. They are collected in one place, `namespace cal` in `src/fs1r_lib.cpp`, so that calibrating against a recording is a table edit rather than a rewrite.

A recording of the right test tones turns each of those guesses into a measurement. That is the whole ask.

## The recordings

`captures/requests/` holds 21 Standard MIDI Files and a manifest. Each file sets up every patch itself with sysex, so the unit needs no preparation and nothing is written to its memory: only the current performance and voice buffers change, and a power cycle clears them.

**Procedure.** Play one MIDI file into the FS1R's MIDI IN from any player or DAW that sends sysex faithfully. Record the digital output for the whole file. Save one WAV per MIDI file, named after it, so `01_reference_1.mid` becomes `01_reference_1.wav`. Start the recorder before playback and stop it afterwards: the files open and close with three short beeps that we align to, so trimming, latency and clock drift are all handled at our end. Nothing else needs to be exact.

**Before the first file**, set MIDI Device Number to 1 and Receive Exclusive on, and leave the system settings at their defaults (master tune centre, velocity curve "thru", master note shift 0). If your device number cannot be 1, tell us the number and we will regenerate the files for it. Do not touch the panel while a file is playing.

**The built-in check.** Every file starts with three short 1 kHz beeps about a second in. If you hear those, the sysex is being received and the rest of the file will work. If you hear nothing at all, stop: something about the device number or the sysex path is wrong and we will fix the files rather than waste your time.

**Format.** 48 kHz, 24-bit or 32-bit, straight off the digital tap, no dither, no sample rate conversion, no processing of any kind. If the analogue outputs are all that is convenient, record those instead and say so: it is still worth having, just with the converters and the output stage in the way.

**Order.** The files are numbered by value. Please do `01_reference_1` and `01_reference_2` first and send just those two, so we can confirm the alignment and the levels look right before you spend more time. After that, 02 (the envelope), 03 (modulation index and feedback) and 04 (the formant window) carry most of the remaining value. 05 to 08 are worth having when you get to them. 09 is the effects and is genuinely optional: those run on the YSS236 and we cannot make them exact from a recording anyway.

`captures/requests/README.md` lists every file with its length and what it settles. Total playback across all 21 is about 46 minutes.

**One more, free-form:** a minute or two of you playing a few of your favourite preset performances normally, recorded the same way, with a note of which performances they were. That is the sanity check that the whole engine sounds like the instrument rather than merely measuring correctly.

## A logic analyzer capture, if you ever have one hooked up

Worth more per minute of your time than all the audio, and no MIDI files needed from us.

The YMP706 bus is 10 address lines, 8 data lines, the chip enables and PBUSY. A capture of chip 0 at 10 MSa/s or better, covering power-up through the first sound, plus a couple of notes on one simple patch, would let us check the entire CPU-side rewrite against the real register stream, byte for byte. It would also answer the one thing about the interface we still cannot read: which write actually starts a note, since the firmware writes the key-on mask at 0xFC/0xFD before it loads the registers.

If your analyzer has only 16 channels, D0-D7 plus A0-A7 is already most of the value; the registers above 0xFF become ambiguous but the per-operator stripes do not. You used a DSLogic for the UART dump, which is fine for this.

Four captures would be plenty: boot to first sound, one note on a plain sine patch, one note with the filter switched on, and an Fseq playing.

## Questions about the hardware

1. Where does the digital tap sit in the chain: before or after the two YSS236 effect DSPs, and before or after the master volume control?
2. What clocks the YMP706? The DAC runs at 48 kHz on your board. Is the tone generator fed from the same crystal, and at what multiple?
3. Can you confirm the CPU is 28 MHz? We inferred it from the SCI divisor being the one that gives exactly 31250 baud, and the LFO and envelope timing in the engine rests on it.
4. Anything you know about the YSS236 pinout or its serial interface, beyond the part markings.
5. Is the output of the two tone generator chips summed in the digital domain before the effects, or are they separate paths?

## What happens to it

The measurements go straight into `namespace cal` and the engine stops being a model of those parts. `tools/analyze_capture.py` already does the work: it aligns a recording to the manifest, measures every segment, fits the constants it can fit, and diffs the result against our own render of the same MIDI file. We have run the whole loop against the engine's renders, so the moment real WAVs arrive there is no tooling to write. Recovering a known constant from a synthetic render currently lands within a fraction of a percent, so the accuracy limit will be the hardware, not the method.

Everything is GPL-3 and your work is credited in `NOTICE.md` and the README. The dumps this project is built on are yours.

## Where you could take the project

You asked about joining the reversing and design side. Three things are open, and all three are more your ground than ours:

**The VOP3 instruction set.** The YSS236 is Yamaha's VOP3, the same DSP as the AN1x synthesis engine. The FS1R uploads its whole program and coefficient tables at boot, and we have extracted the upload with the destination register of every word: `docs/vop3_microcode.md` and `docs/vop3/`. Nobody has decoded the encoding. That is the only route to exact effects, and it would be useful well beyond this project, since MAME's AN1x driver is a stub for the same reason.

**A YMP706 driver.** `docs/ymp706_registers.md` documents the bus, the register map and every CPU-side conversion. What is missing is the chip's response, which is what the recordings above start to pin down. If you still want the MAME driver you mentioned, that document plus the captures is the foundation for it.

**Running the firmware.** gearmulator has an SH-2 core with the SH7040-family peripherals. Running the real flash and EPROM on it and logging the YMP706 writes would verify our rewrite of the CPU side completely, and would catch anything the port missed. `tools/ghidra_ctrl_dests.py` shows the trick for reaching the code Ghidra never turns into functions, which was most of what made the flash hard to read.
