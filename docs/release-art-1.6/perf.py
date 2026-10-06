"""ROCKNIXDS against stock ROCKNIX: the 3D stress ramp at 2x, and the measured wins beside it.

Every number is from the README's "What was achieved" table and docs/img/stress-ramp.svg (the stress ROM, one
level per 300 frames, DraStic at 2x internal resolution, the same handheld): stock ROCKNIX (sway + GL) against
libdsflip, both with DraStic's own renderer. Gengis Engine, 1.6's default, comes on top of that and is only quoted."""
from lib import *

# stress-ramp.svg's polylines, back to fps (y = 330 at 35 fps, 53.7 px per 5 fps)
LEVELS = [192 * (i + 1) for i in range(10)]
def fps(ys):
    return [round(35 + (330 - y) / 10.74, 1) for y in ys]
STOCK = fps([61.5, 61.5, 63.6, 75.4, 100.1, 149.6, 191.4, 229.0, 261.3, 287.0])
OURS = fps([61.5, 62.6, 62.6, 61.5, 63.6, 65.8, 67.9, 91.6, 127.0, 164.6])
STOCK[-1], OURS[-1] = 39.0, 50.4  # the labelled end points

C_STOCK, C_OURS = RED, BLUE  # validated pair on the dark surface (dataviz validate_palette.js: all checks pass)

W, H = 680, 360
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG0), (1, BG2)], bands=8)
a.stars(0, 0, W, 50, 50, seed=60)

# header
lw = logo(a, 16, 16, 20)
a.text(16 + lw + 10, 12, "vs stock ROCKNIX", 16, INK2, medium=True)
a.text(16, 42, "DS games at 2x internal resolution, on the same handheld", 12, INK3)
a.text(W - 16, 12, "FASTER", 32, WHITE, anchor="ra", medium=True, shadow=C_OURS, sh=(2, 2))

# ---- the chart ------------------------------------------------------------------------------------------------
px0, py0, pw, ph = 16, 62, 382, 270
a.panel(px0, py0, pw, ph, fill=BG2, border=BEZEL, hi=BG4)
a.text(px0 + 10, py0 + 6, "3D stress test: emulated fps as the polygons pile up", 12, INK, medium=True)
cx0, cx1, cy0, cy1 = px0 + 34, px0 + pw - 66, py0 + 34, py0 + ph - 40
def X(i):
    return int(round(cx0 + (cx1 - cx0) * i / 9))
def Y(f):
    return int(round(cy1 - (cy1 - cy0) * (f - 35) / 25))
# recessive grid and y labels
for f in (35, 40, 45, 50, 55, 60):
    y = Y(f)
    for x in range(cx0, cx1 + 1, 2 if f == 60 else 4):
        a.px(x, y, INK3 if f == 60 else BG4)
    a.text(cx0 - 6, y - 7, str(f), 12, INK3, anchor="ra")
a.text(cx1 + 4, Y(60) - 7, "60 fps", 12, INK2)
# x labels: every other level
for i in range(0, 10):
    if i in (0, 4, 6, 9):  # 192, 960, 1344, 1920 (the pixel font's 7 is close to its 1)
        a.text(X(i), cy1 + 6, f"{LEVELS[i]}", 12, INK3, anchor="ma")
a.text((cx0 + cx1) // 2, cy1 + 20, "polygons per frame", 12, INK3, anchor="ma")

# the frames won, between the two lines: a dithered fill in our colour
for i in range(9):
    for x in range(X(i), X(i + 1) + 1):
        t = (x - X(i)) / max(X(i + 1) - X(i), 1)
        yt = Y(OURS[i] + (OURS[i + 1] - OURS[i]) * t)
        yb = Y(STOCK[i] + (STOCK[i + 1] - STOCK[i]) * t)
        if yb - yt > 3:
            a.dither_rect(x, yt + 2, 1, yb - yt - 3, mix(C_OURS, BG2, 0.35), 6)

def series(vals, col):
    pts = [(X(i), Y(v)) for i, v in enumerate(vals)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        a.d.line([(x0, y0), (x1, y1)], fill=col, width=2)
    for x, y in pts:
        a.rect(x - 2, y - 2, 5, 5, BG2)       # surface ring
        a.rect(x - 1, y - 1, 3, 3, col)
series(STOCK, C_STOCK)
series(OURS, C_OURS)
# direct labels at the right end (text in ink, a colour chip beside it carries identity)
for val, name, col, dy in ((OURS[-1], "ROCKNIXDS", C_OURS, -8), (STOCK[-1], "stock", C_STOCK, -6)):
    y = Y(val) + dy
    a.rect(cx1 + 6, y + 4, 6, 6, col)
    a.text(cx1 + 15, y, f"{val}", 12, WHITE, medium=True)
    a.text(cx1 + 15, y + 12, name, 12, INK2)
# the headline gap
gx = X(9) - 4
a.text(gx - 4, Y(45.2) - 6, "+29%", 16, WHITE, anchor="ra", medium=True, shadow=BG0)

# ---- the stat tiles -------------------------------------------------------------------------------------------
TILES = [
    ("2.3x", "more 3D at a locked 60 fps", "1344 polygons. Stock held about 580"),
    ("40x", "fewer dropped frames", "HeartGold: 5.2 a second on stock, 0.13 now"),
    ("0.3 ms", "display work per frame", "3.6 ms on stock (GL upload and sway)"),
    ("60.000 Hz", "panels: every frame shown once", "Stock's 60.10 Hz repeats one every 10 s"),
]
tx, tw, th = 410, W - 410 - 16, 62
for k, (big, l1, l2) in enumerate(TILES):
    y = py0 + k * (th + 7)
    a.panel(tx, y, tw, th, fill=BG3 if k == 0 else BG2, border=C_OURS if k == 0 else BEZEL, hi=BG4)
    a.text(tx + 10, y + 5, big, 24, BLUE_LT, medium=True, shadow=BG0)
    a.text(tx + 10, y + 32, l1, 12, INK, medium=True)
    a.text(tx + 10, y + 45, l2, 12, INK3)

a.text(16, H - 20, "Both lines use DraStic's own renderer. Gengis Engine, 1.6's default, takes about 12% more CPU work "
       "off on top.", 12, INK3)
a.render(os.path.join(OUT, "performance.png"))
