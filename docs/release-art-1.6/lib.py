"""Pixel-art toolkit for the 1.6 release artwork.

Art is drawn on a low-resolution base canvas (one base pixel = one "art pixel") in ROCKNIXDS Pixel's palette and
font, then scaled up with nearest-neighbour. Screenshots are pasted afterwards at the final resolution, so they stay
sharp inside the chunky frames.
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FONT_DIR = os.path.join(HERE, "ref/fonts")  # 1.6's Pixelify Sans (5/S, 2/Z, B/G told apart: #34)
REF = os.path.join(HERE, "ref")
OUT = os.path.join(REPO, "docs/img/1.6")

# ROCKNIXDS Pixel's palette (sampled from the theme's renders and its mockup).
BG0 = (7, 10, 14)
BG1 = (12, 13, 15)
BG2 = (19, 25, 33)
BG3 = (29, 39, 51)
BG4 = (46, 58, 72)
BEZEL = (52, 55, 61)
BEZEL_HI = (78, 82, 90)
BEZEL_LO = (34, 36, 40)
TEAL = (32, 69, 73)
TEAL_HI = (52, 98, 102)
TEAL_LIGHT = (192, 222, 222)
INK = (230, 235, 240)
INK2 = (147, 160, 176)
INK3 = (102, 117, 138)
BLUE = (61, 142, 196)
BLUE_DK = (26, 110, 163)
BLUE_LT = (126, 200, 238)
GREEN = (60, 224, 122)
RED = (245, 48, 63)
RED_DK = (150, 22, 34)
GREY = (143, 143, 143)
YELLOW = (255, 206, 84)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

NOLIGA = ["-liga", "-clig", "-calt"]  # Pixelify Sans joins "fi" into one glyph that reads as "A"
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def font(size, medium=False):
    return ImageFont.truetype(os.path.join(FONT_DIR, "PixelifySans-Medium.ttf" if medium else
                                           "PixelifySans-Regular.ttf"), size)


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


class Art:
    def __init__(self, w, h, scale=2, bg=BG2):
        self.w, self.h, self.scale = w, h, scale
        self.im = Image.new("RGBA", (w, h), bg + (255,))
        self.d = ImageDraw.Draw(self.im)
        self.d.fontmode = "1"
        self.overlays = []  # (image, base x, base y, base w, base h)

    # ---- pixels -----------------------------------------------------------------------------------------------
    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.im.putpixel((int(x), int(y)), tuple(c[:3]) + (255,))

    def rect(self, x, y, w, h, c):
        if w > 0 and h > 0:
            self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=tuple(c))

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.rect(x, y, 1, h, c)

    def frame(self, x, y, w, h, c, t=1):
        self.rect(x, y, w, t, c); self.rect(x, y + h - t, w, t, c)
        self.rect(x, y, t, h, c); self.rect(x + w - t, y, t, h, c)

    def rrect(self, x, y, w, h, fill=None, outline=None, r=2, t=1):
        """A rounded rectangle with stepped pixel corners (r = 1..4)."""
        steps = {1: [1], 2: [2, 1], 3: [3, 1, 1], 4: [4, 2, 1, 1], 5: [5, 3, 2, 1, 1], 6: [6, 4, 2, 2, 1, 1]}[r]
        if outline is not None:
            self._rr_fill(x, y, w, h, steps, outline)
            if fill is not None:
                inner = [max(s - 1, 0) for s in steps[1:]] if t == 1 else [max(s - t, 0) for s in steps[t:]]
                self._rr_fill(x + t, y + t, w - 2 * t, h - 2 * t, inner or [0], fill)
            else:
                pass
        elif fill is not None:
            self._rr_fill(x, y, w, h, steps, fill)

    def _rr_fill(self, x, y, w, h, steps, c):
        for i in range(h):
            k = 0
            if i < len(steps):
                k = steps[i]
            elif h - 1 - i < len(steps):
                k = steps[h - 1 - i]
            self.hline(x + k, y + i, w - 2 * k, c)

    def panel(self, x, y, w, h, fill=BG2, border=BEZEL, hi=None, shadow=BG0, r=3):
        """The Pixel theme's panel: a rounded box, a 1 px border and a hard drop shadow."""
        if shadow is not None:
            self.rrect(x + 2, y + 2, w, h, fill=shadow, r=r)
        self.rrect(x, y, w, h, fill=fill, outline=border, r=r)
        if hi is not None:
            self.hline(x + r, y + 1, w - 2 * r, hi)

    # ---- gradients and texture --------------------------------------------------------------------------------
    def vgrad(self, x, y, w, h, stops, bands=None, dither=True):
        """A vertical gradient through [(t, colour)...], Bayer-dithered between neighbouring bands."""
        bands = bands or max(2, h // 6)
        cols = [self._grad_at(stops, i / (bands - 1)) for i in range(bands)]
        for j in range(h):
            f = j / max(h - 1, 1) * (bands - 1)
            i0 = min(int(f), bands - 2)
            frac = f - i0
            for i in range(w):
                thr = (BAYER4[(y + j) % 4][(x + i) % 4] + 0.5) / 16
                c = cols[i0 + 1] if (dither and frac > thr) or (not dither and frac > 0.5) else cols[i0]
                self.im.putpixel((x + i, y + j), c + (255,))

    @staticmethod
    def _grad_at(stops, t):
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1:
                return mix(c0, c1, (t - t0) / max(t1 - t0, 1e-6))
        return stops[-1][1]

    def dither_rect(self, x, y, w, h, c, level):
        """Fill level/16 of the pixels of a rectangle with c (ordered dither)."""
        for j in range(h):
            for i in range(w):
                if BAYER4[(y + j) % 4][(x + i) % 4] < level:
                    self.px(x + i, y + j, c)

    def stars(self, x, y, w, h, n, seed=1, cols=(INK, INK2, INK3, BLUE_LT)):
        rnd = random.Random(seed)
        for _ in range(n):
            sx, sy = x + rnd.randrange(w), y + rnd.randrange(h)
            c = rnd.choice(cols)
            self.px(sx, sy, c)
            if rnd.random() < 0.07:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    self.px(sx + dx, sy + dy, mix(c, BG1, 0.55))

    def scanlines(self, x, y, w, h, c=BG0, alpha=0.18, step=2):
        for j in range(y, y + h, step):
            for i in range(x, x + w):
                p = self.im.getpixel((i, j))[:3]
                self.im.putpixel((i, j), mix(p, c, alpha) + (255,))

    # ---- text -------------------------------------------------------------------------------------------------
    def text(self, x, y, s, size=12, c=INK, anchor="la", medium=False, shadow=None, outline=None, sh=(1, 1)):
        f = font(size, medium)
        if outline is not None:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx or dy:
                        self.d.text((x + dx, y + dy), s, font=f, fill=outline, anchor=anchor, features=NOLIGA)
        if shadow is not None:
            self.d.text((x + sh[0], y + sh[1]), s, font=f, fill=shadow, anchor=anchor, features=NOLIGA)
        self.d.text((x, y), s, font=f, fill=c, anchor=anchor, features=NOLIGA)
        return self.d.textlength(s, font=f, features=NOLIGA)

    def tlen(self, s, size=12, medium=False):
        return self.d.textlength(s, font=font(size, medium), features=NOLIGA)

    def wrap(self, x, y, s, width, size=12, c=INK, lh=None, medium=False):
        lh = lh or size + 3
        words, line = s.split(), ""
        for wd in words:
            t = (line + " " + wd).strip()
            if self.tlen(t, size, medium) > width and line:
                self.text(x, y, line, size, c, medium=medium); y += lh; line = wd
            else:
                line = t
        if line:
            self.text(x, y, line, size, c, medium=medium); y += lh
        return y

    # ---- sprites ----------------------------------------------------------------------------------------------
    def sprite(self, x, y, rows, pal, k=1):
        """rows: strings; pal: {char: colour}; '.' and ' ' are transparent; k: integer magnification."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in pal:
                    self.rect(x + i * k, y + j * k, k, k, pal[ch])

    def paste_base(self, img, x, y):
        """Paste a small image onto the base canvas (it becomes chunky pixels like the rest)."""
        img = img.convert("RGBA")
        self.im.alpha_composite(img, (int(x), int(y)))

    # ---- overlays at final resolution -------------------------------------------------------------------------
    def overlay(self, img, x, y, w, h, resample=Image.LANCZOS):
        """Paste img into the base rectangle (x, y, w, h), drawn at the final resolution."""
        self.overlays.append((img, x, y, w, h, resample))

    def render(self, path):
        s = self.scale
        out = self.im.resize((self.w * s, self.h * s), Image.NEAREST)
        for img, x, y, w, h, rs in self.overlays:
            im = img.convert("RGBA")
            if rs is not None:  # None: already at the final size
                im = im.resize((int(w * s), int(h * s)), rs)
            out.alpha_composite(im, (int(x * s), int(y * s)))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        out.convert("RGB").save(path, optimize=True)
        return out


# ---- shared pieces ------------------------------------------------------------------------------------------------

LOGO_SVG = os.path.join(REPO, "logo/rocknixds-logo.svg")
LOGO_FONT = os.path.join(REPO, "logo/Unbounded-VF.ttf")
LOGO_ASPECT = 933.22 / 139.0


def logo(a, x, y, h=16):
    """The standard ROCKNIXDS logo (logo/rocknixds-logo.svg, the dark-background version), h base pixels tall,
    drawn smooth at the final resolution. Returns its width in base pixels."""
    png = os.path.join(REF, "rocknixds-logo.png")
    if not os.path.exists(png):
        import subprocess
        os.makedirs(REF, exist_ok=True)
        subprocess.run(["node", os.path.join(HERE, "svg2png.mjs"), LOGO_SVG, png, "560"], check=True)
    w = int(round(h * LOGO_ASPECT))
    a.overlay(Image.open(png), x, y, w, h)
    return w


def logo_text(a, x, y, s, h, c=INK, weight=700, shadow=None, sh=(1, 1)):
    """Text in the logo's typeface (Unbounded), smooth at the final resolution; h is the cap height in base
    pixels. Returns the width in base pixels."""
    k = a.scale
    f = ImageFont.truetype(LOGO_FONT, int(h * k * 1.4))
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    bb = f.getbbox(s)
    pad = 2 * k
    im = Image.new("RGBA", (bb[2] + pad * 2, bb[3] - bb[1] + pad * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if shadow is not None:
        d.text((pad + sh[0] * k, pad - bb[1] + sh[1] * k), s, font=f, fill=shadow)
    d.text((pad, pad - bb[1]), s, font=f, fill=c)
    a.overlays.append((im, x - 2, y - 2, im.width / k, im.height / k, None))
    return (bb[2]) / k


def handheld(a, x, y, sw, sh, top=None, bottom=None, hinge=6, pad=5, body=BEZEL, screen_bg=BG2,
             top_crop=None, bottom_crop=None):
    """A clamshell dual-screen handheld (RG DS style): two bezels joined by a hinge, screens sw x sh.

    top/bottom are PIL images shown on the screens at final resolution. Returns the screen rectangles."""
    W = sw + pad * 2
    H1 = sh + pad * 2
    # top shell
    a.rrect(x + 2, y + 2, W, H1 * 2 + hinge, fill=BG0, r=4)
    a.rrect(x, y, W, H1, fill=body, outline=BEZEL_LO, r=4)
    a.hline(x + 4, y + 1, W - 8, BEZEL_HI)
    # hinge
    hy = y + H1
    a.rect(x + 6, hy, W - 12, hinge, BEZEL_LO)
    a.rect(x + 6, hy + 1, W - 12, 1, BEZEL)
    a.rect(x + 2, hy + 1, 8, hinge - 2, BEZEL_HI)
    a.rect(x + W - 10, hy + 1, 8, hinge - 2, BEZEL_HI)
    # bottom shell
    by = hy + hinge
    a.rrect(x, by, W, H1, fill=body, outline=BEZEL_LO, r=4)
    a.hline(x + 4, by + 1, W - 8, BEZEL_HI)
    rects = []
    for (sy, img) in ((y + pad, top), (by + pad, bottom)):
        a.rect(x + pad - 1, sy - 1, sw + 2, sh + 2, BG0)
        a.rect(x + pad, sy, sw, sh, screen_bg)
        if img is not None:
            a.overlay(img, x + pad, sy, sw, sh)
        rects.append((x + pad, sy, sw, sh))
    return rects


def badge(a, x, y, s, c=RED, fg=WHITE, size=12):
    w = int(a.tlen(s, size, True)) + 8
    a.rrect(x + 1, y + 1, w, size + 4, fill=BG0, r=2)
    a.rrect(x, y, w, size + 4, fill=c, r=2)
    a.text(x + 4, y + 1, s, size, fg, medium=True)
    return w


def keycap(a, x, y, s, size=12):
    w = int(a.tlen(s, size)) + 6
    a.rrect(x, y, w, size + 3, fill=BG3, outline=INK3, r=1)
    a.text(x + 3, y, s, size, INK)
    return w


def load_ref(name):
    return Image.open(os.path.join(REF, name)).convert("RGBA")


def split_ds(img):
    """A DS screenshot as (top, bottom): 1280x480 side by side, or 640x972 stacked with a 12 px gap."""
    w, h = img.size
    if w > h:
        return img.crop((0, 0, w // 2, h)), img.crop((w // 2, 0, w, h))
    half = (h - 12) // 2
    return img.crop((0, 0, w, half)), img.crop((0, h - half, w, h))


ICONS = {}


def theme_icon(i):
    """The i-th icon (0-based, row-major, 16 a row) of the theme's icon sheet, as a 36x36 pixel tile."""
    if not ICONS:
        sheet = Image.open(os.path.join(REPO, "docs/img/pixel-icons.png")).convert("RGBA")
        ICONS["sheet"] = sheet
    sheet = ICONS["sheet"]
    col, row = i % 16, i // 16
    tile = sheet.crop((10 + col * 92, 10 + row * 92, 10 + col * 92 + 84, 10 + row * 92 + 84))
    return tile


# ---- Döda Kvarter's own sprites (art/*.txt from the game, parsed by its build_art.parse) -------------------------

_DK = {}


def dk_sprite(name, flip=False):
    """One of Döda Kvarter's sprites as an RGBA image (1 px per art pixel)."""
    if not _DK:
        import glob, re
        src = open(os.path.join(REF, "dk-art/build_art.py"), encoding="utf-8").read()
        i = src.index("def parse"); j = src.index("\ndef ", i + 5)
        ns = {"glob": glob, "os": os, "re": re, "sys": __import__("sys")}
        exec(src[i:j], ns)
        pals, sprites, _ = ns["parse"](sorted(glob.glob(os.path.join(REF, "dk-art/*.txt"))))
        _DK["pals"], _DK["sprites"] = pals, sprites
    w, h, pal, rows = _DK["sprites"][name]
    cols = _DK["pals"][pal]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                v = cols[ch]
                im.putpixel((x, y), (int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16), 255))
    return im.transpose(Image.FLIP_LEFT_RIGHT) if flip else im


def fancy_text(a, x, y, s, size, stops, extrude=4, ext=(60, 0, 10), outline=BLACK, highlight=None):
    """Big 90s box-art lettering: a vertical gradient face, a solid extrusion down-right, a black outline."""
    f = font(size, True)
    bb = a.d.textbbox((0, 0), s, font=f, features=NOLIGA)
    w, h = bb[2] + extrude + 4, bb[3] + extrude + 4
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask); md.fontmode = "1"
    md.text((2, 2), s, font=f, fill=255, features=NOLIGA)
    def stamp(img_mask, dx, dy, col):
        layer = Image.new("RGBA", mask.size, col + (255,))
        a.im.paste(layer, (x + dx, y + dy), img_mask)
    grown = mask.filter(ImageFilter.MaxFilter(3))
    for d in range(extrude, 0, -1):
        stamp(grown, d, d, outline)
    for d in range(extrude, 0, -1):
        stamp(mask, d, d, mix(ext, BLACK, 0.4 * (d / extrude)))
    stamp(grown, 0, 0, outline)
    face = Image.new("RGBA", mask.size)
    top, bot = bb[1] + 2, bb[3] + 2
    for j in range(h):
        t = min(max((j - top) / max(bot - top, 1), 0), 1)
        c = Art._grad_at(stops, t)
        if highlight is not None and abs(t - highlight) < 0.5 / max(bot - top, 1) * 1.01:
            c = mix(c, WHITE, 0.6)
        for i in range(w):
            face.putpixel((i, j), c + (255,))
    a.im.paste(face, (x, y), mask)
    return w


def starburst(a, cx, cy, r_out, r_in, n, fill, outline):
    pts = []
    for k in range(n * 2):
        ang = math.pi * k / n - math.pi / 2
        r = r_out if k % 2 == 0 else r_in
        pts.append((cx + math.cos(ang) * r, cy + math.sin(ang) * r))
    a.d.polygon([(px + 2, py + 2) for px, py in pts], fill=BLACK)
    a.d.polygon(pts, fill=fill, outline=outline)


def crt(img, w, h, s=2):
    """A screenshot as seen on a 90s TV: scaled to the final size, scanlines, a soft glow, darker corners."""
    W, H = w * s, h * s
    im = img.convert("RGB").resize((W, H), Image.LANCZOS)
    glow = im.filter(ImageFilter.GaussianBlur(3))
    im = Image.blend(im, glow, 0.25)
    px = im.load()
    for y in range(H):
        dark = 0.62 if y % 3 == 2 else 1.0
        for x in range(W):
            dx, dy = (x - W / 2) / (W / 2), (y - H / 2) / (H / 2)
            v = dark * (1 - 0.35 * (dx * dx * dy * dy) - 0.12 * (dx * dx + dy * dy) / 2)
            r, g, b = px[x, y]
            px[x, y] = (min(255, int(r * v * 1.08)), int(g * v), min(255, int(b * v * 1.05)))
    return im


def crisp_text(a, x, y, s, size, c=WHITE, shadow=None, anchor="la"):
    """Text at the final resolution in DejaVu Sans Mono Bold, for things people must read exactly (an invite code:
    the pixel font's B is close to its 8). size is in base pixels; returns the width in base pixels."""
    k = a.scale
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", size * k)
    bb = f.getbbox(s)
    w, h = bb[2] + 2 * k, bb[3] + 2 * k
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if shadow is not None:
        d.text((k, k), s, font=f, fill=shadow)
    d.text((0, 0), s, font=f, fill=c)
    wb = -(-w // k)
    if anchor == "ra":
        x -= wb
    a.overlays.append((im, x, y, w / k, h / k, None))
    return wb
