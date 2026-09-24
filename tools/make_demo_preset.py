#!/usr/bin/env python3
"""Rewrite a demo song so it loads its performance from the preset banks instead of its own sysex.

    python tools/make_demo_preset.py "captures/demo/02_Full Tines.mid" 141
        -> captures/demo_preset/02_Full Tines.mid

Every sysex event in the song is dropped and replaced, at the first one's tick, by bank select
(CC0 0x3F, CC32 0x41 + bank) and a program change on the performance channel, which is what a
front-panel or MIDI selection of the same preset does. Rendering both files through the engine says
whether the bundled preset and the demo's own bulk are the same patch (tools/compare_isolated.py or a
straight sample diff). The demo's performances and voices are the unit's own conversions of the
preset banks, hand edited on a few bytes afterwards, so a residual is expected: diff the bulks first.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_var(d, i):
    v = 0
    while True:
        b = d[i]; i += 1; v = v << 7 | b & 0x7F
        if not b & 0x80:
            return v, i


def var(n):
    out = [n & 0x7F]
    while n > 0x7F:
        n >>= 7; out.insert(0, n & 0x7F | 0x80)
    return bytes(out)


def parse(path):
    """One track, format 0: [(tick, bytes)] with running status resolved."""
    d = Path(path).read_bytes()
    assert d[:4] == b"MThd"
    hl = struct.unpack(">I", d[4:8])[0]
    fmt, ntr, div = struct.unpack(">HHH", d[8:14])
    assert fmt == 0 and ntr == 1, "format 0 only"
    i = 8 + hl
    assert d[i:i + 4] == b"MTrk"
    ln = struct.unpack(">I", d[i + 4:i + 8])[0]; i += 8; end = i + ln
    tick, run, ev = 0, None, []
    while i < end:
        dt, i = read_var(d, i); tick += dt
        st = d[i]
        if st == 0xF0:
            n, j = read_var(d, i + 1); ev.append((tick, d[i:j + n])); i = j + n
        elif st == 0xFF:
            n, j = read_var(d, i + 2); ev.append((tick, d[i:j + n])); i = j + n
        else:
            if st & 0x80: run = st; i += 1
            else: st = run
            n = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
            ev.append((tick, bytes([st]) + d[i:i + n])); i += n
    return div, ev


def write(path, div, ev):
    body, last = bytearray(), 0
    for tick, e in ev:
        body += var(tick - last); last = tick
        body += e[:1] + var(len(e) - 1) + e[1:] if e[0] == 0xF0 else e
    Path(path).write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, div) + b"MTrk" + struct.pack(">I", len(body)) + body)


def main():
    src, perf = Path(sys.argv[1]), int(sys.argv[2])
    div, ev = parse(src)
    at = next(t for t, e in ev if e[0] == 0xF0)
    bank, prog = divmod(perf, 128)                       # 0 PrA, 1 PrB, 2 PrC; LSB 0x41 + bank
    load = [(at, bytes([0xB0, 0, 0x3F])), (at, bytes([0xB0, 32, 0x41 + bank])), (at, bytes([0xC0, prog]))]
    ev = [(t, e) for t, e in ev if e[0] != 0xF0]
    k = next(i for i, (t, _) in enumerate(ev) if t >= at)
    ev[k:k] = load
    out = ROOT / "captures" / "demo_preset" / src.name
    out.parent.mkdir(parents=True, exist_ok=True)
    write(out, div, ev)
    print(f"{out.relative_to(ROOT)}: performance {perf} (bank LSB {0x41 + bank:#x}, program {prog}) at tick {at}")


if __name__ == "__main__":
    main()
