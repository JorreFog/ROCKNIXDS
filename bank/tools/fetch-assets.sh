#!/bin/sh
# Fills bank/assets (or the folder given) with what the app draws with:
#   fonts/    Pixelify Sans, the ROCKNIXDS Pixel theme's font (SIL OFL), from this repository
#   sprites/  PKHeX's box sprites (normal and shiny, eggs) and ball icons, from the PKHeX release the app is built on
# Only the sprite folders are fetched (a sparse checkout), about 11 MB.
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$HERE/assets}
PKHEX_TAG=26.08.26          # the PKHeX.Core version in src/Bank.Core/Bank.Core.csproj
FONTS=$HERE/../dii-ess-aye/themes/rocknixds-pixel-dark/rnds/fonts

mkdir -p "$OUT/fonts" "$OUT/sprites"
for f in PixelifySans-Regular.ttf PixelifySans-Medium.ttf OFL.txt; do
    cp "$FONTS/$f" "$OUT/fonts/$f"
done

if [ -f "$OUT/sprites/.pkhex-$PKHEX_TAG" ]; then
    echo "sprites: already PKHeX $PKHEX_TAG"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
IMG=PKHeX.Drawing.PokeSprite/Resources/img
git -C "$TMP" init -q
git -C "$TMP" remote add origin https://github.com/kwsch/PKHeX.git
git -C "$TMP" sparse-checkout set --no-cone "/$IMG/Big Pokemon Sprites/" "/$IMG/Big Shiny Sprites/" "/$IMG/ball/" /LICENSE
git -C "$TMP" fetch -q --depth 1 origin "refs/tags/$PKHEX_TAG"
git -C "$TMP" checkout -q FETCH_HEAD

rm -f "$OUT/sprites/"*.png "$OUT/sprites/".pkhex-*
cp "$TMP/$IMG/Big Pokemon Sprites/"*.png "$OUT/sprites/"
cp "$TMP/$IMG/Big Shiny Sprites/"*.png "$OUT/sprites/"
cp "$TMP/$IMG/ball/"*.png "$OUT/sprites/"
cp "$TMP/LICENSE" "$OUT/sprites/PKHeX-LICENSE.txt"
touch "$OUT/sprites/.pkhex-$PKHEX_TAG"
echo "sprites: $(ls "$OUT/sprites" | grep -c '\.png$') pictures from PKHeX $PKHEX_TAG"
