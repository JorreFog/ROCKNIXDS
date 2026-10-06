"""What's new in 1.6: twelve feature cards in the theme's panels, the three new things first."""
from lib import *
from icons import icon_tile

FEATURES = [
    ("3x", "3x resolution", "3D drawn at 768x576 and supersampled: smooth edges, sharper textures", "NEW", BLUE),
    ("bank", "Bank & Trade", "A Pokémon bank, PKHeX legality checks and Wi-Fi trading", "NEW", BLUE),
    ("zombie", "DÖDA KVARTER", "A zombie roguelike for both screens, in a Swedish suburb", "NEW", RED),
    ("lifebuoy", "Stuck games rescued", "A frozen game is caught and closed. No more restarts", None, None),
    ("menu", "In-game menu on RG DS", "Click L3: eight save slots with pictures, quick settings", None, None),
    ("clock", "Menu back in 1 second", "The menu stays up during DS games, whatever the library", None, None),
    ("card", "A fresh card opens on DS", "No games yet says where they go. No more hangs", None, None),
    ("gear", "Best settings by default", "Gengis Engine, the DS's filter, balanced power, threaded 3D", None, None),
    ("trophy", "Achievements offline", "No connection keeps your login and retries by itself", None, None),
    ("chip", "A lighter menu", "A picture cache limit and a memory watch that never lets go", None, None),
    ("font", "An easier pixel font", "5 is not S, 2 is not Z, B is not G anymore (#34)", None, None),
    ("heart", "From your reports", "Touch scrolling, ROM icons on cartridges, screens that fit", None, None),
]

COLS, CW, CH, GX, GY = 3, 198, 66, 9, 8
X0, Y0 = 16, 74
W = X0 * 2 + COLS * CW + (COLS - 1) * GX
H = Y0 + 4 * CH + 3 * GY + 34

a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG1), (1, BG2)], bands=8)
a.stars(0, 0, W, 64, 70, seed=6)

# header: the logo's badge and the title
logo(a, 16, 18, 16)
a.text(W - 16, 12, "WHAT'S NEW", 24, INK, anchor="ra", medium=True, shadow=BG0, sh=(2, 2))
a.text(W - 16, 40, "in version 1.6 · for the RG DS and RG DS Plus", 12, INK2, anchor="ra")
a.hline(16, 60, W - 32, BEZEL)
for i in range(0, W - 32, 4):
    a.px(16 + i, 61, BG0)

for n, (ic, title, desc, tag, tagc) in enumerate(FEATURES):
    cx, cy = X0 + (n % COLS) * (CW + GX), Y0 + (n // COLS) * (CH + GY)
    new = tag is not None
    a.panel(cx, cy, CW, CH, fill=BG3 if new else BG2, border=tagc if new else BEZEL,
            hi=mix(tagc, BG3, 0.5) if new else BG4)
    icon_tile(a, cx + 7, cy + 8, ic, 26)
    tx = cx + 40
    a.text(tx, cy + 6, title, 12, WHITE if new else INK, medium=True, shadow=BG0)
    a.wrap(tx, cy + 21, desc, CW - 44, 12, INK2, lh=14)
    if new:
        tw = int(a.tlen(tag, 12, True)) + 6
        a.rrect(cx + CW - tw - 5, cy + 6, tw, 14, fill=tagc, r=1)
        a.text(cx + CW - tw - 2, cy + 6, tag, 12, WHITE, medium=True)

# footer
fy = H - 26
a.text(16, fy + 4, "...and every fix since 1.5.12. The full list is in the release notes below.", 12, INK3)
crisp_text(a, W - 16, fy + 3, "discord.gg/uMPB63kF", 11, (150, 160, 250), anchor="ra")

a.render(os.path.join(OUT, "whats-new.png"))
