#!/bin/sh
# Döda Kvarter gameplay clips for the 1.6 release: real play, rendered by the game itself.
#
# The game's headless backend runs its test bot (--bot) and writes every frame (--snap); the debug hooks start the
# run at a round (DK_DEBUG_ROUND), pick the boss (DK_DEBUG_BOSS), keep the bot alive (DK_DEBUG_GOD) and hand it an
# Ak 5 (DK_DEBUG_GUN=3). A seed makes a run repeat frame for frame, so these clips come out the same every time.
#
#   08-doda-kvarter-horde   round 14, winter, seed 5: frames 2460-2879 (a horde, Double Points)
#   09-doda-kvarter-boss    round 20, Draugen, winter, seed 7: frames 150-569 (his title card, his charges)
#
# Each is a GIF at the game's own 320x240 and 20 fps (about 5-6 MB; the scrolling town and the snow change most
# pixels every frame, so a 2x GIF would be 20 MB), and an MP4 at 2x and the full 60 fps (3 MB, for dragging into
# the GitHub release editor, which plays it). The top screen only.
#
# Needs the dodakvarter branch built (sh dodakvarter/build.sh: libdrm and SDL2 headers) and ffmpeg.
#   sh gameplay.sh <dodakvarter/build/dodakvarter>
set -e
BIN=$(realpath "${1:?usage: gameplay.sh <path to the built dodakvarter>}")
cd "$(dirname "$0")"
OUT=$(realpath "../../1.6 artwork")
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

clip() {  # name round boss seed first last
    name=$1; mkdir -p "$W/$name/f" "$W/$name/data" "$W/$name/q60" "$W/$name/q20"
    env DK_DATA="$W/$name/data" DK_NO_SPLASH=1 DK_DEBUG_ROUND=$2 ${3:+DK_DEBUG_BOSS=$3} DK_DEBUG_GOD=1 DK_DEBUG_GUN=3 \
        "$BIN" --backend headless --seed $4 --season 1 --start --bot --frames $(($6 + 1)) --snap "$W/$name/f" \
        --snap-every 1 >/dev/null 2>&1
    i=0; for f in $(seq $5 $6); do ln -s "$W/$name/f/$(printf frame%06d.png $f)" "$W/$name/q60/$(printf %05d.png $i)"; i=$((i + 1)); done
    i=0; for f in $(seq $5 3 $6); do ln -s "$W/$name/f/$(printf frame%06d.png $f)" "$W/$name/q20/$(printf %05d.png $i)"; i=$((i + 1)); done
    ffmpeg -loglevel error -y -framerate 20 -i "$W/$name/q20/%05d.png" -vf "crop=320:240:0:0,split[s0][s1];\
[s0]palettegen=max_colors=256:stats_mode=diff[p];[s1][p]paletteuse=dither=none:diff_mode=rectangle" -loop 0 "$OUT/$name.gif"
    mkdir -p "$OUT/video"
    ffmpeg -loglevel error -y -framerate 60 -i "$W/$name/q60/%05d.png" -vf "crop=320:240:0:0,scale=640:480:flags=neighbor" \
        -c:v libx264 -preset slow -crf 16 -tune animation -pix_fmt yuv420p -movflags +faststart "$OUT/video/$name.mp4"
    echo "$name: $(du -k "$OUT/$name.gif" | cut -f1) KB gif, $(du -k "$OUT/video/$name.mp4" | cut -f1) KB mp4"
    rm -rf "$W/$name"
}
clip 08-doda-kvarter-horde 14 "" 5 2460 2879
clip 09-doda-kvarter-boss 20 0 7 150 569
