#!/usr/bin/env python3
"""ra_panel.py <ra.json> <badge dir> <font> <outdir>

Renders each game's RetroAchievements strip for the game list's top screen from ra-fetch.py's output: the game's
badge, "RetroAchievements", "N of M achievements", a progress bar and the points. 1120x144 transparent PNG (shown
at 560x72, drawn at 2x so it stays sharp), written as <outdir>/<ES game id>.png for ES's "wheel" media.
"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 1120, 144
BLUE, WHITE, GREY, GOLD = (143, 200, 255, 255), (236, 238, 241, 255), (150, 156, 166, 255), (255, 206, 74, 255)
TRACK, EDGE = (22, 24, 28, 255), (70, 75, 83, 255)


def rounded(img, r):
    m = Image.new("L", img.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, img.width - 1, img.height - 1], r, fill=255)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (0, 0), m)
    return out


def main():
    data = json.load(open(sys.argv[1]))
    badges, font_path, out = sys.argv[2], sys.argv[3], sys.argv[4]
    os.makedirs(out, exist_ok=True)
    f_small = ImageFont.truetype(font_path, 26)
    f_big = ImageFont.truetype(font_path, 40)
    for gid, g in data.items():
        if not g.get("ra") or not g.get("total"):
            continue
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        # badge: 128x128, rounded, with a thin rim
        bp = os.path.join(badges, "%d.png" % g["ra"])
        x = 8
        if os.path.exists(bp):
            b = rounded(Image.open(bp).convert("RGBA").resize((128, 128), Image.LANCZOS), 18)
            d.rounded_rectangle([x - 3, 5, x + 131, 139], 21, fill=EDGE)
            img.alpha_composite(b, (x, 8))
            x += 152
        # text
        d.text((x, 6), "RetroAchievements", font=f_small, fill=BLUE)
        line = "%d of %d achievements" % (g["unlocked"], g["total"])
        d.text((x, 38), line, font=f_big, fill=WHITE)
        pts = "%d / %d points" % (g["unlocked_points"], g["points"])
        pw = d.textlength(pts, font=f_small)
        # progress bar
        bx0, bx1, by0, by1 = x, W - 12, 104, 126
        d.rounded_rectangle([bx0, by0, bx1, by1], 11, fill=TRACK, outline=EDGE, width=2)
        frac = g["unlocked"] / g["total"]
        if frac > 0:
            fx = bx0 + 4 + max(18, int((bx1 - bx0 - 8) * frac))
            d.rounded_rectangle([bx0 + 4, by0 + 4, fx, by1 - 4], 7, fill=GOLD)
        d.text((W - 12 - pw, 12), pts, font=f_small, fill=GREY)
        img.save(os.path.join(out, gid + ".png"))
        print(g["name"], line, pts)


if __name__ == "__main__":
    main()
