# The 1.6 release artwork

The pictures in [`1.6 artwork/`](../../1.6%20artwork/), drawn by these scripts in ROCKNIXDS Pixel's palette and pixel font:
art on a small canvas, scaled up 2× with nearest-neighbour, screenshots pasted at full resolution inside the frames.
`./build.sh` makes them all (Python 3 with Pillow and numpy, and Node with Playwright's Chromium for the logo); [`../releases/v1.6-page.md`](../releases/v1.6-page.md)
lays them out for the GitHub release.

| Picture | Script | What's in it |
|---|---|---|
| `01-hero.png` | `hero.py` | The header: ROCKNIXDS 1.6, the last big update, three handhelds (Döda Kvarter, Pixel's home, Bank & Trade) |
| `03-whats-new.png` | `whats_new.py` | Twelve feature cards with pixel icons (`icons.py`), the three new things first |
| `04-performance.png` | `perf.py` | ROCKNIXDS against stock ROCKNIX: the 3D stress ramp at 2× (from `docs/img/stress-ramp.svg`) and four measured wins from the README's *What was achieved* |
| `05-3x-resolution.png` | `res3x.py` | One small DS-style 3D scene rasterised at 1×, 2× and 3× (box-filtered 3:2 into 2×, as Gengis Engine does), the same crop of each |
| `06-bank-trade.png` | `bank.py` | Two handhelds trading over Wi-Fi with the Bank's own screenshots, and what the app does |
| `07-doda-kvarter.png` | `doda.py` | 90s box art: the title screen on a CRT with a VHS overlay, chrome lettering, a sticker, a Win95 window, and the game's own sprites (from its `art/*.txt`) walking the street |
| `02-discord.png` | `discord.py` | Join the Discord. The invite is set in DejaVu Sans Mono: the pixel font's B looks like an 8 |
| `08-doda-kvarter-horde.gif`, `09-doda-kvarter-boss.gif` (and `video/*.mp4`) | `gameplay.sh` | Real Döda Kvarter play: the game's headless backend runs its test bot and writes every frame; debug hooks pick the round and the boss, a seed makes it repeat exactly. Needs the game built and ffmpeg |
| `10-special-thanks.png` | `contributors.py` | Everyone who opened an issue on GitHub, by name: issues, comments, what they found, how many are fixed or built in 1.6. The avatars are pixel identicons made from each name |
| `11-thanks.png` | `thanks.py` | The page's sign-off |

`lib.py` holds the drawing helpers (panels, dithered gradients, the handheld, the pixel text). The logo is the standard one, `logo/rocknixds-logo.svg`, drawn smooth by Chromium (`svg2png.mjs`), with text beside it in its typeface, Unbounded. Until the
Döda Kvarter, Bank and 1.6 branches are merged, `build.sh` takes their screenshots, sprites and 1.6's fixed font
from those branches into `ref/` (ignored by git).
