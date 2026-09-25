Scraping without the ES menu or a ScreenScraper account. Art comes from libretro-thumbnails
(`Nintendo_-_Nintendo_DS/Named_{Boxarts,Snaps,Titles}/<No-Intro name>.png`). DS snaps are
top/bottom stacked 256x384; make them side by side to match the RG DS:
`magick snap.png -crop 256x192 +repage +append wide.png`.
Then run `push.sh` on the device. It posts to ES's local HTTP API (port 1234, localhost only):
metadata as flat JSON to `/systems/nds/games/<id>`, and media to `.../media/{thumbnail,image,titleshot}`.
Game ids come from `curl localhost:1234/systems/nds/games`. ES writes it all into gamelist.xml.
