# Generated

Every file in this folder is written by a tool. **Do not hand-edit any of them.** An edit here survives until the next time someone runs the generator, then disappears without warning, and the diff that removes it will look like the generator is broken.

They are committed rather than built, so the plugin needs no Python at build time. JUCE packs them into the binary through `juce_add_binary_data` in `plugin/CMakeLists.txt`.

| File | Generator | Source |
|---|---|---|
| `fs1r_panel.svg` | `tools/make_panel_svg.py` | `docs/FS1R-front-panel-p14-ny.svg`, the owner's manual page 14, plus the FSVR wordmark from `docs/fsvr.svg` |
| `parameterDescriptions_fs1r.json` | `tools/gen_parameters.py` | the Data List MIDI tables. 893 parameters with sysex addresses and bit layouts |
| `fs1r_presets.syx` and `fs1r_presets.csv` | `tools/make_presets_blob.py` | `presets/`, the 1408 factory voices |
| `fs1r_performances.syx` | `tools/make_presets_blob.py` | `presets/performances`, the 384 factory performances |
| `fs1r_fseqs.syx` | `tools/make_presets_blob.py` | `presets/fseq`, the 90 preset formant sequences |

To change what is in one of these, change the generator or its source and re-run it. `tools/check_panel.py` and `tools/check_presets.py` read them back and both run under `build.bat test`, so a stale or hand-edited file fails the build.

The same rule applies to two generated files that are not here, because they are text and carry the warning in their own header: `src/fs1r/firmware/tables.h` and `src/fs1r/firmware/algorithms.h`, both by `tools/extract_tables.py`.
