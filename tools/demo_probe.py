#!/usr/bin/env python3
"""Work one demo song against rgwan's recording: read its patch, cut the two takes apart,
change one byte and hear what moves.

    python tools/demo_probe.py dump  "04_Fat Line"
    python tools/demo_probe.py cut   "04_Fat Line" [outdir]
    python tools/demo_probe.py patch "04_Fat Line" out.mid --perf 199=0 --voice 0:0x3D=0
    python tools/demo_probe.py score "what I changed"

`dump` prints the performance, its parts, its controller sets and every operator of every voice
the song sends. `cut` aligns our render to the recording the way tools/check_demo.py does, writes
hw.wav and eng.wav next to each other and prints a third-octave comparison and a per-second level
trace. `patch` rewrites the song's own sysex so a render can answer "what if this byte were
different", leaving every length alone so the timing is untouched. `score` reduces a whole set of
renders to one number, the mean absolute band error over the fifteen songs, so two builds of the
engine can be ranked; it appends to tools/demo_scores.txt.

This is the loop that moved FM_INDEX and RESO_PER_OCT off their DX7-lineage guesses. The demo is a
coarse reference, fifteen songs of mixed patches with their effects in the path, so it settles a
constant to a few dB and no better. The capture set in captures/requests/ is the real instrument.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import check_demo as cd

SR = cd.SR
FORMS = ["sine", "all1", "all2", "odd1", "odd2", "res1", "res2", "frmt"]
UMODES = ["normal", "noise", "linkFF", "linkFO", "4", "5", "6", "7"]
DESTS = {15: "SendIns-Rev", 16: "SendIns-Var", 17: "Volume", 18: "Panpot", 19: "RevSend", 20: "VarSend",
         21: "Cutoff", 22: "Resonance", 23: "FltEGDepth", 24: "Attack", 25: "Decay", 26: "Release",
         31: "V/N Balance", 32: "Formant", 33: "FM", 34: "PitchBias", 35: "AmpEGBias", 36: "FreqBias",
         37: "V BandWidth", 38: "N BandWidth", 39: "LFO1 pmod", 42: "LFO1 fltmod", 44: "LFO2 fltmod"}


# ------------------------------------------------------------------ the song's own sysex
def _vlq(b, i):
    v = 0
    while True:
        v = (v << 7) | (b[i] & 0x7F)
        i += 1
        if not (b[i - 1] & 0x80):
            return v, i


def sysex_spans(d):
    """(start, length) of every sysex payload: in a MIDI file that is 43 ... F7, after the length."""
    out, pos = [], 14
    while pos < len(d):
        ln = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        base, trk = pos + 8, d[pos + 8:pos + 8 + ln]
        pos += 8 + ln
        i, run = 0, 0
        while i < len(trk):
            _, i = _vlq(trk, i)
            if trk[i] & 0x80:
                run = trk[i]; i += 1
            if run == 0xF0:
                L, i = _vlq(trk, i)
                out.append((base + i, L))
                i += L
            elif run == 0xFF:
                i += 1; L, i = _vlq(trk, i); i += L
            else:
                i += 1 if (run & 0xF0) in (0xC0, 0xD0) else 2
    return out


def song_bulks(path):
    """The performance and the voices a song sends, by part."""
    d = path.read_bytes()
    perf, voices = None, {}
    for start, ln in sysex_spans(d):
        m = d[start:start + ln]
        if len(m) < 10 or m[0] != 0x43 or m[2] != 0x5E:
            continue
        addr, data = m[5:8], m[8:-2]
        if addr[0] == 0x10 and len(data) == 400:
            perf = data
        elif 0x40 <= addr[0] <= 0x43 and len(data) == 608:
            voices[addr[0] - 0x40] = data
    return perf, voices


def cmd_patch(a):
    d = bytearray((ROOT / "captures/demo" / (a.song + ".mid")).read_bytes())
    perf = dict(kv.split("=") for kv in a.perf)
    voice = {}
    for kv in a.voice:
        part, rest = kv.split(":", 1)
        off, val = rest.split("=")
        voice.setdefault(int(part), {})[int(off, 0)] = int(val, 0)
    perf = {int(k, 0): int(v, 0) for k, v in perf.items()}
    hits = 0
    for start, ln in sysex_spans(d):
        m = d[start:start + ln]
        if len(m) < 10 or m[0] != 0x43 or m[2] != 0x5E:
            continue
        addr, data = m[5:8], bytearray(m[8:-2])
        if addr[0] == 0x10 and perf:
            for off, val in perf.items():
                data[off] = val & 0x7F
        elif 0x40 <= addr[0] <= 0x43 and (addr[0] - 0x40) in voice:
            for off, val in voice[addr[0] - 0x40].items():
                data[off] = val & 0x7F
        else:
            continue
        body = bytes(m[3:8]) + bytes(data)
        d[start:start + ln] = bytes(m[0:3]) + body + bytes([(-sum(body)) & 0x7F, 0xF7])
        hits += 1
    Path(a.out).write_bytes(bytes(d))
    print("patched %d bulks into %s" % (hits, a.out))


# ------------------------------------------------------------------ what the patch is
def cmd_dump(a):
    perf, voices = song_bulks(ROOT / "captures/demo" / (a.song + ".mid"))
    if perf is None:
        sys.exit("%s sends no performance bulk" % a.song)
    fx = 80
    print("performance %-12s vol %d pan %d nshift %+d   rev %d var %d ins %d" %
          (bytes(perf[:12]).decode("latin1"), perf[0x10], perf[0x11], perf[0x12] - 24,
           perf[fx + 0x58], perf[fx + 0x5B], perf[fx + 0x5F]))
    for i in range(4):
        b = 192 + i * 52
        if perf[b + 4] == 0x7F:
            continue
        print("  part%d bank %d pgm %3d ch %d poly %d filt %d nshift %+d det %+d vol %3d pan %3d "
              "dry %3d var %3d rev %3d ins %d cut %+d res %+d" %
              (i + 1, perf[b + 1], perf[b + 2], perf[b + 4], perf[b + 5], perf[b + 7], perf[b + 8] - 24,
               perf[b + 9] - 64, perf[b + 0x0B], perf[b + 0x0E], perf[b + 0x11], perf[b + 0x12],
               perf[b + 0x13], perf[b + 0x14], perf[b + 0x18] - 64, perf[b + 0x19] - 64))
    for s in range(8):
        dest = perf[0x40 + s] & 0x3F
        if dest:
            print("  ctrl set %d: parts %#04x src %#06x dest %2d %-12s depth %+d" %
                  (s, perf[0x28 + s], perf[0x30 + 2 * s] << 7 | perf[0x31 + 2 * s], dest,
                   DESTS.get(dest, ""), perf[0x48 + s] - 64))
    for part, v in sorted(voices.items()):
        print("voice(part %d) %-10s alg %d fb %d nshift %+d  lfo1 wave %d spd %d pmd %d amd %d fmd %d" %
              (part + 1, bytes(v[:10]).decode("latin1"), v[0x2C] + 1, v[0x3D], v[0x1E] - 24,
               v[0x10], v[0x11], v[0x15], v[0x16], v[0x17]))
        print("  filter type %d cut %d reso %d egdepth %+d egL4 %d" %
              (v[0x54], v[0x57], v[0x55] - 16, v[0x64] - 64, v[0x65]))
        print("  op form  fix crs fine det bw sk | egL            egT             lvl bp ld rd | pms ams/avs egb")
        for o in range(8):
            p = 112 + o * 62
            print("  %d  %-5s %d  %3d %4d %+3d %2d %d  | %-14s %-14s %3d %3d %2d %2d | %d %d/%+d %+d" %
                  (o + 1, FORMS[v[p + 4] & 7], (v[p + 5] >> 6) & 1, v[p + 1], v[p + 2], v[p + 7] - 15,
                   v[p + 6], (v[p + 5] >> 3) & 7,
                   str([v[p + 12 + i] for i in range(4)]), str([v[p + 16 + i] for i in range(4)]),
                   v[p + 22], v[p + 23], v[p + 24], v[p + 25],
                   v[p + 31] & 7, v[p + 33] >> 4, (v[p + 33] & 15) - 7, (v[p + 34] & 15) - 7))
        for o in range(8):
            q = 112 + o * 62 + 35
            if v[q + 11]:
                print("  unvoiced %d %-6s crs %2d bw %3d res %d skirt %d lvl %3d" %
                      (o + 1, UMODES[v[q + 1] >> 5], v[q + 1] & 0x1F, v[q + 4], v[q + 6] >> 3, v[q + 6] & 7, v[q + 11]))


# ------------------------------------------------------------------ the two takes side by side
def _align():
    """Every render's lag in the recording, found the way check_demo does it, in song order."""
    ref = cd.mono(ROOT / "captures/raw/FS1R DEMO.flac")
    renv = cd.envelope(ref)
    prev, out = None, []
    # Only the numbered songs. captures/demo/ also holds all.mid, the whole demo in one file, and a
    # render of that sorts in beside the fifteen and aligns against nothing.
    for f in sorted(f for f in (ROOT / "captures/demo/render").glob("*.wav") if f.name[:2].isdigit()):
        own = cd.mono(f)
        lo, hi = (0, 3000) if prev is None else (int(prev * 100) - 100, int((prev + 10) * 100))
        oenv = cd.envelope(own)
        lag, r = cd.find(renv, cd.z(oenv), lo, hi)
        loud = np.nonzero(oenv > oenv.max() / 3000.0)[0]
        prev = lag / 100.0 + (loud[-1] + 1) / 100.0
        out.append((f, own, lag, r, ref))
    return out


def cmd_cut(a):
    out = Path(a.outdir)
    for f, own, lag, r, ref in _align():
        if f.stem != a.song:
            continue
        seg = ref[lag * cd.HOP:lag * cd.HOP + len(own)]
        sf.write(str(out / "hw.wav"), seg, SR)
        sf.write(str(out / "eng.wav"), own, SR)
        print("%s: lag %.2f s, envelope r %.3f" % (f.stem, lag / 100.0, r))
        _thirds(own, seg)
        _trace(own, seg)
        return
    sys.exit("no render called %s in captures/demo/render" % a.song)


def _thirds(a, b):
    k = min(len(a), len(b)) // cd.HOP * cd.HOP
    fa, fb = np.abs(np.fft.rfft(a[:k])) ** 2, np.abs(np.fft.rfft(b[:k])) ** 2
    f = np.fft.rfftfreq(k, 1.0 / SR)
    print("\n third-octave, engine minus hardware")
    lo = 50.0
    while lo < 18000:
        hi = lo * 2 ** (1 / 3.0)
        s = (f >= lo) & (f < hi)
        print("  %6.0f Hz  %+6.1f dB   hw %6.1f  eng %6.1f" %
              (lo, 10 * np.log10((fa[s].sum() + 1e-20) / (fb[s].sum() + 1e-20)),
               10 * np.log10(fb[s].sum() + 1e-20), 10 * np.log10(fa[s].sum() + 1e-20)))
        lo = hi


def _trace(a, b):
    print("\n level per second, dB (engine / hardware / difference)")
    for i in range(min(len(a), len(b)) // SR):
        x, y = a[i * SR:(i + 1) * SR], b[i * SR:(i + 1) * SR]
        da = 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)
        db = 20 * np.log10(np.sqrt((y ** 2).mean()) + 1e-12)
        print("  %3d s  %7.1f  %7.1f  %+6.1f" % (i, da, db, da - db))


# ------------------------------------------------------------------ one number per build
def cmd_score(a):
    rows, rs = [], []
    for f, own, lag, r, ref in _align():
        rows.append(cd.bands(own, ref[lag * cd.HOP:lag * cd.HOP + len(own)]))
        rs.append(r)
    d = np.abs(np.array(rows))
    # The recording's own level is not ours to match: it is one take through whatever gain rgwan's
    # converter sat at, where the capture set was measured off the digital tap. Drop each song's median
    # band and what is left is the tilt, which is the part a calibration can move.
    t = np.abs(np.array(rows) - np.median(rows, 1, keepdims=True))
    line = ("%-24s mean|err| %5.2f dB   tilt %5.2f dB   per band %s   env r median %.3f worst %.3f"
            % (a.label, d.mean(), t.mean(), " ".join("%5.1f" % v for v in t.mean(0)), np.median(rs), min(rs)))
    print(line)
    with open(ROOT / "tools/demo_scores.txt", "a") as fh:
        fh.write(line + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("dump"); p.add_argument("song"); p.set_defaults(fn=cmd_dump)
    p = sub.add_parser("cut"); p.add_argument("song"); p.add_argument("outdir", nargs="?", default=".")
    p.set_defaults(fn=cmd_cut)
    p = sub.add_parser("patch"); p.add_argument("song"); p.add_argument("out")
    p.add_argument("--perf", action="append", default=[], metavar="OFF=VAL")
    p.add_argument("--voice", action="append", default=[], metavar="PART:OFF=VAL")
    p.set_defaults(fn=cmd_patch)
    p = sub.add_parser("score"); p.add_argument("label"); p.set_defaults(fn=cmd_score)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
