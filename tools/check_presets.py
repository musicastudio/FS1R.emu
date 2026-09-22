#!/usr/bin/env python3
"""Check the bundled preset banks against the indexes that name them.

    python tools/check_presets.py

plugin/generated/fs1r_presets.syx is the 1408 factory voices as one sysex stream, and the plugin picks the n-th
dump out of it by the same flat voice number the engine uses. Nothing in the build ties the blob to
plugin/generated/fs1r_presets.csv, so this reads both back: the dump count, every checksum, that the n-th dump is
a voice the loader will actually find, and that the names in the n-th dump match the n-th index row.

plugin/generated/fs1r_performances.syx and plugin/generated/fs1r_fseqs.syx carry their own names, so those are checked for
count, checksum and shape, plus the three performance bank boundaries against the Data List's own
lists, and that every Fseq's frame count agrees with its length. An Fseq bulk's byte count cannot
express a 512 frame dump, which is exactly why the header is what says how long it is.

Exits non-zero on the first mismatch. No dependencies beyond the standard library.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYX = ROOT / "plugin" / "generated" / "fs1r_presets.syx"
CSV = ROOT / "plugin" / "generated" / "fs1r_presets.csv"
PERF = ROOT / "plugin" / "generated" / "fs1r_performances.syx"
FSEQ = ROOT / "plugin" / "generated" / "fs1r_fseqs.syx"
NATIVE = 256           # voices 0..255 are native dumps, the rest DX7 VCED
# The first name of each preset performance bank, from the Data List's own performance lists, and the
# last name in the table. Banks A, B, C in order; Preset C is the one the manual describes as the G50
# guitar bank, and 256-383 is the only block whose receive channels stop at 6.
PERF_BANKS = {0: "Zap !", 128: "Sweepy Voice", 256: "UprightPiano", 383: "Drum Kit 2"}


def dumps(blob):
    i = 0
    while i < len(blob):
        if blob[i] != 0xF0:
            raise ValueError(f"byte {i} is {blob[i]:#04x}, expected a sysex start")
        j = blob.index(0xF7, i)
        yield blob[i:j + 1]
        i = j + 1


def name_of(d, n):
    # A native bulk carries the 10-character name at the front of its 608 data bytes; a DX7 VCED
    # carries it at the end of its 155, which is where tools/extract_presets.py moved it from.
    return (d[9:19] if n < NATIVE else d[6 + 145:6 + 155]).decode("latin1").rstrip()


def check_performances(fails):
    d = list(dumps(PERF.read_bytes()))
    if len(d) != 384:
        fails.append(f"{len(d)} performances, expected 384")
        return
    for n, m in enumerate(d):
        if m[3] != 0x5E or (m[4] << 7 | m[5]) != 400 or m[6] != 0x10 or len(m) != 411:
            fails.append(f"performance {n} is not a 400 byte current-performance bulk")
            break
        if (-sum(m[4:-2])) & 0x7F != m[-2]:
            fails.append(f"performance {n} has a bad checksum")
            break
    for n, want in PERF_BANKS.items():
        got = d[n][9:9 + 12].decode("latin1").rstrip()
        if got != want:
            fails.append(f"performance {n} is {got!r}, the Data List says {want!r}")
    # The Preset C fingerprint, owner's manual page 21: no part above receive channel A6 and every
    # pitch bend range -12..+12. Only the third bank looks like this, which is what names it.
    for n in range(256, 384):
        for part in range(4):
            p = d[n][9 + 192 + 52 * part: 9 + 192 + 52 * (part + 1)]
            if p[1] == 0:
                continue                     # bank off: the part is unused
            if p[4] < 16 and p[4] > 5:
                fails.append(f"performance {n} part {part + 1} receives on A{p[4] + 1}, above Preset C's A6")
            if (p[38], p[39]) != (76, 52):
                fails.append(f"performance {n} part {part + 1} bends {p[38] - 64}..{p[39] - 64}, not -12..+12")
        if len(fails) > 5:
            break


def check_fseqs(fails):
    d = list(dumps(FSEQ.read_bytes()))
    if len(d) != 90:
        fails.append(f"{len(d)} Fseqs, expected 90")
        return
    for n, m in enumerate(d):
        if m[3] != 0x5E or m[6] != 0x70:
            fails.append(f"Fseq {n} is not an Fseq bulk")
            break
        frames = 128 * ((m[9 + 0x1B] & 3) + 1)
        if len(m) != 11 + 32 + frames * 50:
            fails.append(f"Fseq {n} says {frames} frames but is {len(m)} bytes")
            break
        if frames != (512 if n < 10 else 128):
            fails.append(f"Fseq {n} has {frames} frames; 0-9 are 512 and 10-89 are 128")
            break
        if (-sum(m[4:-2])) & 0x7F != m[-2]:
            fails.append(f"Fseq {n} has a bad checksum")
            break


def main():
    for f in (SYX, CSV, PERF, FSEQ):
        if not f.exists():
            print(f"check_presets: missing {f}; run tools/make_presets_blob.py", file=sys.stderr)
            return 1
    rows = [l.split(",") for l in CSV.read_text(encoding="utf-8").splitlines()[1:] if l]
    all_dumps = list(dumps(SYX.read_bytes()))
    fails = []

    if len(all_dumps) != len(rows):
        fails.append(f"{len(all_dumps)} dumps against {len(rows)} index rows")
    for n, (d, row) in enumerate(zip(all_dumps, rows)):
        if int(row[0]) != n:
            fails.append(f"index row {n} says voice {row[0]}")
            break
        # The loader in src/fs1r_lib.cpp matches these two shapes and nothing else.
        native = d[3] == 0x5E and (d[4] << 7 | d[5]) == 608 and d[6] == 0x51
        dx7 = d[3:6] == b"\x00\x01\x1b" and len(d) == 163
        if not (native if n < NATIVE else dx7):
            fails.append(f"voice {n} is not a dump the loader will pick: {d[:8].hex(' ')}")
        body = d[4:-2] if native else d[6:-2]
        if (-sum(body)) & 0x7F != d[-2]:
            fails.append(f"voice {n} has a bad checksum")
        if name_of(d, n) != row[1].strip():
            fails.append(f"voice {n} is named {name_of(d, n)!r}, the index says {row[1].strip()!r}")
        if len(fails) > 5:
            break

    check_performances(fails)
    check_fseqs(fails)

    for f in fails:
        print("check_presets: " + f)
    if fails:
        print("check_presets: the bundled bank and its index have drifted")
        return 1
    print(f"check_presets: {len(all_dumps)} voices, 384 performances and 90 Fseqs; names, checksums, "
          f"bank boundaries and dump shapes all match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
