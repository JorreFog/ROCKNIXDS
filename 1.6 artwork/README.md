# 1.6 artwork

The final pictures for the ROCKNIXDS 1.6 release, and only those. They are numbered in the order they appear on
the GitHub release page; [`docs/releases/v1.6-page.md`](../docs/releases/v1.6-page.md) is that page, ready to paste,
with each picture's alt text. All are PNG, 1280–1360 px wide, shown at 880 px on the page.

| File | Where it goes | What it shows |
|---|---|---|
| `01-hero.png` | Top of the page | ROCKNIXDS 1.6, the last big update: three handhelds (Döda Kvarter, ROCKNIXDS Pixel, Bank & Trade) |
| `02-discord.png` | Right under the header, linked to https://discord.gg/uMPB63kF | Join the ROCKNIXDS Discord. Also used in the main README |
| `03-whats-new.png` | After the summary | What's new in 1.6: twelve feature cards |
| `04-performance.png` | After What's new | ROCKNIXDS vs stock ROCKNIX: the 3D stress test at 2x and four measured wins |
| `05-3x-resolution.png` | The 3x internal resolution section | The same 3D scene at 1x, 2x and 3x |
| `06-bank-trade.png` | The Bank & Trade section | Two handhelds trading over Wi-Fi, and what the app does |
| `07-doda-kvarter.png` | The Döda Kvarter section | 90s box-art poster: CRT, VHS, the game's bosses |
| `08-special-thanks.png` | Special thanks, near the end | Everyone who opened a GitHub issue, by name |
| `09-thanks.png` | The very end | Thank you, the last big update, the Discord link |

Also here, not on the release page: two Döda Kvarter gameplay GIFs, real play at the game's own 320x240 and
20 fps (7 s each, looping, 5–6 MB):

| File | What it shows |
|---|---|
| `doda-kvarter-horde.gif` | A horde on round 14, a winter night, Double Points |
| `doda-kvarter-boss.gif` | Round 20: Draugen's title card, then his charges |

Don't edit these by hand: [`docs/release-art-1.6/build.sh`](../docs/release-art-1.6/) makes the pictures and
`docs/release-art-1.6/gameplay.sh` the clips, straight into this folder.
