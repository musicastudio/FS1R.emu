#!/usr/bin/env python3
"""Check plugin/PanelView.cpp's panel coordinates against the drawing they were taken from.

    python tools/check_panel.py

The plugin draws the front panel from plugin/fs1r_panel.svg and places every live control over it by
hand, in the drawing's own user units. Nothing in the build ties the two together, so if the drawing
is edited - a button nudged, the display resized - the controls silently stop lining up. This reads
the shapes back out of the SVG and compares them with the constants in the art namespace.

Exits non-zero on the first mismatch. No dependencies beyond the standard library.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "plugin" / "fs1r_panel.svg"
CPP = ROOT / "plugin" / "PanelView.cpp"
# User units. The drawing's button and LED grids are not quite regular - the artist placed them by
# eye - and PanelView.cpp models each as one row and column spacing, so allow the tenth of a unit
# that costs. At the size the panel is drawn that is a quarter of a pixel.
TOL = 0.12

NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
CMD = re.compile(r"([MmLlHhVvCcSsQqTtAaZz])([^MmLlHhVvCcSsQqTtAaZz]*)")
XFORM = re.compile(r"(matrix|translate|scale)\s*\(([^)]*)\)")


def transform(attr):
    """The element's own transform as (a, b, c, d, e, f). Inkscape leaves one on some of the paths."""
    m = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
    for kind, args in XFORM.findall(attr or ""):
        n = [float(v) for v in NUM.findall(args)]
        if kind == "matrix" and len(n) == 6:
            t = n
        elif kind == "translate":
            t = [1, 0, 0, 1, n[0], n[1] if len(n) > 1 else 0]
        elif kind == "scale":
            t = [n[0], 0, 0, n[1] if len(n) > 1 else n[0], 0, 0]
        else:
            continue
        m = [m[0] * t[0] + m[2] * t[1], m[1] * t[0] + m[3] * t[1],
             m[0] * t[2] + m[2] * t[3], m[1] * t[2] + m[3] * t[3],
             m[0] * t[4] + m[2] * t[5] + m[4], m[1] * t[4] + m[3] * t[5] + m[5]]
    return m


def path_points(d):
    """Every point a path touches, control points included. For the circle and rectangle outlines in
    this drawing that is exactly the shape's bounding box."""
    pts, x, y, start = [], 0.0, 0.0, (0.0, 0.0)
    for cmd, rest in CMD.findall(d):
        n = [float(v) for v in NUM.findall(rest)]
        rel = cmd.islower()
        up = cmd.upper()
        if up == "Z":
            x, y = start
            continue
        step = {"M": 2, "L": 2, "T": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "A": 7}[up]
        for i in range(0, len(n) - step + 1, step):
            a = n[i:i + step]
            if up in ("M", "L", "T"):
                x, y = (x + a[0], y + a[1]) if rel else (a[0], a[1])
            elif up == "H":
                x = x + a[0] if rel else a[0]
            elif up == "V":
                y = y + a[0] if rel else a[0]
            elif up in ("C", "S", "Q", "A"):
                # only the control points of a cubic sit outside the on-curve points
                if up == "C":
                    for j in (0, 2):
                        pts.append((x + a[j], y + a[j + 1]) if rel else (a[j], a[j + 1]))
                x, y = (x + a[step - 2], y + a[step - 1]) if rel else (a[step - 2], a[step - 1])
            if up == "M" and i == 0:
                start = (x, y)
            pts.append((x, y))
            if up == "M":
                up = "L"                    # a moveto with extra pairs is an implicit lineto
    return pts


def shapes(svg):
    """(fill, cx, cy, width, height) for every path that has a fill colour."""
    out = []
    for tag in re.findall(r"<path\b[^>]*>", svg, flags=re.S):
        fill = re.search(r'fill="(#[0-9a-fA-F]{6})"', tag)
        d = re.search(r'\sd="([^"]*)"', tag, flags=re.S)
        if not fill or not d:
            continue
        pts = path_points(d.group(1))
        if not pts:
            continue
        a, b, c, dd, e, f = transform(re.search(r'transform="([^"]*)"', tag) and
                                      re.search(r'transform="([^"]*)"', tag).group(1))
        pts = [(a * x + c * y + e, b * x + dd * y + f) for x, y in pts]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        out.append((fill.group(1).lower(), (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2,
                    max(xs) - min(xs), max(ys) - min(ys)))
    return out


def constants(cpp):
    """The art namespace as {name: [values]}."""
    block = re.search(r"namespace art \{(.*?)\}\s*//\s*namespace art", cpp, flags=re.S)
    if not block:
        sys.exit("check_panel: no art namespace in PanelView.cpp")
    vals = {}
    for line in block.group(1).splitlines():
        line = line.split("//")[0]
        m = re.match(r"\s*constexpr float (\w+)\[\d*\]\s*=\s*\{([^}]*)\}", line)
        if m:
            vals[m.group(1)] = [float(v) for v in NUM.findall(m.group(2))]
            continue
        for name, value in re.findall(r"(\w+)\s*=\s*([-+0-9.ef]+)", line):
            if name not in ("constexpr", "float"):
                vals[name] = [float(value.rstrip("f"))]
    return vals


def main():
    svg, cpp = SVG.read_text(encoding="utf-8"), CPP.read_text(encoding="utf-8")
    found, k, fails = shapes(svg), constants(cpp), []

    def by(fill=None, w=None):
        hits = [s for s in found
                if (fill is None or s[0] == fill) and (w is None or abs(s[3] - w) < 0.4)]
        return sorted(hits, key=lambda s: (round(s[2]), round(s[1])))

    def want(name, got, expected, tol=TOL):
        if len(got) != len(expected):
            fails.append(f"{name}: drawing has {len(got)} of them, PanelView.cpp expects {len(expected)}")
            return
        for (gx, gy), (ex, ey) in zip(got, expected):
            if abs(gx - ex) > tol or abs(gy - ey) > tol:
                fails.append(f"{name}: drawing has ({gx:.2f}, {gy:.2f}), "
                             f"PanelView.cpp has ({ex:.2f}, {ey:.2f})")

    def centres(hits):
        return [(s[1], s[2]) for s in hits]

    # The panel face and the display glass. These two are rounded rectangles, so their corners are
    # measured off the curves' control points and land within a dot of the real edge.
    face = max((s for s in found if s[0] == "#95b5b7"), key=lambda s: s[3] * s[4])
    want("panel", [(face[1], face[2])],
         [(k["panelX"][0] + k["panelW"][0] / 2, k["panelY"][0] + k["panelH"][0] / 2)], tol=0.6)
    want("LCD", centres(by("#82ca9c")),
         [(k["lcdX"][0] + k["lcdW"][0] / 2, k["lcdY"][0] + k["lcdH"][0] / 2)], tol=0.6)

    # the nine plain buttons and the six lit ones, both read in rows top to bottom
    want("buttons", centres(by("#77787b")),
         [(x, y) for y in k["btnY"] for x in k["btnX"]])
    want("LEDs", centres(by("#8c874f")),
         [(x, y) for y in k["ledY"] for x in k["ledX"]])
    want("knob mode LEDs", centres(by("#827d43")),
         [(k["knobModeX"][0], y) for y in k["knobModeY"]])

    # the knob caps: the only dark discs of their size, and the one black disc of the volume knob
    want("knobs", centres(by("#231f20", w=2 * k["knobR"][0])),
         [(x, k["knobY"][0]) for x in k["knobX"]])
    # The volume knob's outer ring is a stroke, so the grey cap under it is what gives its centre.
    want("volume knob", centres(by("#737373")), [(k["volumeX"][0], k["volumeY"][0])])

    for f in fails:
        print("check_panel: " + f)
    if fails:
        print(f"check_panel: {len(fails)} mismatch(es); the drawing and PanelView.cpp have drifted")
        return 1
    print("check_panel: panel, LCD, 9 buttons, 6 LEDs, 2 knob mode LEDs, 4 knobs and volume all match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
