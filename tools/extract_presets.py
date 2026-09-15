#!/usr/bin/env python3
"""Pull the preset voices, performances and Fseqs out of the FS1R v1.20 EPROM image.

    python tools/extract_presets.py   -> presets/native/NNN_Name.syx       (256 native 608-byte voices)
                                         presets/dx7/NNN_Name.syx          (1152 DX7-format 155-byte voices, as VCED)
                                         presets/performances/NNN_Name.syx (384 400-byte performances)
                                         presets/fseq/NN_Name.syx          (90 preset formant sequences)
                                         presets/index.csv

ROM layout (CPU view, i.e. byte pairs swapped from the raw dump):
  0x20A000   384 x 400 bytes: performance, identical to the sysex bulk layout. The three preset banks
             of 128 in order A, B, C; the run ends at 0x22F800 and 0xFF fill follows, and the names
             match the Data List's own performance lists entry for entry ("Zap !" = PrA 001,
             "Sweepy Voice" = PrB 001, "UprightPiano" = PrC 001). Preset C is confirmed by the
             manual's own description of it, page 21: the G50 guitar controller bank, receive channels
             no higher than 6 and pitch bend range -12..+12, which is what 256-383 holds and nothing
             else does. INTERNAL is the user's battery-backed bank, so it is not in the image.
  0x283000    80 x 6432 bytes: preset Fseq 11-90, a 32-byte header and 128 50-byte frames
  0x300A00    10 x 25632 bytes: preset Fseq 1-10, the same header and 512 frames
  0x230091  1152 x 155 bytes: DX7 VCED voice with the 10-char name moved to the front (9 banks, PrC..PrK)
  0x25C280   256 x 608 bytes: native FS1R voice, identical to the sysex bulk layout (2 banks, PrA, PrB)

The bank order is the manual's, page 21: PRESET A and B are the FS1R's own voices, PRESET C through K
the DX-series ones. The performances confirm it - "FundaBass" asks for bank 2 program 41 and native
voice 41 is "FundaBass".
"""
import csv
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT.parent / "FS1R_DISASM" / "roms" / "fs1r_v120_eprom_cpuview.bin"
OUT = ROOT / "presets"
BASE = 0x200000
DX7_AT, DX7_N = 0x230091 - BASE, 1152
NAT_AT, NAT_N = 0x25C280 - BASE, 256
PERF_AT, PERF_N = 0x20A000 - BASE, 384
FSEQ_LONG_AT, FSEQ_LONG_N = 0x300A00 - BASE, 10       # 512 frames each
FSEQ_SHORT_AT, FSEQ_SHORT_N = 0x283000 - BASE, 80     # 128 frames each
# The three performance banks, in the order the table holds them.
PERF_BANKS = [(0, "PrA"), (128, "PrB"), (256, "PrC")]


def yamaha_bulk(addr_h, addr_m, addr_l, data):
    """FS1R native bulk dump: F0 43 0n 5E bc bc ah am al data cs F7 (device 1)."""
    bc = len(data)
    body = [bc >> 7 & 0x7F, bc & 0x7F, addr_h, addr_m, addr_l] + list(data)
    cs = (-sum(body)) & 0x7F
    return bytes([0xF0, 0x43, 0x00, 0x5E] + body + [cs, 0xF7])


def dx7_vced(vced155):
    body = list(vced155)
    cs = (-sum(body)) & 0x7F
    return bytes([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B] + body + [cs, 0xF7])


def safe(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name.strip()) or "voice"


def main():
    rom = ROM.read_bytes()
    rows = []
    (OUT / "native").mkdir(parents=True, exist_ok=True)
    (OUT / "dx7").mkdir(parents=True, exist_ok=True)
    for i in range(NAT_N):
        v = rom[NAT_AT + i * 608: NAT_AT + (i + 1) * 608]
        name = v[:10].decode("latin1")
        p = OUT / "native" / f"{i:03d}_{safe(name)}.syx"
        p.write_bytes(yamaha_bulk(0x40, 0x00, 0x00, v))
        rows.append(("native", i, name, v[0x0E], v[0x2C] + 1, str(p.relative_to(ROOT))))
    for i in range(DX7_N):
        r = rom[DX7_AT + i * 155: DX7_AT + (i + 1) * 155]
        name = r[:10].decode("latin1")
        vced = r[10:] + r[:10]          # DX7 VCED order: 6 ops, common, then name
        p = OUT / "dx7" / f"{i:03d}_{safe(name)}.syx"
        p.write_bytes(dx7_vced(vced))
        rows.append(("dx7", i, name, "", vced[134] + 1, str(p.relative_to(ROOT))))
    (OUT / "performances").mkdir(parents=True, exist_ok=True)
    for i in range(PERF_N):
        d = rom[PERF_AT + i * 400: PERF_AT + (i + 1) * 400]
        name = d[:12].decode("latin1")
        p = OUT / "performances" / f"{i:03d}_{safe(name)}.syx"
        # 10 00 00 is the current performance bulk: loading one of these files plays it.
        p.write_bytes(yamaha_bulk(0x10, 0x00, 0x00, d))
        rows.append(("performance", i, name, d[0x0E], "", str(p.relative_to(ROOT))))

    # Fseq 1-10 carry 512 frames, 11-90 carry 128. The bulk's byte count cannot express 25632, which
    # is why the Data List says "FSeq Bulk does not interpret Byte Count": the header's own frame
    # count field is what says how long the dump is.
    (OUT / "fseq").mkdir(parents=True, exist_ok=True)
    for i in range(FSEQ_LONG_N + FSEQ_SHORT_N):
        at, size = ((FSEQ_LONG_AT + i * 25632, 25632) if i < FSEQ_LONG_N
                    else (FSEQ_SHORT_AT + (i - FSEQ_LONG_N) * 6432, 6432))
        d = rom[at: at + size]
        name = d[:8].decode("latin1")
        frames = 128 * ((d[0x1B] & 3) + 1)
        assert 32 + frames * 50 == size, f"Fseq {i} header says {frames} frames in {size} bytes"
        p = OUT / "fseq" / f"{i:02d}_{safe(name)}.syx"
        p.write_bytes(yamaha_bulk(0x70, 0x00, 0x00, d))
        rows.append(("fseq", i, name, "", "", str(p.relative_to(ROOT))))

    with open(OUT / "index.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["bank", "index", "name", "category", "algorithm", "file"])
        w.writerows(rows)
    print(f"wrote {NAT_N} native + {DX7_N} dx7 voices, {PERF_N} performances and "
          f"{FSEQ_LONG_N + FSEQ_SHORT_N} Fseqs to {OUT}")


if __name__ == "__main__":
    main()
