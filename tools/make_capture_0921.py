#!/usr/bin/env python3
"""Two more capture request files, and the manifest rows the side files never got.

    python tools/make_capture_0921.py     -> captures/requests/13_unvoiced3.mid, 14_sens.mid
                                             and manifest.json rows for 05b, 08b, 12, 13, 14

**13_unvoiced3** reads the noise formant where it does not fold. Every unvoiced segment recorded so far
puts the band at 1 kHz, and from bandwidth 40 up the band's lower edge is at DC: the 0918 take and the
0919 note-36 take both stop widening at about 1750 Hz and both plateau in level at the same bandwidth,
which is what a band folding on itself looks like and not a law. So the width law, the clamp and the
level law all sit behind the fold. At an 8 kHz centre nothing folds until the band is 16 kHz wide. The
skirt sweep goes with it, because the unit's unvoiced skirt *widens* the band where the engine's cascade
of one-poles narrows it, and that too was only ever read at 1 kHz.

**14_sens** is the two envelope laws no file measures at more than one setting of their own depth.
Amplitude mod sensitivity is swept in `07_modulation_1` at LFO amplitude depth 99 alone, which is one
value of a variable, the same hole the formant window fell into; three depths separate a scaling of the
depth from a curve on the sensitivity. And the filter EG has four segments in the whole set and the
engine is 13 to 15 dB out on three of them, with the 0919 register retake showing nothing moves in the
coefficient region while the EG sweeps, so VOP3-1 runs that EG and only a recording can read it.

Both are additive. The frozen files and their takes do not move.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import make_capture_set as m

CENTRE = 8000.0          # high enough that a band 3 kHz wide still clears DC and Nyquist
CENTRE2 = 4000.0         # a second centre, so "width in Hz" is read at two of them


def g_unvoiced3():
    for b in range(0, 100, 4):
        yield m.Seg(f"u3bw-{b}", f"unvoiced formant at 8 kHz, bandwidth {b}, skirt 0, resonance 0: the "
                    "width law and where it stops, with no DC fold in front of it",
                    ["NOISE_BW"], m.noise(hz=CENTRE, u_bw=b), hold=3000, measure="spectrum")
    for s in range(8):
        yield m.Seg(f"u3skirt-bw20-{s}", f"unvoiced skirt {s} at 8 kHz, bandwidth 20",
                    ["noise skirt"], m.noise(hz=CENTRE, u_bw=20, u_skirt=s), hold=3000, measure="spectrum")
    for s in range(8):
        yield m.Seg(f"u3skirt-bw60-{s}", f"unvoiced skirt {s} at 8 kHz, bandwidth 60: whether the skirt "
                    "scales the width or adds to it",
                    ["noise skirt"], m.noise(hz=CENTRE, u_bw=60, u_skirt=s), hold=3000, measure="spectrum")
    for r in range(8):
        yield m.Seg(f"u3res-{r}", f"unvoiced resonance {r} at 8 kHz, bandwidth 20, skirt 0: where the "
                    "tone comes in, which on the 0918 take is somewhere between 4 and 5",
                    ["noise resonance"], m.noise(hz=CENTRE, u_bw=20, u_res=r), hold=3000, measure="spectrum")
    for b in (4, 20, 36, 52, 68, 84):
        yield m.Seg(f"u3bw4k-{b}", f"unvoiced formant at 4 kHz, bandwidth {b}: the same width at a "
                    "second centre", ["NOISE_BW"], m.noise(hz=CENTRE2, u_bw=b), hold=3000,
                    measure="spectrum")


def g_sens():
    def lfo(v, amd):
        v[0x10], v[0x11], v[0x12] = 0, 40, 0       # triangle, speed 40, no delay
        v[0x15], v[0x16], v[0x17] = 0, amd, 0
        return v
    for amd in (33, 66, 99):
        for k in range(8):
            yield m.Seg(f"ams{k}-amd{amd}", f"amplitude mod sensitivity {k} at LFO1 amplitude depth "
                        f"{amd}: the depth in dB against two variables, not one",
                        ["ams scaling"], lfo(m.sine(ams=k), amd), hold=4000, measure="envelope")

    def probe():
        p = fp.init_performance("FltEG Test  ")
        fp.set_part(p, 0, filter=1)
        for i in (1, 2, 3):
            fp.set_part(p, i)
        return p

    def fltv(depth, t1):
        c, f, _ = fp.fixed_bytes(32.703)                  # a dense comb, as 06_filter uses
        v = m.sine(form=fp.ALL2, level=99)
        v[0x54] = 0                                       # LPF24
        v[0x55] = 16                                      # resonance 0
        v[0x57] = 40                                      # cutoff low, so the EG has somewhere to go
        v[0x5D] = 12                                      # input gain 0 dB
        v[0x64] = depth
        v[0x65] = 0                                       # EG level 4
        v[0x66] = 100                                     # EG level 1
        v[0x67] = v[0x68] = 60
        v[0x69] = t1                                      # EG time 1
        v[0x6A] = v[0x6B] = v[0x6C] = 60
        return v
    for d in (0, 16, 32, 64, 96, 127):
        yield m.Seg(f"flteg2-d{d}-t20", f"filter EG depth byte {d} ({d - 64} displayed) at time 20: how "
                    "far the EG moves the corner", ["filter EG"], fltv(d, 20), note=24, perf=probe(),
                    hold=4000, measure="envelope")
    for t in (0, 40, 60, 80):
        yield m.Seg(f"flteg2-d127-t{t}", f"filter EG time {t} at full depth: the EG's own rate law",
                    ["filter EG"], fltv(127, t), note=24, perf=probe(), hold=4000, measure="envelope")


SIDE = [("make_capture_unvoiced2", "05b_unvoiced2.mid"),
        ("make_capture_perpan", "08b_panlevel_perfpn.mid"),
        ("make_capture_fseqlevel", "12_fseqlevel.mid")]

# what each side file is for, since make_capture_set.py's README table wants a line per file and these
# rows come from generators that never wrote one
WHY = {
    "05b_unvoiced2.mid": "the noise formant at a second fundamental, note 36",
    "08b_panlevel_perfpn.mid": "the performance pan, which 08_panlevel leaves at centre",
    "12_fseqlevel.mid": "what the voiced level register does between Fseq frames",
    "13_unvoiced3.mid": "the noise formant at an 8 kHz centre, where the band does not fold at DC",
    "14_sens.mid": "the filter EG over six depths, and AM sensitivity at three LFO depths",
}


def side_entries(outdir):
    """The manifest rows for the three side files already recorded. Each generator rebuilds its own .mid
    from the same code that wrote the one rgwan played, so regenerating into a scratch directory and
    comparing bytes is what says the row describes the take. A file that does not round-trip is skipped
    with a warning rather than quietly mis-timing somebody's recording."""
    import importlib
    scratch = Path("/tmp/fsvr_sidegen")
    (scratch / "captures" / "requests").mkdir(parents=True, exist_ok=True)
    real_build, caught = m.build_file, []

    def spy(path, segs, marker):
        e = real_build(path, segs, marker)
        caught.append(e)
        return e

    out = []
    for mod, name in SIDE:
        M = importlib.import_module(mod)
        caught.clear()
        keep_root, keep_build = M.ROOT, m.build_file
        M.ROOT, m.build_file = scratch, spy
        try:
            M.main()
        finally:
            M.ROOT, m.build_file = keep_root, keep_build
        if (scratch / "captures" / "requests" / name).read_bytes() != (outdir / name).read_bytes():
            print(f"  ! {name} does not regenerate byte for byte, skipping its manifest row")
            continue
        out.extend(caught)
    return out


def merge_manifest(outdir, entries):
    path = outdir / "manifest.json"
    man = json.loads(path.read_text())
    fresh = {e["file"]: e for e in entries}
    # appended rather than sorted into place: the analyzer keys on the file name and a minimal diff
    # is worth more than alphabetical order in an 8000 line JSON two machines both edit
    kept = [f for f in man["files"] if f["file"] not in fresh]
    man["files"] = kept + [fresh[k] for k in fresh]
    path.write_text(json.dumps(man, indent=1))
    return entries


def main():
    outdir = ROOT / "captures" / "requests"
    outdir.mkdir(parents=True, exist_ok=True)
    entries = []
    for name, gen in (("13_unvoiced3.mid", g_unvoiced3), ("14_sens.mid", g_sens)):
        segs = list(gen())
        e = m.build_file(outdir / name, segs, m.marker_voice())
        entries.append(e)
        print(f"{name:20} {len(segs):3} segments  {e['duration_s'] / 60:.1f} min")

    # the three side files already recorded, whose rows make_capture_set.py has never written
    entries += side_entries(outdir)
    for e in entries:
        e.setdefault("why", WHY.get(e["file"], "an additive follow-up file"))
    added = merge_manifest(outdir, entries)
    print(f"manifest: {len(added)} rows written ({', '.join(e['file'] for e in added) or 'none'})")


if __name__ == "__main__":
    main()
