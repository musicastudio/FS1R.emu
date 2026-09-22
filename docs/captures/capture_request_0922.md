# Capture request, 2026-09-22: the drum's tonal half

Five MIDI files in `captures/requests/`, written by `python tools/make_capture_tonal.py`, with their rows in `manifest.json`. Record each the way the rest of the set was recorded, one WAV of the same name, and `python tools/analyze_capture.py captures/hardware/<name>.wav --compare` reads it.

What they are for is in `docs/findings.md`, 2026-09-22. rgwan's take of Vokodrone's drum part alone put the engine's drum 1.5 to 3.3 dB above the unit on every note and its snare 4 dB hot in the band its two sine carriers occupy, with the unit's modulation deeper in the first six milliseconds. Three things that sound is made of have never been measured, and none of them can be read off a song because a song never holds one still.

| file | segments | minutes | what it settles |
|---|---|---|---|
| `16_velocity_1`, `_2` | 63 | 2.8 | one fixed 2 kHz carrier at velocities 1 to 127 for amplitude velocity sensitivities -7, -3, 0, +2, +3, +5, +7. The engine's `vel_att` is read from the firmware and puts a fixed offset of `15 - 2 * s` steps on each sensitivity, so the level at full velocity differs by sensitivity; nothing has checked that, and every capture segment so far sat at one velocity and one sensitivity |
| `17_egdecay` | 10 | 0.9 | a decay from full to EG level 95, 90, 80, 70, 50 or 30 at chip rate 24, then two at rate 40, a two-stage decay, and the snare carrier's own envelope on a plain sine. `02_envelope` measured every decay to silence and the engine takes the rate as a straight line in dB to any target; a decay that slows into its target is what the snare's early modulation depth looks like |
| `18_fmchain` | 11 | 0.8 | algorithm 2, op1 into op2 into op3 into op4 the carrier: one modulator at three levels as the reference `03_fm` already has, then two deep at four levels, three deep at two, and the second link at coarse 2. Whether every link carries the 3.369 cycle index the first was measured at |
| `19_drums` | 16 | 0.6 | DemoDrums, byte for byte the voice the demo sends, at notes 41, 60 and 91 by velocities 44, 72, 100 and 127, each hit alone with a second of silence, and the note 41 and 60 hits with the unvoiced half silenced and with the voiced half silenced. The recording to hold the other three against |

All four use a centred part (pan scaling 50) with the filter off and no effects, so a hit at note 41 and one at 91 land at the same pan; that differs from the older files, which leave the pan scaling at 0, and the analyzer's `unpan` handles both.

`tools/compare_isolated.py` is the reader for any further isolated-part take of a demo song: give it the recording, the song and the channel, and it renders that channel alone and scores each note.
