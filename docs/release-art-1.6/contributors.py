"""Special thanks: everyone who opened an issue on GitHub (JorreFog/ROCKNIXDS, 25 issues by 2026-10-06), by name.

Counts are issues opened and comments written on them, from the GitHub API; the owner's own comments are left out.
"In 1.6" lists the issues docs/releases/v1.6.md fixes or ships."""
import hashlib
from lib import *

PEOPLE = [
    # login, issues, comments, what they did, issues in 1.6
    ("parth5991", 10, 1, "Bug hunter and ideas: launch loops, clipping, the settings menu", "#36 #37"),
    ("Epherial", 8, 12, "Bug hunter: the Pixel theme, the screens, the microphone", "#26 #27 #32 #34 #35"),
    ("LucasAlexandrou", 2, 3, "The first beta tester, and the 1.3 frontend report", ""),
    ("showcasefloyd", 1, 3, "RetroArch games on the bottom screen (fixed in 1.5.8)", ""),
    ("lunaruchu", 1, 3, "A question about DraStic at 2x", ""),
    ("Nyrhiade", 1, 1, "The first RG DS Plus alpha's misaligned theme", ""),
    ("nelynes", 1, 0, "RetroAchievements hashes for zipped games", "#31"),
    ("riap0526", 1, 0, "Idea: the DS game's own icon on its cartridge", "#44"),
]

AVATAR_COLS = [BLUE, RED, GREEN, YELLOW, (170, 110, 220), (240, 140, 50), TEAL_HI, BLUE_LT]


def avatar(a, x, y, login, k=3):
    """A 7x7 mirrored pixel identicon from the login (the same name always gets the same picture)."""
    h = hashlib.sha1(login.lower().encode()).digest()
    col = AVATAR_COLS[h[0] % len(AVATAR_COLS)]
    bg = mix(col, BG1, 0.82)
    a.rrect(x + 1, y + 1, 7 * k + 6, 7 * k + 6, fill=BG0, r=2)
    a.rrect(x, y, 7 * k + 6, 7 * k + 6, fill=bg, outline=mix(col, BG1, 0.4), r=2)
    bits = int.from_bytes(h[1:6], "big")
    for j in range(7):
        for i in range(4):
            if bits >> (j * 4 + i) & 1:
                for ii in (i, 6 - i):
                    a.rect(x + 3 + ii * k, y + 3 + j * k, k, k, col)


W, H = 680, 440
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG0), (1, BG2)], bands=10)
a.stars(0, 0, W, H, 140, seed=25)

# header
lw = logo(a, 16, 16, 18)
a.text(16, 42, "github.com/JorreFog/ROCKNIXDS/issues", 12, INK3)
a.text(W - 16, 10, "SPECIAL THANKS", 32, WHITE, anchor="ra", medium=True, shadow=RED_DK, sh=(2, 2))
a.text(W - 16, 44, "to everyone who reported a bug or shared an idea", 12, INK2, anchor="ra")

# the cards: two columns, four rows
CW, CH, GX, GY, X0, Y0 = 318, 76, 12, 8, 16, 68
for n, (login, iss, com, what, fixed) in enumerate(PEOPLE):
    cx, cy = X0 + (n % 2) * (CW + GX), Y0 + (n // 2) * (CH + GY)
    a.panel(cx, cy, CW, CH, fill=BG2, border=BEZEL, hi=BG4)
    avatar(a, cx + 8, cy + 8, login)
    tx = cx + 44
    a.text(tx, cy + 5, login, 16, WHITE, medium=True, shadow=BG0)
    stats = f"{iss} issue{'s' if iss != 1 else ''}" + (f" · {com} comment{'s' if com != 1 else ''}" if com else "")
    a.text(tx, cy + 25, stats, 12, BLUE_LT, medium=True)
    a.wrap(tx, cy + 39, what, CW - 54, 12, INK2, lh=13)
    if fixed:
        tag = f"{len(fixed.split())} in 1.6"
        tw = int(a.tlen(tag, 12, True)) + 8
        a.rrect(cx + CW - tw - 6, cy + 6, tw, 15, fill=mix(GREEN, BG2, 0.75), outline=mix(GREEN, BG2, 0.3), r=1)
        a.text(cx + CW - tw - 2, cy + 6, tag, 12, GREEN, medium=True)

# footer
fy = Y0 + 4 * (CH + GY) + 4
heart = ["..kk...kk..", ".krrk.krrk.", "krwrrkrrrrk", "krrrrrrrrrk", ".krrrrrrrk.", "..krrrrrk..",
         "...krrrk...", "....krk....", ".....k....."]
a.sprite(16, fy + 2, heart, {"k": BG0, "r": RED, "w": WHITE})
n16 = sum(len(p[4].split()) for p in PEOPLE)
a.text(32, fy + 1, f"25 issues from 8 people, {n16} of them fixed or built in 1.6. Thank you.", 12, INK)
crisp_text(a, W - 16, fy + 1, "discord.gg/uMPB63kF", 11, (150, 160, 250), anchor="ra")

a.render(os.path.join(OUT, "10-special-thanks.png"))
