#!/bin/sh
# ROCKNIXDS: ES runs this when it starts and after every game (scripts/start, scripts/game-end: the menu has just
# opened), and when a game starts (scripts/game-start). media-auto.sh does the work, in the background: ES waits for
# its scripts.
M=/storage/.config/drastic/dsflip/media-auto.sh
[ -x $M ] || exit 0
case "$0" in
    */game-start/*) $M --stop ;;
    *) $M ;;
esac
exit 0
