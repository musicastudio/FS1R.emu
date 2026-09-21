"""Turns the owner's manual panel drawing into the one the plugin embeds.

    python tools/make_panel_svg.py

docs/FS1R-front-panel-p14-ny.svg is page 14 of the FS1R owner's manual converted from the PDF and
cleaned up in Inkscape. The conversion left behind a clip path per drawing operation - more than a
thousand of them, every one a clip to the page box, so every one a no-op. JUCE's SVG reader applies
them through lunasvg and ends up clipping the whole drawing away, so they are stripped here rather
than worked around in the plugin. Coordinates are untouched: plugin/PanelView.cpp addresses the
drawing in its own user units and tools/check_panel.py checks them against this same file.

The one thing that does not carry over is the logo: the panel the plugin shows is this project, so the
FS1R wordmark is swapped for the FSVR one from docs/fsvr.svg, which is the same four letters with the
manual's own F, S and R and a V drawn to match. That file is the wordmark; nothing is redrawn here.

Writes plugin/fs1r_panel.svg, which is committed so the build needs no Python.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "FS1R-front-panel-p14-ny.svg"
LOGO = ROOT / "docs" / "fsvr.svg"
DST = ROOT / "plugin" / "fs1r_panel.svg"
# The 1 and the R in the drawing. The V takes the 1's path and the R moves right by what the wider
# letter costs, which docs/fsvr.svg carries as the R's own translate. F and S are already right.
ONE, ARR = "path9570", "path9568"


def strip_clips(svg: str) -> str:
    # The <clipPath> definitions, then every reference to one, as an attribute or inside a style.
    svg = re.sub(r"<clipPath\b.*?</clipPath>", "", svg, flags=re.S)
    svg = re.sub(r"<clipPath\b[^>]*/>", "", svg)
    svg = re.sub(r"\s*clip-path\s*=\s*\"[^\"]*\"", "", svg)
    svg = re.sub(r"\s*clip-path\s*:\s*url\([^)]*\)\s*;?", "", svg)
    # Groups that held nothing but a clip are now empty; drop the obvious ones.
    while True:
        out = re.sub(r"<g>\s*</g>", "", svg)
        if out == svg:
            return out
        svg = out


def swap_logo(svg: str, logo: str) -> str:
    v = re.search(r'<path id="V" d="([^"]*)"', logo)
    r = re.search(r'<path id="R" transform="(translate\([^"]*\))"', logo)
    if not v or not r:
        sys.exit(f"make_panel_svg: no V path or R translate in {LOGO.name}")
    svg, n = re.subn(r'(<path\s+d=")[^"]*("[^>]*id="%s")' % ONE, lambda m: m.group(1) + v.group(1) + m.group(2), svg)
    svg, m = re.subn(r'(<path\s+d="[^"]*"[^>]*id="%s")' % ARR,
                     lambda g: g.group(1) + f'\n     transform="{r.group(1)}"', svg)
    if n != 1 or m != 1:
        sys.exit(f"make_panel_svg: logo paths {ONE}/{ARR} matched {n}/{m} times, expected 1 each")
    return svg


def main() -> int:
    if not SRC.exists():
        print(f"missing {SRC}", file=sys.stderr)
        return 1
    svg = SRC.read_text(encoding="utf-8")
    out = swap_logo(strip_clips(svg), LOGO.read_text(encoding="utf-8"))
    before, after = svg.count("<clipPath"), out.count("<clipPath")
    DST.write_text(out, encoding="utf-8")
    print(f"{SRC.name}: {before} clip paths -> {after}, {len(svg)} -> {len(out)} bytes")
    print(f"wrote {DST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
