#!/usr/bin/env python3
"""Rebuild a README clip pair, `fs1r_demoNN_engine.mp4`/`.ogg` and `fs1r_demoNN_hardware.mp4`.

    python tools/fs1r_demo_clip.py                          # song 1, the engine clip
    python tools/fs1r_demo_clip.py --song 2 --len 19.5       # song 2, engine
    python tools/fs1r_demo_clip.py --song 2 --len 19.5 --hardware   # its counterpart

One demo song out of the EPROM, through the current build, encoded next to the same song cut out of
rgwan's recording of the real unit. Rerun it after anything that changes the sound and the README's
before-and-after is current.

The two clips of a pair must stay comparable, so the length and the frame size are the same for both.
`--len` defaults to the song's own events rounded to the second, less half a second, which is the cut
song 1 has always had. Override it where the point of the clip is past the last event: song 2 "Full
Tines" is about its four-second tail, so it goes out at 19.5 s rather than the 17.5 s that rule gives.
`--gain` lifts a quiet song into view; give both halves of a pair the same value and the difference
between them is untouched. Song 2 goes out at +12 dB, which puts the louder of the two just under
full scale.

`--hardware` finds the song in the recording the way `tools/check_demo.py` does, by walking the songs
in order and correlating log envelopes, which means it renders every song before this one. That is a
few seconds each and it beats an offset pasted into a script.

The video track is a scrolling waveform because GitHub gives an audio-only mp4 no player at all. The
.ogg beside it is the same cut as plain audio, for anywhere a video player is the wrong shape. Any
ffmpeg with drawtext will do: a system one, the static-ffmpeg wheel, or the imageio-ffmpeg wheel.

The 48 kHz render is left in captures/demo/render/ next to the other songs', which is where they are
kept to listen to. --drop-wav deletes it instead.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ROM = ROOT.parent / "FS1R_DISASM" / "roms" / "fs1r_v120_eprom_cpuview.bin"
EXE = ROOT / "bin" / ("fs1r_emu.exe" if sys.platform == "win32" else "fs1r_emu")
RENDER = ROOT / "bin" / ("render_capture.exe" if sys.platform == "win32" else "render_capture")
# The script moved from captures/ to tools/ when the tree was sorted; the songs, the recording and the
# clips stayed where captures/README.md says they live.
DEMO = ROOT / "captures" / "demo"
RECORDING = ROOT / "captures" / "raw" / "FS1R DEMO.flac"
MEDIA = ROOT / "docs" / "media"
LABEL = {False: "FSVR engine", True: "FS1R hardware (digital out)"}
# drawtext asks fontconfig for the font, and fontconfig is not set up on every machine that has an
# ffmpeg. Where it comes back empty, hand drawtext a file instead. $FS1R_DEMO_FONT wins if it is set,
# and matplotlib's own DejaVu is the last resort on a container with no system fonts at all.
FONTS = [os.environ.get("FS1R_DEMO_FONT", ""),
         "C:/Windows/Fonts/arial.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/Library/Fonts/Arial.ttf",
         str(Path(sys.prefix) / "lib/site-packages/matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf")]


def run(cmd, what):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode:
        sys.exit("%s failed:\n%s" % (what, (r.stderr or r.stdout)[-3000:]))
    return r.stdout


def _drawtext(ffmpeg, font):
    """Can this ffmpeg draw a label with this font, or with fontconfig's own if font is empty."""
    return subprocess.run([str(ffmpeg), "-v", "error", "-f", "lavfi", "-i", "color=s=32x32:d=1",
                           "-vf", "drawtext=%stext=x:fontsize=8" % font, "-frames:v", "1",
                           "-f", "null", "-"], capture_output=True, text=True)


def encoder():
    """An ffmpeg that has drawtext, and the fontfile= argument it needs.

    The imageio-ffmpeg wheel is the one this was written against, but its build carries no drawtext
    filter on every platform, so a system ffmpeg or the static-ffmpeg wheel gets first refusal.
    """
    cands = [shutil.which("ffmpeg")]
    try:
        import static_ffmpeg
        static_ffmpeg.add_paths()
        cands.append(shutil.which("ffmpeg"))
    except ImportError:
        pass
    try:
        import imageio_ffmpeg
        cands.append(imageio_ffmpeg.get_ffmpeg_exe())
    except Exception:
        pass
    tried = []
    for exe in [c for c in cands if c]:
        for font in [""] + ["fontfile='%s':" % f for f in FONTS if f and Path(f).exists()]:
            probe = _drawtext(exe, font)
            if not probe.returncode:
                return exe, font
            tried.append("%s %s: %s" % (exe, font or "(fontconfig)", probe.stderr.strip()[:200]))
    sys.exit("no ffmpeg here can draw the label. Point $FS1R_DEMO_FONT at a .ttf, or\n"
             "pip install static-ffmpeg. Tried:\n  " + "\n  ".join(tried))


def renderer(rom):
    """Either renderer produces the same audio. The console is the Windows one and needs WinMM;
    render_capture is the portable one the calibration loop uses, so this runs anywhere."""
    if RENDER.exists():
        return lambda mid, wav: run([RENDER, "-r", rom, "-d", 2, wav, mid], "render")
    if EXE.exists():
        return lambda mid, wav: run([EXE, "-r", rom, "-smf", mid, "-w", wav, "-d", 2], "render")
    sys.exit("no engine at %s or %s, build one first" % (RENDER, EXE))


def wav_secs(path):
    with wave.open(str(path)) as w:
        return w.getnframes() / float(w.getframerate())


def song_mid(n):
    hits = sorted(DEMO.glob("%02d_*.mid" % n))
    if not hits:
        sys.exit("no captures/demo/%02d_*.mid; run tools/extract_demo.py first" % n)
    return hits[0]


def render_to(render, mid, wav):
    wav.parent.mkdir(parents=True, exist_ok=True)
    was = wav.stat().st_mtime if wav.exists() else 0
    out = render(mid, wav)
    # fs1r_emu exits 0 even when it cannot open the wav, and then the clip below would be encoded from
    # whatever stale render was lying there. Seen for real: a concurrent build held the file open.
    if not wav.exists() or wav.stat().st_mtime <= was:
        sys.exit("the engine did not write %s:\n%s" % (wav, out))
    return out


def cut_hardware(render, n, out_wav):
    """The same seconds of rgwan's take. check_demo walks the songs in order because a 40 s envelope
    matches loosely enough in a 500 s recording that a free search lands on the wrong song."""
    sys.path.insert(0, str(ROOT / "tools"))
    import numpy as np
    import soundfile as sf
    import check_demo as cd
    if not RECORDING.exists():
        sys.exit("no %s to cut the hardware clip from" % RECORDING)
    ref = cd.mono(RECORDING)
    renv = cd.envelope(ref)
    prev, lag, r = None, 0, 0.0
    with tempfile.TemporaryDirectory() as tmp:
        for i in range(1, n + 1):
            wav = Path(tmp) / ("%02d.wav" % i)
            render_to(render, song_mid(i), wav)
            own = cd.mono(wav)
            oenv = cd.envelope(own)
            lo, hi = (0, 3000) if prev is None else (int(prev * 100) - 100, int((prev + 10) * 100))
            lag, r = cd.find(renv, cd.z(oenv), lo, hi)
            loud = np.nonzero(oenv > oenv.max() / 3000.0)[0]
            prev = lag / 100.0 + (loud[-1] + 1) / 100.0
            print("  song %d at %.2f s, envelope r %.3f" % (i, lag / 100.0, r))
    # The lag comes off mono envelopes, but the clip keeps the take's own channels: the engine's is
    # stereo and a mono counterpart beside it would not be the same comparison.
    stereo, _ = sf.read(str(RECORDING), dtype="float32", always_2d=True)
    sf.write(str(out_wav), stereo[lag * cd.HOP:lag * cd.HOP + len(own)], cd.SR)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--song", type=int, default=1, help="demo song number, 1 to 15")
    ap.add_argument("--rom", default=str(ROM))
    ap.add_argument("--len", dest="length", type=float,
                    help="seconds to encode; the song's events rounded, less half a second, by default")
    ap.add_argument("--gain", type=float, default=0.0,
                    help="dB applied to the clip, audio and waveform both. Use the same value for "
                         "both halves of a pair: it makes a quiet song visible and audible without "
                         "changing what the comparison says")
    ap.add_argument("--hardware", action="store_true",
                    help="cut the same seconds out of the recording instead of rendering them")
    ap.add_argument("--drop-wav", action="store_true", help="delete the 48 kHz render afterwards")
    args = ap.parse_args()

    render = renderer(args.rom)
    if not sorted(DEMO.glob("%02d_*.mid" % args.song)):
        run([sys.executable, ROOT / "tools" / "extract_demo.py", args.rom], "extract_demo.py")
    mid = song_mid(args.song)
    stem = "fs1r_demo%02d_%s" % (args.song, "hardware" if args.hardware else "engine")
    mp4, ogg = MEDIA / (stem + ".mp4"), MEDIA / (stem + ".ogg")

    # The hardware cut stays out of captures/demo/render: check_demo.py globs every wav in there as
    # an engine render, and a cut of the recording sitting among them would score itself.
    scratch = tempfile.TemporaryDirectory()
    if args.hardware:
        wav = Path(scratch.name) / (mid.stem + "_hw.wav")
        print("cutting %s out of %s" % (mid.stem, RECORDING.name))
        print("  matched at r %.3f" % cut_hardware(render, args.song, wav))
    else:
        wav = DEMO / "render" / (mid.stem + ".wav")
        print(render_to(render, mid, wav).strip())

    # The render carries the song's events plus the two-second tail -d asks for. Taking the tail off
    # and rounding gives the song's own length, and half a second back off that is the cut song 1 has
    # always had. The hardware cut is the same number of samples, so one rule covers both.
    length = args.length if args.length is not None else round(wav_secs(wav) - 2.0) - 0.5

    ffmpeg, font = encoder()
    vol = "volume=%.3fdB," % args.gain if args.gain else ""
    waves = ("[0:a]%sasplit=2[a][w];"
             "[w]showwaves=s=960x360:mode=line:rate=30,format=yuv420p,"
             "drawtext=%stext='%s':x=24:y=24:fontsize=22:fontcolor=white@0.85[v]"
             % (vol, font, LABEL[args.hardware]))
    run([ffmpeg, "-y", "-i", wav, "-t", length, "-filter_complex", waves,
         "-map", "[v]", "-map", "[a]", "-c:v", "libx264", "-preset", "veryfast", "-crf", 26,
         "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-c:a", "aac", "-b:a", "192k", mp4], "ffmpeg")
    # The engine's cut again as plain audio, for anywhere a video player is the wrong shape. The
    # hardware side does not get one: nothing links it, and the recording is in the repo already.
    if not args.hardware:
        run([ffmpeg, "-y", "-i", wav, "-t", length] + (["-af", "volume=%.3fdB" % args.gain] if args.gain else [])
            + ["-c:a", "libvorbis", "-q:a", 5, ogg], "ffmpeg (ogg)")
    if args.drop_wav and not args.hardware:
        wav.unlink()
    scratch.cleanup()
    for f in (mp4, ogg):
        if f.exists():
            print("wrote %s, %.1f MB" % (f, f.stat().st_size / 1e6))


if __name__ == "__main__":
    main()
