#!/usr/bin/env python3
"""Render one demo song through the current engine, for listening to rather than measuring.

    python tools/render_song.py                    01_Vokodrone, the usual baseline
    python tools/render_song.py "04 Fat Line"      any song, by name or number
    python tools/render_song.py 9 --out mine.wav   somewhere of your choosing

Writes captures/demo/baseline/<song> <commit>.wav unless --out says otherwise, so two renders
from different builds sit next to each other and say which is which. Warns if the engine binary
is older than the source, since a baseline off a stale build is worse than none.

tools/demo_probe.py cut <song> puts the unit's own take of the same song beside ours for an A/B.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "bin/fs1r_emu.exe"
ROM = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
SONGS = ROOT / "captures/demo"


def find_song(name):
    mids = sorted(SONGS.glob("[0-9]*.mid"))
    if not mids:
        sys.exit("no demo songs in %s: run tools/extract_demo.py first" % SONGS)
    name = name.strip()
    if name.isdigit():                                  # "9" or "09"
        want = "%02d_" % int(name)
        for m in mids:
            if m.stem.startswith(want):
                return m
        sys.exit("no song numbered %s" % name)
    low = name.lower().replace("_", " ")
    for m in mids:                                      # exact stem, then substring
        if m.stem.lower() == low or m.stem.lower().replace("_", " ") == low:
            return m
    hits = [m for m in mids if low in m.stem.lower().replace("_", " ")]
    if len(hits) == 1:
        return hits[0]
    sys.exit("%s matches %s" % (name, [m.stem for m in hits] or "nothing"))


def commit():
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                           capture_output=True, text=True, timeout=10)
        h = r.stdout.strip()
        if h and subprocess.run(["git", "diff", "--quiet", "HEAD", "--", "src"], cwd=ROOT).returncode:
            h += "+dirty"
        return h or "nogit"
    except Exception:
        return "nogit"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song", nargs="?", default="01_Vokodrone")
    ap.add_argument("--out", help="output wav (default captures/demo/baseline/<song> <commit>.wav)")
    ap.add_argument("--rom", default=str(ROM), help="EPROM image, for the preset banks the demo plays")
    ap.add_argument("--tail", type=float, default=3.0, help="seconds of tail after the last event")
    a = ap.parse_args()

    if not EXE.exists():
        sys.exit("%s is not built: run build.bat" % EXE)
    newest = max((p.stat().st_mtime for p in (ROOT / "src").glob("*.*")), default=0)
    if newest > EXE.stat().st_mtime:
        print("warning: src/ is newer than the engine binary, run build.bat for a current baseline")
    mid = find_song(a.song)
    out = Path(a.out) if a.out else ROOT / "captures/demo/baseline" / ("%s %s.wav" % (mid.stem, commit()))
    out.parent.mkdir(parents=True, exist_ok=True)

    cmd = [str(EXE)]
    if Path(a.rom).exists():
        cmd += ["-r", a.rom]
    else:
        print("warning: %s is missing, so the demo's preset voices fall back to the built-in blob" % a.rom)
    cmd += ["-smf", str(mid), "-w", str(out), "-d", "%g" % a.tail]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.stdout.write(r.stderr)
        sys.exit("render failed")
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
