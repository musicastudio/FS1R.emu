#!/usr/bin/env python3
"""Which renderer to drive, for the tools that render a note and measure it.

bin/fs1r_emu.exe is the console and only runs on Windows; bin/render_capture is the same engine with
no host in it and runs anywhere, and takes the same flags for the offline-render path. Both are in
bin/ on a Windows checkout, so the choice cannot be made from the file names alone: under WSL the
.exe is present and fails before main(). Ask it instead.
"""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONSOLE = ROOT / "bin/fs1r_emu.exe"
PORTABLE_NAMES = ("bin/render_capture.exe", "bin/render_capture")


def _runs(exe):
    if not exe.exists():
        return False
    try:
        r = subprocess.run([str(exe)], capture_output=True, timeout=30)
    except OSError:
        return False
    out = (r.stdout + r.stderr).lower()
    return b"usage" in out or b"performance" in out


def renderer():
    """The console when it runs here, otherwise the portable renderer. Raises if neither works."""
    if _runs(CONSOLE):
        return CONSOLE
    for name in PORTABLE_NAMES:
        p = ROOT / name
        if _runs(p):
            return p
    raise SystemExit(f"build {CONSOLE.name} (build.bat) or render_capture (cmake) first")


def note_args(exe, syx=None, wav=None, note=60, secs=None, rom=None, perf=None, pick=None,
              fseq=None, note2=None, cc=(), mono=None):
    """One offline-render command line, spelled for whichever renderer this is. The two differ on the
    Fseq flag only: -f is the index on the console and the float-output switch on the portable one."""
    a = [str(exe)]
    if rom is not None: a += ["-r", str(rom)]
    if perf is not None: a += ["-P", str(perf)]
    if syx is not None: a += ["-v", str(syx)]
    if pick is not None: a += ["-p", str(pick)]
    if fseq is not None: a += ["-f" if exe.name.startswith("fs1r_emu") else "-fseq", str(fseq)]
    if mono is not None: a += ["-mono", str(mono)]
    if note2 is not None: a += ["-n2", str(note2)]
    for num, val in cc: a += ["-cc", f"{num}={val}"]
    if wav is not None: a += ["-w", str(wav)]
    a += ["-n", str(note)]
    if secs is not None: a += ["-d", str(secs)]
    return a
