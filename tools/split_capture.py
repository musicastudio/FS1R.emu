#!/usr/bin/env python3
"""Cut one continuous recording of the whole capture set into one WAV per request file.

    python tools/split_capture.py captures/FS1R_capture_request_0918.flac

The request asks for one recording per MIDI file, but playing all 21 back to back into one recorder is
less work at the hardware end, so a single long file is what arrives. Every request file opens and
closes with three 1 kHz bursts, which is enough to find all 42 markers in one pass and assign them to
files: the span between a file's two markers is fixed by the manifest, and no two files share a span
closely enough to be confused.

Writes captures/hardware/<name>.wav at the recording's own rate and bit depth, then leaves the
measuring to tools/analyze_capture.py, which realigns each piece on its own markers.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
REQUESTS = ROOT / "captures/requests"
HARDWARE = ROOT / "captures/hardware"

HOP_MS = 5
BURST_MS = 120


def tone_envelope(mono, sr, hz):
    """Energy at one frequency per 5 ms hop, the same measure tools/analyze_capture.py aligns on."""
    h = max(1, int(sr * HOP_MS / 1000))
    n = len(mono) // h
    frames = mono[:n * h].reshape(n, h)
    k = np.exp(-2j * np.pi * hz * np.arange(h) / sr)
    return np.abs(frames @ k) / h


def marker_score(e, gap_s):
    """Normalized cross-correlation of the envelope against a three-burst marker, so the score is the
    shape of the match and not its loudness."""
    burst = int(round(BURST_MS / HOP_MS))
    gap = int(round(gap_s * 1000 / HOP_MS))
    tmpl = np.zeros(2 * gap + burst)
    for k in range(3):
        tmpl[k * gap: k * gap + burst] = 1.0
    L = len(tmpl)
    t0 = tmpl - tmpl.mean()
    t0 /= np.linalg.norm(t0)
    dot = np.correlate(e, t0, mode="valid")
    cs, cs2 = np.cumsum(np.insert(e, 0, 0)), np.cumsum(np.insert(e * e, 0, 0))
    s, s2 = cs[L:] - cs[:-L], cs2[L:] - cs2[:-L]
    var = s2 - s * s / L
    floor = (0.02 * float(np.std(e))) ** 2 * L
    return np.where(var > floor, dot / np.sqrt(np.maximum(var, 1e-30)), 0.0)


def peaks_above(score, threshold, min_sep_s):
    """Every local match above the threshold, tallest first, keeping them apart."""
    sep = int(min_sep_s * 1000 / HOP_MS)
    found = []
    for i in np.argsort(score)[::-1]:
        if score[i] < threshold:
            break
        if all(abs(int(i) - p) > sep for p in found):
            found.append(int(i))
    return sorted(found)


def assign(times, scores, files, tol):
    """Walk the files in order, taking the first pair of markers whose spacing matches the next file.

    The recording is the files played in the order the manifest lists them, so this only has to decide
    where each file's two markers are, not which file they belong to. A segment that happens to look
    like a three-burst marker, and there is one in 07_modulation_2, is skipped because no file starts
    at a distance from it that matches any span."""
    out, i = [], 0
    for f in files:
        want = f["marker_end"][0] - f["marker_start"][0]
        for a in range(i, len(times)):
            b = next((j for j in range(a + 1, len(times)) if abs(times[j] - times[a] - want) <= tol), None)
            if b is not None:
                out.append((f, times[a], times[b], min(scores[a], scores[b])))
                i = b + 1
                break
        else:
            raise SystemExit(f"{f['file']}: no pair of markers {want:.1f} s apart after {times[i]:.1f} s")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recording", help="one file holding the whole capture set, any format libsndfile reads")
    ap.add_argument("--out", default=str(HARDWARE), help="where the per-file WAVs go")
    ap.add_argument("--tol", type=float, default=0.25, help="how far a marker span may sit from the manifest, seconds")
    ap.add_argument("--dry-run", action="store_true", help="report the split without writing anything")
    args = ap.parse_args()

    manifest = json.loads((REQUESTS / "manifest.json").read_text())
    files = manifest["files"]
    hz = manifest.get("marker_hz", 1002.3)

    x, sr = sf.read(args.recording, dtype="float64", always_2d=True)
    info = sf.info(args.recording)
    print(f"{args.recording}: {len(x) / sr / 60:.1f} min, {sr} Hz, {x.shape[1]} ch, {info.subtype}")
    if sr != manifest["engine_rate_hz"]:
        print(f"  warning: the request asks for {manifest['engine_rate_hz']} Hz")

    e = tone_envelope(x.mean(axis=1), sr, hz)
    gap = files[0]["marker_start"][1] - files[0]["marker_start"][0]
    score = marker_score(e, gap)
    # 1.5 s apart, because a file's closing marker and the next one's opening marker land about 3.4 s
    # apart when the files are played back to back with no pause.
    hits = peaks_above(score, 0.60, 1.5)
    times = [p * HOP_MS / 1000.0 for p in hits]
    print(f"found {len(hits)} markers, expected {2 * len(files)}")

    HARD = Path(args.out)
    if not args.dry_run:
        HARD.mkdir(parents=True, exist_ok=True)

    for f, t_start, t_end, sc in assign(times, [score[p] for p in hits], files, args.tol):
        # The peak sits on the first burst, which the manifest puts at marker_start[0] into the file.
        origin = t_start - f["marker_start"][0]
        ppm = ((t_end - t_start) / (f["marker_end"][0] - f["marker_start"][0]) - 1) * 1e6
        a = max(0, int(round(origin * sr)))
        b = min(len(x), int(round((origin + f["duration_s"]) * sr)))
        name = Path(f["file"]).with_suffix(".wav").name
        short = "" if a == int(round(origin * sr)) else f", {origin:+.2f} s of head missing"
        print(f"  {name:22s} {origin:9.3f} s  {(b - a) / sr:7.2f} s  match {sc:.2f}  {ppm:+5.0f} ppm{short}")
        if not args.dry_run:
            sf.write(HARD / name, x[a:b], sr, subtype=info.subtype)

    if args.dry_run:
        print("nothing written")
    else:
        print(f"\nwrote {len(files)} files to {HARD}")
        print("next: python tools/analyze_capture.py captures/hardware/*.wav --compare")


if __name__ == "__main__":
    main()
