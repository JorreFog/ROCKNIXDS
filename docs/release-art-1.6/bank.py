"""ROCKNIXDS Bank & Trade: two handhelds trading over Wi-Fi, and what the app does."""
from lib import *
from icons import icon_tile, ICONS16, P

W, H = 640, 340
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG0), (0.6, BG2), (1, (20, 40, 62))], bands=10)
a.stars(0, 0, W, 140, 120, seed=21)
# a faint grid like the bank's boxes
for y in range(150, H, 12):
    for x in range(0, 340, 12):
        a.px(x, y, BG3)

# title
a.text(16, 12, "BANK & TRADE", 32, INK, medium=True, shadow=BLUE_DK, sh=(2, 2))
a.text(18, 48, "A Pokémon bank, legality checker and trading app", 12, INK2)
a.text(18, 61, "for the RG DS and RG DS Plus, built on PKHeX.Core", 12, INK2)
badge(a, 254, 18, "NEW APP", BLUE)

# two handhelds and the link between them
bt, bb = split_ds(load_ref("bank-boxes.png"))
tt, tb = split_ds(load_ref("bank-trade.png"))
SW, SH = 120, 90
handheld(a, 16, 84, SW, SH, bt, bb, hinge=5, pad=5)
handheld(a, 206, 84, SW, SH, tt, tb, hinge=5, pad=5)

# Wi-Fi waves out of each handheld, a capsule crossing between them on a dotted arc
x0, x1, yb = 146, 206, 112
for i in range(0, 41):
    t = i / 40
    x = x0 + (x1 - x0) * t
    y = yb - math.sin(t * math.pi) * 26
    if i % 4 < 2:
        a.rect(int(x), int(y), 2, 2, BLUE_LT)
cap = ["..kkkk..", ".krrrrk.", "krrwrrrk", "kkkwwkkk", "kwwkkwwk", "kwwwwwwk", ".kwwwwk.", "..kkkk.."]
a.sprite(168, 76, cap, P, 2)
for r, c in ((5, BLUE_LT), (9, BLUE), (13, BLUE_DK)):
    for ang in range(-50, 51, 6):
        rad = math.radians(ang)
        a.px(int(146 + math.cos(rad) * r * 0.6), int(yb + 12 + math.sin(rad) * r), c)
        a.px(int(206 - math.cos(rad) * r * 0.6), int(yb + 12 + math.sin(rad) * r), c)
a.text(176, 130, "Wi-Fi", 12, BLUE_LT, anchor="ma", medium=True)

# what it does
FEATS = [
    ("bank", "A bank of 40 boxes", "Plain PKHeX files you can copy off over the network"),
    ("card", "Reads your game saves", "DraStic, melonDS, the GBA emulators and RetroArch. Gen 1 to 5"),
    ("trophy", "PKHeX legality checks", "Every Pokémon in sight is checked. An illegal one gets a red mark"),
    ("link", "Trades over Wi-Fi", "Open a lobby with your offer and wish, or a private room code"),
    ("stars", "Trade evolutions", "Kadabra, Haunter, Onix with a Metal Coat... an Everstone stops them"),
]
fx, fy = 350, 82
for i, (ic, t1, t2) in enumerate(FEATS):
    y = fy + i * 48
    a.panel(fx, y, W - fx - 12, 43, fill=BG2, border=BEZEL, hi=BG4)
    ix, iy = fx + 5, y + 8
    if ic in ICONS16:
        icon_tile(a, ix, iy, ic, 26)
    else:
        a.rrect(ix + 1, iy + 1, 26, 26, fill=BG0, r=3)
        a.rrect(ix, iy, 26, 26, fill=(233, 237, 241), outline=BEZEL, r=3)
    if ic == "link":  # two handhelds side by side
        for dx in (4, 14):
            a.rect(ix + dx, iy + 5, 8, 7, BEZEL); a.rect(ix + dx + 1, iy + 6, 6, 5, BLUE)
            a.rect(ix + dx, iy + 13, 8, 7, BEZEL); a.rect(ix + dx + 1, iy + 14, 6, 5, TEAL)
        a.hline(ix + 12, iy + 8, 2, RED)
    if ic == "stars":  # two sparkles: an evolution
        star = ["...k...", "..kyk..", "kkkyykk", "kyyyyyk", ".kyyyk.", "kyykyyk", "kk...kk"]
        a.sprite(ix + 3, iy + 3, star, P)
        a.sprite(ix + 13, iy + 13, star, P)
    a.text(fx + 38, y + 3, t1, 12, INK, medium=True)
    a.wrap(fx + 38, y + 17, t2, W - fx - 12 - 44, 12, INK2, lh=12)

a.text(16, H - 16, "Ports > ROCKNIXDS Bank", 12, BLUE_LT, medium=True)
a.render(os.path.join(OUT, "06-bank-trade.png"))
