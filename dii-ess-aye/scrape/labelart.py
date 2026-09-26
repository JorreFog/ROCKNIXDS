#!/usr/bin/env python3
"""labelart.py <front-cover.png> <out.png>

Makes a game card label image from a DS box cover: removes the case's white NINTENDO DS strip down the left edge
(found as the run of near-white columns from the left) and crops the rest to the label window's shape (76:66),
keeping the most detailed band (title logo, character), so the theme can fill the whole label with it. Output: 304x264 PNG, for ES's "cartridge" media.
"""
import sys
from PIL import Image, ImageFilter

LABEL_W, LABEL_H = 290, 264                  # the card art window as drawn (74.2x67.6 px)


def strip_width(img):
    """Columns from the left that are mostly near-white (the NINTENDO DS band), capped at 20% of the width."""
    g = img.convert("L")
    w, h = g.size
    px = g.load()
    limit = int(w * 0.20)
    x = 0
    while x < limit:
        bright = sum(1 for y in range(0, h, 2) if px[x, y] > 200)
        if bright < 0.55 * (h // 2):            # the band is white apart from its logo: mostly bright
            break
        x += 1
    # a band is at least a few percent wide; anything thinner is just a light border. The band is ~13.5% of the
    # width on DS covers, but a big dark DS logo inside it can end the bright run early (Black 2): use at least that
    return max(x, int(w * 0.135)) if x > w * 0.04 else 0


def main():
    img = Image.open(sys.argv[1]).convert("RGB")
    sx = strip_width(img)
    if sx:
        img = img.crop((sx + 2, 0, img.width, img.height))   # +2: the band's edge line
    # centre-crop to the label's aspect
    target = LABEL_W / LABEL_H
    w, h = img.size
    if w / h > target:
        nw = int(h * target)
        img = img.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:
        # too tall: keep the band of rows with the most detail (edges), which holds the title logo and the
        # character on these covers, instead of a blind centre crop that cut logos off
        nh = int(w / target)
        edges = img.convert("L").filter(ImageFilter.FIND_EDGES)
        row = [sum(i * c for i, c in enumerate(edges.crop((0, y, w, y + 1)).histogram())) for y in range(h)]
        best, best_y, acc = -1, (h - nh) // 2, sum(row[:nh])
        for y in range(0, h - nh + 1):
            if y:
                acc += row[y + nh - 1] - row[y - 1]
            if acc > best:
                best, best_y = acc, y
        img = img.crop((0, best_y, w, best_y + nh))
    img.resize((LABEL_W, LABEL_H), Image.LANCZOS).save(sys.argv[2])
    print(f"{sys.argv[1]}: strip {sx}px")


if __name__ == "__main__":
    main()
