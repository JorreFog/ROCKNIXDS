"""Join the Discord: a banner for the release page and the README."""
from lib import *

BLURPLE, BLURPLE_DK, BLURPLE_LT = (88, 101, 242), (60, 69, 180), (160, 170, 255)
W, H = 640, 168
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, (16, 18, 40)), (1, (34, 38, 92))], bands=10)
a.stars(0, 0, W, H, 120, seed=42, cols=(INK, INK2, BLURPLE_LT))

# a big pixel speech bubble with a controller in it (a generic chat mark, not Discord's logo)
bx, by = 24, 28
bubble = [
    "....kkkkkkkkkkkkkkkkkk....",
    "..kkbbbbbbbbbbbbbbbbbbkk..",
    ".kbbbbbbbbbbbbbbbbbbbbbbk.",
    "kbbbbbbbbbbbbbbbbbbbbbbbbk",
    "kbbbbwwwwbbbbbbbbbbwwbbbbk",
    "kbbbbwwwwbbbbbbbbwwbbwwbbk",
    "kbbwwwwwwwwbbbbbbbbwwbbbbk",
    "kbbwwwwwwwwbbbbbbwwbbwwbbk",
    "kbbbbwwwwbbbbbbbbbbwwbbbbk",
    "kbbbbwwwwbbbbbbbbbbbbbbbbk",
    "kbbbbbbbbbbbbbbbbbbbbbbbbk",
    ".kbbbbbbbbbbbbbbbbbbbbbbk.",
    "..kkbbbbbbbbbbbbbbbbbbkk..",
    "....kkkkkkbbbkkkkkkkkk....",
    "........kbbbk.............",
    ".......kbbk...............",
    "......kbk.................",
    "......kk..................",
]
a.sprite(bx + 3, by + 3, bubble, {"k": BG0, "b": BG0, "w": BG0}, 4)
a.sprite(bx, by, bubble, {"k": (20, 22, 50), "b": BLURPLE, "w": WHITE}, 4)
for i in range(3, 23):
    a.rect(bx + i * 4, by + 4, 4, 2, BLURPLE_LT)

tx = 150
a.text(tx, 22, "JOIN THE", 16, BLURPLE_LT, medium=True, shadow=BG0)
lw = logo(a, tx, 42, 32)
a.text(tx + lw + 10, 42, "DISCORD", 32, WHITE, medium=True, shadow=BLURPLE_DK, sh=(2, 2))
a.text(tx, 84, "Help with setup, bug reports, your best Döda Kvarter run,", 12, INK)
a.text(tx, 98, "trades for the Bank, and news about the next fix.", 12, INK)

# the invite, as a big button
url = "discord.gg/uMPB63kF"
uw = 214
a.rrect(tx + 2, 122, uw, 28, fill=BG0, r=3)
a.rrect(tx, 120, uw, 28, fill=BLURPLE, outline=(20, 22, 50), r=3)
a.hline(tx + 3, 121, uw - 6, BLURPLE_LT)
crisp_text(a, tx + 12, 124, url, 16, WHITE, shadow=BLURPLE_DK)
a.sprite(tx + uw + 8, 128, ["k...", "kk..", "kkk.", "kkkk", "kkk.", "kk..", "k..."], {"k": BLURPLE_LT}, 2)

# the community on the right: the survivor, a zombie, a dalahäst, a troll: Döda Kvarter's cast says hi
cast = ["player_down_0", "dalahast", "zombie_down_0", "boss_troll_roar"]
for i in range(436, 632):
    a.dither_rect(i, 148, 1, 4, BG0, 8)
x = 440
for name in cast:
    im = dk_sprite(name)
    if im.height < 30:  # the small ones at twice the size, next to the troll
        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    a.paste_base(im, x, 150 - im.height)
    x += im.width + 8

a.render(os.path.join(OUT, "discord.png"))
