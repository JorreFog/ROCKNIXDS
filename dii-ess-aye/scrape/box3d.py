#!/usr/bin/env python3
"""box3d.py <front-cover.png> <out.png> [font.ttf|otf]

Renders a 3D Nintendo DS game case from a flat front cover (e.g. a libretro-thumbnails Named_Boxarts image):
the cover on the front face turned slightly to the right, the case's white spine on the left with a vertical
NINTENDO DS mark, plastic edge shading and sheen, and a soft drop shadow. Output: 420x460 RGBA PNG, for ES's
"boxart" media (the theme shows it on the game list's top screen).
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 420, 460
# front face corners (TL, TR, BR, BL): left edge nearer (taller), right edge further (shorter)
FRONT = [(118, 34), (392, 62), (392, 398), (118, 426)]
# spine: its far (left) edge is further away, so shorter
SPINE = [(76, 58), (118, 34), (118, 426), (76, 402)]


def coeffs(dst, src):
    """PIL PERSPECTIVE coefficients mapping output (dst) points to input (src) points."""
    A, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    return solve(A, b)


def solve(A, b):
    """A x = b for the 8x8 system above, Gaussian elimination with partial pivoting (no numpy: this also runs on
    the device, whose Python has no numpy)."""
    n = len(b)
    M = [list(map(float, row)) + [float(v)] for row, v in zip(A, b)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        for r in range(c + 1, n):
            f = M[r][c] / M[c][c]
            for k in range(c, n + 1):
                M[r][k] -= f * M[c][k]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        x[r] = (M[r][n] - sum(M[r][k] * x[k] for k in range(r + 1, n))) / M[r][r]
    return x


def warp(img, quad):
    w, h = img.size
    return img.transform((W, H), Image.PERSPECTIVE, coeffs(quad, [(0, 0), (w, 0), (w, h), (0, h)]),
                         Image.BICUBIC, fillcolor=(0, 0, 0, 0))


def quad_mask(quad):
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).polygon(quad, fill=255)
    return m


def main():
    cover = Image.open(sys.argv[1]).convert("RGBA")
    font_path = sys.argv[3] if len(sys.argv) > 3 else None
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # shadow: the case outline, dropped and blurred
    sh = Image.new("L", (W, H), 0)
    ImageDraw.Draw(sh).polygon([(p[0] + 10, p[1] + 14) for p in SPINE[:1] + FRONT[1:3] + [SPINE[3]]], fill=150)
    sh = sh.filter(ImageFilter.GaussianBlur(12))
    out.paste((0, 0, 0, 255), (0, 0), sh)

    # spine: white plastic, lit from the front-left, with a vertical NINTENDO DS mark near the top
    sw, shh = 44, 392
    spine = Image.new("RGBA", (sw, shh))
    px = spine.load()
    for x in range(sw):
        t = x / (sw - 1)
        base = 214 + 26 * (1 - abs(t - 0.35) / 0.65)          # brightest a third of the way in
        for y in range(shh):
            v = int(base - 18 * (y / shh))
            px[x, y] = (v, v, min(255, v + 4), 255)
    d = ImageDraw.Draw(spine)
    d.rectangle([0, 0, sw - 1, 30], fill=(38, 40, 44, 255))  # the dark DS band at the top of a real spine
    if font_path:
        try:
            f = ImageFont.truetype(font_path, 15)
            txt = Image.new("RGBA", (220, 24), (0, 0, 0, 0))
            ImageDraw.Draw(txt).text((0, 2), "NINTENDO DS", font=f, fill=(58, 60, 66, 255))
            txt = txt.crop(txt.getbbox()).rotate(90, expand=True)
            spine.alpha_composite(txt, ((sw - txt.width) // 2, 44))
        except OSError:
            pass
    out.alpha_composite(warp(spine, SPINE))
    # spine shading: darker toward its far edge
    shade = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(shade)
    for i in range(20):
        x = SPINE[0][0] + i
        sd.line([(x, 0), (x, H)], fill=int(90 * (1 - i / 20)))
    out.paste((0, 0, 0, 255), (0, 0), Image.composite(shade, Image.new("L", (W, H), 0), quad_mask(SPINE)))

    # front: the cover inside a thin white case rim
    rim = Image.new("RGBA", (cover.width + 24, cover.height + 24), (236, 237, 240, 255))
    rim.alpha_composite(cover, (12, 12))
    front = warp(rim, FRONT)
    out.alpha_composite(front)
    fm = quad_mask(FRONT)
    # far-edge darkening and a diagonal plastic sheen
    g = Image.new("L", (W, H), 0)
    gd = ImageDraw.Draw(g)
    for x in range(FRONT[0][0], FRONT[1][0] + 1):
        t = (x - FRONT[0][0]) / (FRONT[1][0] - FRONT[0][0])
        gd.line([(x, 0), (x, H)], fill=int(70 * t ** 1.6))
    out.paste((0, 0, 0, 255), (0, 0), Image.composite(g, Image.new("L", (W, H), 0), fm))
    sheen = Image.new("L", (W, H), 0)
    ImageDraw.Draw(sheen).polygon([(118, 34), (230, 45), (118, 230)], fill=70)
    sheen = sheen.filter(ImageFilter.GaussianBlur(18))
    out.paste((255, 255, 255, 255), (0, 0), Image.composite(sheen, Image.new("L", (W, H), 0), fm))
    # crisp edges: the spine/front corner highlight and a thin outline
    d = ImageDraw.Draw(out)
    d.line([FRONT[0], FRONT[3]], fill=(255, 255, 255, 170), width=2)
    d.line(FRONT + [FRONT[0]], fill=(20, 21, 24, 140), width=1)
    d.line([SPINE[0], SPINE[3]], fill=(20, 21, 24, 120), width=1)
    out.save(sys.argv[2])


if __name__ == "__main__":
    main()
