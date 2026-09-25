#!/usr/bin/env python3
"""Render regression: a fixed preset list, a stored fingerprint of pitch, harmonics and envelope.

    python tools/regress.py                 check against tools/regress_ref.json
    python tools/regress.py --update        rewrite the reference from the current build
    python tools/regress.py --rom PATH      use this EPROM image (default: ../FS1R_DISASM/roms/...)

Run it after every engine change. ROM cases are skipped with a warning if the image is missing.
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fs1r_render import renderer

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build"
REF = Path(__file__).resolve().parent / "regress_ref.json"
DEFAULT_ROM = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"

# name, extra args (ROM args are added when {rom} appears), note, seconds
CASES = [
    ("init",          [],                              60, 2.0),
    ("init-low",      [],                              36, 2.0),
    ("init-high",     [],                              84, 2.0),
    ("mono-porta",    ["-mono", "40", "-n2", "67"],    60, 2.0),
    ("rom-voice0",    ["-r", "{rom}", "-p", "0"],      60, 2.0),
    ("rom-voice300",  ["-r", "{rom}", "-p", "300"],    60, 2.0),
    ("rom-voice900",  ["-r", "{rom}", "-p", "900"],    48, 2.0),
    ("perf-everybody",["-r", "{rom}", "-P", "24"],     60, 3.0),
    # "Spacy Pad" is a slow pad: its own voice does not reach peak until about 5 s, so 3 s of it is
    # silence and measures nothing.
    ("perf-spacypad", ["-r", "{rom}", "-P", "78"],     60, 8.0),
    ("perf-towarp",   ["-r", "{rom}", "-P", "42"],     60, 3.0),
    ("perf-manhattan",["-r", "{rom}", "-P", "100"],    60, 3.0),
    ("fseq-shoobydo", ["-r", "{rom}", "-f", "1", "-p", "0"], 60, 3.0),
    # Away from the Fseq's assigned note, so the formant frequencies and the fundamental move apart.
    ("fseq-high",     ["-r", "{rom}", "-f", "1", "-p", "0"], 72, 3.0),
    ("fseq-low",      ["-r", "{rom}", "-f", "1", "-p", "0"], 48, 3.0),
    # The controller matrix. Every preset performance routes something, so these drive real destinations:
    # "Everybody" (24) sends the wheel to frequency bias and KN3 to volume, "Digital" (28) sends it to
    # bandwidth, amplitude EG bias, filter cutoff and resonance, "Dist Mini" (26) sends it to LFO1 pitch
    # mod. "Trance Cosmo" (39) is a single part and routes the wheel to frequency bias at depth +32; -p
    # swaps in a native voice with a frequency bias sense on every operator, so the pair brackets
    # destination 36. The numbers are indexes into the EPROM's 384 entry performance table at 0x20A000.
    ("ctrl-freqbias-off", ["-r", "{rom}", "-P", "39", "-p", "183"],             60, 3.0),
    ("ctrl-freqbias", ["-r", "{rom}", "-P", "39", "-p", "183", "-cc", "1=127"], 60, 3.0),
    ("ctrl-volume",   ["-r", "{rom}", "-P", "24", "-cc", "18=40"], 60, 3.0),
    ("ctrl-multi",    ["-r", "{rom}", "-P", "28", "-cc", "1=127"], 60, 3.0),
    ("ctrl-lfo1pmod", ["-r", "{rom}", "-P", "26", "-cc", "1=127"], 60, 3.0),
]


def read_wav(path):
    d = path.read_bytes()
    assert d[:4] == b"RIFF", path
    sr = struct.unpack("<I", d[24:28])[0]
    i = 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"data":
            x = np.frombuffer(d[i + 8:i + 8 + n], dtype=np.int16).reshape(-1, 2) / 32768.0
            return sr, x
        i += 8 + n + (n & 1)
    raise SystemExit("no data chunk in " + str(path))


def fingerprint(path):
    """Numbers that move when the engine changes and stay put when it does not."""
    sr, x = read_wav(path)
    L, R = x[:, 0], x[:, 1]
    n = 16384
    start = min(int(0.3 * sr), max(0, len(L) - n))
    seg = L[start:start + n]
    if len(seg) < n:
        seg = np.pad(seg, (0, n - len(seg)))
    S = np.abs(np.fft.rfft(seg * np.hanning(n)))
    fr = np.fft.rfftfreq(n, 1 / sr)
    top = sorted(((S[i], fr[i]) for i in range(2, len(S) - 2)
                  if S[i] > S[i - 1] and S[i] > S[i + 1] and S[i] > S.max() * 0.03), reverse=True)[:6]
    peaks = [[round(float(f), 1), round(float(20 * np.log10(a / (S.max() + 1e-30))), 1)] for a, f in top]
    step = sr // 4
    env = [round(float(20 * np.log10(np.sqrt(np.mean(L[i:i + step] ** 2)) + 1e-9)), 1)
           for i in range(0, len(L) - step + 1, step)]
    return {
        "peak": round(float(np.abs(x).max()), 4),
        "rms": round(float(np.sqrt(np.mean(x ** 2))), 5),
        "stereo": round(float(np.sqrt(np.mean((L - R) ** 2))), 5),
        "centroid": round(float((S * fr).sum() / (S.sum() + 1e-30)), 1),
        "peaks": peaks,
        "env": env,
    }


def compare(name, ref, got, tol):
    bad = []
    for k in ("peak", "rms", "stereo", "centroid"):
        a, b = ref[k], got[k]
        if abs(a - b) > tol * max(abs(a), abs(b), 1e-3):
            bad.append(f"{k} {a} -> {b}")
    if len(ref["peaks"]) != len(got["peaks"]):
        bad.append(f"peak count {len(ref['peaks'])} -> {len(got['peaks'])}")
    else:
        # Matched by frequency, not by rank: peaks a fraction of a dB apart swap places on any change.
        for a in ref["peaks"]:
            if not any(abs(a[0] - b[0]) <= max(2.0, tol * a[0]) and abs(a[1] - b[1]) <= 1.5 for b in got["peaks"]):
                bad.append(f"peak {a} not in {got['peaks']}")
    if len(ref["env"]) != len(got["env"]):
        bad.append(f"env length {len(ref['env'])} -> {len(got['env'])}")
    else:
        for i, (a, b) in enumerate(zip(ref["env"], got["env"])):
            if abs(a - b) > 1.0:
                bad.append(f"env[{i}] {a} -> {b}")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--rom", default=str(DEFAULT_ROM))
    ap.add_argument("--tol", type=float, default=0.02)
    a = ap.parse_args()
    exe = renderer()
    print(f"renderer: {exe.name}")
    rom = Path(a.rom)
    OUT.mkdir(exist_ok=True)
    ref = json.loads(REF.read_text()) if REF.exists() and not a.update else {}
    out, fails, skipped = {}, 0, 0
    for name, extra, note, secs in CASES:
        if "{rom}" in " ".join(extra) and not rom.exists():
            skipped += 1
            continue
        args = [str(exe)] + [x.replace("{rom}", str(rom)) for x in extra]
        # The portable renderer spells the Fseq index -fseq, since -f alone is its float-output flag.
        if not exe.name.startswith("fs1r_emu"):
            args = ["-fseq" if x == "-f" else x for x in args]
        wav = OUT / f"regress_{name}.wav"
        args += ["-w", str(wav), "-n", str(note), "-d", str(secs)]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"FAIL {name}: exit {r.returncode}\n{r.stdout}{r.stderr}")
            fails += 1
            continue
        fp = fingerprint(wav)
        out[name] = fp
        if a.update:
            print(f"  {name}: rms {fp['rms']} centroid {fp['centroid']}")
        elif name not in ref:
            print(f"NEW  {name} (no reference; run --update)")
        else:
            bad = compare(name, ref[name], fp, a.tol)
            if bad:
                fails += 1
                print(f"FAIL {name}: " + "; ".join(bad[:6]) + (" ..." if len(bad) > 6 else ""))
            else:
                print(f"ok   {name}")
    if a.update:
        REF.write_text(json.dumps(out, indent=1, sort_keys=True) + "\n")
        print(f"wrote {REF} ({len(out)} cases)")
        return 0
    if skipped:
        print(f"note: {skipped} cases skipped, no EPROM image at {rom}")
    print("regress: FAILED" if fails else "regress: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
