# Game art and text, one command

```sh
python3 dii-ess-aye/scrape/rocknixds-media.py --device <RG DS ip>
```

Run it on a PC (python3, Pillow, ssh as root to the device). For every DS game ES knows it fetches and
renders everything the theme shows and pushes it through ES's local HTTP API, with no scraper account.

It also runs on the device itself (`--local`); ROCKNIX's Python has no Pillow, so the first run there downloads the
Pillow wheel for the device's Python from PyPI (checked against PyPI's sha256) into
`/storage/.config/rocknixds/pylib`. The installer puts the tool in `/storage/.config/rocknixds/media` and runs it
on its own (`dsflip/device/media-auto.sh`):

- **every time the menu opens** (ES starts, a game ends): `--local --auto`, in the background at idle priority,
  once ES is idle, stopped when a game starts. Only games missing their 3D box (`boxart`), screenshot (`image`) or
  cartridge are scraped (everything else they're missing comes along); a game that still has no match is tried
  again after a week, an offline device tries again at the next menu. The RetroAchievements strip is redrawn for
  every game played since its strip was drawn (ES's `lastplayed`), and made for games that have none.
- **when a game ends**, before ES is back (`session.sh`): `--ra-rom <rom>` redraws that game's strip over its
  current file, so the menu opens on the progress just made (ES caches a picture it is showing by its file, so a
  strip pushed while the game is selected would only show after moving off it).

`rocknixds.automedia=0` in `system.cfg` switches both off. Logs: `/storage/.config/rocknixds/media.log` and
`media-ra.log` (the last run of each); state: `media-state.json`.

| ES media type | What the theme shows | Source |
|---|---|---|
| `thumbnail` | box art | [libretro-thumbnails](https://github.com/libretro-thumbnails) `Named_Boxarts` |
| `image` | screenshot, made side by side to match the RG DS | libretro-thumbnails `Named_Snaps` |
| `titleshot` | title screen, side by side | libretro-thumbnails `Named_Titles` |
| `boxart` | the 3D game case on the game list's top screen | `box3d.py` from the cover |
| `boxback` | label art for the drawn cartridge (games with no scan) | `labelart.py` from the cover |
| `cartridge` | the real DS card on the carousel | LaunchBox Games Database cart scans, via `nds-carts.json` |
| `wheel` | the RetroAchievements strip (badge, N of M, progress, points) | `ra-fetch.py` **on the device**, then `ra_panel.py` |
| description, genre, developer, publisher, release date | the bubble and the game list card | `nds-meta.json.gz` (LaunchBox overviews), only where ES has nothing |

Games are matched by name: the ROM's file name (No-Intro style) for libretro, a normalised title for LaunchBox,
with a fuzzy fallback; retail releases before kiosk demos, betas and the like, then the ROM's own region, then
USA/World/Europe. A game ES hasn't hashed yet (a newly copied ROM) gets its RetroAchievements ID here: the RA hash
is computed on the device (rcheevos' DS method) and looked up on RA, so its strip is made in the same run. Media a game already has is kept unless
`--force`; text fields are only ever filled where empty. `--game <substring>` limits it, `--no-push` and `--dry-run`
render or list without touching the device, `--no-ra` skips RetroAchievements (it needs the account set up in ES).
Downloads are cached under `--out` (default `media-out/`).

`nds-carts.json` (3338 DS games with "Cart - Front" scans) and `nds-meta.json.gz` (4533 games' overviews) are
built from the LaunchBox Games Database `Metadata.zip`, so nobody has to download its 108 MB; the images themselves
come from `images.launchbox-app.com` on demand. RetroAchievements numbers come from the device's own account and
token (`ra-fetch.py` runs there and only reads the set and your softcore unlocks).

**Never upload `mix`.** It has no file suffix of its own in this ES build and overwrites the screenshot.

The single-purpose tools (`box3d.py`, `labelart.py`, `cartart.py`, `ra-fetch.py`, `ra_panel.py`, `push.sh`) still
work on their own; the top of each says how.
