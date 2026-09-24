# Working on FSVR

FSVR is a software reconstruction of the Yamaha FS1R. The firmware's control logic is rewritten in C++ from the decompiled v1.20 ROM, and the two custom chips, for which no register documentation exists anywhere, are modelled and calibrated against recordings of a real unit.

That split is the whole project, and it decides how you should answer almost any question here.

## Read this first

- **[STATUS.md](STATUS.md)** what is known, what is modelled, what nobody knows, and the open work
- **[docs/findings.md](docs/findings.md)** the research log: every conclusion and the evidence behind it
- **[docs/fidelity_plan.md](docs/fidelity_plan.md)** where the engine stands against each recording, ranked

## Where a claim comes from

The path tells you, and it tells you what a disagreement with real hardware means.

| Path | Status | A disagreement with the unit means |
|---|---|---|
| `src/fs1r/firmware/` | **KNOWN**, rewritten from the disassembly | you have a bug. Go read the firmware |
| `src/fs1r/hardware.h` | **KNOWN**, read off the board | you read it wrong. It is not a knob |
| `src/fs1r/chips/` | **INFERRED**, modelled | it needs calibrating against a recording |
| `src/fs1r/chips/cal.h` | the calibration surface | this is the file you edit to calibrate |
| `src/fsvr/` | **ours**, no hardware counterpart | nothing. The hardware has no opinion |
| `src/fsvr/tuning.h` | our cost knobs | nothing, and changing one must not alter the output |
| `plugin/` | the JUCE layer | it never models synthesis, see below |

## The rules

**Never invent a constant.** If a number is not read from the firmware or measured off hardware, it does not go in. Every modelled constant lives in `src/fs1r/chips/cal.h` and names the document that measured it.

**A number fitted against one recording is not measured.** This is the expensive lesson of this project. Three conclusions in two days were written up with a residual quoted and all three were wrong, because a model was fitted against a single recording until the error stopped moving. The modulation index was published twice at the wrong value. Read the 2026-09-19 entry in `docs/findings.md` before you quote a residual. Two datasets, or a firmware trace, or it is not settled.

**Cite the firmware by address.** Anything read from the disassembly names its `FUN_xxxxxxxx` so the next person can re-check it in Ghidra. `tools/ghidra_ctrl_dests.py` shows how to reach code Ghidra never turns into functions.

**The plugin never models synthesis.** It moves parameter values in and out of the engine as sysex, exactly as a hardware editor would, and learns state back from the engine's MIDI output. If you find yourself computing audio in `plugin/`, you are in the wrong layer.

**Generated files are generated.** Do not hand-edit anything in `plugin/generated/`, nor `src/fs1r/firmware/tables.h` or `algorithms.h`. Change the generator or its source and re-run it. `plugin/generated/README.md` names each generator.

**Scale is not fidelity.** `src/fsvr/tuning.h` holds knobs that trade CPU for nothing else. If changing one moves a measurement, the value is wrong, not the hardware's.

## Releases

Every release tag carries release notes: an annotated tag (`git tag -a vX.Y.Z -F notes.md`) whose body says what was measured, what changed and the numbers, and which collaborator reports it answers. The Actions job's `generate_release_notes` alone leaves only a compare link, which is not release notes. Skip them only when told to for that release.

The release job reads that body off the tag *object* through the API, not from a checkout: `actions/checkout` peels a tag ref to its commit, so `git tag -l --format='%(contents)'` in the job returned the commit message and v0.4.7 shipped with it as the body (patched by hand afterwards). A lightweight tag has no object and fails the job on purpose. Check the release page's body after the run, not just the zips.

**One release per request.** A release is cut when asked for, then the next one waits for the next ask: more changes pushed later in the same conversation go to `main` and stay there until the user says to release them. Do not chain point releases on your own because the previous one has already built.

## Before you say it works

```bash
build.bat test
```

That builds the engine and the console, then runs the effect self check, the engine self check, and `check_formant`, `check_panel`, `check_presets` and `check_interface`. All of them must pass.

If you touched anything the audio path reaches, also run the render regression:

```bash
python tools/regress.py
```

Nineteen fixed cases against `tools/regress_ref.json`. It compares pitch, harmonic peaks, envelope, stereo width and centroid. **If it fails, that is a finding, not an inconvenience.** Only run `--update` when you meant to change the output and can say why in the commit message.

For a change that should not alter the output at all, prove it rather than assume it. Render a fixed case before and after and compare the file hashes:

```bash
bin/fs1r_emu.exe -w before.wav -n 60 -d 3
```

## Finding work

[STATUS.md](STATUS.md)'s "Open work" section is the list, and it is grouped by what actually blocks each item rather than by subsystem: what anyone can start today, what is waiting on a hardware session, what is open research, and what is parked. Start there.

The maintainers mirror the same list on a private project board. If you are outside the org you cannot see it, and you are not missing anything: STATUS.md is the copy that is kept current, and the board follows it.

Reported bugs are ordinary issues and are separate from the fidelity work:

```bash
gh issue list -R musicastudio/FSVR
```

## What you cannot see

The register-level captures that settled the CPU side of the engine were taken with a patched firmware over sysex, and they live in a **private** companion repo. If you do not have access, you cannot read them, and you should not go looking. Their conclusions are public and are indexed in [docs/captures/sessions.md](docs/captures/sessions.md), with the working in `docs/findings.md`.

The EPROM image itself is not in this repo either. It is rgwan's dump, kept outside the tree.

## Things that will trip you up

- `captures/` is measurement data with two pipelines and a rule, stated in `captures/README.md`. Recordings and analysis are gitignored; only inputs are tracked
- The engine always runs at 48 kHz whatever the host rate. `fs1r::Device` resamples on the way out, so patch timing is identical everywhere
- `TICK_HZ` is 192.3 Hz. Only `render`, `render_chan` and `refresh_ctl` run per sample; everything `tick()` reaches is control rate
- Three tables are filled once by `init_tables()` and have external linkage on purpose. Making them file-static again would give every translation unit its own zeroed copy and the engine would render silence
- Commits here never carry a `Co-Authored-By` line
