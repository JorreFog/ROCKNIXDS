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


def glyph_run(text, x, y, height, fill, anchor="middle", stroke=0.0, font=None):
    """Text as SVG paths; (x, y) is the baseline anchor, height is cap height."""
    font = font or FONT
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
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

# --- game_slot: a Nintendo DS Game Card seen from the label side, 100x104 ---
# Modelled on photos of real cards: a thin charcoal plastic rim, the bottom-left corner chamfered, a small notch
# in the left edge, and almost the whole front a white paper label with rounded corners: the NINTENDO DS logo on
# the white top strip, the art below (window x12..88 y21..87 is a hole; the art is drawn under this), and a
# small embossed arrow under the label. game_label.svg is the paper drawn behind the art, so art that keeps its
# aspect ratio sits on white like a printed label. empty_slot.svg is the same outline, recessed and empty.
# narrower than the real 35x33 mm on purpose: at true proportions it read as too wide on screen
CARD_W, CARD_H = 100, 104
CW = CARD_W
card = (f"M5,0 H{CW - 5} A5,5 0 0 1 {CW},5 V99 A5,5 0 0 1 {CW - 5},104 H11 L0,93 V66 H1.6 V55 H0 V5 A5,5 0 0 1 5,0 Z")
label = rr(6.5, 4.5, CW - 13, 86, 6)
window = rr_ccw(12, 21, CW - 24, 66, 1)     # the art window: x12..CW-12, y21..87


def ds_logo(x, base, h, color):
    """NINTENDO + the two-screen mark + DS, left-aligned at x on baseline base (h = cap height of DS)."""
    small = h * 0.52
    out = [glyph_run("NINTENDO", x, base, small, color, anchor="start")]
    # width of the small word, measured from the glyph advances
    gs = FONT.getGlyphSet(); cmap = FONT.getBestCmap()
    caps = BoundsPen(gs); gs[cmap[ord("H")]].draw(caps)
    sc = small / caps.bounds[3]
    wx = x + sum(gs[cmap[ord(c)]].width for c in "NINTENDO") * sc + small * 0.35
    b = small * 0.42                                   # the two stacked screens
    out.append(f'<rect x="{wx:.2f}" y="{base - small:.2f}" width="{b:.2f}" height="{b * 0.85:.2f}" rx="0.4" '
               f'fill="none" stroke="{color}" stroke-width="0.6"/>')
    out.append(f'<rect x="{wx:.2f}" y="{base - small + b:.2f}" width="{b:.2f}" height="{b * 0.85:.2f}" rx="0.4" '
               f'fill="none" stroke="{color}" stroke-width="0.6"/>')
    out.append(glyph_run("DS", wx + b + small * 0.3, base, h, color, anchor="start", stroke=0.9))
    return "\n".join(out)


write("game_slot.svg", CARD_W, CARD_H, f'''<defs>{grad("card", [(0, "#606166"), (0.5, "#525358"), (1, "#45464a")])}
{grad("paper", [(0, "#fbfbf8"), (1, "#ecebe6")])}</defs>
<path d="{card} {window}" fill="url(#card)" fill-rule="nonzero"/>
<path d="M5.5,1 H{CW - 5.5}" stroke="#ffffff" stroke-opacity="0.22" stroke-width="1.2"/>
<path d="M1.1,6 V54 M1.1,67 V92" stroke="#ffffff" stroke-opacity="0.10" stroke-width="1"/>
<path d="{card}" fill="none" stroke="{EDGE}" stroke-width="1.6" stroke-linejoin="round"/>
<path d="{rr(5.2, 3.2, CW - 10.4, 88.6, 7)}" fill="none" stroke="#000000" stroke-opacity="0.35" stroke-width="1.3"/>
<path d="{label} {window}" fill="url(#paper)" fill-rule="nonzero"/>
<path d="{rr(11.6, 20.6, CW - 23.2, 66.8, 1.2)}" fill="none" stroke="#000000" stroke-opacity="0.18" stroke-width="0.8"/>
{ds_logo(13, 16.3, 8.2, "#1c1d20")}
<path d="M{CW / 2 - 4},94.5 H{CW / 2 + 4} L{CW / 2},99.5 Z" fill="#3a3b3f"/>
<path d="M{CW / 2 + 4.2},94.8 L{CW / 2 + 0.3},99.6" stroke="#ffffff" stroke-opacity="0.14" stroke-width="0.8"/>''')
write("game_label.svg", CW - 24, 66, f'''<defs>{grad("gl", [(0, "#fbfbf8"), (1, "#ecebe6")])}</defs>
<rect width="{CW - 24}" height="66" fill="url(#gl)"/>''')
write("empty_slot.svg", CARD_W, CARD_H, f'''<path d="{card}" fill="#000000" fill-opacity="0.22"/>
<path d="{card}" fill="none" stroke="#000000" stroke-opacity="0.45" stroke-width="1.6" stroke-linejoin="round"/>
<path d="M5.5,103 H{CW - 5.5}" stroke="#ffffff" stroke-opacity="0.07" stroke-width="1.2"/>
<path d="{rr(6.5, 4.5, CW - 13, 86, 6)}" fill="none" stroke="#ffffff" stroke-opacity="0.05" stroke-width="1"/>''')

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

# --- info_panel: the game list's top-screen card, 608x360: 3D box on the left (drawn over), screenshot window
# x214..592 y18..160 (the 512:192 side-by-side DS screens), details below it, RetroAchievements strip under a
# divider at y250 ---
write("info_panel.svg", 608, 360, f'''<defs>{grad("ip", [(0, "#43474e"), (1, "#2f3136")])}
{grad("scr", [(0, "#1d1f23"), (1, "#15171a")])}</defs>
<path d="{rr(2, 6, 604, 352, 18)}" fill="#000000" fill-opacity="0.4"/>
<path d="{rr(2, 2, 604, 352, 18)}" fill="url(#ip)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(4, 4, 600, 348, 16)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
<path d="{rr(211, 15, 384, 148, 8)}" fill="url(#scr)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(212.5, 16.5, 381, 145, 7)}" fill="none" stroke="{ACCENT}" stroke-opacity="0.25" stroke-width="1"/>
<path d="M22,250 H586" stroke="#000000" stroke-opacity="0.45" stroke-width="1.5"/>
<path d="M22,251.5 H586" stroke="#ffffff" stroke-opacity="0.08" stroke-width="1"/>''')

# --- home panels (main menu top screen) ---
# home_clock_panel: 608x190 card for the clock and date (left) and a DSi-style calendar tile (right, x432..588
# y16..174: red month band y16..58 with binder rings, white page below)
write("home_clock_panel.svg", 608, 190, f'''<defs>{grad("hp", [(0, "#43474e"), (1, "#2f3136")])}
{grad("calr", [(0, "#f0605f"), (1, "#c7302f")])}{grad("calp", [(0, "#fbfbf8"), (1, "#e2e1dc")])}</defs>
<path d="{rr(2, 6, 604, 182, 18)}" fill="#000000" fill-opacity="0.4"/>
<path d="{rr(2, 2, 604, 182, 18)}" fill="url(#hp)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(4, 4, 600, 178, 16)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
<path d="{rr(432, 20, 156, 158, 10)}" fill="#000000" fill-opacity="0.35"/>
<path d="{rr(432, 16, 156, 158, 10)}" fill="url(#calp)" stroke="{EDGE}" stroke-width="1.5"/>
<path d="M442,16 H578 A10,10 0 0 1 588,26 V58 H432 V26 A10,10 0 0 1 442,16 Z" fill="url(#calr)"/>
<path d="M433,58 H587" stroke="#8e1f1f" stroke-width="1.5"/>
<circle cx="470" cy="16" r="5.5" fill="#2b2d31" stroke="{EDGE}" stroke-width="1"/>
<circle cx="550" cy="16" r="5.5" fill="#2b2d31" stroke="{EDGE}" stroke-width="1"/>
<path d="M470,6 V16 M550,6 V16" stroke="#9aa0a8" stroke-width="3" stroke-linecap="round"/>''')
# home_system_panel: 608x150 card for the selected system: icon well x20..130 y20..130, details to the right
write("home_system_panel.svg", 608, 150, f'''<defs>{grad("hs", [(0, "#43474e"), (1, "#2f3136")])}
{grad("well", [(0, "#1d1f23"), (1, "#15171a")])}</defs>
<path d="{rr(2, 6, 604, 142, 18)}" fill="#000000" fill-opacity="0.4"/>
<path d="{rr(2, 2, 604, 142, 18)}" fill="url(#hs)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(4, 4, 600, 138, 16)}" fill="none" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
<path d="{rr(18, 18, 110, 110, 14)}" fill="url(#well)" stroke="{EDGE}" stroke-width="2"/>
<path d="{rr(19.5, 19.5, 107, 107, 13)}" fill="none" stroke="{ACCENT}" stroke-opacity="0.25" stroke-width="1"/>''')

# --- tabs (108x56, opaque x0..100 / x8..108, y8..56) with L2/R2 badge ---
def tab(name, flip, label):
    """160x72, drawn at the size the theme shows it (no stretching). The visible tab body is
    y 10..72; the badge is centred on it (y 41) so it lines up with the vertically centred text."""
    W, H, top = 160, 72, 10
    shape = f"M0,{top} H118 A30,30 0 0 1 148,{top + 30} V{H} H0 Z"
    tr = f' transform="translate({W} 0) scale(-1 1)"' if flip else ""
    bw, bh = 36, 24
    bx = W - 30 - bw if flip else 30      # the badge + word group sits centred in the visible body (x0..148)
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

# --- scroll bar: retro pixel-art style, drawn on the exact 1:1 pixel grid (600x28 at 660,438;
#     thumb 84x20). Only integer-aligned rects, so nanosvg's antialiasing never softens an edge.
def px(x, y, w, h, c):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{c}"/>'

def px_box(x, y, w, h, fill, light, dark, outline, bevel=2):
    """Beveled box with 1px notched corners, 16-bit style."""
    o = [px(x + 1, y, w - 2, 1, outline), px(x + 1, y + h - 1, w - 2, 1, outline),
         px(x, y + 1, 1, h - 2, outline), px(x + w - 1, y + 1, 1, h - 2, outline),
         px(x + 1, y + 1, w - 2, h - 2, fill),
         px(x + 1, y + 1, w - 2, bevel, light), px(x + 1, y + 1, bevel, h - 2, light),
         px(x + 1, y + h - 1 - bevel, w - 2, bevel, dark), px(x + w - 1 - bevel, y + 1, bevel, h - 2, dark)]
    return "\n".join(o)

def px_arrow(cx, cy, left, c, shadow, b=2):
    """Stair-stepped triangle in chunky b x b pixels (5 columns), with a drop shadow."""
    out = []
    for dx, col in ((b // 2, shadow), (0, c)):
        for k in range(5):
            n = (k + 1) if left else (5 - k)          # half-height in blocks
            x = cx - 5 * b // 2 + k * b + dx
            out.append(px(x, cy - n * b + b // 2 + dx, b, (2 * n - 1) * b, col))
    return "\n".join(out)

BLUE, BLUE_HI, BLUE_LO, BLUE_EDGE = "#2b8fe6", "#7cc8ff", "#1a5fa8", "#0a2f57"
W, H = 600, 28
track = [
    # recessed groove: dark outline, inner shadow on top, highlight on the bottom edge
    px(40, 7, 520, 1, "#08090b"), px(40, 20, 520, 1, "#08090b"),
    px(39, 8, 1, 12, "#08090b"), px(560, 8, 1, 12, "#08090b"),
    px(40, 8, 520, 12, "#16181b"), px(40, 8, 520, 2, "#0d0e10"),
    px(40, 21, 520, 1, "#3a3e45"),
    # dotted row, like the DSi menu bar
    *[px(x, 13, 2, 2, "#3b4048") for x in range(46, 556, 8)],
]
write("scroll_track.svg", W, H, "\n".join(track) + "\n"
      + px_box(0, 0, 32, 28, BLUE, BLUE_HI, BLUE_LO, BLUE_EDGE) + "\n"
      + px_box(W - 32, 0, 32, 28, BLUE, BLUE_HI, BLUE_LO, BLUE_EDGE) + "\n"
      + px_arrow(16, 14, True, "#ffffff", BLUE_EDGE) + "\n"
      + px_arrow(W - 16, 14, False, "#ffffff", BLUE_EDGE))

# thumb: chunky beveled block with grip lines at both ends (the label sits in the middle)
grips = [px(x, 6, 1, 8, c) for x0 in (6, 75) for x, c in ((x0, BLUE_HI), (x0 + 1, BLUE_EDGE), (x0 + 3, BLUE_HI), (x0 + 4, BLUE_EDGE))]
write("scroll_thumb.svg", 84, 20, px_box(0, 0, 84, 20, BLUE, BLUE_HI, BLUE_LO, BLUE_EDGE) + "\n" + "\n".join(grips))

# --- boot splash (640x480 per panel), shown by swayimg while ES loads hidden (rendered to rgds-splash.png) ---
# DSi-style, in the system font: top = the ROCKNIX logo with a soft glow and the device name; bottom = a DS game
# card with a ROCKNIX label in the blue selection frame, and "Loading" with progress dots.
SPLASH = os.path.join(THEME, "assets/images/splash")
os.makedirs(SPLASH, exist_ok=True)
grid640 = " ".join([f"M{x},0 V480" for x in range(0, 641, 20)] + [f"M0,{y} H640" for y in range(0, 481, 20)])
base = f'''<defs>{grad("bg", [(0, BG_TOP), (1, BG_BOT)])}
<radialGradient id="vig" cx="0.5" cy="0.45" r="0.75"><stop offset="0.55" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.35"/></radialGradient>
<radialGradient id="glow" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="{ACCENT_HI}" stop-opacity="0.24"/><stop offset="0.45" stop-color="{ACCENT}" stop-opacity="0.09"/><stop offset="1" stop-color="{ACCENT}" stop-opacity="0"/></radialGradient></defs>
<rect width="640" height="480" fill="url(#bg)"/>
<path d="{grid640}" stroke="#ffffff" stroke-opacity="0.04" stroke-width="1" fill="none"/>
<rect width="640" height="480" fill="url(#vig)"/>'''
logo = open(os.path.join(HERE, "rocknix_logo.paths")).read()
logo = logo.replace('fill="RED"', 'fill="url(#rk)"').replace('fill="GREY"', 'fill="url(#nx)"')
logo_defs = f'<defs>{grad("rk", [(0, "#ff6b6b"), (1, "#e8403f")])}{grad("nx", [(0, "#f2f3f5"), (1, "#c9ccd1")])}</defs>'
# top: logo 400 px wide (native 500 x ~95) centred at y 205, device name, divider
write("../splash/splash_top.svg", 640, 480, base + f'''
{logo_defs}
<ellipse cx="320" cy="205" rx="300" ry="130" fill="url(#glow)"/>
<g transform="translate(120 {205 - 38}) scale(0.8) translate(0 -53)">{logo}</g>
<path d="M200,268 H440" stroke="#ffffff" stroke-opacity="0.12" stroke-width="1.2"/>
{glyph_run("Anbernic RG DS", 320, 300, 15, "#a9afb8")}''')
# bottom: the DS card (100x104, drawn 1.6x) with the logo on its label, in the selection frame; Loading + dots
card = open(os.path.join(OUT, "game_slot.svg")).read().split(">", 1)[1].rsplit("</svg>", 1)[0]
lab = f'<rect x="12" y="21" width="76" height="66" fill="#fbfbf8"/><g transform="translate(18 {54 - 6}) scale(0.128) translate(0 -53)">{logo}</g>'
cx, cy, sc = 320, 190, 1.6
cw, ch = 100 * sc, 104 * sc
fw, fh = cw + 26, ch + 26
write("../splash/splash_bottom.svg", 640, 480, base + f'''
{logo_defs}{grad("sw", [(0, ACCENT_HI), (0.45, ACCENT), (1, ACCENT_LO)])}
<ellipse cx="{cx}" cy="{cy}" rx="220" ry="160" fill="url(#glow)"/>
<path d="{rr(cx - fw / 2, cy - fh / 2 + 4, fw, fh, 16)}" fill="#000000" fill-opacity="0.35"/>
<path d="{rr(cx - fw / 2, cy - fh / 2, fw, fh, 16)}" fill="url(#sw)" stroke="#0d3a6b" stroke-width="2.5"/>
<path d="{rr(cx - fw / 2 + 3, cy - fh / 2 + 3, fw - 6, fh - 6, 13)}" fill="none" stroke="#ffffff" stroke-opacity="0.35" stroke-width="1.5"/>
<path d="{rr(cx - cw / 2 - 5, cy - ch / 2 - 5, cw + 10, ch + 10, 9)}" fill="#14161a"/>
<g transform="translate({cx - cw / 2} {cy - ch / 2}) scale({sc})">{lab}{card}</g>
{glyph_run("Loading", 320, 345, 17, "#e8e9eb")}
<circle cx="296" cy="372" r="5" fill="{ACCENT_HI}"/>
<circle cx="320" cy="372" r="5" fill="{ACCENT_HI}" fill-opacity="0.6"/>
<circle cx="344" cy="372" r="5" fill="{ACCENT_HI}" fill-opacity="0.3"/>''')

print("polish ok")
