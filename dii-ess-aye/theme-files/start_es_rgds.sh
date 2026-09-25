#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 ROCKNIX (https://github.com/ROCKNIX)

### setup is the same
. $(dirname $0)/es_settings

# Patched ES (see es-rgds-uiwidth.patch): sizes popups, keyboard, sliders and the
# game options panel against one 640px screen instead of the 1920px canvas.
# Falls back to the stock binary if the patched one keeps crashing on startup.
ES_BIN=/storage/.config/emulationstation/themes/dii-ess-aye/bin/emulationstation
FAILS=/tmp/es-rgds-fails

# Boot splash: a 1280x480 image across both panels while ES loads. The theme's sway config
# sends new swayimg windows to the scratchpad (so the first, unplaced frame never shows);
# bring it back already placed, in one command. ES is revealed over it, then it is closed.
SOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)
SPLASH=/storage/.config/emulationstation/themes/dii-ess-aye/assets/images/splash/rgds-splash.png
SPLASH_PID=
if [ -f "$SPLASH" ] && [ -n "$SOCK" ] && command -v swayimg >/dev/null; then
    swayimg -g 1280,480 -s real -c info.mode=off "$SPLASH" >/dev/null 2>&1 &
    SPLASH_PID=$!
    for i in $(seq 1 60); do
        swaymsg -s "$SOCK" '[app_id="swayimg" title="rgds-splash"] scratchpad show, floating enable, border none, move absolute position 0 0' 2>/dev/null | grep -q true && break
        sleep 0.05
    done
fi

# The theme's sway config places the 1920px window with a `reload` trick that can
# race (e.g. when another output had focus), leaving ES fullscreen on one panel.
# Re-apply the layout once the window exists.
(
    for i in $(seq 1 60); do
        sleep 0.5
        SOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)
        [ -n "$SOCK" ] && swaymsg -s "$SOCK" -t get_tree 2>/dev/null | grep -q '"app_id": "emulationstation"' && break
    done
    # The theme's sway config sends the new ES window to the scratchpad, so the boot splash
    # (the panels' sway background) shows while ES loads instead of a half-placed menu.
    # ES starts its HTTP API right before showing its first view: reveal it then, already
    # placed across both panels (one command = one frame, no visible move).
    for i in $(seq 1 90); do
        curl -s -m 1 -o /dev/null localhost:1234/isIdle && break
        sleep 0.5
    done
    sleep 1
    LAYOUT='[app_id="emulationstation"] floating enable, fullscreen disable, move absolute position 0 0'
    swaymsg -s "$SOCK" "[app_id=\"emulationstation\"] scratchpad show, floating enable, fullscreen disable, move absolute position 0 0, focus" >/dev/null 2>&1
    swaymsg -s "$SOCK" "$LAYOUT, focus" >/dev/null 2>&1
    swaymsg -s "$SOCK" reload >/dev/null 2>&1    # runs the config's exec_always seat/touch setup now that ES exists
    [ -n "$SPLASH_PID" ] && { sleep 0.3; kill "$SPLASH_PID" 2>/dev/null; }
    # ES can be made fullscreen again later (e.g. when a game's window closes), which
    # puts the 1920px canvas on one panel = black/garbled menu. Undo it whenever it happens.
    swaymsg -s "$SOCK" -r -m -t subscribe '["window"]' 2>/dev/null | while read -r ev; do
        # the event carries the state from before the change, so check the tree
        case "$ev" in
            *'"change": "fullscreen_mode"'*'"app_id": "emulationstation"'*)
                sleep 0.2
                swaymsg -s "$SOCK" -t get_tree | tr -d ' \n' | grep -q '"fullscreen_mode":1,[^}]*"app_id":"emulationstation"' &&
                    swaymsg -s "$SOCK" "$LAYOUT" >/dev/null 2>&1 ;;
        esac
    done
) &

if [ -x "$ES_BIN" ] && [ "$(cat $FAILS 2>/dev/null || echo 0)" -lt 2 ]; then
    export ES_UI_WIDTH=640
    START=$(date +%s)
    "$ES_BIN" --log-path /var/log --no-splash --resolution 1920 480
    RC=$?
    if [ $RC -ne 0 ] && [ $(( $(date +%s) - START )) -lt 30 ]; then
        echo $(( $(cat $FAILS 2>/dev/null || echo 0) + 1 )) > $FAILS
    fi
    exit $RC
fi

emulationstation --log-path /var/log --no-splash --resolution 1920 480
