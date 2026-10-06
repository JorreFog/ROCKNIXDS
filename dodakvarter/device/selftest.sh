#!/bin/sh
# Döda Kvarter's self-test on the RG DS, over ssh: /storage/.config/rocknixds/dodakvarter/selftest.sh
# The game's session takes the screens from ES as the game would, and instead of the game shows on each panel which
# one it is (TOP, BOTTOM), its size, a grid and colour bars; touches draw a cross where they land, the buttons light
# up as they're pressed, a beep plays every second. START and SELECT together end it (or 20 s), then ES comes back.
# What it saw (panels, the pad, both touchscreens, the sound) is in data/selftest.txt, printed here at the end.
D=/storage/.config/rocknixds/dodakvarter
[ -x $D/dodakvarter ] || { echo "Döda Kvarter isn't installed in $D"; exit 1; }
systemd-run --wait --unit=dodakvarter-selftest --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=10 \
    $D/session.sh --selftest >/dev/null 2>&1
cat $D/data/selftest.txt
