#!/usr/bin/env python3
"""ROCKNIXDS logo: the ROCKNIX wordmark, the DS two-screen icon, then "DS". Flat colours, one line.

  ROCK (red) NIX (grey)  [two stacked rounded screens: red over grey]  DS (white)

"ROCKNIX" is the stock ROCKNIX wordmark traced to vectors (../dii-ess-aye/rocknix_logo.paths, a 500x195 space with
the letters at y 53..142). "DS" is set in Unbounded Black (SIL OFL 1.1, logo/OFL-Unbounded.txt), instanced from the
variable font, as paths (ES's nanosvg can't draw <text>, and GitHub shouldn't need the font).

Writes, next to this script:
  rocknixds-logo.svg          one line, for dark backgrounds (white DS): README dark mode, the theme, the splash
  rocknixds-logo-light.svg    the same for light backgrounds (near-black DS): README light mode
  rocknixds-logo-stack.svg    ROCKNIX above [icon DS], for small square spaces; -light = near-black DS (the card label)
  *.frag                      the same art without the <svg> wrapper, for gen_skin.py ("<!-- W x H -->" first)
"""
import os
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
WORDMARK = open(os.path.join(HERE, "..", "dii-ess-aye", "rocknix_logo.paths")).read()
WM_W, WM_TOP, WM_BASE = 499.5, 53.0, 142.0          # measured bounds of the traced wordmark
WM_CAP = WM_BASE - WM_TOP
HEAVY = instantiateVariableFont(TTFont(os.path.join(HERE, "Unbounded-VF.ttf")), {"wght": 800})

RED, GREY = "#f5303f", "#8f8f8f"


def ds_glyphs(cap, tracking=0.06):
    """"DS" as one path, baseline y = 0, cap height `cap`. Returns (d, width)."""
    gs, cmap = HEAVY.getGlyphSet(), HEAVY.getBestCmap()
    b = BoundsPen(gs); gs[cmap[ord("H")]].draw(b)
    s = cap / b.bounds[3]
    lsb = BoundsPen(gs); gs[cmap[ord("D")]].draw(lsb)
    x, d, right = -lsb.bounds[0] * s, [], 0.0
    for i, ch in enumerate("DS"):
        g = gs[cmap[ord(ch)]]
        pen = SVGPathPen(gs); g.draw(TransformPen(pen, (s, 0, 0, -s, x, 0)))
        d.append(pen.getCommands())
        gb = BoundsPen(gs); g.draw(gb); right = x + gb.bounds[2] * s
        x += g.width * s + (tracking * cap if i == 0 else 0)
    return " ".join(d), right


def wordmark(x, baseline, cap, red=RED, grey=GREY):
    """The traced ROCKNIX wordmark with its baseline at y and cap height `cap`. Returns (svg, width)."""
    s = cap / WM_CAP
    paths = WORDMARK.replace('fill="RED"', f'fill="{red}"').replace('fill="GREY"', f'fill="{grey}"')
    return f'<g transform="translate({x:.2f} {baseline:.2f}) scale({s:.5f}) translate(0 {-WM_BASE})">{paths}</g>', WM_W * s


def icon(x, cy, cap, red=RED, grey=GREY):
    """The DS two-screen icon: two rounded-rect outlines, red over grey, centred on y = cy. Returns (svg, w, h)."""
    w, h, gap, stroke, r = 0.69 * cap, 0.54 * cap, 0.07 * cap, 0.085 * cap, 0.11 * cap
    top = cy - (2 * h + gap) / 2
    def rect(y, col):
        return (f'<rect x="{x + stroke / 2:.2f}" y="{y + stroke / 2:.2f}" width="{w - stroke:.2f}" height="{h - stroke:.2f}" '
                f'rx="{r - stroke / 2:.2f}" fill="none" stroke="{col}" stroke-width="{stroke:.2f}"/>')
    return rect(top, red) + "\n" + rect(top + h + gap, grey), w, 2 * h + gap


def line(cap, ds_fill):
    """One-line lockup at cap height `cap`, origin at the top-left of its padding box. Returns (svg, W, H)."""
    pad = 0.12 * cap
    ih = 1.15 * cap                                     # icon height (it overhangs the cap band a little)
    H = ih + 2 * pad
    base = pad + ih / 2 + cap / 2                       # baseline: cap band centred on the icon
    x = pad
    wm, wmw = wordmark(x, base, cap); x += wmw + 0.17 * cap
    ic, iw, _ = icon(x, base - cap / 2, cap); x += iw + 0.23 * cap
    d, dw = ds_glyphs(1.03 * cap)
    ds = f'<path transform="translate({x:.2f} {base:.2f})" d="{d}" fill="{ds_fill}"/>'
    x += dw + pad
    return "\n".join((wm, ic, ds)), x, H


def stack(cap, ds_fill):
    """ROCKNIX over [icon DS]: the second line at cap height `cap`, the wordmark scaled to the same width."""
    pad = 0.12 * cap
    ic_svg, iw, ih = icon(0, 0, cap)                    # measure only
    d, dw = ds_glyphs(1.03 * cap)
    lw = iw + 0.23 * cap + dw                           # second line's width
    wcap = lw / (WM_W / WM_CAP)                         # wordmark cap height that gives the same width
    gap = 0.22 * cap
    H = pad + wcap + gap + ih + pad
    base2 = pad + wcap + gap + ih / 2 + cap / 2
    wm, _ = wordmark(pad, pad + wcap, wcap)
    ic, _, _ = icon(pad, base2 - cap / 2, cap)
    ds = f'<path transform="translate({pad + iw + 0.23 * cap:.2f} {base2:.2f})" d="{d}" fill="{ds_fill}"/>'
    return "\n".join((wm, ic, ds)), lw + 2 * pad, H


def write(name, body, w, h):
    with open(os.path.join(HERE, name + ".frag"), "w") as f:
        f.write(f"<!-- {w:.1f} x {h:.1f} -->\n{body}\n")
    with open(os.path.join(HERE, name + ".svg"), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.0f}" height="{h:.0f}" viewBox="0 0 {w:.2f} {h:.2f}">\n{body}\n</svg>\n')
    print(name, f"{w:.0f}x{h:.0f}")


CAP = 100.0
write("rocknixds-logo", *line(CAP, "#f4f4f4"))
write("rocknixds-logo-light", *line(CAP, "#1c1c1c"))
write("rocknixds-logo-stack", *stack(CAP, "#f4f4f4"))
write("rocknixds-logo-stack-light", *stack(CAP, "#1c1c1c"))   # for light surfaces (the splash's card label)
