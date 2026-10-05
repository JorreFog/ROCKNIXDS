#!/bin/sh
# Starts ROCKNIXDS Bank & Trade on the handheld (the Ports entry runs this). It runs in the sway session the menu runs
# in, as one window over both panels; the app places it with swaymsg. Arguments go to the app.
DIR=$(cd "$(dirname "$0")" && pwd)

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}
if [ -z "$SWAYSOCK" ]; then
    SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -n1)
    export SWAYSOCK
fi
if [ -z "$WAYLAND_DISPLAY" ]; then
    for s in "$XDG_RUNTIME_DIR"/wayland-*; do
        case "$s" in *.lock) continue ;; esac
        [ -S "$s" ] && { WAYLAND_DISPLAY=$(basename "$s"); export WAYLAND_DISPLAY; break; }
    done
fi
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-wayland}
export DOTNET_GCConserveMemory=5   # give memory back between legality checks

# the bottom panel is off under single-screen themes: on while the app runs, as it was afterwards
BOTTOM_WAS_OFF=
if [ -n "$SWAYSOCK" ] && command -v swaymsg >/dev/null; then
    if swaymsg -t get_outputs 2>/dev/null | tr -d ' \n' | grep -q '"name":"DSI-1"[^}]*"power":false'; then
        BOTTOM_WAS_OFF=1
        swaymsg output DSI-1 power on >/dev/null 2>&1
        sleep 0.3
    fi
    # touches on the whole layout (ROCKNIX's calibration puts them on the bottom panel), as for the menu
    swaymsg input type:touch map_to_output '*' >/dev/null 2>&1
fi

"$DIR/rocknixds-bank" "$@"
rc=$?

if [ -n "$BOTTOM_WAS_OFF" ]; then
    swaymsg output DSI-1 power off >/dev/null 2>&1
fi
exit $rc
