#!/usr/bin/env python
"""fs1r_uart_probe.py — host-side driver for a live FS1R debug UART.

Two transports, same API:

  --via sci1   : direct serial on the test-mode SCI1 pads (rgwan wiring)
  --via sysex  : future path, if a peek/poke sysex is ever patched into the
                 firmware (see captures/debug_interface_research.md §4)

Usage:
  python fs1r_uart_probe.py --via sci1 --port COM5 peek 0xC000F8 16
  python fs1r_uart_probe.py --via sci1 --port COM5 poke 0xC000F8 0x12
  python fs1r_uart_probe.py --via sci1 --port COM5 exec 0x01040000 my_stub.bin

Test-mode framing (from FUN_003c01e8 / FUN_003c07a6 in v1.20, SH-2 big-endian
SCI1): 8N1, baud 31250 (same as MIDI), one-byte commands followed by a
big-endian payload. Command set below is what the debug stub implements;
the probe refuses to talk until the stub answers PING with 0xA5.
"""
from __future__ import annotations
import argparse, struct, sys, time
from pathlib import Path

BAUD_TESTMODE = 31250            # same clock as MIDI, from BRR=27 @28MHz
CMD_PING  = 0x00
CMD_PEEK  = 0x01                 # <addr:be32> <len:be16> -> len bytes
CMD_POKE  = 0x02                 # <addr:be32> <len:be16> <bytes>     -> 0x06 ack
CMD_EXEC  = 0x03                 # <addr:be32> <len:be16> <bytes>     -> 0x06 ack
ACK       = 0x06
PING_REPLY= 0xA5


def _open_serial(port: str, timeout: float = 1.0):
    import serial  # pyserial, required lazily
    s = serial.Serial(port, BAUD_TESTMODE, bytesize=8, parity='N',
                      stopbits=1, timeout=timeout, write_timeout=timeout)
    s.reset_input_buffer()
    s.reset_output_buffer()
    return s


class StubError(RuntimeError):
    pass


class FS1RDebug:
    """Thin client over the SCI1 test-mode stub."""
    def __init__(self, port: str):
        self.ser = _open_serial(port)
        self._ping()

    def _ping(self):
        self.ser.write(bytes([CMD_PING]))
        r = self.ser.read(1)
        if r != bytes([PING_REPLY]):
            raise StubError(f"no stub on {self.ser.port} (got {r!r}); "
                            "check panel test-mode entry and wiring")

    def _read_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.ser.read(n - len(buf))
            if not chunk:
                raise StubError(f"timeout after {len(buf)}/{n} bytes")
            buf += chunk
        return bytes(buf)

    def peek(self, addr: int, length: int) -> bytes:
        if length <= 0 or length > 0xFFFF:
            raise ValueError("length")
        self.ser.write(bytes([CMD_PEEK]) +
                       struct.pack('>I', addr) + struct.pack('>H', length))
        return self._read_exact(length)

    def poke(self, addr: int, data: bytes):
        self.ser.write(bytes([CMD_POKE]) +
                       struct.pack('>I', addr) +
                       struct.pack('>H', len(data)) + data)
        if self._read_exact(1) != bytes([ACK]):
            raise StubError("poke not ACKed")

    def exec(self, addr: int, payload: bytes):
        self.ser.write(bytes([CMD_EXEC]) +
                       struct.pack('>I', addr) +
                       struct.pack('>H', len(payload)) + payload)
        if self._read_exact(1) != bytes([ACK]):
            raise StubError("exec not ACKed")

    # --- high-level helpers -------------------------------------------------
    def peek32(self, addr: int) -> int:
        return struct.unpack('>I', self.peek(addr, 4))[0]

    def poke32(self, addr: int, value: int):
        return self.poke(addr, struct.pack('>I', value))

    def dump_region(self, addr: int, length: int, out: Path,
                    chunk: int = 0x400):
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open('wb') as f:
            done = 0
            while done < length:
                n = min(chunk, length - done)
                f.write(self.peek(addr + done, n))
                done += n
        print(f"wrote {out} ({done} bytes)")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--via', choices=['sci1', 'sysex'], required=True)
    p.add_argument('--port', help='serial port for --via sci1 (e.g. COM5)')
    sub = p.add_subparsers(dest='cmd', required=True)
    q = sub.add_parser('peek');  q.add_argument('addr', lambda x: int(x, 0))
    q.add_argument('length', type=int); q.add_argument('--out', type=Path)
    w = sub.add_parser('poke');  w.add_argument('addr', lambda x: int(x, 0))
    w.add_argument('value', lambda x: int(x, 0))
    e = sub.add_parser('exec');  e.add_argument('addr', lambda x: int(x, 0))
    e.add_argument('payload', type=Path)
    d = sub.add_parser('dump');  d.add_argument('addr', lambda x: int(x, 0))
    d.add_argument('length', lambda x: int(x, 0)); d.add_argument('out', type=Path)
    args = p.parse_args(argv)

    if args.via != 'sci1':
        sys.exit("sysex transport is a placeholder until the patched firmware "
                 "exists; see captures/debug_interface_research.md")
    if not args.port:
        p.error("--port is required with --via sci1")

    dbg = FS1RDebug(args.port)
    if args.cmd == 'peek':
        data = dbg.peek(args.addr, args.length)
        if args.out:
            args.out.write_bytes(data)
            print(f"wrote {args.out} ({len(data)} bytes)")
        else:
            print(data.hex())
    elif args.cmd == 'poke':
        dbg.poke32(args.addr, args.value); print("ok")
    elif args.cmd == 'exec':
        dbg.exec(args.addr, args.payload.read_bytes()); print("ok")
    elif args.cmd == 'dump':
        dbg.dump_region(args.addr, args.length, args.out)


if __name__ == '__main__':
    main()
