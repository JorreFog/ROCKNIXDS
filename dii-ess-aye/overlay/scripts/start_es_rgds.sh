#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 ROCKNIX (https://github.com/ROCKNIX)

### setup is the same, but faster: ROCKNIX's es_settings imports ~64 variables into systemd's environment with one
### `systemctl import-environment` call each (1.35 s of every ES start, including every return from a game). One
### call with all the names imports the same set (verified: identical `systemctl show-environment`) in ~30 ms.
_rgds_imports=
systemctl() {
    if [ "$1" = import-environment ]; then shift; _rgds_imports="$_rgds_imports $*"; else command systemctl "$@"; fi
}
. $(dirname $0)/es_settings
unset -f systemctl
[ -n "$_rgds_imports" ] && systemctl import-environment $_rgds_imports 2>/dev/null
unset _rgds_imports

# ES needs sway's outputs. systemd starts sway and this script together (at boot, and after a game restore.sh does
# the same); es_settings used to take 2 s, which hid the race. Wait for an active output (up to 10 s).
for i in $(seq 1 200); do
    SOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)
    [ -n "$SOCK" ] && XDG_RUNTIME_DIR=/var/run/0-runtime-dir swaymsg -s "$SOCK" -t get_outputs 2>/dev/null |
        grep -q '"active": true' && break
    sleep 0.05
done

# Patched ES (see es-rgds-uiwidth.patch): sizes popups, keyboard, sliders and the
# game options panel against one 640px screen instead of the 1920px canvas.
# Falls back to the stock binary if the patched one keeps crashing on startup.
ES_BIN=/storage/.config/emulationstation/themes/dii-ess-aye/bin/emulationstation
FAILS=/tmp/es-rgds-fails
REVEAL_DELAY=1          # after ES answers its API, before its window is shown. Measured: at 0-0.3 s the top panel is still
                        # black (ES hasn't drawn its first view); at 1 s both panels are complete

# Which ES: the patched one is built against one ROCKNIX release (bin/rocknix-version = its OS_VERSION). On another
# release it may not start, or misread newer settings, so run stock ES there and say why, once per release
# (touch /storage/.config/rocknixds-any-rocknix to run it anyway). Two quick crashes in a row also mean stock ES.
ES_FOR=$(cat "${ES_BIN%/*}/rocknix-version" 2>/dev/null)
OS_VER=$(. /etc/os-release 2>/dev/null; echo "$OS_VERSION")
USE_PATCHED=1 RGDS_NOTICE= RGDS_MARK=
if [ ! -x "$ES_BIN" ]; then
    USE_PATCHED=
elif [ -n "$ES_FOR" ] && [ "$ES_FOR" != "$OS_VER" ] && [ ! -e /storage/.config/rocknixds-any-rocknix ]; then
    USE_PATCHED=
    if [ "$(cat /storage/.config/rocknixds-es-notice 2>/dev/null)" != "$OS_VER" ]; then
        RGDS_NOTICE="ROCKNIXDS: its patched EmulationStation is built for ROCKNIX $ES_FOR and this is $OS_VER, so stock EmulationStation is running. The theme works, but menus and popups span both screens until a ROCKNIXDS release for this ROCKNIX."
        RGDS_MARK=/storage/.config/rocknixds-es-notice
    fi
elif [ "$(cat $FAILS 2>/dev/null || echo 0)" -ge 2 ]; then
    USE_PATCHED=
    if [ ! -e /tmp/es-rgds-fails-shown ]; then
        RGDS_NOTICE="ROCKNIXDS: the patched EmulationStation crashed twice while starting, so stock EmulationStation runs until the next reboot. Log: /var/log/es_log.txt"
        RGDS_MARK=/tmp/es-rgds-fails-shown
    fi
fi

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
    # Poll every 0.1 s (not 0.5): this runs on every boot and every return from a game.
    for i in $(seq 1 300); do
        SOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)
        [ -n "$SOCK" ] && swaymsg -s "$SOCK" -t get_tree 2>/dev/null | grep -q '"app_id": "emulationstation"' && break
        sleep 0.1
    done
    # The theme's sway config sends the new ES window to the scratchpad, so the boot splash
    # (the panels' sway background) shows while ES loads instead of a half-placed menu.
    # ES starts its HTTP API right before showing its first view: reveal it then, already
    # placed across both panels (one command = one frame, no visible move).
    for i in $(seq 1 450); do
        curl -s -m 1 -o /dev/null localhost:1234/isIdle && break
        sleep 0.1
    done
    sleep $REVEAL_DELAY
    LAYOUT='[app_id="emulationstation"] floating enable, fullscreen disable, move absolute position 0 0'
    swaymsg -s "$SOCK" "[app_id=\"emulationstation\"] scratchpad show, floating enable, fullscreen disable, move absolute position 0 0, focus" >/dev/null 2>&1
    swaymsg -s "$SOCK" "$LAYOUT, focus" >/dev/null 2>&1
    # The sway config's exec_always seat/touch setup for ES, now that its window exists. This used to be a sway
    # `reload`, which blocks sway for 2.1-2.5 s (measured): the panels froze just as ES appeared, on every boot and
    # every return from a game. Running the config's ES lines directly does the same without the reload.
    sed -n 's/^exec_always \(swaymsg .*emulationstation.*\)$/\1/p' /storage/.config/sway/config 2>/dev/null |
        while read -r cmd; do SWAYSOCK="$SOCK" sh -c "$cmd" >/dev/null 2>&1; done
    # stock ES sizes popups for the 1920 px canvas, wider than the panels: short centred lines stay visible
    if [ -n "$RGDS_NOTICE" ] &&
       curl -s -m 5 -X POST --data-binary "$(printf '%s\n' "$RGDS_NOTICE" | awk '{ n = split($0, w, " "); l = ""
           for (i = 1; i <= n; i++) if (l != "" && length(l) + length(w[i]) >= 44) { print l; l = w[i] } else l = l (l == "" ? "" : " ") w[i]
           print l }')" localhost:1234/messagebox >/dev/null; then
        echo "$OS_VER" > $RGDS_MARK
    fi
    [ -n "$SPLASH_PID" ] && { sleep 0.3; kill "$SPLASH_PID" 2>/dev/null; }
    # ES can be made fullscreen again later (e.g. when a game's window closes), which
    # puts the 1920px canvas on one panel = black/garbled menu. Undo it whenever it happens.
    swaymsg -s "$SOCK" -r -m -t subscribe '["window"]' 2>/dev/null | while read -r ev; do
        # the event carries the state from before the change, so check the tree
        case "$ev" in
            # ES destroys its window while a game runs and makes a new one afterwards; the
            # sway config hides new ES windows in the scratchpad, so bring it back placed
            # (the event may not carry the app_id yet, so check the tree for a hidden ES)
            *'"change": "new"'*)
                sleep 0.3
                if swaymsg -s "$SOCK" -t get_tree | python3 -c '
import json, sys
def walk(n):
    if n.get("app_id") == "emulationstation" and not n.get("visible"): sys.exit(0)
    for c in n.get("nodes", []) + n.get("floating_nodes", []): walk(c)
walk(json.load(sys.stdin)); sys.exit(1)'; then
                    swaymsg -s "$SOCK" '[app_id="emulationstation"] scratchpad show' >/dev/null 2>&1
                    swaymsg -s "$SOCK" "$LAYOUT, focus" >/dev/null 2>&1
                fi ;;
            *'"change": "fullscreen_mode"'*'"app_id": "emulationstation"'*)
                sleep 0.2
                swaymsg -s "$SOCK" -t get_tree | tr -d ' \n' | grep -q '"fullscreen_mode":1,[^}]*"app_id":"emulationstation"' &&
                    swaymsg -s "$SOCK" "$LAYOUT" >/dev/null 2>&1 ;;
        esac
    done
) &

if [ -n "$USE_PATCHED" ]; then
    export ES_UI_WIDTH=640
    START=$(date +%s)
    "$ES_BIN" --log-path /var/log --no-splash --resolution 1920 480
    RC=$?
    DUR=$(( $(date +%s) - START ))
    # Only two quick crashes in a row fall back to stock ES (a genuinely broken patched binary). A session that
    # ran a while is proof the binary is fine, so forget earlier counts -- otherwise a one-off early crash (e.g.
    # sway came up with no outputs) stuck us on stock ES until reboot, which drops theme features like the
    # clock <format> (the date then shows as the time).
    if [ $RC -ne 0 ] && [ $DUR -lt 30 ]; then
        echo $(( $(cat $FAILS 2>/dev/null || echo 0) + 1 )) > $FAILS
    elif [ $DUR -ge 120 ]; then
        rm -f $FAILS
    fi
    exit $RC
fi

emulationstation --log-path /var/log --no-splash --resolution 1920 480
