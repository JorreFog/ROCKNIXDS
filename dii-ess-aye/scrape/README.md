# Game art and text, one command

```sh
python3 dii-ess-aye/scrape/rocknixds-media.py --device <RG DS ip>
```

Run it on a PC (python3, Pillow, numpy, ssh as root to the device). For every DS game ES knows it fetches and
renders everything the theme shows and pushes it through ES's local HTTP API, with no scraper account:

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
with a fuzzy fallback and a preference for USA/World/Europe releases. Media a game already has is kept unless
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
