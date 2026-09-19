#!/usr/bin/env python3
"""Build FS1R voices, performances, bulk dumps and Standard MIDI Files from Python.

Used by tools/make_capture_set.py to write the hardware capture requests, and by anything else that
needs a patch built from named parameters rather than from a preset.

The layouts are the sysex bulk layouts: voice 608 bytes, performance 400 bytes (80 common, 112 effect,
4 x 52 part). Offsets and their meanings come from the Data List's tables 1 and 2 and from the engine's
own decoder in src/fs1r_lib.cpp, so a patch built here is the same patch the engine reads back.

A note on EG times: 0 is the fastest and 99 the slowest, because the firmware turns a time into a chip
rate with (99 - T) * 0xA4 >> 8. rate_for_time() and time_for_rate() are that mapping and its inverse,
which is what a rate sweep needs.
"""
import struct

VOICE_LEN = 608
PERF_LEN = 400

# Spectral forms, voice operator byte 4 bits 0-2.
SINE, ALL1, ALL2, ODD1, ODD2, RES1, RES2, FRMT = range(8)
FORM_NAMES = ["sine", "all1", "all2", "odd1", "odd2", "res1", "res2", "frmt"]
# Unvoiced frequency modes, operator byte 0x24 bits 5-6.
UV_NORMAL, UV_LINK_FO, UV_LINK_FF = 0, 1, 2


# ---------------------------------------------------------------------------- sysex
def bulk(addr_h, addr_m, addr_l, data):
    """FS1R bulk dump: F0 43 0n 5E bc bc ah am al <data> cs F7, device 1."""
    n = len(data)
    body = [n >> 7 & 0x7F, n & 0x7F, addr_h, addr_m, addr_l] + [b & 0x7F for b in data]
    return bytes([0xF0, 0x43, 0x00, 0x5E] + body + [(-sum(body)) & 0x7F, 0xF7])


def voice_bulk(voice, part=0):
    """Into the part's voice edit buffer, which is what the part plays."""
    return bulk(0x40 + part, 0x00, 0x00, voice)


def perf_bulk(perf):
    """Into the current performance buffer. Reloads each part's voice from its bank and program, so a
    voice bulk meant for the same segment has to follow this, not precede it."""
    return bulk(0x10, 0x00, 0x00, perf)


def param(addr_h, addr_m, addr_l, value):
    """Parameter change: F0 43 1n 5E ah am al vh vl F7."""
    return bytes([0xF0, 0x43, 0x10, 0x5E, addr_h, addr_m, addr_l, value >> 7 & 0x7F, value & 0x7F, 0xF7])


# ---------------------------------------------------------------- EG time and rate
def rate_for_time(t):
    return ((99 - max(0, min(99, t))) * 0xA4) >> 8


def time_for_rate(r):
    """The lowest EG time that produces this chip rate, or None when no time reaches it."""
    for t in range(100):
        if rate_for_time(t) == r:
            return t
    return None


def load_tables():
    """The EPROM conversion tables as Python lists, straight out of src/fs1r_rom_tables.h."""
    import re
    from pathlib import Path
    txt = (Path(__file__).resolve().parents[1] / "src/fs1r_rom_tables.h").read_text()
    return {m.group(1): [int(x) for x in m.group(2).replace("\n", "").split(",") if x.strip()]
            for m in re.finditer(r"(\w+)\[\d+\] = \{(.*?)\};", txt, re.S)}


def algorithm(index):
    """One algorithm from src/fs1r_algorithms.h as eight dicts: where the operator takes its input and
    whether it reaches the mix. Lets a test patch assert its routing instead of assuming it."""
    import re
    from pathlib import Path
    txt = (Path(__file__).resolve().parents[1] / "src/fs1r_algorithms.h").read_text()
    rows = re.findall(r"\{((?:0x[0-9a-f]{2},?\s*){16})\}", txt)
    b = [int(x, 16) for x in rows[index].replace("\n", "").split(",") if x.strip()]
    inputs = {0: "none", 1: "none", 3: "feedback", 4: "chain", 5: "held", 6: "sum"}
    return [{"input": inputs.get((b[2 * o] >> 3) & 7, "?"), "writes_feedback": bool(b[2 * o] & 4),
             "carrier": bool(b[2 * o + 1] & 1)} for o in range(8)]


def fixed_hz(coarse, fine):
    """Fixed and formant frequency: the firmware's 8 * (coarse * 128 + fine) + 0x28ED on the
    1024-per-octave scale."""
    return 440.0 * 2.0 ** (coarse - 16 + fine / 128.0)


def fixed_bytes(hz):
    """Nearest coarse/fine pair for a frequency, and what it actually lands on."""
    x = __import__("math").log2(hz / 440.0) + 16.0
    c = int(x)
    f = int(round((x - c) * 128))
    if f > 127:
        c, f = c + 1, 0
    if f < 0:
        c, f = c - 1, f + 128
    c = max(0, min(31, c))
    return c, max(0, min(127, f)), fixed_hz(c, f)


# ---------------------------------------------------------------------------- voice
def init_voice(name="CapTest   "):
    """Every operator silent, sine, square envelope, no modulation, no filter, no LFO. Algorithm 1, in
    which all eight operators are carriers, so switching one on gives exactly that operator and nothing
    else. This is the base every capture patch starts from."""
    v = bytearray(VOICE_LEN)
    v[0:10] = name.encode("latin1")[:10].ljust(10)
    v[0x10] = 0          # LFO1 triangle
    v[0x11] = 0          # LFO1 speed 0
    v[0x12] = 0          # LFO1 delay 0
    v[0x13] = 1          # LFO1 key sync on: every note starts at the same phase
    v[0x15] = v[0x16] = v[0x17] = 0        # pitch, amplitude and frequency mod depths 0
    v[0x18] = 0          # LFO2 triangle
    v[0x19] = 0
    v[0x1C] = 0
    v[0x1D] = 1          # LFO2 key sync on
    v[0x1E] = 24         # note shift 0
    for k in (0x1F, 0x20, 0x21, 0x22, 0x3E):
        v[k] = 50        # pitch EG levels centred: flat
    v[0x27] = 0          # pitch EG velocity sensitivity 0
    v[0x2C] = 0          # algorithm 1
    v[0x3D] = 0          # feedback 0
    for k in range(0x45, 0x4A):
        v[k] = 64        # formant control depths 0
    for k in range(0x4F, 0x54):
        v[k] = 64        # FM control depths 0
    v[0x54] = 0          # filter LPF24 (the part's filter switch is off anyway)
    v[0x55] = 16         # filter resonance 0 (byte 16 of 0..116 is -16..+100)
    v[0x56] = 7          # resonance velocity sensitivity 0
    v[0x57] = 127        # cutoff wide open
    v[0x58] = 7          # filter EG depth velocity sensitivity 0
    v[0x5B] = 64         # cutoff key scaling 0
    v[0x5C] = 60
    v[0x5D] = 12         # filter input gain 0 dB
    v[0x64] = 64         # filter EG depth 0
    for k in (0x65, 0x66, 0x67, 0x68):
        v[k] = 100       # filter EG levels at maximum: flat
    for o in range(8):
        set_op(v, o)
    return v


def set_op(v, o, **kw):
    """One voiced operator. Defaults are the silent sine above; every keyword is the parameter as the
    front panel names it, not the packed byte."""
    p = 112 + o * 62
    g = kw.get
    v[p + 0] = (1 if g("keysync", 0) else 0) << 6 | (g("transpose", 0) + 24) & 0x3F
    v[p + 1] = g("coarse", 1)
    v[p + 2] = g("fine", 0)
    v[p + 3] = g("notescale", 0)
    v[p + 4] = (g("bwbias", 0) + 7) << 3 | g("form", SINE)
    v[p + 5] = (1 if g("fixed", 0) else 0) << 6 | g("skirt", 0) << 3 | g("fseqtrk", o)
    v[p + 6] = g("bw", 0)                       # bandwidth for frmt, resonance for res1/res2
    v[p + 7] = g("detune", 0) + 15
    v[p + 8] = g("feg_init", 0) + 50
    v[p + 9] = g("feg_attack", 0) + 50
    v[p + 10] = g("feg_attack_t", 0)
    v[p + 11] = g("feg_decay_t", 0)
    L = g("eg_l", (99, 99, 99, 0))
    T = g("eg_t", (0, 0, 0, 0))
    for i in range(4):
        v[p + 12 + i] = L[i]
        v[p + 16 + i] = T[i]
    v[p + 20] = g("hold", 0)
    v[p + 21] = g("tscale", 0)
    v[p + 22] = g("level", 0)
    v[p + 23] = g("bp", 39)
    v[p + 24] = g("ld", 0)
    v[p + 25] = g("rd", 0)
    v[p + 26] = g("lc", 0)
    v[p + 27] = g("rc", 0)
    v[p + 31] = (g("fbias", 0) + 7) << 3 | g("pms", 0)
    v[p + 32] = g("fms", 0) << 4 | (g("fvs", 0) + 7)
    v[p + 33] = g("ams", 0) << 4 | (g("avs", 0) + 7)
    v[p + 34] = g("egbias", 0) + 7
    set_uop(v, o, **{k[2:]: val for k, val in kw.items() if k.startswith("u_")})


def set_uop(v, o, **kw):
    """One unvoiced (noise formant) operator. Same idea, defaults silent."""
    q = 112 + o * 62 + 35
    g = kw.get
    v[q + 0] = (g("transpose", 0) + 24) & 0x3F
    v[q + 1] = g("mode", UV_NORMAL) << 5 | (g("coarse", 0) & 0x1F)
    v[q + 2] = g("fine", 0)
    v[q + 3] = g("notescale", 0)
    v[q + 4] = g("bw", 0)
    v[q + 5] = g("bwbias", 0) + 7
    v[q + 6] = g("res", 0) << 3 | g("skirt", 0)
    v[q + 7] = g("feg_init", 0) + 50
    v[q + 8] = g("feg_attack", 0) + 50
    v[q + 9] = g("feg_attack_t", 0)
    v[q + 10] = g("feg_decay_t", 0)
    v[q + 11] = g("level", 0)
    v[q + 12] = g("lks", 0) + 7
    L = g("eg_l", (99, 99, 99, 0))
    T = g("eg_t", (0, 0, 0, 0))
    for i in range(4):
        v[q + 13 + i] = L[i]
        v[q + 17 + i] = T[i]
    v[q + 21] = g("hold", 0)
    v[q + 22] = g("tscale", 0)
    v[q + 23] = g("fbias", 0) + 7
    v[q + 24] = g("fms", 0) << 4 | (g("fvs", 0) + 7)
    v[q + 25] = g("ams", 0) << 4 | (g("avs", 0) + 7)
    v[q + 26] = g("egbias", 0) + 7


# ---------------------------------------------------------------------- performance
def init_performance(name="Capture Test"):
    """Part 1 alone on MIDI channel 1, dry, no effects, no filter, no controller sets, no Fseq. Every
    offset that can bias a voice parameter sits at its centre, so what is heard is the voice."""
    p = bytearray(PERF_LEN)
    p[0:12] = name.encode("latin1")[:12].ljust(12)
    p[0x10] = 127        # performance volume
    p[0x11] = 64         # performance pan centre
    p[0x12] = 24         # performance note shift 0
    p[0x15] = 0          # Fseq part off
    p[0x18], p[0x19] = 7, 104      # Fseq speed ratio 1000 = 100 %
    p[0x21] = 2          # Fseq play mode "fseq" (inactive while the part is off)
    p[0x27] = 64         # Fseq level velocity sensitivity 0
    for k in range(0x48, 0x50):
        p[k] = 64        # controller depths 0 (the source and part switches above are all 0)
    fx = 80
    p[fx + 0x58] = 0     # reverb type: No Effect
    p[fx + 0x5B] = 0     # variation type: No Effect
    p[fx + 0x5F] = 0     # insertion type: No Effect
    p[fx + 0x59] = 64    # reverb pan centre
    p[fx + 0x5C] = 64    # variation pan centre
    p[fx + 0x60] = 64    # insertion pan centre
    p[fx + 0x63] = 127   # insertion level
    for gain, freq, q in ((0x64, 0x65, 0x66), (0x68, 0x69, 0x6A), (0x6B, 0x6C, 0x6D)):
        p[fx + gain] = 64            # 0 dB, which is flat whatever the frequency and Q
        p[fx + freq] = 0x20
        p[fx + q] = 10
    for i in range(4):
        set_part(p, i)
    return p


def set_part(p, i, **kw):
    b = 192 + i * 52
    g = kw.get
    on = g("on", i == 0)
    p[b + 0x00] = 8                              # note reserve
    p[b + 0x01] = 1 if on else 0                 # bank: Int, or off
    p[b + 0x02] = 0                              # program
    p[b + 0x03] = 0x7F                           # receive channel max: off
    p[b + 0x04] = g("channel", 0) if on else 0x7F
    p[b + 0x05] = g("poly", 1)
    p[b + 0x06] = g("priority", 0)
    p[b + 0x07] = g("filter", 0)
    p[b + 0x08] = g("noteshift", 0) + 24
    p[b + 0x09] = g("detune", 0) + 64
    p[b + 0x0A] = g("balance", 64)
    p[b + 0x0B] = g("volume", 127)
    p[b + 0x0C] = g("velodepth", 64)
    p[b + 0x0D] = g("velooffset", 64)
    p[b + 0x0E] = g("pan", 64)
    p[b + 0x0F] = 0
    p[b + 0x10] = 127
    p[b + 0x11] = g("dry", 127)
    p[b + 0x12] = g("varsend", 0)
    p[b + 0x13] = g("revsend", 0)
    p[b + 0x14] = g("inssw", 0)
    for k in range(0x15, 0x24):
        p[b + k] = 64                            # LFO, filter, EG and pitch EG offsets: no offset
    p[b + 0x18] = g("cutoff", 64)
    p[b + 0x19] = g("resonance", 64)
    p[b + 0x1F] = g("filtegdepth", 64)
    p[b + 0x24] = g("porta", 0)
    p[b + 0x25] = g("portatime", 0)
    p[b + 0x26] = 0x40 + 2
    p[b + 0x27] = 0x40 - 2
    # 0 is the extreme, not the centre: the Data List gives PAN SCALING as 0..100 and the chip pans a full
    # 64 index steps per 48 semitones at 0, so every file built on this default pans hard by key. Left alone
    # because the recordings that exist were made with it; pass panscale=50 for a centred image.
    p[b + 0x28] = g("panscale", 0)
    p[b + 0x29] = g("panlfo", 0)
    p[b + 0x2A] = 1
    p[b + 0x2B] = 127
    p[b + 0x2C] = 0
    p[b + 0x2D] = 1
    p[b + 0x2E] = 64
    p[b + 0x2F] = 64


# ------------------------------------------------------------------- Standard MIDI File
def _varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


class Smf:
    """Type 0 file at one tick per millisecond, so an event time in the manifest is a time in the WAV."""

    def __init__(self):
        self.events = []          # (millisecond, bytes)

    def at(self, ms, data):
        self.events.append((int(round(ms)), bytes(data)))

    def note(self, ms, note, vel, dur_ms, channel=0):
        self.at(ms, [0x90 | channel, note, vel])
        self.at(ms + dur_ms, [0x80 | channel, note, 0])

    def cc(self, ms, num, val, channel=0):
        self.at(ms, [0xB0 | channel, num, val])

    def length_ms(self):
        return max((t for t, _ in self.events), default=0)

    def write(self, path, tail_ms=2000):
        track = bytearray()
        track += b"\x00\xff\x51\x03" + struct.pack(">I", 1000000)[1:]     # 1 quarter note = 1 s
        last = 0
        for t, data in sorted(self.events, key=lambda e: e[0]):
            track += _varlen(t - last)
            last = t
            if data[0] == 0xF0:
                track += b"\xf0" + _varlen(len(data) - 1) + data[1:]
            else:
                track += data
        track += _varlen(tail_ms) + b"\xff\x2f\x00"
        with open(path, "wb") as f:
            f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 1000))
            f.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))
        return last + tail_ms


def demo():
    """Self check: the builders produce the documented sizes, the checksums verify, and the packed
    fields read back as what went in."""
    v = init_voice()
    assert len(v) == VOICE_LEN
    set_op(v, 3, form=FRMT, level=99, bw=40, skirt=2, coarse=17, fine=24, fixed=1, detune=-4, pms=5)
    p = 112 + 3 * 62
    assert v[p + 4] & 7 == FRMT and v[p + 5] >> 3 & 7 == 2 and v[p + 5] >> 6 == 1
    assert v[p + 6] == 40 and v[p + 7] == 11 and v[p + 22] == 99 and v[p + 31] & 7 == 5
    b = voice_bulk(v)
    assert b[:4] == b"\xf0\x43\x00\x5e" and b[-1] == 0xF7
    assert sum(b[4:-2]) % 128 == (-b[-2]) % 128
    perf = init_performance()
    assert len(perf) == PERF_LEN
    assert perf[192 + 0x04] == 0 and perf[192 + 52 + 0x04] == 0x7F        # part 1 on, part 2 off
    assert perf[80 + 0x58] == 0 and perf[80 + 0x5B] == 0 and perf[80 + 0x5F] == 0
    assert len(perf_bulk(perf)) == PERF_LEN + 11      # F0 43 0n 5E bc bc ah am al ... cs F7
    assert rate_for_time(0) == 63 and rate_for_time(99) == 0
    assert rate_for_time(time_for_rate(37)) == 37
    c, f, hz = fixed_bytes(1000.0)
    assert abs(hz - 1000.0) < 6.0, hz
    assert abs(fixed_hz(16, 0) - 440.0) < 1e-9
    s = Smf()
    s.note(0, 60, 100, 500)
    s.at(100, bulk(0x40, 0, 0, v))
    assert s.length_ms() == 500
    print("fs1r_patch: ok")


if __name__ == "__main__":
    demo()
