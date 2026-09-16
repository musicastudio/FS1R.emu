#!/usr/bin/env python3
"""Pull the 15 demo songs out of the FS1R v1.20 EPROM image and write them as standard MIDI files.

    python tools/extract_demo.py   -> captures/demo/NN_Name.mid   (one file per song)
                                     captures/demo/all.mid        (all 15 back to back, as the unit plays them)

Format, read out of the player at flash 0x0002E8B8 (the event loop), 0x0002EACA (fetch a byte and
advance) and 0x0002EC0C (load the song pointer). It is the same byte stream Yamaha used in the TG100,
MU and VL70-m, so ValleyBell's YamahaDemoSongDump reads it too, with one wrinkle: this ROM puts an
`FF 00` before every sysex block and an `F1 00` at the top of every song, and the firmware simply
drops them. The fetch loop skips any byte with bit 7 clear, and 0xFF and 0xF1 fall through every
status comparison, so both pairs are two bytes of nothing.

  0x80-0xE0  running MIDI, 3 bytes for 8n/9n/An/Bn/En and 2 for Cn/Dn. Note off carries a velocity.
  0xF0       sysex, up to and including 0xF7. The demo loads its own performances and voices this way.
  0xF2       end of song. The player then steps the song number, wrapping after 15.
  0xF3 dd    delay, dd >> 2 ticks of the player's own counter.
  0xF4 lo hi delay, ((lo & 0x7F) | (hi << 7)) >> 2 ticks.

The player's counter is stepped every fourth period of the timer ISR at flash 0x0002F250, and every
delay in the ROM is a multiple of four, so a delay of `dd` is dd/4 counter periods. The ISR does not
set its compare register to a fixed period: at 0x0002F28C it reads the running count, adds 0x1116 and
writes that back, so every period is the nominal 28 MHz / 16 / 4374 = 2.5 ms plus however long the
interrupt took to get there. Fourteen register pushes into a 4374-count period is a percent or three,
and rgwan's recording puts it at four: the distance between consecutive songs in his take of the whole
demo is 1.040 times what this file computes at the nominal rate, on all fifteen songs, with no reload
gap left over to explain it away.

So one counter period is 10.4 ms rather than 10.0. The SMF below is written at 200 ticks per quarter
with a 520000 us quarter, which is 2.6 ms a tick, and each delay is emitted as (raw >> 2) * 4 ticks:
one ISR period per tick. Before this the renders drifted 4 % against the recording, which is what held
the envelope correlations down.

The 15 songs run 503 s of events at that rate. The reference recording of the whole demo is 523 s,
so the unit spends roughly a second between songs reloading; --gap sets that for all.mid.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT.parent / "FS1R_DISASM" / "roms" / "fs1r_v120_eprom_cpuview.bin"
OUT = ROOT / "captures" / "demo"
BASE = 0x200000
PTRS, NSONGS = 0x389160, 15          # the pointer table the player indexes, flash literal at 0x0002EC28
RES = 200                            # ticks per quarter
TEMPO = 520000                       # us per quarter: 2.6 ms a tick, one period of the player's counter
EVLEN = {0x80: 3, 0x90: 3, 0xA0: 3, 0xB0: 3, 0xC0: 2, 0xD0: 2, 0xE0: 3}


def decode(rom, at):
    """One song, as (delay units, message bytes) pairs plus the offset one past its 0xF2."""
    out, dly, o = [], 0, at - BASE
    while True:
        c = rom[o]
        if c & 0xF0 in EVLEN and c < 0xF0:
            n = EVLEN[c & 0xF0]
            out.append((dly, rom[o:o + n])); dly = 0; o += n
        elif c == 0xF0:
            e = rom.index(0xF7, o) + 1
            out.append((dly, rom[o:e])); dly = 0; o = e
        elif c == 0xF2:
            return out, o + 1
        elif c == 0xF3:
            dly += (rom[o + 1] >> 2) * 4; o += 2
        elif c == 0xF4:
            dly += ((((rom[o + 1] & 0x7F) | (rom[o + 2] << 7)) >> 2) * 4); o += 3
        else:
            o += 1                   # 0xF1, 0xFF and any data byte: the fetch loop walks past it


def varlen(n):
    b = bytearray([n & 0x7F])
    n >>= 7
    while n:
        b.append(0x80 | (n & 0x7F)); n >>= 7
    return bytes(reversed(b))


def smf(tracks):
    hdr = struct.pack(">4sIHHH", b"MThd", 6, 0 if len(tracks) == 1 else 1, len(tracks), RES)
    out = bytearray(hdr)
    for k, ev in enumerate(tracks):
        t = bytearray()
        if k == 0:
            t += bytes([0x00, 0xFF, 0x51, 0x03]) + struct.pack(">I", TEMPO)[1:]
        for dly, msg in ev:
            t += varlen(dly)
            t += (b"\xF0" + varlen(len(msg) - 1) + bytes(msg[1:])) if msg[0] == 0xF0 else bytes(msg)
        t += b"\x00\xFF\x2F\x00"
        out += struct.pack(">4sI", b"MTrk", len(t)) + t
    return bytes(out)


def title(ev, n):
    """The name of the first performance the song loads, which is what the demo is showing off."""
    for _, m in ev:
        if m[0] == 0xF0 and len(m) > 21 and bytes(m[6:9]) == b"\x10\x00\x00":
            s = " ".join(bytes(m[9:21]).decode("ascii", "replace").split())
            return re.sub(r"[^A-Za-z0-9 !+-]", "_", s) or "Song%02d" % n
    return "Song%02d" % n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="?", default=str(ROM))
    ap.add_argument("--gap", type=float, default=1.0, help="seconds between songs in all.mid")
    args = ap.parse_args()
    rom = Path(args.rom).read_bytes()
    if len(rom) != 0x200000:
        sys.exit("%s is not a 2 MB EPROM image" % args.rom)
    ptrs = struct.unpack_from(">%dI" % NSONGS, rom, PTRS - BASE)
    OUT.mkdir(parents=True, exist_ok=True)
    every, total = [], 0
    for i, p in enumerate(ptrs):
        ev, end = decode(rom, p)
        # Each song's stream runs right up to the next one's start, so a clean decode lands there.
        limit = ptrs[i + 1] - BASE if i + 1 < NSONGS else end
        assert end <= limit and limit - end < 4, "song %d ended at %#x, next starts at %#x" % (i + 1, end + BASE, limit + BASE)
        name = "%02d_%s.mid" % (i + 1, title(ev, i + 1))
        (OUT / name).write_bytes(smf([ev]))
        ticks = sum(d for d, _ in ev)
        total += ticks
        print("%2d %#08x %-24s %5d events %7.1f s" % (i + 1, p, name, len(ev), ticks * TEMPO / 1e6 / RES))
        if every:
            ev = [(ev[0][0] + int(args.gap * 400), ev[0][1])] + ev[1:]
        every += ev
    (OUT / "all.mid").write_bytes(smf([every]))
    print("all.mid %.1f s of events, %.1f s with the gaps" % (total * TEMPO / 1e6 / RES, total * TEMPO / 1e6 / RES + 14 * args.gap))


if __name__ == "__main__":
    main()
