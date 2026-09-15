#!/usr/bin/env python3
"""Check the panel's cursor stops against the firmware's own screen tables.

    python tools/check_interface.py [--rom PATH]

The FS1R keeps one 40-byte record per cursor stop in the EPROM: the PLAY screens at 0x392FEC with
their MIDI View records at 0x39312C, and the PART ASSIGN screens at 0x39328C with theirs at 0x39346C.
Each record is the two display lines for that stop, so the records are the stop list: their order is
the order the CURSOR buttons walk, and the name on the upper line is what the display calls the stop.
plugin/PanelView.cpp's kPlayFields and kFields have to be the same two lists in the same order, and
nothing in the build ties them together.

This reads the records back out and compares them. The EPROM is not in this repository, so the check
skips with a warning when the image is missing, the way tools/regress.py does with its ROM cases.

docs/interface_from_firmware.md explains the record format and lists what came out of it.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ROM = ROOT.parent / "FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin"
BASE = 0x200000                 # the EPROM's CPU address; file offset = address - BASE
PART_SCREENS, PART_MIDI = 0x39328C, 0x39346C
PLAY_SCREENS, PLAY_MIDI = 0x392FEC, 0x39312C
STRIDE, UPPER = 40, 19          # a record is 19 upper characters, 20 lower ones and a NUL

# In both tables two adjacent stops leave the upper line blank and name the thing being selected on the
# lower one - "Perform" or "Voice" - because the display puts the bank and program number pair there
# instead of a parameter name. They are matched by position: the pair is at 1 and 2 on the PLAY screen
# and at 2 and 3 on the PART ASSIGN one.
PAIR_NAMES = ("Bank", "Pgm#")


def records(rom, base, n):
    out = []
    for i in range(n):
        off = base - BASE + i * STRIDE
        r = rom[off:off + STRIDE]
        if len(r) != STRIDE or r[39] != 0:
            raise ValueError(f"record {i} at {base + i * STRIDE:#08x} is not NUL terminated")
        text = lambda b: "".join(chr(c) if 32 <= c < 127 else " " for c in b)
        out.append((text(r[:UPPER]).strip(), text(r[UPPER:39]).strip()))
    return out


def code_fields(table, count):
    """(label, midi) for every entry of one Field table in plugin/PanelView.cpp, in order."""
    src = (ROOT / "plugin" / "PanelView.cpp").read_text(encoding="utf-8")
    block = re.search(r"const Field " + table + r"\[" + count + r"\] = \{(.*?)\n\};", src, re.S)
    if not block:
        sys.exit(f"check_interface: no {table} table in plugin/PanelView.cpp")
    return re.findall(r'\{\s*"([^"]*)",[^,]+,\s*(?:"[^"]*"|nullptr),\s*(?:"[^"]*"|nullptr),'
                      r'\s*"([^"]*)"\s*\}', block.group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=str(DEFAULT_ROM))
    rom_path = Path(ap.parse_args().rom)
    if not rom_path.exists():
        print(f"check_interface: no EPROM image at {rom_path}, skipping")
        return 0
    rom = rom_path.read_bytes()

    fails = []

    def compare(what, screens, midi, fields, pair_lower, pair_at):
        by_position = {pair_at: PAIR_NAMES[0], pair_at + 1: PAIR_NAMES[1]}
        if len(fields) != len(screens):
            fails.append(f"{what}: the panel has {len(fields)} stops, the firmware has {len(screens)}")
            return
        for i, ((upper, lower), (label, msg)) in enumerate(zip(screens, fields)):
            want = by_position.get(i, upper)
            if i in by_position and lower != pair_lower:
                fails.append(f"{what} stop {i}: expected the bank/program pair, "
                             f"the firmware's lower line is {lower!r}")
            elif label != want:
                fails.append(f"{what} stop {i}: the panel calls it {label!r}, "
                             f"the firmware calls it {want!r}")
            # "MIDI     Volume  =" / "Bn 07     =" - the message is the lower line up to its "="
            want_msg = midi[i][1].split("=")[0].strip() if midi[i][0] else ""
            if msg != want_msg:
                fails.append(f"{what} stop {i} ({label}): MIDI View is {msg!r}, "
                             f"the firmware shows {want_msg!r}")

    compare("PART ASSIGN", records(rom, PART_SCREENS, 12), records(rom, PART_MIDI, 12),
            code_fields("kFields", "kNumFields"), "Voice", 2)
    compare("PLAY", records(rom, PLAY_SCREENS, 8), records(rom, PLAY_MIDI, 8),
            code_fields("kPlayFields", "kNumPlayFields"), "Perform", 1)

    for f in fails:
        print("check_interface: " + f)
    if fails:
        print("check_interface: the panel and the firmware's screen tables have drifted")
        return 1
    print("check_interface: 8 PLAY and 12 PART ASSIGN cursor stops, their order and their MIDI View "
          "messages all match the firmware")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
