#!/usr/bin/env python3
"""gen_icons.py <upstream systems dir> <out dir>

Re-renders dii-ess-aye's system icons (flat glyphs on a #d3d3d3 square) as modern tiles: a rounded square with a
dark gradient tinted by a per-system accent colour, a soft top highlight and a bottom shadow, and the glyph on top.
Dark monochrome glyphs (the DS logo, the "?" tile) are inverted to light so they read on the dark tile; coloured
glyphs keep their colours. The glyph is separated from its grey square by un-mixing the anti-aliased edge, so the
result has clean alpha at 256x256, the size the theme's carousel and hero card scale from.
"""
import colorsys, hashlib, os, sys
from PIL import Image, ImageDraw, ImageFilter

SRC, OUT = sys.argv[1], sys.argv[2]
BG = (211, 211, 211)                      # upstream's tile grey
SIZE, R, SS = 256, 52, 4                  # tile size, corner radius, supersampling for the shape

# accent hues (degrees) and saturation per system; anything else gets a hue from its name
HUE = {
    "nds": 212, "ndsiware": 212, "3ds": 205, "gba": 268, "gbah": 268, "gb": 252, "gbh": 252, "gbc": 140, "gbch": 140,
    "nes": 2, "famicom": 2, "fds": 8, "snes": 275, "sfc": 275, "n64": 148, "gc": 260, "wii": 205, "wiiware": 205,
    "psx": 352, "psp": 222, "ps2": 230, "genesis": 226, "megadrive": 226, "megadrive-japan": 226, "megacd": 232,
    "sega32x": 232, "mastersystem": 214, "gamegear": 236, "saturn": 224, "dreamcast": 26, "sg-1000": 214,
    "arcade": 322, "mame": 322, "fbneo": 322, "neogeo": 44, "neogeocd": 44, "ngp": 36, "ngpc": 36, "cps1": 322,
    "cps2": 322, "cps3": 322, "naomi": 26, "atomiswave": 26, "pcengine": 22, "pcenginecd": 22, "pcfx": 22,
    "supergrafx": 22, "tg16": 22, "tg16cd": 22, "atari2600": 30, "atari5200": 30, "atari7800": 30, "atari800": 30,
    "atarist": 30, "atarilynx": 34, "atarijaguar": 18, "amiga": 198, "amigacd32": 198, "c64": 208, "c128": 208,
    "vic20": 208, "pc": 196, "dos": 196, "scummvm": 42, "wonderswan": 12, "wonderswancolor": 12, "pokemini": 48,
    "virtualboy": 356, "vectrex": 190, "intellivision": 40, "colecovision": 300, "msx": 200, "msx2": 200,
    "ports": 24, "doom": 14, "pico8": 330, "tic80": 300, "openbor": 20, "easyrpg": 120, "j2me": 160,
    "auto-favorites": 346, "auto-lastplayed": 200, "auto-allgames": 212, "auto-at2players": 180,
    "auto-at4players": 180, "auto-neverplayed": 60, "auto-retroachievements": 46, "auto-verticalarcade": 322,
}
LOW_SAT = {"tools", "settings", "default", "musicplayer", "music", "retropie", "esde", "auto-allgames"}


def accent(name):
    if name in LOW_SAT: return 215 / 360, 0.12              # cool grey for tools, defaults, collections of everything
    if name in HUE: h = HUE[name]
    else: h = int(hashlib.md5(name.encode()).hexdigest()[:4], 16) % 360
    s = 0.6
    return h / 360, s


def hsl(h, s, l):
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5)


def glyph(im):
    """The glyph with alpha, un-mixed from the grey square. Returns (RGBA image, fraction of dark grey pixels)."""
    px = im.convert("RGB").load(); w, h = im.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0)); op = out.load()
    T = 36.0; dark = total = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            d = max(abs(r - BG[0]), abs(g - BG[1]), abs(b - BG[2]))
            if d < 6: continue
            a = min(1.0, d / T)
            # un-premultiply against the grey: c = a * fg + (1 - a) * bg
            fr = (r - (1 - a) * BG[0]) / a; fg_ = (g - (1 - a) * BG[1]) / a; fb = (b - (1 - a) * BG[2]) / a
            fr, fg_, fb = (max(0, min(255, int(v + 0.5))) for v in (fr, fg_, fb))
            op[x, y] = (fr, fg_, fb, int(a * 255 + 0.5))
            if a > 0.6:
                total += 1
                mx, mn = max(fr, fg_, fb), min(fr, fg_, fb)
                if mx - mn < 40 and mx < 120: dark += 1
    return out, (dark / total if total else 0)


def invert_light(g):
    """Map a dark monochrome glyph to a light one (keeps alpha and the grey steps)."""
    r, gg, b, a = g.split()
    from PIL import ImageOps
    return Image.merge("RGBA", (ImageOps.invert(r), ImageOps.invert(gg), ImageOps.invert(b), a))


def tile(name):
    h, s = accent(name)
    W = SIZE * SS
    # gradient fill: accent-tinted dark, lighter at the top
    grad = Image.new("RGB", (1, W))
    for y in range(W):
        t = y / (W - 1)
        grad.putpixel((0, y), hsl(h, s, 0.42 - 0.16 * t))
    grad = grad.resize((W, W))
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, W - 1), radius=R * SS, fill=255)
    base = Image.new("RGBA", (W, W), (0, 0, 0, 0)); base.paste(grad, (0, 0), mask)
    # soft light from the top-left, and a darker foot
    light = Image.new("L", (W, W), 0)
    ImageDraw.Draw(light).ellipse((-W * 0.2, -W * 0.55, W * 0.9, W * 0.45), fill=84)
    light = light.filter(ImageFilter.GaussianBlur(W * 0.12))
    base.paste(Image.new("RGBA", (W, W), (255, 255, 255, 255)), (0, 0), Image.composite(light, Image.new("L", (W, W), 0), mask))
    foot = Image.new("L", (W, W), 0)
    ImageDraw.Draw(foot).rectangle((0, W * 0.72, W, W), fill=60)
    foot = foot.filter(ImageFilter.GaussianBlur(W * 0.1))
    base.paste(Image.new("RGBA", (W, W), (0, 0, 0, 255)), (0, 0), Image.composite(foot, Image.new("L", (W, W), 0), mask))
    # rim: a thin lighter edge, stronger along the top
    d = ImageDraw.Draw(base)
    d.rounded_rectangle((SS, SS, W - 1 - SS, W - 1 - SS), radius=R * SS - SS, outline=(255, 255, 255, 46), width=2 * SS)
    return base.resize((SIZE, SIZE), Image.LANCZOS)


os.makedirs(OUT, exist_ok=True)
n = 0
for f in sorted(os.listdir(SRC)):
    if not f.endswith(".png"): continue
    name = f[:-4]
    im = Image.open(os.path.join(SRC, f)).convert("RGBA")
    if im.size != (SIZE, SIZE): im = im.resize((SIZE, SIZE), Image.LANCZOS)
    g, dark = glyph(im)
    if dark > 0.6: g = invert_light(g)
    # glyph shadow for depth, then the glyph, slightly smaller so it never touches the rim
    g = g.resize((int(SIZE * 0.9), int(SIZE * 0.9)), Image.LANCZOS)
    sh = Image.new("RGBA", g.size, (0, 0, 0, 0)); sh.paste((0, 0, 0, 110), (0, 0), g.split()[3])
    sh = sh.filter(ImageFilter.GaussianBlur(3))
    t = tile(name)
    off = (SIZE - g.width) // 2
    t.alpha_composite(sh, (off, off + 3))
    t.alpha_composite(g, (off, off))
    t.save(os.path.join(OUT, f), optimize=True)
    n += 1
print(f"{n} icons -> {OUT}")
