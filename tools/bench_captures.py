#!/usr/bin/env python3
"""Score the whole capture set against the current source, one line per file.

    python tools/bench_captures.py [-o scores.txt] [files...]

Rebuilds render_capture from the source as it stands (a checked-in binary is a build of whatever the
source was when it was compiled), renders every request file that has a hardware take into
captures/engine/, runs analyze_capture.py --compare and prints `file level shape envelope` per file.
Run it before and after an engine edit and diff the two tables; a shared-path change moves files the
edit never targeted and the per-file table is the only way to see it. Takes about ten minutes.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EPROM = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
SRC = sorted(str(p) for p in (list((ROOT / "src/fs1r/chips").glob("*.cpp")) + list((ROOT / "src/fs1r/firmware").glob("*.cpp"))
                              + list((ROOT / "src/fsvr").glob("*.cpp")) + [ROOT / "tools/render_capture.cpp"]))


def build(exe):
    cmd = ["g++", "-O2", "-std=c++17", "-I", str(ROOT / "src")] + SRC + ["-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("-o", "--out")
    ap.add_argument("--exe", default="/tmp/rc_bench")
    args = ap.parse_args()
    exe = Path(args.exe)
    build(exe)
    hw = sorted((ROOT / "captures/hardware").glob("*.wav"))
    if args.files:
        hw = [w for w in hw if w.stem in args.files]
    lines = []
    for w in hw:
        mid = ROOT / "captures/requests" / (w.stem + ".mid")
        out = ROOT / "captures/engine" / w.name
        subprocess.run([str(exe), "-r", str(EPROM), "-f", "-d", "1", str(out), str(mid)], check=True, capture_output=True)
        r = subprocess.run([sys.executable, str(ROOT / "tools/analyze_capture.py"), str(w), "--compare"],
                           capture_output=True, text=True, cwd=ROOT)
        lvl = re.search(r"median \|difference\| ([\d.]+) dB", r.stdout)
        shp = re.search(r"mean ([\d.]+) dB over \d+ spectra", r.stdout)
        env = re.search(r"mean ([\d.]+) dB over \d+ envelopes", r.stdout)
        f = lambda m: m.group(1) if m else "-"
        line = f"{w.stem:24s} {f(lvl):>6s} {f(shp):>6s} {f(env):>6s}"
        print(line, flush=True)
        lines.append(line)
    if args.out:
        Path(args.out).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
