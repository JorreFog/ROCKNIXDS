#!/usr/bin/env python3
"""Frame a 1280x480 RG DS capture (top panel left, bottom panel right, as grim writes it) as a DS-style
clamshell: top screen above the bottom one. Transparent outside the body, so it suits light and dark pages.

usage: ds_frame.py in.png out.png [--scale 0.5]
"""
import sys
from PIL import Image, ImageDraw, ImageFilter

SS = 4                                   # supersampling for the body's curves
M, HINGE, R, BEZ = 34, 46, 44, 10        # side margin, hinge gap, corner radius, screen bezel


def rrect(draw, box, r, **kw):
    draw.rounded_rectangle([v * SS for v in box], radius=r * SS, **kw)


def frame(src):
    im = Image.open(src).convert("RGB")
    top, bot = im.crop((0, 0, 640, 480)), im.crop((640, 0, 1280, 480))
    W = 640 + 2 * M
    half = M + 480 + HINGE // 2          # each shell's height
    H = 2 * half
    sh = 18                              # room for the drop shadow
    out = Image.new("RGBA", (W + 2 * sh, H + 2 * sh), (0, 0, 0, 0))

    # soft shadow
    shadow = Image.new("L", ((W + 2 * sh) * SS, (H + 2 * sh) * SS), 0)
    rrect(ImageDraw.Draw(shadow), (sh, sh + 6, sh + W, sh + H + 6), R, fill=110)
    shadow = shadow.resize(out.size, Image.LANCZOS).filter(ImageFilter.GaussianBlur(9))
    out.paste(Image.new("RGBA", out.size, (0, 0, 0, 255)), (0, 0), shadow)

    body = Image.new("RGBA", ((W + 2 * sh) * SS, (H + 2 * sh) * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    for i, (y0, y1) in enumerate(((0, half - 3), (half + 3, H))):       # two shells with a hinge seam
        rrect(d, (sh, sh + y0, sh + W, sh + y1), R, fill=(22, 23, 26, 255))                        # edge
        rrect(d, (sh + 2, sh + y0 + 2, sh + W - 2, sh + y1 - 2), R - 2, fill=(52, 55, 61, 255))     # shell
        rrect(d, (sh + 3, sh + y0 + 3, sh + W - 3, sh + y1 - 3), R - 3, outline=(88, 92, 99, 255), width=SS)
    # hinge barrel
    rrect(d, (sh + 60, sh + half - 9, sh + W - 60, sh + half + 9), 9, fill=(34, 36, 40, 255), outline=(20, 21, 24, 255), width=SS)
    # screen bezels
    for y in (M, half + HINGE // 2):
        rrect(d, (sh + M - BEZ, sh + y - BEZ, sh + M + 640 + BEZ, sh + y + 480 + BEZ), 12, fill=(12, 13, 15, 255))
    body = body.resize(out.size, Image.LANCZOS)
    out.alpha_composite(body)
    out.paste(top, (sh + M, sh + M))
    out.paste(bot, (sh + M, sh + half + HINGE // 2))
    return out


if __name__ == "__main__":
    img = frame(sys.argv[1])
    if "--scale" in sys.argv:
        s = float(sys.argv[sys.argv.index("--scale") + 1])
        img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    img.save(sys.argv[2], optimize=True)
