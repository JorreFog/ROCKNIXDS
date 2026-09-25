#!/usr/bin/env python3
"""Generate the dark-grey vector skin for dii-ess-aye.

Every SVG keeps the pixel size and transparent-window geometry of the PNG it
replaces, so theme layout is unchanged; ES rasterises SVG at display size, so
the art stays sharp however far the carousel scales it.
nanosvg (ES's SVG renderer) ignores <text>, so glyphs are emitted as paths.
"""
import os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen

THEME = sys.argv[1]
OUT = os.path.join(THEME, "assets/images/common")
FONT = TTFont(os.path.join(THEME, "assets/fonts/dsi_font.otf"))

# Palette
BG_TOP, BG_BOT = "#2b2e33", "#1f2124"
GRID = "#ffffff"
PANEL_HI, PANEL, PANEL_LO = "#4a4e55", "#3a3d43", "#303338"
EDGE = "#15161a"
RIM = "#5d626a"
TEXT = "#e8e9eb"
ACCENT_HI, ACCENT, ACCENT_LO = "#5cc0ff", "#2b8fe6", "#1a64b3"


def glyph_run(text, x, y, height, fill, anchor="middle", stroke=0.0):
    """Text as SVG paths; (x, y) is the baseline anchor, height is cap height."""
    gs = FONT.getGlyphSet()
    cmap = FONT.getBestCmap()
    caps = BoundsPen(gs); gs[cmap[ord("H")]].draw(caps)
    scale = height / caps.bounds[3]
    advance, parts = 0, []
    for ch in text:
        g = gs[cmap[ord(ch)]]
        pen = SVGPathPen(gs); g.draw(pen)
        parts.append((advance, pen.getCommands()))
        advance += g.width
    width = advance * scale
    x0 = {"middle": x - width / 2, "start": x, "end": x - width}[anchor]
    st = f' stroke="{fill}" stroke-width="{stroke / scale:.2f}" stroke-linejoin="round"' if stroke else ""
    out = []
    for adv, d in parts:
        out.append(f'<path transform="translate({x0 + adv * scale:.3f} {y:.3f}) scale({scale:.5f} {-scale:.5f})" '
                   f'd="{d}" fill="{fill}"{st}/>')
    return "\n".join(out)


def rr(x, y, w, h, r):
    """Rounded-rect path data (clockwise)."""
    return (f"M{x + r},{y} H{x + w - r} A{r},{r} 0 0 1 {x + w},{y + r} V{y + h - r} "
            f"A{r},{r} 0 0 1 {x + w - r},{y + h} H{x + r} A{r},{r} 0 0 1 {x},{y + h - r} "
            f"V{y + r} A{r},{r} 0 0 1 {x + r},{y} Z")


def rr_ccw(x, y, w, h, r):
    """Rounded-rect path data (counter-clockwise) — used to punch holes with nonzero fill."""
    return (f"M{x + r},{y} A{r},{r} 0 0 0 {x},{y + r} V{y + h - r} A{r},{r} 0 0 0 {x + r},{y + h} "
            f"H{x + w - r} A{r},{r} 0 0 0 {x + w},{y + h - r} V{y + r} A{r},{r} 0 0 0 {x + w - r},{y} Z")


def write(name, w, h, body):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">\n'
           f'{body}\n</svg>\n')
    with open(os.path.join(OUT, name), "w") as f:
        f.write(svg)


def grad(id_, stops, x2=0, y2=1):
    s = "".join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in stops)
    return f'<linearGradient id="{id_}" x1="0" y1="0" x2="{x2}" y2="{y2}">{s}</linearGradient>'


# --- background: 1920x480 canvas (three 640px screens), DSi-style faint grid ---
lines = []
for x in range(0, 1921, 20):
    lines.append(f"M{x},0 V480")
for y in range(0, 481, 20):
    lines.append(f"M0,{y} H1920")
write("background.svg", 1920, 480, f'''<defs>{grad("bg", [(0, BG_TOP), (1, BG_BOT)])}</defs>
<rect width="1920" height="480" fill="url(#bg)"/>
<path d="{' '.join(lines)}" stroke="{GRID}" stroke-opacity="0.035" stroke-width="1" fill="none"/>''')

# --- game_slot: DS game card, window x14..105 y19..103 ---
hole = rr_ccw(14, 19, 92, 85, 6)
body = "M3,0 H117 A3,3 0 0 1 120,3 V137 A3,3 0 0 1 117,140 H14 L0,126 V3 A3,3 0 0 1 3,0 Z"
write("game_slot.svg", 120, 140, f'''<defs>{grad("card", [(0, "#5a5e65"), (0.5, "#474a50"), (1, "#393c41")])}
{grad("lbl", [(0, "#2a2c30"), (1, "#1c1d20")])}</defs>
<path d="{body} {hole}" fill="url(#card)" fill-rule="nonzero"/>
<path d="{body}" fill="none" stroke="{EDGE}" stroke-width="2"/>
<path d="M4,2 H116" stroke="#ffffff" stroke-opacity="0.18" stroke-width="1.5"/>
<path d="{rr(12.5, 17.5, 95, 88, 7)}" fill="none" stroke="{EDGE}" stroke-width="3"/>
<path d="{rr(11, 16, 98, 91, 8.5)}" fill="none" stroke="#ffffff" stroke-opacity="0.10" stroke-width="1"/>
<path d="M50,114 H70 L60,124 Z" fill="{EDGE}" fill-opacity="0.85"/>
<path d="M2,125 L15,138" stroke="{EDGE}" stroke-opacity="0.6" stroke-width="1.5"/>
<path d="M104,128 H116 M104,132 H116" stroke="{EDGE}" stroke-opacity="0.45" stroke-width="1.5"/>''')

# --- menu_slot: rounded icon frame, window x25..99 y25..99 ---
hole = rr_ccw(25, 25, 75, 75, 7)
outer = rr(2, 2, 121, 134, 14)
write("menu_slot.svg", 125, 140, f'''<defs>{grad("ms", [(0, PANEL_HI), (0.6, PANEL), (1, PANEL_LO)])}</defs>
<path d="{rr(2, 5, 121, 134, 14)}" fill="#000000" fill-opacity="0.35"/>
<path d="{outer} {hole}" fill="url(#ms)"/>
<path d="{outer}" fill="none" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(4, 4, 117, 130, 12)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
<path d="{rr(23.5, 23.5, 78, 78, 8.5)}" fill="none" stroke="{EDGE}" stroke-width="3"/>''')

# --- start_window: DSi blue selection frame, window x20..139 y22..141 ---
hole = rr_ccw(20, 22, 120, 120, 9)
outer = rr(2, 2, 156, 196, 14)
write("start_window.svg", 160, 200, f'''<defs>{grad("sw", [(0, ACCENT_HI), (0.45, ACCENT), (1, ACCENT_LO)])}</defs>
<path d="{outer} {hole}" fill="url(#sw)"/>
<path d="{outer}" fill="none" stroke="#0d3a6b" stroke-width="2.5"/>
<path d="{rr(5, 5, 150, 190, 11)}" fill="none" stroke="#ffffff" stroke-opacity="0.35" stroke-width="1.5"/>
<path d="{rr(18.5, 20.5, 123, 123, 10.5)}" fill="none" stroke="#0d3a6b" stroke-width="3"/>
{glyph_run("START", 80, 184.5, 26, "#0d3a6b", stroke=3.2)}
{glyph_run("START", 80, 182.5, 26, "#ffffff", stroke=1.6)}''')

# --- text_bubble: speech bubble with centred tail ---
bubble = ("M10,1 H240 A9,9 0 0 1 249,10 V67 A9,9 0 0 1 240,76 H134 L125,84 L116,76 "
          "H10 A9,9 0 0 1 1,67 V10 A9,9 0 0 1 10,1 Z")
write("text_bubble.svg", 250, 85, f'''<defs>{grad("tb", [(0, "#42464d"), (1, "#34373c")])}</defs>
<path d="{bubble}" fill="url(#tb)" stroke="{RIM}" stroke-width="1.6" stroke-linejoin="round"/>''')

# --- preview_panel: dark bevelled screen for box art / video ---
write("preview_panel.svg", 492, 369, f'''<defs>{grad("pp", [(0, "#454950"), (1, "#2e3035")])}
{grad("scr", [(0, "#1d1f23"), (1, "#15171a")])}</defs>
<path d="{rr(2, 6, 488, 361, 16)}" fill="#000000" fill-opacity="0.4"/>
<path d="{rr(2, 2, 488, 360, 16)}" fill="url(#pp)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(4, 4, 484, 356, 14)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
<path d="{rr(18, 18, 456, 328, 8)}" fill="url(#scr)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(19.5, 19.5, 453, 325, 7)}" fill="none" stroke="{ACCENT}" stroke-opacity="0.25" stroke-width="1"/>''')

# --- tabs (108x56, opaque x0..100 / x8..108, y8..56) with L2/R2 badge ---
def tab(name, flip, label):
    """160x72, drawn at the size the theme shows it (no stretching). The visible tab body is
    y 10..72; the badge is centred on it (y 41) so it lines up with the vertically centred text."""
    W, H, top = 160, 72, 10
    shape = f"M0,{top} H118 A30,30 0 0 1 148,{top + 30} V{H} H0 Z"
    tr = f' transform="translate({W} 0) scale(-1 1)"' if flip else ""
    bw, bh = 36, 24
    bx = W - 12 - bw if flip else 12
    cy = top + (H - top) / 2
    write(name, W, H, f'''<defs>{grad("tab", [(0, PANEL_HI), (1, PANEL_LO)])}</defs>
<g{tr}>
<path d="{shape}" fill="url(#tab)"/>
<path d="M0,{top + 1} H118 A29,29 0 0 1 147,{top + 30} V{H}" fill="none" stroke="{EDGE}" stroke-width="2"/>
<path d="M0,{top + 3} H117 A26,26 0 0 1 144,{top + 29}" fill="none" stroke="#ffffff" stroke-opacity="0.13" stroke-width="1.2"/>
</g>
<path d="{rr(bx, cy - bh / 2, bw, bh, 7)}" fill="#1c1d21" stroke="#5d626a" stroke-width="1.2"/>
{glyph_run(label, bx + bw / 2, cy + 6.5, 13, TEXT, stroke=0.7)}''')

tab("left_tab.svg", False, "L2")
tab("right_tab.svg", True, "R2")

# --- scroll_bar 256x22: track + blue arrow caps ---
dots = " ".join(f"M{x},11 h1.5" for x in range(30, 227, 4))
write("scroll_bar.svg", 256, 22, f'''<defs>{grad("sb", [(0, "#3b3e44"), (1, "#2b2d32")])}
{grad("cap", [(0, ACCENT_HI), (1, ACCENT_LO)])}</defs>
<rect x="18" y="4" width="220" height="14" rx="3" fill="url(#sb)" stroke="{EDGE}" stroke-width="1.2"/>
<path d="{dots}" stroke="#7a8089" stroke-width="1.2"/>
<rect x="1" y="1" width="22" height="20" rx="4" fill="url(#cap)" stroke="#0d3a6b" stroke-width="1.2"/>
<rect x="233" y="1" width="22" height="20" rx="4" fill="url(#cap)" stroke="#0d3a6b" stroke-width="1.2"/>
<path d="M16,6 L7,11 L16,16 Z M240,6 L249,11 L240,16 Z" fill="#ffffff"/>''')

# --- menu switch / slider knob for the settings menus ---
write("switch_on.svg", 44, 28, f'''<rect x="1" y="4" width="42" height="20" rx="10" fill="{ACCENT}" stroke="#0d3a6b" stroke-width="1.5"/>
<circle cx="33" cy="14" r="8" fill="#ffffff"/>''')
write("switch_off.svg", 44, 28, f'''<rect x="1" y="4" width="42" height="20" rx="10" fill="#2a2c31" stroke="{RIM}" stroke-width="1.5"/>
<circle cx="11" cy="14" r="8" fill="#8a9098"/>''')
write("slider_knob.svg", 32, 32, f'''<circle cx="16" cy="16" r="13" fill="#ffffff" stroke="{ACCENT}" stroke-width="3"/>''')

print("ok")


# ===== 2026-09-26 polish pass =====
HERE = os.path.dirname(os.path.abspath(__file__))

# --- background split: static gradient + a grid layer the theme drifts slowly ---
write("background_plain.svg", 1920, 480, f'''<defs>{grad("bg", [(0, BG_TOP), (1, BG_BOT)])}
<radialGradient id="vig" cx="0.5" cy="0.45" r="0.75"><stop offset="0.55" stop-color="#000000" stop-opacity="0"/><stop offset="1" stop-color="#000000" stop-opacity="0.35"/></radialGradient></defs>
<rect width="1920" height="480" fill="url(#bg)"/>
<rect width="640" height="480" fill="url(#vig)"/>
<rect x="640" width="640" height="480" fill="url(#vig)"/>''')
# 1960x520: 40px larger so it can slide 20px (one cell) diagonally and loop seamlessly
lines = [f"M{x},0 V520" for x in range(0, 1961, 20)] + [f"M0,{y} H1960" for y in range(0, 521, 20)]
write("grid.svg", 1960, 520, f'<path d="{" ".join(lines)}" stroke="{GRID}" stroke-opacity="0.04" stroke-width="1" fill="none"/>')

# --- ROCKNIX wordmark: traced from the stock 500x195 PNG (trace_logo.py), now vector ---
paths = open(os.path.join(HERE, "rocknix_logo.paths")).read()
paths = paths.replace('fill="RED"', 'fill="url(#rk)"').replace('fill="GREY"', 'fill="url(#nx)"')
shadow = paths.replace('fill="url(#rk)"', 'fill="#000000"').replace('fill="url(#nx)"', 'fill="#000000"')
write("distro_logo.svg", 500, 95, f'''<defs>{grad("rk", [(0, "#ff6b6b"), (1, "#e8403f")])}
{grad("nx", [(0, "#f2f3f5"), (1, "#c9ccd1")])}</defs>
<g transform="translate(0 -53)">
<g transform="translate(0 4)" opacity="0.35">{shadow}</g>
{paths}
</g>''')

# --- soft light behind the logo (the theme makes it breathe) ---
write("logo_glow.svg", 560, 240, f'''<defs><radialGradient id="g" cx="0.5" cy="0.5" r="0.5">
<stop offset="0" stop-color="{ACCENT_HI}" stop-opacity="0.22"/>
<stop offset="0.45" stop-color="{ACCENT}" stop-opacity="0.08"/>
<stop offset="1" stop-color="{ACCENT}" stop-opacity="0"/></radialGradient></defs>
<ellipse cx="280" cy="120" rx="280" ry="120" fill="url(#g)"/>''')

# --- top-screen status strip ---
write("status_bar.svg", 640, 44, f'''<defs>{grad("st", [(0, "#000000"), (1, "#000000")])}</defs>
<rect width="640" height="43" fill="#000000" fill-opacity="0.22"/>
<path d="M0,43.5 H640" stroke="#ffffff" stroke-opacity="0.07" stroke-width="1"/>''')

# --- scroll bar, sized 1:1 for the 640px bottom screen: arrow caps + groove.
#     The thumb is a separate image the theme positions from {system:index}/{game:index}.
W, H = 600, 28
write("scroll_track.svg", W, H, f'''<defs>{grad("cap", [(0, ACCENT_HI), (1, ACCENT_LO)])}
{grad("gv", [(0, "#16171a"), (1, "#26282c")])}</defs>
<path d="{rr(40, 7, 520, 14, 7)}" fill="url(#gv)" stroke="#000000" stroke-opacity="0.45" stroke-width="1"/>
<path d="M47,21.5 H553" stroke="#ffffff" stroke-opacity="0.08" stroke-width="1"/>
<path d="{rr(1, 1, 32, 26, 7)}" fill="url(#cap)" stroke="#0d3a6b" stroke-width="1.5"/>
<path d="{rr(W - 33, 1, 32, 26, 7)}" fill="url(#cap)" stroke="#0d3a6b" stroke-width="1.5"/>
<path d="{rr(3, 2.5, 28, 11, 5)}" fill="#ffffff" fill-opacity="0.18"/>
<path d="{rr(W - 31, 2.5, 28, 11, 5)}" fill="#ffffff" fill-opacity="0.18"/>
<path d="M21,8 L11,14 L21,20 Z M{W - 21},8 L{W - 11},14 L{W - 21},20 Z" fill="#ffffff" stroke="#ffffff" stroke-width="1.5" stroke-linejoin="round"/>''')
write("scroll_thumb.svg", 84, 20, f'''<defs>{grad("th", [(0, ACCENT_HI), (0.5, ACCENT), (1, ACCENT_LO)])}</defs>
<path d="{rr(1, 1, 82, 18, 9)}" fill="url(#th)" stroke="#0d3a6b" stroke-width="1.5"/>
<path d="{rr(4, 3, 76, 6, 3)}" fill="#ffffff" fill-opacity="0.28"/>''')

print("polish ok")
