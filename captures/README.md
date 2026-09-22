# Captures

Audio measurement data. Two pipelines live here, and nothing else does. The scripts that drive them are in `tools/`, the conclusions they reach are in `docs/`, and the presentation clips are in `docs/media/`.

Register-level captures are not here. Those come off a patched firmware over sysex and live in the private companion repo `FS1R.unlock`, indexed by [docs/captures/sessions.md](../docs/captures/sessions.md).

## The capture kit

A measurement is a MIDI file that plays itself into a real FS1R, sets up its own patches by sysex, and opens and closes with a marker so a recording aligns itself. One filename carries through the whole pipeline.

```
requests/NN_name.mid   →   hardware/NN_name.wav   →   analysis/NN_name.json
     tracked                    ignored                    ignored
        │                                                      ▲
        └──────────→   engine/NN_name.wav   ───────────────────┘
                            ignored
```

| Folder | Holds | Written by |
|---|---|---|
| `requests/` | 21 MIDI files, 576 segments, 46 minutes, plus `manifest.json` and a README saying what each file settles | `tools/make_capture_set.py` |
| `hardware/` | what came back from a real unit, one WAV per request | rgwan's recording rig, split by `tools/split_capture.py` |
| `engine/` | our render of the same requests, for the A/B | `bin/render_capture -f out.wav file.mid` |
| `analysis/` | per-segment measurements, `NN_name.json` for the unit and `NN_name_engine.json` for ours | `tools/analyze_capture.py` |
| `raw/` | continuous recordings as they arrive, before splitting | rgwan, then `tools/split_capture.py` |

## The demo songs

The FS1R plays fifteen demo songs out of its own EPROM, so the same byte stream drives the hardware and the engine and no capture rig is needed. This is the cheaper reference, and the only one that existed before 2026-09-18.

| Folder | Holds |
|---|---|
| `demo/` | the fifteen songs plus `all.mid`, extracted by `tools/extract_demo.py`. `demo/render/` holds ours, ignored |
| `demo_nofilterfx/` | the same songs with the effect section and the per-voice filter switched off, by `tools/strip_demo_fx.py`, so an A/B skips the layers still being modelled |

## What is tracked

Inputs are tracked, results are not. The MIDI files and the manifests are small, deterministic and regenerable from a script, so they are in git. The WAVs, the FLACs and the JSON are large, and every one of them is reproducible from a tracked input plus either a recording session or a build, so they are ignored. `.gitignore` carries the list.

That is why a fresh clone has `requests/` and `demo/` full and everything else empty. `engine/` and `analysis/` refill from a build. `hardware/` and `raw/` only refill from a recording session, which is why those two are the ones worth backing up off this machine.
