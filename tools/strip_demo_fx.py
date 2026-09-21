#!/usr/bin/env python3
"""Copy the demo songs with the effect section and the per-part filter switched off.

    python tools/strip_demo_fx.py   -> captures/demo_nofilterfx/*.mid

Every demo song loads its own performance with one 400-byte bulk dump (F0 43 00 5E 03 10 10 00 00 ...)
and then one voice bulk per part. Effects and the filter are both reached from the performance, so the
voices come through untouched and only that one block is rewritten, in place and at the same length:

  effect block (offset 80)  reverb / variation / insertion type -> No Effect, the three master EQ bands -> 0 dB
  each part (offset 192)    FilterSw -> off, Dry Level -> 127, RevSend / VarSend -> 0, InsSw -> off

FilterSw is the whole gate, both in the firmware and in src/fs1r_lib.cpp (start_filter reads part byte
0x07), so the voice's own cutoff, resonance and filter EG can stay as they are: nothing reads them. The
CCs the songs send to filter and send destinations land on bytes that no longer feed anything, and the
one parameter change in 12_Acid King writes voice cutoff, which is equally inert.

Dry Level is forced to 127 because a part routed through the insertion block carries no dry level of
its own, and switching InsSw off with a stored 0 in place would silence it. All fifteen demo
performances already sit at 127, so nothing actually moves; the line is there for safety.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "captures" / "demo"
DST = ROOT / "captures" / "demo_nofilterfx"
PERF_LEN = 400
# Everything after the F0, which an SMF puts a length byte behind: 43 00 5E, the byte count, the address.
HDR = bytes([0x43, 0x00, 0x5E, PERF_LEN >> 7, PERF_LEN & 0x7F, 0x10, 0x00, 0x00])


def strip(perf):
    """One performance bulk body, effects and filter off."""
    p = bytearray(perf)
    fx = 80
    p[fx + 0x58] = p[fx + 0x5B] = p[fx + 0x5F] = 0          # reverb, variation, insertion type: No Effect
    p[fx + 0x64] = p[fx + 0x68] = p[fx + 0x6B] = 64         # master EQ low, mid, high gain: 0 dB
    for i in range(4):
        b = 192 + i * 52
        p[b + 0x07] = 0                                     # FilterSw off
        p[b + 0x11] = 127                                   # Dry Level
        p[b + 0x12] = p[b + 0x13] = 0                       # VarSend, RevSend
        p[b + 0x14] = 0                                     # InsSw off
    return bytes(p)


def rewrite(data):
    """Every performance bulk in a MIDI file, stripped. Same length, so the track stays valid as is."""
    out, at, n = bytearray(data), 0, 0
    while True:
        at = out.find(HDR, at)
        if at < 0:
            return bytes(out), n
        body, cs = at + 3, at + 3 + 5 + PERF_LEN
        assert out[cs + 1] == 0xF7, f"bulk at {at} does not end in F7"
        assert sum(out[body:cs]) % 128 == (-out[cs]) % 128, f"bulk at {at} fails its checksum"
        out[at + 8:cs] = strip(out[at + 8:cs])
        out[cs] = (-sum(out[body:cs])) & 0x7F
        at, n = cs + 2, n + 1


def check():
    src = (SRC / "01_Vokodrone.mid").read_bytes()
    got, n = rewrite(src)
    assert n == 1 and len(got) == len(src)
    at = got.find(HDR) + 8
    perf = got[at:at + PERF_LEN]
    assert perf[80 + 0x58] == perf[80 + 0x5B] == perf[80 + 0x5F] == 0
    assert all(perf[192 + i * 52 + 0x07] == 0 for i in range(4))
    assert perf[0:12] == src[at:at + 12]                     # the name, and everything else, is untouched
    assert rewrite(got)[0] == got                            # stripping twice changes nothing more
    assert sum(a != b for a, b in zip(src, got)) <= 40        # only the bytes above moved


def main():
    check()
    DST.mkdir(exist_ok=True)
    for f in sorted(SRC.glob("*.mid")):
        data, n = rewrite(f.read_bytes())
        assert n, f"{f.name}: no performance bulk found"
        (DST / f.name).write_bytes(data)
        print(f"{f.name}: {n} performance{'s' if n > 1 else ''} stripped")


if __name__ == "__main__":
    sys.exit(main())
