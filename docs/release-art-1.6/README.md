# The 1.6 release artwork

The pictures in [`docs/img/1.6/`](../img/1.6/), drawn by these scripts in ROCKNIXDS Pixel's palette and pixel font:
art on a small canvas, scaled up 2× with nearest-neighbour, screenshots pasted at full resolution inside the frames.
`./build.sh` makes them all (Python 3 with Pillow and numpy, and Node with Playwright's Chromium for the logo); [`../releases/v1.6-page.md`](../releases/v1.6-page.md)
lays them out for the GitHub release.

| Picture | Script | What's in it |
|---|---|---|
| `hero.png` | `hero.py` | The header: ROCKNIXDS 1.6, the last big update, three handhelds (Döda Kvarter, Pixel's home, Bank & Trade) |
| `whats-new.png` | `whats_new.py` | Twelve feature cards with pixel icons (`icons.py`), the three new things first |
| `performance.png` | `perf.py` | ROCKNIXDS against stock ROCKNIX: the 3D stress ramp at 2× (from `docs/img/stress-ramp.svg`) and four measured wins from the README's *What was achieved* |
| `3x-resolution.png` | `res3x.py` | One small DS-style 3D scene rasterised at 1×, 2× and 3× (box-filtered 3:2 into 2×, as Gengis Engine does), the same crop of each |
| `bank-trade.png` | `bank.py` | Two handhelds trading over Wi-Fi with the Bank's own screenshots, and what the app does |
| `doda-kvarter.png` | `doda.py` | 90s box art: the title screen on a CRT with a VHS overlay, chrome lettering, a sticker, a Win95 window, and the game's own sprites (from its `art/*.txt`) walking the street |
| `discord.png` | `discord.py` | Join the Discord. The invite is set in DejaVu Sans Mono: the pixel font's B looks like an 8 |
| `thanks.png` | `thanks.py` | The page's sign-off |

`lib.py` holds the drawing helpers (panels, dithered gradients, the handheld, the pixel text). The logo is the standard one, `logo/rocknixds-logo.svg`, drawn smooth by Chromium (`svg2png.mjs`), with text beside it in its typeface, Unbounded. Until the
Döda Kvarter, Bank and 1.6 branches are merged, `build.sh` takes their screenshots, sprites and 1.6's fixed font
from those branches into `ref/` (ignored by git).
