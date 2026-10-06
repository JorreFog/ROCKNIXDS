"""Döda Kvarter, the 90s way: a VHS tape playing on a CRT, box-art lettering, a sticker, a Win95 window, and the
game's own bosses walking the snowy street below (sprites straight from the game's art files)."""
from lib import *

W, H = 640, 400
a = Art(W, H, bg=BG0)

# sky: deep violet night into a sodium-lit haze at the horizon (90s horror-box colours), stars, a big pixel moon
a.vgrad(0, 0, W, 300, [(0, (8, 6, 18)), (0.5, (30, 14, 44)), (0.85, (70, 24, 52)), (1, (120, 44, 50))], bands=16)
a.stars(0, 0, W, 200, 220, seed=96, cols=(INK, INK2, (200, 160, 220), (255, 220, 200)))
mx, my, mr = 560, 70, 26
for yy in range(-mr, mr + 1):
    for xx in range(-mr, mr + 1):
        d = math.hypot(xx, yy)
        if d <= mr:
            c = (236, 228, 200) if d < mr - 2 else (200, 190, 160)
            if (xx + 8) ** 2 + (yy + 5) ** 2 < 40 or (xx - 9) ** 2 + (yy - 8) ** 2 < 18 or (xx - 2) ** 2 + (yy + 14) ** 2 < 10:
                c = (196, 186, 156)
            a.px(mx + xx, my + yy, c)
        elif d <= mr + 6 and BAYER4[(my + yy) % 4][(mx + xx) % 4] < int(6 * (1 - (d - mr) / 6)):
            a.px(mx + xx, my + yy, (90, 60, 90))

# skyline: miljonprogram blocks, a church spire, birches, lit windows
rnd = random.Random(7)
SKY = (14, 10, 24)
x = 0
while x < W:
    bw, bh = rnd.randint(40, 90), rnd.randint(50, 100)
    top = 300 - bh
    a.rect(x, top, bw, bh, SKY)
    for wy in range(top + 6, 296, 9):
        for wx in range(x + 4, x + bw - 4, 8):
            if rnd.random() < 0.28:
                a.rect(wx, wy, 4, 4, rnd.choice([(255, 196, 90), (240, 160, 70), (180, 200, 255)]))
            else:
                a.rect(wx, wy, 4, 4, (24, 18, 36))
    x += bw + rnd.randint(4, 20)
# spire
sx = 300
a.d.polygon([(sx, 170), (sx - 7, 230), (sx + 7, 230)], fill=SKY)
a.rect(sx - 10, 230, 20, 70, SKY)
a.rect(sx - 1, 160, 2, 12, SKY); a.rect(sx - 4, 164, 8, 2, SKY)

# the street: snow, a lamp's pool of light
a.vgrad(0, 300, W, 100, [(0, (60, 60, 84)), (1, (24, 24, 40))], bands=8)
for i in range(400):
    a.px(rnd.randrange(W), 300 + rnd.randrange(100), (180, 186, 210))
for lx in (110, 470):
    a.rect(lx, 214, 2, 96, (30, 26, 40))
    a.rect(lx - 6, 212, 10, 3, (30, 26, 40))
    a.rect(lx - 6, 215, 6, 2, (255, 230, 160))
    for i in range(-30, 31):
        t = 1 - (i / 30) ** 2
        a.dither_rect(lx + i, 306, 1, 8, (130, 120, 120), int(10 * t))

# the lineup: the survivor, then zombies and the eight bosses closing in
ground = 380
def put(name, x, flip=False):
    im = dk_sprite(name, flip)
    # shadow
    for i in range(im.width):
        a.dither_rect(x + i, ground - 2, 1, 4, BLACK, 8)
    a.paste_base(im, x, ground - im.height)
    return im.width

put("player_side_0", 22)
# muzzle flash and a torch beam to the right
a.rect(40, ground - 11, 3, 2, (255, 240, 180)); a.px(43, ground - 11, YELLOW)
for j in range(60):
    hw = j // 5
    a.dither_rect(44 + j, ground - 12 - hw, 1, hw * 2 + 3, (255, 240, 200), 3)
lineup = [("zombie_side_0", True), ("boss_draugen_walk_0", True), ("zombie_down_1", False),
          ("boss_troll_walk_0", True), ("boss_nacken_play_0", False), ("brute_side_0", True),
          ("boss_varulv_walk_0", True), ("boss_gloson_walk_0", True), ("zombie_side_1", True),
          ("boss_skogsra_walk_0", True), ("bloater_down_0", False), ("boss_lindorm_head_0", True),
          ("boss_haxan_fly_0", True), ("moose_0", True)]
x = 72
for name, flip in lineup:
    im = dk_sprite(name, flip)
    lift = 18 if "haxan" in name else 0
    if lift:
        a.paste_base(im, x, ground - im.height - lift)
        w = im.width
    else:
        w = put(name, x, flip)
    x += w + (4 if "zombie" in name or "brute" in name or "bloater" in name else 2)

# the TV: a beige 90s set with rabbit ears, the title screen on its tube
tx, ty, sw, sh = 22, 46, 176, 132
BEIGE, BEIGE_HI, BEIGE_LO = (206, 196, 172), (232, 224, 204), (150, 140, 118)
# antenna
for k in range(26):
    a.px(tx + 70 - k, ty - 6 - k, (180, 180, 190)); a.px(tx + 110 + k, ty - 6 - k, (180, 180, 190))
a.rrect(tx + 78, ty - 8, 24, 8, fill=(60, 56, 60), r=2)
a.rrect(tx - 8 + 3, ty - 8 + 3, sw + 60, sh + 22, fill=BLACK, r=6)
a.rrect(tx - 8, ty - 8, sw + 60, sh + 22, fill=BEIGE, outline=BEIGE_LO, r=6)
a.hline(tx - 2, ty - 7, sw + 48, BEIGE_HI)
a.rrect(tx - 3, ty - 3, sw + 6, sh + 6, fill=(30, 28, 30), r=5)
title_top, _ = split_ds(load_ref("dk-title.png"))
a.rect(tx, ty, sw, sh, BLACK)
a.overlay(crt(title_top, sw, sh), tx, ty, sw, sh, resample=Image.NEAREST)
# control panel: knobs, a red power light, the speaker grille, a brand plate
px0 = tx + sw + 8
for k, ky in enumerate((ty + 6, ty + 30)):
    a.rrect(px0 + 10, ky, 18, 18, fill=(70, 66, 64), outline=(30, 28, 30), r=4)
    a.rect(px0 + 18, ky + 2, 2, 7, BEIGE_HI)
for gy in range(ty + 58, ty + 108, 4):
    a.hline(px0 + 6, gy, 28, BEIGE_LO)
a.rect(px0 + 30, ty + sh - 2, 3, 3, RED)
a.rrect(px0 + 4, ty + sh - 4, 22, 7, fill=(70, 66, 64), r=1)
a.text(tx + sw // 2, ty + sh + 3, "JORREVISION", 12, BEIGE_LO, anchor="ma", medium=True)

# VHS on-screen display
a.text(tx + 6, ty + 4, "PLAY", 16, WHITE, medium=True, shadow=BLACK)
a.sprite(tx + 44, ty + 7, ["k....", "kkk..", "kkkkk", "kkk..", "k...."], {"k": WHITE}, 2)
a.text(tx + sw - 6, ty + sh - 20, "SP 0:19:96", 16, WHITE, anchor="ra", medium=True, shadow=BLACK)

# the logo
lx, ly = 250, 18
fancy_text(a, lx, ly, "DÖDA", 64, [(0, (255, 236, 160)), (0.35, (255, 120, 60)), (0.7, (220, 30, 40)),
                                   (1, (120, 10, 24))], extrude=5, ext=(110, 10, 20))
fancy_text(a, lx + 4, ly + 62, "KVARTER", 48, [(0, (240, 244, 255)), (0.45, (150, 170, 200)), (0.5, (60, 70, 100)),
                                             (0.75, (170, 186, 220)), (1, (240, 244, 255))], extrude=4,
           ext=(30, 34, 60))
a.text(lx + 6, ly + 124, "en zombie-roguelike i en svensk förort", 12, (255, 200, 160), shadow=BLACK)
a.text(lx + 6, ly + 138, "a zombie roguelike in Swedish suburbia", 12, INK2, shadow=BLACK)

# the sticker
starburst(a, 584, 150, 38, 28, 14, YELLOW, RED_DK)
a.text(584, 133, "NYTT", 16, RED, anchor="ma", medium=True)
a.text(584, 149, "SPEL!", 16, RED, anchor="ma", medium=True)
a.text(584, 166, "i 1.6", 12, (120, 20, 30), anchor="ma", medium=True)

# a Win95-style window with what's in the game
wx, wy, ww, wh = 250, 178, 300, 110
G0, G1, G2 = (192, 192, 192), (255, 255, 255), (128, 128, 128)
a.rect(wx + 2, wy + 2, ww, wh, BLACK)
a.rect(wx, wy, ww, wh, G0)
a.hline(wx, wy, ww, G1); a.vline(wx, wy, wh, G1)
a.hline(wx, wy + wh - 1, ww, (64, 64, 64)); a.vline(wx + ww - 1, wy, wh, (64, 64, 64))
a.hline(wx + 1, wy + wh - 2, ww - 2, G2); a.vline(wx + ww - 2, wy + 1, wh - 2, G2)
for i in range(ww - 6):
    a.vline(wx + 3 + i, wy + 3, 13, mix((0, 0, 128), (16, 132, 208), i / (ww - 6)))
a.text(wx + 6, wy + 3, "DODAKVAR.EXE", 12, WHITE, medium=True)
for k, s in enumerate(("_", "□", "x")):
    bx = wx + ww - 50 + k * 15
    a.rect(bx, wy + 5, 13, 10, G0); a.hline(bx, wy + 5, 13, G1); a.vline(bx, wy + 5, 10, G1)
    a.hline(bx, wy + 14, 13, (64, 64, 64)); a.vline(bx + 12, wy + 5, 10, (64, 64, 64))
    a.text(bx + 6, wy + 3, s if s != "□" else "o", 12, BLACK, anchor="ma", medium=True)
items = [
    "Rounds that never end, kronor for every hit",
    "Mystery Box, perks, the power, Pack-a-Punch",
    "Eight bosses from Swedish folklore",
    "Winter nights, autumn storms, midsummer",
    "A new town every run. 60 fps on both screens",
]
for k, s in enumerate(items):
    yy = wy + 22 + k * 17
    a.rect(wx + 8, yy + 5, 4, 4, (0, 0, 128))
    a.text(wx + 18, yy, s, 12, BLACK)

# credits line
em = dk_sprite("logo_emblem").resize((32, 32), Image.NEAREST)
a.paste_base(em, 570, 236)
a.text(586, 270, "JorreFog", 12, INK, anchor="ma", medium=True, shadow=BLACK)
a.text(586, 282, "productions", 12, INK2, anchor="ma", shadow=BLACK)

a.text(W - 8, H - 16, "Ports > Döda Kvarter", 12, WHITE, anchor="ra", medium=True, shadow=BLACK)
# VHS tracking noise: a couple of bright streaks, and scanlines over everything
for y0, x0, ln in ((96, 0, 140), (236, 420, 200)):
    for i in range(ln):
        if rnd.random() < 0.6:
            a.px(x0 + i, y0, (230, 230, 240))
a.scanlines(0, 0, W, H, BLACK, 0.12, 2)

a.render(os.path.join(OUT, "07-doda-kvarter.png"))
