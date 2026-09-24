# CPU cost, and why A020 Vox Morph is the worst case

A user report: "A020 Vox Morph is very CPU hungry. With 5 to 8 voices it brings my Intel i7 to the limit" (AU build). This is what the engine does, measured, and what would make it cheaper. Nothing here is a fidelity question: the constants in `cal.h` are not involved, and every option below has to leave the output bit-identical or it is not an optimisation.

## What the patch asks for

A020 is two layered parts of PrB 097 `SpacySweep` (algorithm 8) on the performance channel, so every key is two channels. Each channel runs five voiced sine operators (three sit at output level 0) and **all eight unvoiced operators** at levels 39 to 71 with their own EGs. The unvoiced path is a noise generator, two one-poles and a resonance carrier per operator per sample, and no other kind of preset uses eight of them at once. Eight held keys is 16 channels × (8 voiced + 8 unvoiced) = 256 operator paths per sample at 48 kHz. On the hardware this costs nothing extra: the YMP706 runs every slot of every channel whether it sounds or not.

## Measured

`build/perfbench.cpp` (not committed: a twenty-line harness that loads a ROM performance through `Synth`, holds N notes and times `render` over four seconds) on one core of a Ryzen 9 3900X, gcc `-O2`, denormals flushed as the plugin does:

| performance | 8 held notes | channels | load, one core |
|---|---|---|---|
| A001 Zap ! | | 1 | 4 % |
| B014 Full Tines | | 16 | 41 % |
| A014 Homy | | 16 | 25 % |
| A020 Vox Morph | | 16 | 53 % |

A laptop i7 core is one and a half to two times slower than this, and a DAW shares it, so "5 to 8 voices hits the limit" is the engine as built, not the AU wrapper. The plugin does flush denormals (`juce::ScopedNoDenormals` in `processBlock`); without it Vox Morph reads 120 % on the same core, which is the number anyone benchmarking the console or `render_capture` sees, since those do not set FTZ.

The profile (`-fno-inline -pg`) is flat: `fsin` 14 %, `EG::tick` 10 %, the unvoiced block about 15 %, `db2lin_fast` 5 %, the rest spread across `render_chan`. There is no single hot spot; it is the count of operator paths.

## What would make it cheaper

In order of cost to do, each with the measurement that says what it buys:

1. **Gate at −90 dB, not −100.** `render_chan` skips an operator whose EG-minus-attenuation is under −100 dB. An operator at output level 0 sits at exactly −96 dB (register 255 × 0.376 dB), so it never gates out: Vox Morph renders three silent voiced operators per channel forever. Raising the gate to −90 dB takes 8 notes from 53 % to 46 %. −96 dB is below the 16-bit floor after `OUT_GAIN`, so the render is unchanged, but this needs the capture bench and the demo ledger run before it ships, like any edit in `ymp706.cpp`. One constant.
2. **Skip an unvoiced operator whose EG has finished.** `ueg.done()` is already tracked for the channel's liveness; the per-sample block does not test it. Vox Morph's unvoiced EGs decay to L4 = 0, so after the decay every one of its 128 noise paths is computing silence. Not measured yet; likely the larger win on this patch after the notes have been held a second.
3. **`-O3`.** 128 % → 93 % at `-O2` → `-O3` without FTZ on the bench. The release builds at `-O2` (CMake Release). Free, but check the regression renders hash-identical first, since the compiler may reassociate the EG maths.
4. **Vectorise the eight operators.** The operator loop is written per operator with a scalar `fsin` table read. Eight lanes of phase, level and EG would map onto SSE/NEON, but the algorithm routing (`Cb`, `H`, `S`, feedback) is serial between operators, so only the EG and level path vectorises cleanly. The real change, and the last one worth doing.

`-ffast-math` reads 33 % on the same bench, but that number is mostly FTZ, which the plugin already has, plus reassociation that changes the output; it is not an option.

## What not to do

Do not touch `tuning::CTL_DECIMATION` to chase this: its rule is that changing it must not change the output, and at 16 the control maths is already well under the operator loop in the profile. Do not lower the voiced gate under the 16-bit floor to chase the number further, and do not skip operators by their *level byte*: a level-0 operator can still be a modulator with a control set or an Fseq track driving it.
