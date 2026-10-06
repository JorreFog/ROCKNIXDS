#!/bin/sh
# Builds the 1.6 release artwork into "1.6 artwork/" at the top of the repo. Needs python3 with Pillow and numpy, and node with Playwright.
#
# The inputs that aren't on main yet (Döda Kvarter's sprites and screenshots, the Bank's screenshots, 1.6's fixed
# pixel font) are taken from their branches into ref/ the first time; delete ref/ to take them again.
set -e
cd "$(dirname "$0")"
DK=origin/claude/zombie-shooter-sweden-kgklz6
BANK=origin/claude/pokemon-bank-trading-app-3rpffe
PREP=origin/claude/1-6-prep-work-kwq9lc
if [ ! -d ref ]; then
    git fetch -q origin "${DK#origin/}" "${BANK#origin/}" "${PREP#origin/}"
    mkdir -p ref/fonts ref/dk-art
    for f in PixelifySans-Regular PixelifySans-Medium; do
        git show "$PREP:dii-ess-aye/themes/rocknixds-pixel-dark/rnds/fonts/$f.ttf" > "ref/fonts/$f.ttf"
    done
    for f in bank-boxes bank-trade bank-lobbies bank-report; do
        git show "$BANK:docs/img/$f.png" > "ref/$f.png"
    done
    for f in $(git ls-tree --full-tree --name-only "$DK" dodakvarter/docs/img/); do
        git show "$DK:$f" > "ref/dk-$(basename "$f")"
    done
    for f in $(git ls-tree --full-tree --name-only "$DK" dodakvarter/art/); do
        git show "$DK:$f" > "ref/dk-art/$(basename "$f")"
    done
fi
for s in hero whats_new perf res3x bank doda discord contributors thanks; do
    echo "$s"
    python3 "$s.py"
done
