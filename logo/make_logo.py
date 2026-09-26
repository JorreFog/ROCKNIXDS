#!/usr/bin/env python3
"""ROCKNIXDS logo: a wide ROCKNIX line over a big chrome "DS", after the Nintendo DS logo's layout.

All glyphs are emitted as paths (ES's nanosvg can't draw <text>, and GitHub shouldn't need the font).
Font: Unbounded (SIL OFL 1.1, logo/OFL-Unbounded.txt), instanced from the variable font.
Every letter has a dark outline, so the logo reads on light and dark backgrounds.

Writes, next to this script:
  rocknixds-logo.svg        stacked lockup (README, splash)
  rocknixds-logo-wide.svg   one line, for the theme's bar between the L2/R2 tabs
  rocknixds-logo.frag / rocknixds-logo-wide.frag   the same art without the <svg> wrapper, for gen_skin.py
"""
import os
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
VF = os.path.join(HERE, "Unbounded-VF.ttf")
HEAVY = instantiateVariableFont(TTFont(VF), {"wght": 900})
BOLD = instantiateVariableFont(TTFont(VF), {"wght": 800})

OUTLINE = "#17191d"


def run(font, text, cap, tracking=0.0):
    """Glyph outlines of `text`, baseline at y=0, cap height `cap`. Returns (svg path d, width, cap)."""
    gs, cmap = font.getGlyphSet(), font.getBestCmap()
    b = BoundsPen(gs); gs[cmap[ord("H")]].draw(b)
    s = cap / b.bounds[3]
    x, d = 0.0, []
    first_lsb = None
    for i, ch in enumerate(text):
        g = gs[cmap[ord(ch)]]
        gb = BoundsPen(gs); g.draw(gb)
        if first_lsb is None:
            first_lsb = gb.bounds[0] * s
        pen = SVGPathPen(gs)
        g.draw(TransformPen(pen, (s, 0, 0, -s, x - first_lsb, 0)))
        d.append(pen.getCommands())
        last_right = x - first_lsb + gb.bounds[2] * s
        x += g.width * s + (tracking * cap if i < len(text) - 1 else 0)
    return " ".join(d), last_right


def defs(p):
    return f'''<linearGradient id="{p}chrome" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#ffffff"/><stop offset="0.46" stop-color="#e3e6ea"/>
<stop offset="0.5" stop-color="#a3a9b2"/><stop offset="0.78" stop-color="#c3c8cf"/><stop offset="1" stop-color="#e9ecef"/></linearGradient>
<linearGradient id="{p}red" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff6b6b"/><stop offset="1" stop-color="#e3393a"/></linearGradient>
<linearGradient id="{p}grey" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f4f5f7"/><stop offset="1" stop-color="#b9bdc4"/></linearGradient>'''


def outlined(d, fill, cap, keyline=False, shadow=True):
    """Dark outline (stroke under the fill), optional white keyline between fill and outline, drop shadow."""
    o = cap * 0.11          # outline width outside the glyph edge
    k = cap * 0.04          # keyline width outside the edge
    out = []
    if shadow:
        out.append(f'<path d="{d}" transform="translate({cap*0.03:.2f} {cap*0.05:.2f})" fill="#000000" fill-opacity="0.3" '
                   f'stroke="#000000" stroke-opacity="0.3" stroke-width="{2*o:.2f}" stroke-linejoin="round"/>')
    out.append(f'<path d="{d}" fill="{OUTLINE}" stroke="{OUTLINE}" stroke-width="{2*o:.2f}" stroke-linejoin="round"/>')
    if keyline:
        out.append(f'<path d="{d}" fill="#ffffff" stroke="#ffffff" stroke-width="{2*k:.2f}" stroke-linejoin="round"/>')
    out.append(f'<path d="{d}" fill="{fill}"/>')
    return "\n".join(out)


def rocknix(p, cap, width=None):
    """ROCK in red, NIX in silver, optionally tracked out to `width`."""
    rock, rw = run(BOLD, "ROCK", cap)
    both, bw = run(BOLD, "ROCKNIX", cap)
    tr = 0.0
    if width:
        tr = (width - bw) / (6 * cap)
        both, bw = run(BOLD, "ROCKNIX", cap, tr)
    rock, rw = run(BOLD, "ROCK", cap, tr)
    nix_all, _ = run(BOLD, "ROCKNIX", cap, tr)
    # NIX = the last three glyphs: re-run from N with the same offset
    gs, cmap = BOLD.getGlyphSet(), BOLD.getBestCmap()
    b = BoundsPen(gs); gs[cmap[ord("H")]].draw(b); s = cap / b.bounds[3]
    lsb = BoundsPen(gs); gs[cmap[ord("R")]].draw(lsb)
    x = -lsb.bounds[0] * s
    for ch in "ROCK":
        x += gs[cmap[ord(ch)]].width * s + tr * cap
    nlsb = BoundsPen(gs); gs[cmap[ord("N")]].draw(nlsb)
    nix, _ = run(BOLD, "NIX", cap, tr)
    nix_x = x + nlsb.bounds[0] * s
    g = f'<g>{outlined(rock, f"url(#{p}red)", cap)}</g>\n<g transform="translate({nix_x:.2f} 0)">{outlined(nix, f"url(#{p}grey)", cap)}</g>'
    return g, bw


def write(name, w, h, body, p):
    frag = f'<defs>{defs(p)}</defs>\n{body}'
    with open(os.path.join(HERE, name + ".frag"), "w") as f:
        f.write(f"<!-- {w:.1f} x {h:.1f} -->\n" + frag + "\n")
    with open(os.path.join(HERE, name + ".svg"), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.0f}" height="{h:.0f}" viewBox="0 0 {w:.2f} {h:.2f}">\n{frag}\n</svg>\n')
    print(name, f"{w:.0f}x{h:.0f}")


# ---- stacked: ROCKNIX tracked to the width of DS, above it ----
CAP = 200.0
ds, dsw = run(HEAVY, "DS", CAP, 0.06)
pad = CAP * 0.16                        # room for outline + shadow
rcap = CAP * 0.27
rk, _ = rocknix("s", rcap, dsw)
gap = CAP * 0.2
top = pad + rcap                        # ROCKNIX baseline
base = top + gap + CAP                  # DS baseline
W, H = dsw + 2 * pad, base + pad
write("rocknixds-logo", W, H, f'''<g transform="translate({pad:.2f} {top:.2f})">{rk}</g>
<g transform="translate({pad:.2f} {base:.2f})">{outlined(ds, "url(#schrome)", CAP, keyline=True)}</g>''', "s")

# ---- wide: ROCKNIX then DS on one line, DS a bit taller, bottoms aligned ----
C2 = 100.0
rk2, rw2 = rocknix("w", C2, None)
dcap = C2 * 1.3
ds2, dw2 = run(HEAVY, "DS", dcap, 0.06)
pad2 = dcap * 0.16
sp = C2 * 0.45
W2 = pad2 + rw2 + sp + dw2 + pad2
H2 = pad2 + dcap + pad2
b2 = pad2 + dcap
write("rocknixds-logo-wide", W2, H2, f'''<g transform="translate({pad2:.2f} {b2:.2f})">{rk2}</g>
<g transform="translate({pad2 + rw2 + sp:.2f} {b2:.2f})">{outlined(ds2, "url(#wchrome)", dcap, keyline=True)}</g>''', "w")
