"""The page's sign-off: the last big update, and thanks."""
from lib import *

W, H = 640, 176
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG2), (1, BG0)], bands=8)
a.stars(0, 0, W, H, 90, seed=160)

# the theme's own system icons, as a shelf along the top
sheet = Image.open(os.path.join(REPO, "docs/img/pixel-icons.png")).convert("RGBA")
row = sheet.crop((0, 0, sheet.width, 98))
sw = 600
sh = int(row.height * sw / row.width)
a.overlay(row, 20, 12, sw, sh, resample=Image.LANCZOS)

# a heart between the two screens of a little handheld
hx, hy = 40, 64
handheld(a, hx, hy, 48, 36, hinge=3, pad=4)
a.rect(hx + 4, hy + 4, 48, 36, TEAL)
heart = ["..kk...kk..", ".krrk.krrk.", "krwrrkrrrrk", "krrrrrrrrrk", ".krrrrrrrk.", "..krrrrrk..",
         "...krrrk...", "....krk....", ".....k....."]
a.sprite(hx + 4 + 13, hy + 4 + 9, heart, {"k": BG0, "r": RED, "w": WHITE}, 2)
a.rect(hx + 4, hy + 4 + 36 + 3 + 8, 48, 36, BG2)
a.text(hx + 4 + 24, hy + 4 + 36 + 3 + 8 + 11, "1.6", 12, INK, anchor="ma", medium=True)

tx = 130
a.text(tx, 66, "THANK YOU", 32, INK, medium=True, shadow=RED_DK, sh=(2, 2))
a.text(tx, 104, "1.6 is the last big update of ROCKNIXDS.", 12, INK)
a.text(tx, 118, "Everyone who tested, reported a bug or sent a log: this was built with you.", 12, INK2)
a.text(tx, 132, "And all the love to GammaOS and its creator, the inspiration for all of it.", 12, INK2)
a.text(tx, 152, "See you on Discord:", 12, INK3)
crisp_text(a, tx + int(a.tlen("See you on Discord:", 12)) + 6, 151, "discord.gg/uMPB63kF", 11, (150, 160, 250))

a.render(os.path.join(OUT, "11-thanks.png"))
