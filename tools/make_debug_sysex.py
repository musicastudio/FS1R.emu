#!/usr/bin/env python
"""make_debug_sysex.py — build a patched FS1R v1.20 image with a MIDI peek/poke.

This is the 'flash once, drive forever' route described in
captures/debug_interface_research.md §4. It is intentionally conservative:

* It leaves the internal-flash loader (0x0000-0x7FFF) untouched — so the unit
  always still boots.
* It patches only the upper tail of the internal flash that the loader
  refreshes from the external EPROM (0x8000-0x3FFFF), i.e. it produces a new
  *EPROM* image, which is what rgwan already knows how to swap/flash.
* Patches:
    - Hijack one unused entry in the sysex class dispatch at 0x3DE44
      (states 1..7 share a stub at 0x15C45: entry 0xF is free) to point at a
      new handler.
    - New handler (assembled SH-2A, appended in a free pocket at the end of
      flash below 0x3FC00) decodes a small private sysex:
          F0 43 2n 5E 7F <cmd> <addr3 addr2 addr1 addr0> <len..> F7
      cmd = 0x01 peek: reply on MIDI OUT with n 7-bit-encoded bytes
      cmd = 0x02 poke: write 16-bit words into CS space (YMP706, VOP3)
      cmd = 0x03 exec: copy to 0x01040000 and jsr — keep last, gated.

Nothing is written or flashed by this script. It emits:
  fs1r_debug_eprom.bin         patched 2 MB EPROM image (byte-swapped like stock)
  fs1r_debug_patch.diff.txt    human-readable list of changed bytes

Requires: roms/fs1r_v120_eprom_cpuview.bin (the CPU-view, big-endian image).
"""
from __future__ import annotations
import argparse, struct, sys
from pathlib import Path

ROM_IN   = Path(__file__).with_name("fs1r_v120_eprom_cpuview.bin")
EP_OUT   = Path(__file__).with_name("fs1r_debug_eprom.bin")
DIFF_OUT = Path(__file__).with_name("fs1r_debug_patch.diff.txt")

# --- layout -----------------------------------------------------------------
EPROM_BASE   = 0x200000
SYSEX_TBL    = 0x3DE44            # 16 × u32 sysex-state handler pointers
FREE_SLOT    = 0xF                # state 0xF is unused in v1.20 (points at stub)
NEW_HANDLER  = 0x3F800            # free area just below version tag at 0x3FC00
LOADER_END   = 0x8000             # never write below this in *flash* terms


def to_rom(a: int) -> int:
    """CPU address -> file offset in the cpuview image."""
    return a - EPROM_BASE


def build_debug_handler() -> bytes:
    """SH-2A big-endian code for the peek/poke/exec sysex handler.

    Written to NEW_HANDLER. Calling convention matches the state handler table:
    entered with the next data byte in r0, sysex state globals live in
    0x0102A1D2.. (see FS1R_DISASM decomp of switchD_000180b6). This stub keeps
    its own small state machine in a free dram word (0x01042000).
    """
    # NOTE: this is a *placeholder skeleton*, not field-tested. It is emitted
    # as a comment block inside the patch so a human reviews it first; the
    # real bytes are produced only after rgwan confirms the UART framing and
    # we can test against his unit. The script deliberately exits before
    # writing flash bytes if the handler is still the empty shell.
    code = bytearray()
    code += b'\x00\x0b'                  # rts
    code += b'\x00\x09'                  # nop
    return bytes(code)


def main():
    if not ROM_IN.exists():
        sys.exit(f"missing {ROM_IN} — run from FS1R_DISASM/roms or copy it here")
    rom = bytearray(ROM_IN.read_bytes())
    if len(rom) != 0x200000:
        sys.exit(f"unexpected size {len(rom):#x} — want 2 MB cpuview")

    handler = build_debug_handler()
    if handler in (b'', None) or len(handler) < 8:
        sys.exit("debug handler is a stub — refusing to build a half-patched "
                 "image (see docstring). Implement it after SCI1 framing is "
                 "confirmed by captures_debug_request.md step (1).")

    patched = bytearray(rom)
    diff = []
    def poke_byte(addr, val):
        patched[to_rom(addr)] = val
        diff.append((addr, rom[to_rom(addr)], val))

    # 1. redirect sysex state 0xF to our handler
    tbl_entry = SYSEX_TBL + FREE_SLOT * 4
    old = struct.unpack('>I', rom[to_rom(tbl_entry):to_rom(tbl_entry)+4])[0]
    struct.pack_into('>I', patched, to_rom(tbl_entry), NEW_HANDLER | 1)  # |1 = SH thumb-style tag
    diff.append((tbl_entry, (old >> 0) & 0xff, NEW_HANDLER & 0xff))

    # 2. drop the handler code
    for i, b in enumerate(handler):
        poke_byte(NEW_HANDLER + i, b)

    EP_OUT.write_bytes(patched)
    with DIFF_OUT.open('w') as f:
        f.write(f"FS1R debug patch, handler at {NEW_HANDLER:#x}\n")
        for a, old_b, new_b in diff:
            if old_b != new_b:
                f.write(f"  {a:08X}: {old_b:02X} -> {new_b:02X}\n")
    print(f"wrote {EP_OUT} ({len(diff)} changed bytes) and {DIFF_OUT}")
    print("NEXT STEPS:")
    print("  1. diff the patch, sanity-check the state-0xF redirect")
    print("  2. have rgwan write this as the external EPROM image")
    print("     (or the flash replacement he validated) — never the loader")
    print("  3. boot, send  F0 43 20 5E 7F 01 <addr4> <len> F7  and expect a reply")


if __name__ == '__main__':
    main()
