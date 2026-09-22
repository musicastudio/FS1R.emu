#!/usr/bin/env python3
"""Line a recording of one part of a demo song up against the engine's render of that part alone, and
compare it hit by hit.

    python tools/compare_isolated.py ../FS1R.unlock/captures/2026-09-22/vokodrone_drum_ch3.flac \\
        "captures/demo_nofilterfx/01_Vokodrone.mid" 2

The third argument is the MIDI channel the unit was recorded playing on its own. The song's other
channels have their notes dropped and everything else, sysex and controllers included, is kept, so the
engine sees the same performance the unit did with only that part sounding. The render goes through
bin/render_capture, so this runs anywhere.

The take is found in the render by cross-correlating one-millisecond log envelopes and refined on the
first hits, so the two can be trimmed differently. Then, per note number: the level and octave-band
difference over the first 250 ms of the hits that have no other note inside that window, a sixth-octave
spectrum of both, and the envelope at 10 ms steps. This is what put the 2026-09-22 drum finding on
its two sine carriers rather than on the noise.
"""
import collections
import subprocess
import sys
from pathlib import Path

import mido
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / ("bin/render_capture.exe" if sys.platform == "win32" else "bin/render_capture")
EPROM = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
SR = 48000
BANDS = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240),
         (10240, 20000)]


def mono(path):
    x, sr = sf.read(str(path), dtype="float32", always_2d=True)
    if sr != SR:
        sys.exit("%s is %d Hz, expected %d" % (path, sr, SR))
    return x.mean(1)


def env(m, hop=48):
    n = len(m) // hop
    return np.sqrt((m[:n * hop].reshape(n, hop) ** 2).mean(1) + 1e-12)


def channel_only(src, keep, out):
    m = mido.MidiFile(str(src))
    o = mido.MidiFile(type=m.type, ticks_per_beat=m.ticks_per_beat)
    for tr in m.tracks:
        nt, acc = mido.MidiTrack(), 0
        for msg in tr:
            acc += msg.time
            if msg.type in ("note_on", "note_off") and msg.channel != keep:
                continue
            nt.append(msg.copy(time=acc))
            acc = 0
        o.tracks.append(nt)
    o.save(str(out))
    t, ons = 0, []
    for msg in m:
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0 and msg.channel == keep:
            ons.append((t, msg.note, msg.velocity))
    return ons


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    take, song, chan = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
    mid = Path("/tmp") / ("%s_ch%d.mid" % (song.stem, chan))
    wav = mid.with_suffix(".wav")
    ons = channel_only(song, chan, mid)
    cmd = [str(RENDER)] + (["-r", str(EPROM)] if EPROM.exists() else []) + ["-f", "-d", "2", str(wav), str(mid)]
    subprocess.run(cmd, check=True, capture_output=True)
    hw, eng = mono(take), mono(wav)

    he, ee = np.log10(env(hw)), np.log10(env(eng))
    he, ee = (he - he.mean()) / he.std(), (ee - ee.mean()) / ee.std()
    c = np.correlate(he, ee, "full")
    k = int(np.argmax(c))
    lag = (k - (len(ee) - 1)) * 48
    print("coarse lag %.3f s, correlation %.3f" % (lag / SR, c[k] / min(len(he), len(ee))))

    def seg(x, t0, dur, off=0):
        s = int(t0 * SR) + off
        return x[s:s + int(dur * SR)]

    refine = []
    for t0, n, v in ons[:8]:
        a, b = seg(eng, t0, 0.3), seg(hw, t0, 0.3, lag)
        if len(a) < 14000 or len(b) < 14000:
            continue
        ea, eb = env(a, 8), env(b, 8)
        cc = np.correlate(eb - eb.mean(), ea - ea.mean(), "full")
        refine.append((int(np.argmax(cc)) - (len(ea) - 1)) * 8)
    if refine:
        lag += int(np.median(refine))
    print("lag %d samples, %d onsets on channel %d" % (lag, len(ons), chan))

    by = collections.defaultdict(list)
    for t0, n, v in ons:
        by[n].append((t0, v))
    N = int(0.25 * SR)
    f = np.fft.rfftfreq(N, 1.0 / SR)
    w = np.hanning(N)
    for n in sorted(by):
        hits = by[n]
        alone = [(t0, v) for t0, v in hits if not [o for o, _, _ in ons if t0 < o < t0 + 0.25]] or hits[:20]
        Sa, Sb, Ea, Eb, used = np.zeros(len(f)), np.zeros(len(f)), [], [], 0
        for t0, v in alone[:40]:
            a, b = seg(eng, t0 - 0.005, 0.25), seg(hw, t0 - 0.005, 0.25, lag)
            if len(a) < N or len(b) < N:
                continue
            Sa += np.abs(np.fft.rfft(a * w)) ** 2
            Sb += np.abs(np.fft.rfft(b * w)) ** 2
            Ea.append(20 * np.log10(env(a) + 1e-9))
            Eb.append(20 * np.log10(env(b) + 1e-9))
            used += 1
        if not used:
            continue
        Ea, Eb = np.mean(Ea, 0), np.mean(Eb, 0)
        print("\nnote %d: %d hits used of %d, velocities %s" % (n, used, len(hits), sorted(set(v for _, v in hits))))
        print("  total dB engine minus unit: %+.2f" % (10 * np.log10(Sa.sum() / Sb.sum())))
        print("  octave bands from 40 Hz:   " + "  ".join(
            "%+5.1f" % (10 * np.log10((Sa[(f >= lo) & (f < hi)].sum() + 1e-20) / (Sb[(f >= lo) & (f < hi)].sum() + 1e-20)))
            for lo, hi in BANDS))
        edges = 40 * 2 ** (np.arange(0, 60) / 6.0)
        pa = np.array([10 * np.log10(Sa[(f >= lo) & (f < hi)].sum() + 1e-20) for lo, hi in zip(edges[:-1], edges[1:])])
        pb = np.array([10 * np.log10(Sb[(f >= lo) & (f < hi)].sum() + 1e-20) for lo, hi in zip(edges[:-1], edges[1:])])
        ref = max(pa.max(), pb.max())
        print("  sixth-octave spectrum, unit / engine, dB below the louder peak:")
        for i in range(len(pa)):
            if max(pa[i], pb[i]) > ref - 45:
                print("   %6.0f Hz  unit %6.1f  engine %6.1f  %+5.1f" % (edges[i], pb[i] - ref, pa[i] - ref, pa[i] - pb[i]))
        print("  envelope, 10 ms steps, unit:   " + " ".join("%.0f" % x for x in Eb[::10][:25]))
        print("                       engine: " + " ".join("%.0f" % x for x in Ea[::10][:25]))


if __name__ == "__main__":
    main()
