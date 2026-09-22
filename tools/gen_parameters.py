#!/usr/bin/env python3
"""Generate plugin/generated/parameterDescriptions_fs1r.json from the Data List MIDI tables.

    python tools/gen_parameters.py

Every entry carries the sysex parameter address, so the plugin never needs to know anything about the
engine: it writes F0 43 1n 5E <address> vh vl F7 and reads the engine's echo back.

Tables 1 to 4 of docs/FS1R_DataList_text.txt are parsed directly. Three things the text extraction
cannot carry are supplied here instead: the runs the printed tables abbreviate with flow marks
(controller sets 2-7, formant and FM control destinations 2-4), the bit layouts of the bytes that pack
two fields ([-stttttt] and friends), and which addresses are 14-bit pairs.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "docs/FS1R_DataList_text.txt"
OUT = ROOT / "plugin/generated/parameterDescriptions_fs1r.json"
RANGE = r"[0-9A-F]{2}(?:-[0-9A-F]{2})?"
ROW = re.compile(r"^([0-9A-F]{2})\s+(" + RANGE + r"(?:[,/]\s*" + RANGE + r")*)\s+(.+?)\s*$")

PERF, PART, VOICE, OPV, OPU, SYS = "perf", "part", "voice", "opv", "opu", "sys"
SECTION_HEADER = {
    PERF:  "Performance Common Parameter (Byte Count: 80 bytes)",
    PART:  "Performance Part Parameter (Byte Count : 52 x 4 = 208 bytes )",
    VOICE: "Voice Common Parameter (Byte Count: 112 bytes)",
    OPV:   "Voice Voiced Parameter (Byte Count : 35 bytes / op )",
    OPU:   "Voice Unvoiced Parameter (Byte Count : 27 bytes / op )",
    SYS:   "<Table 4> System Parameter (Byte Count: 76 bytes )",
}
SECTION_LAST = {PERF: 0x4F, PART: 0x2F, VOICE: 0x6F, OPV: 0x22, OPU: 0x3D, SYS: 0x4B}

# Bytes that carry two or three fields. The plugin reads the current byte back from the engine and
# merges, the way a hardware editor does. (shift, width, name, bipolar offset, value list)
PACKED = {
    (PART, 0x24): [(0, 1, "Portamento Switch", 0, ["off", "on"]),
                   (1, 1, "Portamento Mode", 0, ["fingered", "fulltime"])],
    (VOICE, 0x6E): [(3, 3, "Filter EG Attack Time Velocity", 0, None),
                    (0, 3, "Filter EG Time Scaling", 0, None)],
    (OPV, 0x00): [(6, 1, "Oscillator Key Sync", 0, ["off", "on"]),
                  (0, 6, "Oscillator Transpose", 24, None)],
    (OPV, 0x04): [(3, 4, "Bandwidth Bias Sense", 7, None),
                  (0, 3, "Spectral Form", 0, ["sine", "all 1", "all 2", "odd 1", "odd 2", "res 1", "res 2", "frmt"])],
    (OPV, 0x05): [(6, 1, "Oscillator Mode", 0, ["ratio", "fixed"]),
                  (3, 3, "Spectral Skirt", 0, None),
                  (0, 3, "Fseq Track", 0, None)],
    (OPV, 0x1F): [(3, 4, "Frequency Bias Sense", 7, None),
                  (0, 3, "Pitch Mod Sense", 0, None)],
    (OPV, 0x20): [(4, 3, "Frequency Mod Sense", 0, None),
                  (0, 4, "Frequency Velocity Sense", 7, None)],
    (OPV, 0x21): [(4, 3, "Amplitude Mod Sense", 0, None),
                  (0, 4, "Amplitude Velocity Sense", 7, None)],
    (OPU, 0x24): [(5, 2, "Formant Pitch Mode", 0, ["ratio", "fundamental", "formant"]),
                  (0, 5, "Formant Pitch Coarse", 0, None)],
    (OPU, 0x29): [(3, 3, "Formant Resonance", 0, None),
                  (0, 3, "Formant Skirt", 0, None)],
    (OPU, 0x3B): [(4, 3, "Frequency Mod Sense", 0, None),
                  (0, 4, "Frequency Velocity Sense", 7, None)],
    (OPU, 0x3C): [(4, 3, "Amplitude Mod Sense", 0, None),
                  (0, 4, "Amplitude Velocity Sense", 7, None)],
}
CTRL_DEST = [(4, 2, "Destination", 0, ["off", "out", "frequency", "width"]),
             (3, 1, "Voiced/Unvoiced", 0, ["voiced", "unvoiced"]),
             (0, 3, "Operator", 0, None)]

# Runs the printed table abbreviates with flow marks: first address, count, stride, name template,
# min, max, bipolar offset, value list, packed layout.
RUNS = {
    PERF: [(0x28, 8, 1, "Controller {n} Part Switch", 0, 15, 0, None,
            [(i, 1, "Part %d" % (i + 1), 0, ["off", "on"]) for i in range(4)]),
           (0x40, 8, 1, "Controller {n} Destination", 0, 47, 0, None, None),
           (0x48, 8, 1, "Controller {n} Depth", 0, 127, 64, None, None)],
    VOICE: [(0x40, 5, 1, "Formant Control Destination {n}", 0, 0x37, 0, None, CTRL_DEST),
            (0x45, 5, 1, "Formant Control Depth {n}", 0, 127, 64, None, None),
            (0x4A, 5, 1, "FM Control Destination {n}", 0, 0x37, 0, None, CTRL_DEST),
            (0x4F, 5, 1, "FM Control Depth {n}", 0, 127, 64, None, None)],
}
# 14-bit pairs (high byte address, name, min, max)
WIDE = {
    PERF: [(0x18, "Fseq Speed Ratio", 0, 5000), (0x1A, "Fseq Start Step Offset", 0, 511),
           (0x1C, "Fseq Loop Start", 0, 511), (0x1E, "Fseq Loop End", 0, 511)],
}
WIDE_RUNS = {PERF: [(0x30, 8, 2, "Controller {n} Source Switch", 0, 16383)]}
SKIP = {(PERF, ll) for ll in range(0x0C)} | {(VOICE, ll) for ll in range(0x0A)}

EFFECT_TAIL = [
    (0x128, "Reverb Type", 0, 16, "revType"), (0x129, "Reverb Pan", 1, 127, "pan"),
    (0x12A, "Reverb Return", 0, 127, None), (0x12B, "Variation Type", 0, 28, "varType"),
    (0x12C, "Variation Pan", 1, 127, "pan"), (0x12D, "Variation Return", 0, 127, None),
    (0x12E, "Send Variation to Reverb", 0, 127, None), (0x12F, "Insertion Type", 0, 40, "insType"),
    (0x130, "Insertion Pan", 1, 127, "pan"), (0x131, "Send Insertion to Reverb", 0, 127, None),
    (0x132, "Send Insertion to Variation", 0, 127, None), (0x133, "Insertion Level", 0, 127, None),
    (0x134, "EQ Low Gain", 52, 76, "gain"), (0x135, "EQ Low Frequency", 4, 40, "freq"),
    (0x136, "EQ Low Q", 1, 120, "q"), (0x137, "EQ Low Shape", 0, 1, "shelfPeak"),
    (0x138, "EQ Mid Gain", 52, 76, "gain"), (0x139, "EQ Mid Frequency", 14, 54, "freq"),
    (0x13A, "EQ Mid Q", 1, 120, "q"), (0x13B, "EQ High Gain", 52, 76, "gain"),
    (0x13C, "EQ High Frequency", 28, 58, "freq"), (0x13D, "EQ High Q", 1, 120, "q"),
    (0x13E, "EQ High Shape", 0, 1, "shelfPeak"),
]
REVERB_TYPES = ["No Effect", "Hall1", "Hall2", "Room1", "Room2", "Room3", "Stage1", "Stage2", "Plate",
                "White Room", "Tunnel", "Basement", "Canyon", "Delay LCR", "Delay L,R", "Echo", "CrossDelay"]
VARIATION_TYPES = ["No Effect", "Chorus", "Celeste", "Flanger", "Symphonic", "Phaser1", "Phaser2", "Ens Detune",
                   "Rotary SP", "Tremolo", "Auto Pan", "Auto Wah", "Touch Wah", "3-Band EQ", "HM Enhncer",
                   "Noise Gate", "Compressor", "Distortion", "Overdrive", "Amp Sim", "Delay LCR", "Delay L,R",
                   "Echo", "CrossDelay", "Karaoke", "Hall", "Room", "Stage", "Plate"]
INSERTION_TYPES = ["Thru", "Chorus", "Celeste", "Flanger", "Symphonic", "Phaser1", "Phaser2", "Pitch Chng",
                   "Ens Detune", "Rotary SP", "2WayRotary", "Tremolo", "Auto Pan", "Ambience", "A-Wah+Dist",
                   "A-Wah+Odrv", "T-Wah+Dist", "T-Wah+Odrv", "Wah+DS+Dly", "Wah+OD+Dly", "Lo-Fi", "3-Band EQ",
                   "HM Enhncer", "Noise Gate", "Compressor", "Comp+Dist", "Cmp+DS+Dly", "Cmp+OD+Dly",
                   "Distortion", "Dist+Delay", "Overdrive", "Odrv+Delay", "Amp Sim", "Delay LCR", "Delay L,R",
                   "Echo", "CrossDelay", "ER 1", "ER 2", "Gate Rev", "Revrs Gate"]
ENUMS = {"revType": REVERB_TYPES, "varType": VARIATION_TYPES, "insType": INSERTION_TYPES,
         "shelfPeak": ["shelving", "peaking"]}
INLINE_ENUM = re.compile(r"(\d)\s*:\s*([A-Za-z0-9/+\-. ]+?)\s*(?=,\s*\d\s*:|$)")
TRAILING = re.compile(r"\s*(?:[-+]?\d+\s*~\s*[-+]?\d+.*|0~\d+.*|off/on|on/off|\d\s*:\s*.*|"
                      r"L63.*|C-2~G8|\[.*\]|\(.*\))\s*$")


def clean(t):
    return re.sub(r"\s+", " ", t.replace("ﬂ", "").replace("­", "")).strip()


def tidy(name):
    n = name
    for _ in range(3):
        n2 = TRAILING.sub("", n).strip(" -/")
        if n2 == n or not n2:
            break
        n = n2
    n = re.sub(r"^(COMMON|VOICED|UNVOICED)\s+", "", n)
    n = n.strip(" -")
    return n or name.strip()


def bipolar_offset(desc, lo, hi):
    for pat, off in ((r"-64\s*~\s*\+?\s*0?\s*~?\s*\+?63", 64), (r"-24\s*~\s*0?\s*~?\s*\+?24", 24),
                     (r"-7\s*~\s*\+?7", 7), (r"-12\s*~\s*\+?12", 12), (r"-63\s*~\s*\+?63", 63),
                     (r"-50\s*~\s*\+?50", 50), (r"-16\s*~\s*\+?100", 16), (r"-48\s*~\s*\+?24", 64)):
        if re.search(pat, desc):
            return off
    if "L63" in desc and "R63" in desc:
        return 64
    return 0


# A section ends at the next table header; without this the rows of the following table bleed in.
STOP = ("<Table", "Performance Effect Parameter", "Performance Common Parameter",
        "Performance Part Parameter", "Voice Common Parameter", "Voice Voiced Parameter",
        "Voice Unvoiced Parameter", "Header Parameter", "Frame Parameter")


def parse_section(lines, key):
    header, last = SECTION_HEADER[key], SECTION_LAST[key]
    i = next(k for k, l in enumerate(lines) if l.strip().startswith(header))
    rows, seen = [], set()
    for l in lines[i + 1: i + 400]:
        t = clean(l)
        if t.startswith(STOP):
            break
        if not t or t.startswith("====") or t.startswith("Datalist/"):
            continue
        m = ROW.match(t)
        if not m:
            continue
        ll = int(m.group(1), 16)
        if ll in seen or ll > last:
            continue
        seen.add(ll)
        desc = clean(m.group(3))
        if desc.lower().startswith(("reserved", "not used")) or desc.lower() in ("used",):
            continue
        rng = m.group(2)
        vals = [v.strip() for _, v in INLINE_ENUM.findall(desc)]
        lo, hi = 0, 127
        nums = re.findall(r"([0-9A-F]{2})-([0-9A-F]{2})", rng)
        if nums:
            lo = min(int(a, 16) for a, _ in nums)
            hi = max(int(b, 16) for _, b in nums)
        rows.append({"ll": ll, "min": lo, "max": hi, "desc": desc,
                     "values": vals if len(vals) >= 2 else None})
    return rows


def main():
    lines = DOC.read_text(encoding="utf-8", errors="replace").split("\n")
    parsed = {k: parse_section(lines, k) for k in SECTION_HEADER}
    params = []

    part_rel = [False]

    def emit(name, group, addr, lo, hi, default=None, off=0, values=None, bits=None, wide=False, unit="", note=""):
        e = {"name": name, "group": group,
             "address": [addr >> 16 & 0x7F, addr >> 8 & 0x7F, addr & 0x7F],
             "min": lo, "max": hi, "default": default if default is not None else (off if off else lo)}
        if off:
            e["bipolar"] = True
            e["displayOffset"] = -off
        if values:
            e["values"] = values
            e["discrete"] = True
        if bits:
            e["shift"], e["width"] = bits
        if wide:
            e["wide"] = True
        if unit:
            e["unit"] = unit
        if part_rel[0]:
            e["partRelative"] = True
        if note:
            e["description"] = note
        params.append(e)

    def add(key, group, addr_of, part_relative=False):
        part_rel[0] = part_relative
        run_addrs, wide_addrs = set(), set()
        for first, count, stride, tmpl, lo, hi, off, values, packed in RUNS.get(key, []):
            for n in range(count):
                a = first + n * stride
                run_addrs.add(a)
                nm = tmpl.format(n=n + 1)
                if packed:
                    for sh, w, sub, soff, sv in packed:
                        emit(f"{nm} {sub}", group, addr_of(a), 0, (1 << w) - 1, soff or None, soff, sv, (sh, w))
                else:
                    emit(nm, group, addr_of(a), lo, hi, None, off, values)
        for first, count, stride, tmpl, lo, hi in WIDE_RUNS.get(key, []):
            for n in range(count):
                a = first + n * stride
                wide_addrs.update({a, a + 1})
                emit(tmpl.format(n=n + 1), group, addr_of(a), lo, hi, None, 0, None, None, True)
        for first, nm, lo, hi in WIDE.get(key, []):
            wide_addrs.update({first, first + 1})
            emit(nm, group, addr_of(first), lo, hi, None, 0, None, None, True)
        for r in parsed[key]:
            ll = r["ll"]
            if (key, ll) in SKIP or ll in run_addrs or ll in wide_addrs:
                continue
            if (key, ll) in PACKED:
                nm = tidy(r["desc"]).split("/")[0].strip()
                for sh, w, sub, soff, sv in PACKED[(key, ll)]:
                    emit(sub, group, addr_of(ll), 0, (1 << w) - 1, soff or None, soff, sv, (sh, w), note=nm)
                continue
            off = bipolar_offset(r["desc"], r["min"], r["max"])
            emit(tidy(r["desc"]), group, addr_of(ll), r["min"], r["max"], None, off, r["values"])

    # The part, voice and operator blocks are emitted once and marked partRelative: the plugin adds the
    # selected part (0-3) to the address high byte, the way the front panel edits one part at a time.
    # Four copies would be four times the parameters for no extra reach.
    add(PERF, "Performance", lambda ll: 0x100000 | ll)
    add(PART, "Part", lambda ll: 0x300000 | ll, part_relative=True)
    add(VOICE, "Voice", lambda ll: 0x400000 | ll, part_relative=True)
    for op in range(8):
        add(OPV, f"Op {op + 1}", lambda ll, op=op: 0x600000 | op << 8 | ll, part_relative=True)
        add(OPU, f"Op {op + 1} Unvoiced", lambda ll, op=op: 0x600000 | op << 8 | ll, part_relative=True)
    add(SYS, "System", lambda ll: ll)

    for addr, name, lo, hi, kind in EFFECT_TAIL:
        off = 64 if kind in ("pan", "gain") else 0
        emit(name, "Effects", 0x100000 | addr, lo, hi, None, off, ENUMS.get(kind), None, False,
             {"freq": "Hz", "q": "Q", "gain": "dB"}.get(kind, ""))
    # The parameter words in front of the tail change meaning with the selected effect type, so they are
    # exposed positionally; the Effect Parameter List gives the per-type names.
    for label, base, count in [("Reverb", 0x100050, 8), ("Variation", 0x100068, 12),
                               ("Variation", 0x100100, 4), ("Insertion", 0x100108, 16)]:
        start = 1 if base in (0x100050, 0x100068, 0x100108) else 13
        for i in range(count):
            a = base + i * 2
            emit(f"{label} Param {start + i}", "Effects", a, 0, 16383, 0, 0, None, None, True, "",
                 "meaning depends on the selected effect type")
    for i in range(8):                       # the reverb block's eight single-byte parameters
        emit(f"Reverb Param {9 + i}", "Effects", 0x100060 + i, 0, 127, 0, 0, None, None, False, "",
             "meaning depends on the selected reverb type")

    OUT.parent.mkdir(exist_ok=True)
    doc = {
        "device": "Yamaha FS1R",
        "note": ("Generated by tools/gen_parameters.py from docs/FS1R_DataList_text.txt. address is the "
                 "sysex parameter address [high, mid, low]: the plugin sends F0 43 1n 5E <address> vh vl F7 "
                 "and reads the engine's echo back. shift/width mark a field packed inside a byte, so the "
                 "plugin merges it into the byte it last read. wide marks a 14-bit value spanning this "
                 "address and the next. Entries whose description mentions the effect type change meaning "
                 "with the selected type, see the Effect Parameter List. partRelative entries address the "
                 "part the plugin has selected: add the part index 0-3 to the address high byte."),
        "types": {"reverb": REVERB_TYPES, "variation": VARIATION_TYPES, "insertion": INSERTION_TYPES},
        "parameters": params,
    }
    OUT.write_text(json.dumps(doc, indent=1) + "\n")
    groups = {}
    for p in params:
        groups[p["group"]] = groups.get(p["group"], 0) + 1
    print(f"wrote {OUT} with {len(params)} parameters")
    for g in ("Performance", "Part", "Voice", "Op 1", "Op 1 Unvoiced", "Effects", "System"):
        print(f"  {g}: {groups.get(g, 0)}")


if __name__ == "__main__":
    main()
