"""The release's header: ROCKNIXDS 1.6, the last big update, three handhelds showing what's in it."""
from lib import *

a = Art(640, 372)
# night sky: a dithered fall from near-black into the theme's blue-grey, stars, a low teal glow on the horizon
a.vgrad(0, 0, 640, 372, [(0, BG0), (0.55, BG2), (0.85, (24, 44, 52)), (1, (30, 60, 66))], bands=14)
a.stars(0, 0, 640, 210, 260, seed=16)
# a dot grid floor
for y in range(276, 372, 6):
    for x in range((y // 6) % 2 * 4, 640, 8):
        a.px(x, y, mix(BG3, TEAL_HI, (y - 276) / 96))

# title
lw = 0
f = 32
tw = a.tlen("ROCKNIX", f, True) + 12 + a.tlen("DS", f, True) + 4
x0 = int(320 - (tw + 14 + a.tlen("1.6", 48, True)) / 2)
logo(a, x0, 18, f)
vx = x0 + int(tw) + 14
a.text(vx + 2, 6 + 2, "1.6", 48, RED_DK, medium=True)
a.text(vx, 6, "1.6", 48, INK, medium=True, outline=None)
a.text(320, 62, "THE LAST BIG UPDATE", 16, BLUE_LT, anchor="ma", medium=True, shadow=BG0)
a.text(320, 82, "One release for the RG DS and the RG DS Plus", 12, INK2, anchor="ma")

# shadows on the floor
for cx, rw in ((145, 70), (320, 84), (495, 70)):
    for i in range(-rw, rw):
        t = 1 - (i / rw) ** 2
        a.dither_rect(cx + i, 334 - 4, 1, 8, BG0, int(12 * t))
# three handhelds: Döda Kvarter, ROCKNIXDS Pixel, Bank & Trade
home = Image.open(os.path.join(REPO, "docs/img/pixel-dark-home.png")).convert("RGBA")
hw, hh = home.size
pt, pb = home.crop((23, 23, hw - 23, 23 + 245)), home.crop((23, hh - 23 - 245, hw - 23, hh - 23))
dk_t, dk_b = split_ds(load_ref("dk-horde-winter.png"))
bk_t, bk_b = split_ds(load_ref("bank-boxes.png"))

handheld(a, 84, 128, 112, 84, dk_t, dk_b, hinge=5, pad=5)
handheld(a, 434, 128, 112, 84, bk_t, bk_b, hinge=5, pad=5)
handheld(a, 247, 104, 136, 102, pt, pb, hinge=6, pad=5)

# captions under the side handhelds and the centre
for cx, title, sub, col in ((145, "DÖDA KVARTER", "zombie roguelike", RED), (495, "BANK & TRADE", "Pokémon bank, Wi-Fi trades", BLUE),):
    a.text(cx, 330, title, 12, col, anchor="ma", medium=True, shadow=BG0)
    a.text(cx, 344, sub, 12, INK2, anchor="ma")

# sparkle badges
badge(a, 90, 112, "NEW GAME", RED)
badge(a, 494, 112, "NEW APP", BLUE)

a.render(os.path.join(OUT, "hero.png"))
