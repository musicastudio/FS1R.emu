#!/usr/bin/env python3
"""Render each harmonic form at note 36 through the engine at every skirt and score its lines against
FS1R.unlock's 2026-09-19 skirt sweep (60 partials per step).

    python tools/check_skirt.py [/tmp/rc]       # render_capture build to use (default bin/render_capture)
"""
import json, subprocess, sys, tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fs1r_patch as fp
import analyze_capture as ac

SWEEP = ROOT.parent / "FS1R.unlock/captures/2026-09-19-sweep/sweeps"
F0 = 65.406
FORMS = {"all1": fp.ALL1, "all2": fp.ALL2, "odd1": fp.ODD1, "odd2": fp.ODD2, "res1": fp.RES1, "res2": fp.RES2}


def partials(seg, sr, f0, K=60):
    n = len(seg) // 2 * 2
    W = np.abs(np.fft.rfft((seg[:n] - seg[:n].mean()) * np.hanning(n))) / n * 4
    fr = np.fft.rfftfreq(n, 1 / sr)
    out = []
    for k in range(1, K + 1):
        m = (fr > f0 * k - f0 * 0.35) & (fr < f0 * k + f0 * 0.35)
        out.append(20 * np.log10(W[m].max() + 1e-14))
    return np.array(out)


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "bin/render_capture"
    tmp = Path(tempfile.mkdtemp())
    worst = 0.0
    for name, form in FORMS.items():
        rows = [[float(x) for x in r.split(",")[1:] if x] for r in (SWEEP / f"skirt_{name}_partials.csv").read_text().splitlines()[1:]]
        s = fp.Smf()
        s.at(0, fp.perf_bulk(fp.init_performance()))
        t = 600
        for sk in range(8):
            v = fp.init_voice("Skirt     ")
            kw = dict(keysync=1, level=99, form=form, skirt=sk)
            if form in (fp.RES1, fp.RES2):
                kw["bw"] = 8
            fp.set_op(v, 1, **kw)
            s.at(t, fp.voice_bulk(v)); t += 500
            s.note(t, 36, 100, 1500); t += 2200
        mid = tmp / f"{name}.mid"; wav = tmp / f"{name}.wav"
        s.write(mid, tail_ms=500)
        subprocess.run([str(exe), "-f", "-d", "0.5", str(wav), str(mid)], check=True, capture_output=True)
        sr, w = ac.read_wav(wav)
        x = ac.unpan(w)
        errs = []
        for sk in range(8):
            on = (600 + sk * 2700 + 500) / 1000.0
            seg = x[int((on + 0.3) * sr):int((on + 1.4) * sr)]
            e = partials(seg, sr, F0)
            u = np.array(rows[sk][:60])
            # The take's floor sits 70 to 80 dB under each step's peak (the tail of every row), so a
            # line says something about the window only while it is clear of that.
            floor = np.median(u[40:]) + 12
            m = (u > floor) & (u > u.max() - 50)
            e, u = e - e.max(), u - u.max()
            errs.append(float(np.sqrt(np.mean((e[m] - u[m]) ** 2))))
        worst = max(worst, max(errs))
        print(f"{name:5s} rms dB per skirt: " + " ".join(f"{v:5.2f}" for v in errs))
    print(f"worst {worst:.2f} dB")
    return 0 if worst < 3.0 else 1


if __name__ == "__main__":
    sys.exit(main())
