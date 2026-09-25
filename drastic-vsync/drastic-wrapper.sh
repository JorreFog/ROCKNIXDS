#!/bin/sh
# DraStic launcher: drastouch (touch/shader hooks) + dvsync (vblank-locked pacing).
#
# While the game runs:
#  - sway composites 6 ms before vblank (dvsync assumes this; sway's default adds a frame)
#  - the GPU clock floor is raised to 600 MHz: at the ondemand governor's 200-400 MHz
#    the screen shader (e.g. lcd3x) finishes so late that every frame needs ~20 ms
# ROCKNIX quits games with `kill -9 drastic`, which would also kill cleanup in this
# script, so a detached watcher puts both settings back when DraStic exits.
# Run with DVSYNC=0 (or create ./nodvsync) for stock behaviour; DVSYNC_GPU_MIN overrides the clock floor.

SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)
GPU=/sys/class/devfreq/fde60000.gpu
PRELOAD="/storage/.config/drastic/libdrastouch.so"

# Off switch without ssh/env: touch /storage/.config/drastic/nodvsync for stock pacing.
# (With the 60.000 Hz panel DTB, stock pacing is a per-launch lottery: its fixed phase
# can sit on the compositor latch for minutes. The shim holds a good phase.)
[ -z "$DVSYNC" ] && [ -e /storage/.config/drastic/nodvsync ] && DVSYNC=0

if [ "${DVSYNC:-1}" != "0" ] && [ -n "$SWAYSOCK" ] && [ -f /storage/.config/drastic/libdvsync.so ]; then
    PRELOAD="/storage/.config/drastic/libdvsync.so:$PRELOAD"
    swaymsg -s "$SWAYSOCK" "output DSI-1 max_render_time 6; output DSI-2 max_render_time 6" >/dev/null 2>&1
    OLD_MIN=$(cat $GPU/min_freq 2>/dev/null)
    [ -n "$OLD_MIN" ] && echo "${DVSYNC_GPU_MIN:-600000000}" > $GPU/min_freq 2>/dev/null
    PARENT=$$
    setsid sh -c "
        while kill -0 $PARENT 2>/dev/null; do sleep 1; done
        swaymsg -s '$SWAYSOCK' 'output DSI-1 max_render_time off; output DSI-2 max_render_time off'
        [ -n '$OLD_MIN' ] && echo '$OLD_MIN' > $GPU/min_freq
    " </dev/null >/dev/null 2>&1 &
fi

export LD_PRELOAD="$PRELOAD"
exec /storage/.config/drastic/drastic.real "$@"
