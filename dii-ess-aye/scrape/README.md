# Scraping without an account

Everything here fills ES's gamelist through its local HTTP API (port 1234, localhost only), so no ScreenScraper
account and no ES scraper menu are needed. Upload media with
`curl -X POST -H 'Content-Type: image/png' --data-binary @file.png localhost:1234/systems/nds/games/<id>/media/<type>`.
Game ids come from `curl localhost:1234/systems/nds/games`. ES writes it all into `gamelist.xml`.

| ES media type | What the theme shows | Made by |
|---|---|---|
| `cartridge` | the real DS game card on the carousel | `cartart.py`: "Cart - Front" scans from the [LaunchBox Games Database](https://gamesdb.launchbox-app.com) (`Metadata.zip`), cut-outs first, then by region |
| `boxback` | drawn-card fallback: the cover as label art, for games with no cart scan | `labelart.py` from the front cover |
| `boxart` | the 3D game case on the game list's top screen | `box3d.py` from the front cover |
| `image` | the screenshot next to it | libretro-thumbnails `Named_Snaps`, made side by side (below) |
| `wheel` | the RetroAchievements strip (badge, N of M, progress bar, points) | `ra-fetch.py` **on the device**, then `ra_panel.py` |
| `thumbnail`, `titleshot` | box art and title screen (ES menus) | libretro-thumbnails `Named_Boxarts`, `Named_Titles` |

**Never upload `mix`.** It has no file suffix of its own in this ES build and overwrites the screenshot (`-image.png`).

Covers, snaps and titles come from [libretro-thumbnails](https://github.com/libretro-thumbnails)
(`Nintendo_-_Nintendo_DS/Named_{Boxarts,Snaps,Titles}/<No-Intro name>.png`). DS snaps are top/bottom stacked
256x384. Make them side by side to match the RG DS: `magick snap.png -crop 256x192 +repage +append wide.png`.

`ra-fetch.py` uses the RetroAchievements account ROCKNIX already has (`system.cfg`), so the token never leaves the
device. It only reads the achievement set and your softcore unlocks and doesn't start a play session.

`push.sh` (run on the device) posts metadata (`g*.json`, flat JSON) and the libretro images for each game listed in
`map.txt` (`<ES game id> <file prefix>`).
