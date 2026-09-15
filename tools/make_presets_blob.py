#!/usr/bin/env python3
"""Packs presets/ into the files the plugin embeds.

    python tools/make_presets_blob.py

presets/ holds the 1408 factory voices as one .syx per voice, pulled out of the EPROM image by
tools/extract_presets.py. The plugin has to be able to browse and load them with no EPROM in sight, so
they are concatenated into one sysex stream and indexed by the flat voice number the engine already
uses: 0-255 are the native voices (banks PrA and PrB), 256-1407 the DX7-format ones (PrC..PrK). That
is the same numbering as bank_voice_index() in src/fs1r_lib.cpp, so a bank and program number pair
picks the same voice with or without the EPROM.

The native dumps come out of the extractor addressed to part 1 (address high 0x40), which would pin
every one of them to that part. They are re-addressed to 0x51 - the current voice buffer of whichever
part the loader asks for - and their checksums fixed.

The 384 performances and the 90 preset Fseqs are packed the same way, one stream each, picked by their
own index. Neither needs a name index: a performance dump carries its 12-character name and its
category in its own data, and an Fseq dump carries its 8-character name in its header.

Writes plugin/fs1r_presets.syx, plugin/fs1r_presets.csv, plugin/fs1r_performances.syx and
plugin/fs1r_fseqs.syx, all committed so the build needs no Python.
"""
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INDEX = ROOT / "presets" / "index.csv"
OUT_SYX = ROOT / "plugin" / "fs1r_presets.syx"
OUT_CSV = ROOT / "plugin" / "fs1r_presets.csv"
OUT_PERF = ROOT / "plugin" / "fs1r_performances.syx"
OUT_FSEQ = ROOT / "plugin" / "fs1r_fseqs.syx"


def to_part_relative(dump: bytes) -> bytes:
    """F0 43 0n 5E bc bc ah am al <data> cs F7 with ah 0x40 (part 1) re-addressed to 0x51 (current)."""
    if len(dump) < 12 or dump[3] != 0x5E or dump[6] != 0x51 and dump[6] != 0x40:
        return dump
    b = bytearray(dump)
    b[6] = 0x51
    b[-2] = (-sum(b[4:-2])) & 0x7F
    return bytes(b)


def main() -> int:
    if not INDEX.exists():
        print(f"missing {INDEX}; run tools/extract_presets.py first", file=sys.stderr)
        return 1
    rows = list(csv.DictReader(INDEX.open(encoding="utf-8")))
    entries = {}
    for r in rows:
        if r["bank"] not in ("native", "dx7"):
            continue
        flat = int(r["index"]) + (0 if r["bank"] == "native" else 256)
        entries[flat] = r
    if sorted(entries) != list(range(1408)):
        print(f"index.csv covers {len(entries)} voices, expected a full 0..1407", file=sys.stderr)
        return 1

    blob = bytearray()
    out = []
    for flat in range(1408):
        r = entries[flat]
        dump = (ROOT / r["file"].replace("\\", "/")).read_bytes()
        blob += to_part_relative(dump)
        out.append((flat, r["name"].rstrip(), r["category"] or "-1"))

    def pack(kind, dest, expected):
        files = [r["file"] for r in rows if r["bank"] == kind]
        if len(files) != expected:
            print(f"index.csv has {len(files)} {kind} rows, expected {expected}", file=sys.stderr)
            return None
        data = b"".join((ROOT / f.replace("\\", "/")).read_bytes() for f in files)
        dest.write_bytes(data)
        return len(files), len(data)

    packed = [pack("performance", OUT_PERF, 384), pack("fseq", OUT_FSEQ, 90)]
    if any(p is None for p in packed):
        return 1

    OUT_SYX.write_bytes(blob)
    with OUT_CSV.open("w", encoding="utf-8", newline="\n") as f:
        f.write("index,name,category\n")
        for flat, name, cat in out:
            f.write(f"{flat},{name},{cat}\n")
    print(f"{len(out)} voices -> {OUT_SYX.relative_to(ROOT)} ({len(blob)} bytes), "
          f"{OUT_CSV.relative_to(ROOT)} ({OUT_CSV.stat().st_size} bytes)")
    for dest, (n, size) in zip((OUT_PERF, OUT_FSEQ), packed):
        print(f"{n} -> {dest.relative_to(ROOT)} ({size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
