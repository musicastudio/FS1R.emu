The fifteen demo songs, and `all.mid`, with the effect section and the per-voice filter switched off. Written by `tools/strip_demo_fx.py`, which rewrites the one performance bulk in each file from `captures/demo` and leaves everything else, voices included, byte for byte the same.

Off in each performance: reverb, variation and insertion type set to No Effect, the three master EQ bands to 0 dB, every part's FilterSw off with its RevSend, VarSend and InsSw cleared and Dry Level at 127.

The point is an A/B against the hardware that does not run through the layers still being worked out, the VOP3 filter and the YSS236 effect DSP. Play a file from here on the unit and render the same file with `python tools/render_song.py "captures/demo_nofilterfx/<song>.mid"`, and what is left between the two is the tone generator.
