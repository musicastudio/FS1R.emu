#!/usr/bin/env python3
"""Turn a recording of a capture request into measurements, and compare it with our engine.

    python tools/analyze_capture.py captures/hardware/01_reference_1.wav
    python tools/analyze_capture.py captures/hardware/*.wav --compare
    python tools/analyze_capture.py --check captures/engine/02_envelope_1.wav

Reads captures/requests/manifest.json for the segment times, aligns the recording with the marker bursts
at each end of the file, measures each segment the way its "measure" field asks, and writes
captures/analysis/<name>.json. With --compare it measures our engine's render of the same request file
and prints the differences, which is the calibration loop: the numbers that disagree are the numbers in
namespace cal that are wrong.

--check is the same thing pointed at an engine render, which is how the analyzer itself gets tested
without any hardware.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
REQUESTS = ROOT / "captures/requests"
ANALYSIS = ROOT / "captures/analysis"
ENGINE = ROOT / "captures/engine"


# ---------------------------------------------------------------------------- wav
def read_wav(path):
    """16, 24 and 32-bit integer and 32-bit float WAV, any channel count, as float64 [n, ch]."""
    d = Path(path).read_bytes()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise SystemExit(f"{path} is not a WAV file")
    fmt = None
    i = 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        body = d[i + 8:i + 8 + n]
        if cid == b"fmt ":
            tag, ch, sr, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            fmt = (tag, ch, sr, bits)
        elif cid == b"data" and fmt:
            tag, ch, sr, bits = fmt
            if tag == 3 and bits == 32:
                x = np.frombuffer(body, dtype="<f4").astype(np.float64)
            elif bits == 16:
                x = np.frombuffer(body, dtype="<i2").astype(np.float64) / 32768.0
            elif bits == 24:
                raw = np.frombuffer(body[:len(body) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
                v = raw[:, 0] | raw[:, 1] << 8 | raw[:, 2] << 16
                x = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
            elif bits == 32:
                x = np.frombuffer(body, dtype="<i4").astype(np.float64) / 2147483648.0
            else:
                raise SystemExit(f"{path}: {bits}-bit samples are not supported")
            return sr, x.reshape(-1, ch)
        i += 8 + n + (n & 1)
    raise SystemExit(f"{path}: no data chunk")


def db(x):
    return -200.0 if x <= 1e-10 else 20.0 * np.log10(x)


def rms_db(seg):
    return db(float(np.sqrt(np.mean(seg ** 2)))) if len(seg) else -200.0


# ---------------------------------------------------------------------------- alignment
HOP_MS = 5


def tone_envelope(mono, sr, hz):
    """How much energy sits at one frequency, per 5 ms hop. The markers are found in this rather than in
    the broadband level, so neither a loud segment nor a long reverb tail can hide them."""
    h = max(1, int(sr * HOP_MS / 1000))
    n = len(mono) // h
    frames = mono[:n * h].reshape(n, h)
    k = np.exp(-2j * np.pi * hz * np.arange(h) / sr)
    return np.abs(frames @ k) / h


def align(x, sr, entry, marker_hz):
    """Map manifest time to recording time by matched-filtering the three-burst marker at each end.
    Returns (scale, offset)."""
    mono = np.mean(x, axis=1) if x.ndim > 1 else x        # a hard-panned segment must not hide a burst
    e = tone_envelope(mono, sr, marker_hz)
    if not len(e) or np.max(e) <= 0:
        raise SystemExit("the recording is silent")
    want_start, want_end = entry["marker_start"], entry["marker_end"]
    burst = int(round(120 / HOP_MS))
    gap = int(round((want_start[1] - want_start[0]) * 1000 / HOP_MS))
    tmpl = np.zeros(2 * gap + burst)
    for k in range(3):
        tmpl[k * gap: k * gap + burst] = 1.0
    # Normalized cross-correlation, so the score is the shape of the match and not its loudness. A
    # segment louder than the marker, or an effect that runs away, scores on its shape like anything else.
    L = len(tmpl)
    t0 = tmpl - tmpl.mean()
    t0 /= np.linalg.norm(t0)
    if len(e) < L + 10:
        raise SystemExit("the recording is shorter than one marker")
    dot = np.correlate(e, t0, mode="valid")
    cs, cs2 = np.cumsum(np.insert(e, 0, 0)), np.cumsum(np.insert(e * e, 0, 0))
    s, s2 = cs[L:] - cs[:-L], cs2[L:] - cs2[:-L]
    var = s2 - s * s / L
    # A window with no variation at all (a runaway effect pinned against the limiter) would otherwise
    # divide a near-zero dot product by a near-zero norm and score arbitrarily high.
    floor = (0.02 * float(np.std(e))) ** 2 * L
    score = np.where(var > floor, dot / np.sqrt(np.maximum(var, 1e-30)), 0.0)
    half = len(score) // 2
    first = int(np.argmax(score[:half]))
    last = half + int(np.argmax(score[half:]))
    if score[first] < 0.5 or score[last] < 0.5:
        raise SystemExit(f"could not find the three-burst markers (best match {max(score[first], score[last]):.2f} "
                         "of 1.0): is this a recording of the whole file, at the right frequency?")
    first, last = first * HOP_MS / 1000.0, last * HOP_MS / 1000.0
    scale = (last - first) / (want_end[0] - want_start[0])
    if abs(scale - 1.0) > 0.01:
        raise SystemExit(f"the markers are {scale:.4f}x apart: this recording does not match the request")
    if abs(scale - 1.0) > 2e-4:
        print(f"  the recording runs {(scale - 1) * 1e6:.0f} ppm off the request; clocks differ, corrected")
    return scale, first - scale * want_start[0]


# ---------------------------------------------------------------------------- measurement
def spectrum(seg, sr, n=16384):
    if len(seg) < n:
        seg = np.pad(seg, (0, n - len(seg)))
    S = np.abs(np.fft.rfft(seg[:n] * np.hanning(n))) / n
    return np.fft.rfftfreq(n, 1 / sr), S


def peaks(fr, S, count=10, floor_db=-60.0):
    m = np.max(S)
    if m <= 0:
        return []
    idx = [i for i in range(2, len(S) - 2) if S[i] > S[i - 1] and S[i] > S[i + 1]]
    idx.sort(key=lambda i: -S[i])
    out = []
    for i in idx[:count]:
        d = db(S[i] / m)
        if d < floor_db:
            break
        # parabolic interpolation, so a peak is located to a fraction of a bin
        y0, y1, y2 = S[i - 1], S[i], S[i + 1]
        off = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) else 0.0
        out.append([round(float(fr[i] + off * (fr[1] - fr[0])), 3), round(float(db(S[i])), 2)])
    return out


def bands(fr, S, lo=20.0, hi=20000.0, per_octave=6):
    """A coarse spectral envelope: one number per sixth of an octave, in dB. This is what a formant, a
    noise band or a filter response is read off."""
    n = int(np.log2(hi / lo) * per_octave)
    edges = lo * 2 ** (np.arange(n + 1) / per_octave)
    out = []
    for a, b in zip(edges[:-1], edges[1:]):
        m = (fr >= a) & (fr < b)
        out.append(round(float(db(np.sqrt(np.mean(S[m] ** 2)))) if m.any() else -200.0, 2))
    return {"lo_hz": lo, "per_octave": per_octave, "db": out}


def envelope(seg, sr, step_ms=10, analytic=False):
    """RMS per step, or, for a decay faster than a few carrier periods, the magnitude of the analytic
    signal, which tracks the envelope without waiting for a whole cycle."""
    h = max(1, int(sr * step_ms / 1000))
    n = len(seg) // h
    if n == 0:
        return []
    if analytic and len(seg) > 16:
        m = len(seg)
        H = np.zeros(m)
        H[0] = 1
        if m % 2 == 0:
            H[m // 2] = 1
            H[1:m // 2] = 2
        else:
            H[1:(m + 1) // 2] = 2
        a = np.abs(np.fft.ifft(np.fft.fft(seg) * H))
        e = np.max(a[:n * h].reshape(n, h), axis=1) / np.sqrt(2.0)      # as an equivalent RMS
    else:
        e = np.sqrt(np.mean(seg[:n * h].reshape(n, h) ** 2, axis=1) + 1e-20)
    return [round(float(db(v)), 2) for v in e]


def measure_segment(x, sr, seg, scale, offset):
    def cut(t0, t1):
        a = int((scale * t0 + offset) * sr)
        b = int((scale * t1 + offset) * sr)
        a, b = max(0, a), min(len(x), b)
        return x[a:b]

    kind = seg["measure"]
    # A decaying segment has no steady state, so its level is read just after the attack instead.
    body = cut(seg["t_on"] + 0.05, seg["t_on"] + 0.15) if kind in ("envelope", "impulse") else cut(*seg["steady"])
    whole = cut(seg["t_on"] - 0.05, seg["t_end"])
    L = body[:, 0] if body.size else np.zeros(1)
    R = body[:, 1] if body.ndim > 1 and body.shape[1] > 1 and body.size else L
    out = {"id": seg["id"], "measure": kind,
           "rms_db": round(rms_db(L), 3),
           "peak": round(float(np.max(np.abs(body))) if body.size else 0.0, 6)}
    if kind == "stereo":
        out["rms_db_l"], out["rms_db_r"] = round(rms_db(L), 3), round(rms_db(R), 3)
    if kind in ("spectrum", "stereo"):
        fr, S = spectrum(L, sr)
        out["peaks"] = peaks(fr, S)
        out["bands"] = bands(fr, S)
    if kind in ("envelope", "impulse"):
        w = whole[:, 0] if whole.size else np.zeros(1)
        e = envelope(w, sr)
        out["envelope_db"] = e
        out["envelope_step_ms"] = 10
        out["t_on_index"] = 5                                     # the envelope starts 50 ms before note on
        out["off_index"] = int(round((seg["t_off"] - seg["t_on"] + 0.05) * 100))
        out["peak_db"] = round(max(e), 2) if e else -200.0
        # The fastest EG rates cross 96 dB in a tenth of a second, which a 10 ms step cannot resolve.
        fast = cut(seg["t_on"] - 0.02, seg["t_on"] + 0.4)
        out["envelope_fast_db"] = envelope(fast[:, 0] if fast.size else np.zeros(1), sr, step_ms=1,
                                           analytic=True)
        out["envelope_fast_step_ms"] = 1
    return out


# ---------------------------------------------------------------------------- fits
def fit_level_law(segs, tables):
    """dB per step of the 8-bit level register, from the level ladder in file 01."""
    pts = []
    for s in segs:
        if s["id"].startswith("level-"):
            L = int(s["id"].split("-")[1])
            pts.append((2 * tables["LEVTAB"][L], s["rms_db"]))
    pts = [(reg, d) for reg, d in pts if d > -70]                     # above the noise floor
    if len(pts) < 6:
        return None
    reg = np.array([p[0] for p in pts]);  y = np.array([p[1] for p in pts])
    a, b = np.polyfit(reg, y, 1)
    resid = float(np.max(np.abs(y - (a * reg + b))))
    return {"LEVEL_DB": round(float(-a), 4), "points": len(pts), "max_residual_db": round(resid, 2)}


def fit_eg_rates(segs):
    """Seconds for a 96 dB fall at each chip rate, from the slope of each decay."""
    out = {}
    for s in segs:
        if not s["id"].startswith("decay-rate") or "envelope_db" not in s:
            continue
        r = int(s["id"][len("decay-rate"):])
        # A fast decay is read off the 1 ms envelope, a slow one off the 10 ms envelope up to note off.
        fast = np.array(s.get("envelope_fast_db", []))
        step = 0.01
        e = np.array(s["envelope_db"][:s.get("off_index", len(s["envelope_db"]))])
        if len(fast) > 20 and max(fast) - fast[-1] > 30:
            e, step = fast, 0.001
        top = int(np.argmax(e)) if len(e) else 0
        e = e[top:]
        if len(e) < 10:
            continue
        use = np.flatnonzero((e < e[0] - 6) & (e > e[0] - 60))
        if len(use) < 5:
            continue
        t = use * step
        a, _ = np.polyfit(t, e[use], 1)
        if a >= -0.01:
            continue
        out[r] = round(float(96.0 / -a), 4)
    return out or None


def fit_detune(segs):
    """Cents per detune step, from the measured frequency of each."""
    pts = []
    for s in segs:
        if s["id"].startswith("detune-") and s.get("peaks"):
            pts.append((int(s["id"].split("-", 1)[1]), s["peaks"][0][0]))
    if len(pts) < 4:
        return None
    ref = [f for d, f in pts if d == 0]
    if not ref:
        return None
    x = np.array([d for d, _ in pts], dtype=float)
    y = np.array([1200 * np.log2(f / ref[0]) for _, f in pts])
    a, b = np.polyfit(x, y, 1)
    return {"DETUNE_CENTS": round(float(a), 4), "points": len(pts),
            "max_residual_cents": round(float(np.max(np.abs(y - (a * x + b)))), 2)}


def fits(result, tables):
    segs = result["segments"]
    out = {}
    for name, fn in (("level", lambda: fit_level_law(segs, tables)), ("eg_rates", lambda: fit_eg_rates(segs)),
                     ("detune", lambda: fit_detune(segs))):
        v = fn()
        if v:
            out[name] = v
    return out


# ---------------------------------------------------------------------------- driver
def analyze(wav, manifest, tables, label):
    name = Path(wav).stem
    entry = next((f for f in manifest["files"] if Path(f["file"]).stem == name), None)
    if entry is None:
        raise SystemExit(f"{name} is not in the manifest: is the file named after its request?")
    sr, x = read_wav(wav)
    print(f"{label} {name}: {sr} Hz, {x.shape[1]} ch, {len(x) / sr:.1f} s "
          f"(request is {entry['duration_s']:.1f} s)")
    scale, offset = align(x, sr, entry, manifest.get("marker_hz", 1002.3))
    res = {"file": entry["file"], "source": str(wav), "sample_rate": sr, "channels": int(x.shape[1]),
           "align_scale": round(scale, 9), "align_offset_s": round(offset, 4),
           "segments": [measure_segment(x, sr, s, scale, offset) for s in entry["segments"]]}
    silent = [s["id"] for s in res["segments"]
              if s.get("peak_db", s["rms_db"]) < -90 and s["id"] != "silence"]
    if silent:
        print(f"  {len(silent)} segments are silent: {', '.join(silent[:6])}"
              f"{' ...' if len(silent) > 6 else ''}")
    return res


def compare(hw, eng):
    """Where the engine and the hardware disagree, worst first."""
    byid = {s["id"]: s for s in eng["segments"]}
    rows = []
    for s in hw["segments"]:
        e = byid.get(s["id"])
        if not e:
            continue
        d = s["rms_db"] - e["rms_db"]
        f = None
        if s.get("peaks") and e.get("peaks"):
            f = 1200 * np.log2(s["peaks"][0][0] / e["peaks"][0][0]) if e["peaks"][0][0] > 0 else None
        rows.append((abs(d), s["id"], d, f))
    rows.sort(reverse=True)
    print("  level differences, hardware minus engine (worst 12):")
    for _, sid, d, f in rows[:12]:
        print(f"    {sid:22s} {d:+7.2f} dB" + (f"   {f:+7.1f} cents" if f is not None else ""))
    rest = [r[2] for r in rows]
    if rest:
        print(f"    median |difference| {np.median([abs(v) for v in rest]):.2f} dB over {len(rest)} segments")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wavs", nargs="+")
    ap.add_argument("--compare", action="store_true", help="also measure our engine's render and diff it")
    ap.add_argument("--check", action="store_true", help="the input is an engine render, not hardware")
    args = ap.parse_args()
    manifest = json.loads((REQUESTS / "manifest.json").read_text())
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import fs1r_patch
    tables = fs1r_patch.load_tables()
    ANALYSIS.mkdir(parents=True, exist_ok=True)
    every = []                       # a sweep can straddle a file split, so the fits run over the union
    for w in args.wavs:
        res = analyze(w, manifest, tables, "engine" if args.check else "hardware")
        every += res["segments"]
        out = ANALYSIS / (Path(w).stem + ("_engine" if args.check else "") + ".json")
        out.write_text(json.dumps(res, indent=1))
        if args.compare:
            ew = ENGINE / (Path(w).stem + ".wav")
            if not ew.exists():
                print(f"  no engine render at {ew}: run tools/make_capture_set.py --render")
                continue
            eng = analyze(ew, manifest, tables, "engine")
            (ANALYSIS / (Path(w).stem + "_engine.json")).write_text(json.dumps(eng, indent=1))
            compare(res, eng)
        print(f"  wrote {out}")
    f = fits({"segments": every}, tables)
    if f:
        print("\nmeasured constants:")
        for k, v in f.items():
            if k == "eg_rates":
                lo, hi = min(v), max(v)
                print(f"  rate_secs: {len(v)} rates measured, rate {hi} takes {v[hi]:.4f} s per 96 dB, "
                      f"rate {lo} takes {v[lo]:.2f} s")
            else:
                print(f"  {k}: {v}")
        name = "fits_engine.json" if args.check else "fits.json"
        (ANALYSIS / name).write_text(json.dumps(f, indent=1))
        print(f"  wrote {ANALYSIS / name}")


if __name__ == "__main__":
    main()
