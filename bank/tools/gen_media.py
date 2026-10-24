#!/usr/bin/env python3
"""Poketool's pictures (formerly ROCKNIXDS Bank & Trade's), drawn here in BananaOS's own look: pixel art at whole-pixel
steps, outlined in black, on the menu's dark dotted sheet, in its banana yellows and cream, with the words in Pixelify
Sans (the app's and the menu's font). The logo is a capture ball of our own, banana yellow over cream, with a spanner
behind it: a tool for Pokémon. No game's artwork is used.

    python3 gen_media.py <out dir> [<font.ttf>]

writes, at the sizes and names the menu and the Store expect:
    bank-image.png    640x480  the cover on the top screen       } device/media (the package's menu pictures)
    bank-thumb.png    640x480  the cartridge's label (the menu   }
                               shows its middle about 90 px wide)
    bank-marquee.png  400x164  the title strip                   }
    icon.png          32x32    the Store's icon (RGBA)           } device/logo (BananaOS's store/apps/bank, pages)
    logo-256.png      256x256  the logo alone, 8x (RGBA)         }
Needs Pillow. The font defaults to ../assets/fonts/PixelifySans-Medium.ttf (tools/fetch-assets.sh puts it there).
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))


def rgb(h):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


# BananaOS's colours: the menu's sheet and dots, the banana (ui/assets/banana), its cream, the cards' teal
INK = rgb('000000')
SHEET, SHEET_DOT, SHEET_DEEP = rgb('17161c'), rgb('24232b'), rgb('0b0a0d')
B_HI, B_LIGHT, B_MAIN, B_SHADE, B_DEEP = rgb('fff4aa'), rgb('ffd640'), rgb('f6b21c'), rgb('d68412'), rgb('965210')
CREAM, CREAM_SHADE, CREAM_DEEP = rgb('efe6cf'), rgb('cfc5ab'), rgb('9d937c')
STEEL_HI, STEEL, STEEL_SHADE = rgb('dfe6ea'), rgb('a9b4bb'), rgb('6b767e')
TEAL, TEAL_LIGHT = rgb('234346'), rgb('2d4a4c')
WORD_YELLOW, WORD_CREAM, MUTED = rgb('ffde52'), rgb('efe6cf'), rgb('8f8a7a')


def logo():
    """The 32x32 logo, RGBA: a spanner behind a capture ball."""
    n = 32
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    px = img.load()

    def put(x, y, c):
        if 0 <= x < n and 0 <= y < n:
            px[x, y] = c + (255,)

    # the spanner, corner to corner behind the ball: a ring at the handle's end (bottom left), an open jaw facing the
    # top right corner
    def spanner():
        cells = set()
        hx, hy, fx, fy = 26.6, 5.0, 5.0, 26.6
        ux, uy = 1 / math.sqrt(2), -1 / math.sqrt(2)  # toward the top right
        for y in range(n):
            for x in range(n):
                px_, py_ = x + .5, y + .5
                # the handle: a bar between the jaw and the ring
                vx, vy = hx - fx, hy - fy
                t = max(0.0, min(1.0, ((px_ - fx) * vx + (py_ - fy) * vy) / (vx * vx + vy * vy)))
                bar = math.hypot(px_ - (fx + t * vx), py_ - (fy + t * vy)) < 2.0
                # the jaw: a disc with a slot cut from its middle out to the corner
                dh = math.hypot(px_ - hx, py_ - hy)
                along = (px_ - hx) * ux + (py_ - hy) * uy
                across = (px_ - hx) * -uy + (py_ - hy) * ux
                slot = along > -0.6 and abs(across) < 1.3
                jaw = dh < 4.9 and not slot
                # the ring: a disc with a hole
                df = math.hypot(px_ - fx, py_ - fy)
                ring = 1.6 < df < 3.9
                if (bar and df >= 1.6 and not (dh < 4.9 and slot)) or jaw or ring:
                    cells.add((x, y))
        for (x, y) in cells:
            edge = any((x + dx, y + dy) not in cells for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if edge:
                put(x, y, INK)
            else:
                lit = x - y  # lit from the top: toward the top right corner is lighter
                put(x, y, STEEL_HI if lit > 14 else STEEL if lit > -24 else STEEL_SHADE)

    spanner()

    # the ball
    cx = cy = 15.5
    r = 12.6
    for y in range(n):
        for x in range(n):
            dx, dy = x + .5 - cx - .0, y + .5 - cy
            d = math.hypot(dx, dy)
            if d > r:
                continue
            if d > r - 1.25:
                put(x, y, INK)
                continue
            # the band across the middle and the button in it
            bd = math.hypot(dx, dy)
            if bd < 2.6:
                put(x, y, B_HI if (dx < -0.5 and dy < -0.5) else CREAM)
                continue
            if bd < 4.3:
                put(x, y, INK)
                continue
            if abs(dy) < 1.5:
                put(x, y, INK)
                continue
            # light from the top left: brighter toward it, darker toward the bottom right rim
            lx, ly = -0.55, -0.65
            shade = (dx * lx + dy * ly) / r  # 1 at the lit side, -1 at the far side
            rim = d / r
            if dy < 0:
                if (x, y) in ((8, 8), (9, 7), (10, 7), (8, 9), (9, 8)):
                    c = B_HI  # the shine
                elif shade > 0.25:
                    c = B_LIGHT
                elif shade > -0.35 or rim < 0.7:
                    c = B_MAIN
                elif rim > 0.86:
                    c = B_DEEP
                else:
                    c = B_SHADE
            else:
                if shade > -0.1 or rim < 0.62:
                    c = CREAM
                elif rim > 0.86:
                    c = CREAM_DEEP
                else:
                    c = CREAM_SHADE
            put(x, y, c)
    return img


def sheet(w, h):
    """The menu's backdrop: a dark sheet with its fine dots."""
    img = Image.new('RGB', (w, h), SHEET)
    d = ImageDraw.Draw(img)
    for y in range(0, h, 4):
        for x in range((y // 4 % 2) * 2, w, 4):
            d.point((x, y), SHEET_DOT)
    return img


def card(img, x0, y0, x1, y1, k):
    """A card as the menu draws them: teal, a cream frame, a drop shadow, corners cut at whole pixels."""
    d = ImageDraw.Draw(img)
    d.rectangle((x0 + k, y1 + 1, x1 + k, y1 + k * 2), SHEET_DEEP)  # the shadow under it
    d.rectangle((x0 + k, y0, x1 - k, y1), WORD_CREAM)
    d.rectangle((x0, y0 + k, x1, y1 - k), WORD_CREAM)
    d.rectangle((x0 + k, y0 + k, x1 - k, y1 - k), TEAL)
    for y in range(y0 + k, y1 - k + 1, 4):
        for x in range(x0 + k + (y // 4 % 2) * 2, x1 - k + 1, 4):
            d.point((x, y), TEAL_LIGHT)


def word(text, size, colour, font, bold=True):
    """Text in Pixelify Sans on its own pixel grid (no smoothing), 1-bit, as an RGBA image to scale by whole steps."""
    f = ImageFont.truetype(font, size)
    l, t, r, b = f.getbbox(text)
    m = Image.new('1', (r - l + 3, b - t + 3), 0)
    dr = ImageDraw.Draw(m)
    dr.fontmode = '1'
    dr.text((1 - l, 1 - t), text, font=f, fill=1)
    if bold:
        dr.text((2 - l, 1 - t), text, font=f, fill=1)  # the menu's wordmark is set heavy: one more column
    out = Image.new('RGBA', m.size, (0, 0, 0, 0))
    out.paste(colour + (255,), mask=m)
    return out


def words(parts, size, font):
    """Several coloured runs side by side ("POKE" yellow, "TOOL" cream), as the menu's BANANA/OS wordmark."""
    imgs = [word(t, size, c, font) for t, c in parts]
    w = sum(i.width for i in imgs) - 2 * (len(imgs) - 1)
    h = max(i.height for i in imgs)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    x = 0
    for i in imgs:
        out.alpha_composite(i, (x, h - i.height))
        x += i.width - 2
    return out


def outlined(img):
    """A black outline round a sprite, a pixel wide, and a shadow a pixel down and right (the menu's style)."""
    w, h = img.size
    out = Image.new('RGBA', (w + 3, h + 3), (0, 0, 0, 0))
    a = img.split()[3]
    black = Image.new('RGBA', img.size, INK + (255,))
    for dx, dy in ((0, 1), (1, 0), (1, 2), (2, 1), (2, 2), (1, 1), (0, 0), (2, 0), (0, 2)):
        out.paste(black, (dx, dy), a)
    out.alpha_composite(img, (1, 1))
    return out


def place(dst, src, k, cx, y):
    big = src.resize((src.width * k, src.height * k), Image.NEAREST)
    x = cx - big.width // 2
    dst.paste(big, (x, y), big)
    return big.width, big.height


def cover(path, mark, font):
    img = sheet(640, 480)
    card(img, 40, 24, 599, 455, 4)
    place(img, mark, 7, 320, 46)
    title = outlined(words([('POKE', WORD_YELLOW), ('TOOL', WORD_CREAM)], 13, font))
    place(img, title, 5, 320, 286)
    sub = word('BANK · EDITOR · TRADE', 10, MUTED, font, bold=False)
    place(img, sub, 3, 320, 372)
    # a row of a box: the cards' slots, a few with a ball in them
    d = ImageDraw.Draw(img)
    for i in range(7):
        x = 320 - (7 * 52 - 8) // 2 + i * 52
        d.rectangle((x, 414, x + 43, 445), SHEET_DEEP)
        d.rectangle((x + 2, 416, x + 41, 443), TEAL_LIGHT)
        if i in (0, 2, 3, 5):
            dot = mark.resize((32, 32), Image.NEAREST)
            img.paste(dot, (x + 6, 414), dot)
    img.save(path)


def label(path, mark, font):
    # the cartridge: the logo large, one word under it in letters that survive being shown 90 px wide
    img = sheet(640, 480)
    place(img, mark, 10, 320, 10)
    title = outlined(words([('POKE', WORD_YELLOW), ('TOOL', WORD_CREAM)], 13, font))
    place(img, title, 6, 320, 344)
    img.save(path)


def marquee(path, mark, font):
    img = sheet(400, 164)
    w = mark.width * 3
    title = outlined(words([('POKE', WORD_YELLOW), ('TOOL', WORD_CREAM)], 13, font))
    tw = title.width * 4
    x0 = (400 - (w + 16 + tw)) // 2
    big = mark.resize((w, w), Image.NEAREST)
    img.paste(big, (x0, (164 - w) // 2), big)
    t = title.resize((tw, title.height * 4), Image.NEAREST)
    img.paste(t, (x0 + w + 16, (164 - t.height) // 2), t)
    img.save(path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    font = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'assets', 'fonts', 'PixelifySans-Medium.ttf')
    os.makedirs(out, exist_ok=True)
    mark = logo()
    mark.save(os.path.join(out, 'icon.png'))
    mark.resize((256, 256), Image.NEAREST).save(os.path.join(out, 'logo-256.png'))
    cover(os.path.join(out, 'bank-image.png'), mark, font)
    label(os.path.join(out, 'bank-thumb.png'), mark, font)
    marquee(os.path.join(out, 'bank-marquee.png'), mark, font)


if __name__ == '__main__':
    main()
