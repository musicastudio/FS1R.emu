#!/usr/bin/env python3
"""Rebuild captures/fs1r_demo01_engine.mp4 and .ogg, the README clip, from whatever the engine does today.

    python captures/fs1r_demo01_engine.py            [--rom eprom.bin] [--drop-wav]

Demo song 1 "Vokodrone" out of the EPROM, through the current build of `bin/fs1r_emu`, encoded next to
its counterpart `fs1r_demo01_hardware.mp4`, which is the same song cut out of rgwan's recording of the
real unit. Rerun it after anything that changes the sound, and the README's before-and-after is current.

The two clips must stay comparable, so the length and the frame size are fixed here and match the cut
taken from the recording: 50.5 s from the top of the song, which is its 51 s of events minus the last
half second, and stops well short of song 2. The video track is a scrolling waveform because GitHub
gives an audio-only mp4 no player at all. The .ogg beside it is the same cut as plain audio, for
anywhere a video player is the wrong shape. ffmpeg comes from the imageio-ffmpeg wheel; there is no
system ffmpeg on the machine this was written on.

The 48 kHz render is left in captures/demo/render/ next to the other songs', which is where they are
kept to listen to. --drop-wav deletes it instead.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

import imageio_ffmpeg

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ROM = ROOT.parent / "FS1R_DISASM" / "roms" / "fs1r_v120_eprom_cpuview.bin"
EXE = ROOT / "bin" / ("fs1r_emu.exe" if sys.platform == "win32" else "fs1r_emu")
RENDER = ROOT / "bin" / ("render_capture.exe" if sys.platform == "win32" else "render_capture")
MID = HERE / "demo" / "01_Vokodrone.mid"
WAV = HERE / "demo" / "render" / "01_Vokodrone.wav"
MP4 = HERE / "fs1r_demo01_engine.mp4"
OGG = HERE / "fs1r_demo01_engine.ogg"
LEN = 50.5                                        # seconds, the same cut as fs1r_demo01_hardware.mp4
# drawtext asks fontconfig for the font, and fontconfig is not set up on every machine that has an
# ffmpeg. Where it comes back empty, hand drawtext a file instead. $FS1R_DEMO_FONT wins if it is set.
FONTS = [os.environ.get("FS1R_DEMO_FONT", ""),
         "C:/Windows/Fonts/arial.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/Library/Fonts/Arial.ttf"]


def run(cmd, what):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode:
        sys.exit("%s failed:\n%s" % (what, (r.stderr or r.stdout)[-3000:]))
    return r.stdout


def font_arg(ffmpeg):
    """The fontfile= drawtext needs, or nothing at all where fontconfig can answer for itself."""
    probe = subprocess.run([str(ffmpeg), "-v", "error", "-f", "lavfi", "-i", "color=s=32x32:d=1",
                            "-vf", "drawtext=text=x:fontsize=8", "-frames:v", "1", "-f", "null", "-"],
                           capture_output=True, text=True)
    if not probe.returncode:
        return ""
    for f in FONTS:
        if f and Path(f).exists():
            return "fontfile='%s':" % f
    sys.exit("%s cannot find a font for drawtext. Point $FS1R_DEMO_FONT at a .ttf.\n%s"
             % (ffmpeg, probe.stderr.strip()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=str(ROM))
    ap.add_argument("--drop-wav", action="store_true", help="delete the 48 kHz render afterwards")
    args = ap.parse_args()
    # Either renderer produces the same audio. The console is the Windows one and needs WinMM;
    # render_capture is the portable one the calibration loop uses, so this runs anywhere.
    if RENDER.exists():
        def render(wav):
            return run([RENDER, "-r", args.rom, "-d", 2, wav, MID], "render")
    elif EXE.exists():
        def render(wav):
            return run([EXE, "-r", args.rom, "-smf", MID, "-w", wav, "-d", 2], "render")
    else:
        sys.exit("no engine at %s or %s, build one first" % (RENDER, EXE))
    if not MID.exists():
        run([sys.executable, ROOT / "tools" / "extract_demo.py", args.rom], "extract_demo.py")

    WAV.parent.mkdir(parents=True, exist_ok=True)
    was = WAV.stat().st_mtime if WAV.exists() else 0
    out = render(WAV)
    print(out.strip())
    # fs1r_emu exits 0 even when it cannot open the wav, and then the clip below would be encoded from
    # whatever stale render was lying there. Seen for real: a concurrent build held the file open.
    if not WAV.exists() or WAV.stat().st_mtime <= was:
        sys.exit("the engine did not write %s:\n%s" % (WAV, out))

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    waves = ("[0:a]showwaves=s=960x360:mode=line:rate=30,format=yuv420p,"
             "drawtext=%stext='FS1R.emu engine':x=24:y=24:fontsize=22:fontcolor=white@0.85[v]"
             % font_arg(ffmpeg))
    run([ffmpeg, "-y", "-i", WAV, "-t", LEN, "-filter_complex", waves,
         "-map", "[v]", "-map", "0:a", "-c:v", "libx264", "-preset", "veryfast", "-crf", 26,
         "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-c:a", "aac", "-b:a", "192k", MP4], "ffmpeg")
    # The same cut again as plain audio, for anywhere a video player is the wrong shape.
    run([ffmpeg, "-y", "-i", WAV, "-t", LEN,
         "-c:a", "libvorbis", "-q:a", 5, OGG], "ffmpeg (ogg)")
    if args.drop_wav:
        WAV.unlink()
    print("wrote %s, %.1f MB" % (MP4, MP4.stat().st_size / 1e6))
    print("wrote %s, %.1f MB" % (OGG, OGG.stat().st_size / 1e6))


if __name__ == "__main__":
    main()
