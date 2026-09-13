#!/usr/bin/env python3
"""Pull the preset voices out of the FS1R v1.20 EPROM image.

    python tools/extract_presets.py            -> presets/native/NNN_Name.syx (256 native 608-byte voices)
                                                  presets/dx7/NNN_Name.syx    (1152 DX7-format 155-byte voices, as DX7 VCED)
                                                  presets/index.csv

ROM layout (CPU view, i.e. byte pairs swapped from the raw dump):
  0x230091  1152 x 155 bytes: DX7 VCED voice with the 10-char name moved to the front (9 banks, PrA..PrI)
  0x25C280   256 x 608 bytes: native FS1R voice, identical to the sysex bulk layout (2 banks, PrJ..PrK)
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
    with open(OUT / "index.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["bank", "index", "name", "category", "algorithm", "file"])
        w.writerows(rows)
    print(f"wrote {NAT_N} native + {DX7_N} dx7 voices to {OUT}")


if __name__ == "__main__":
    main()
