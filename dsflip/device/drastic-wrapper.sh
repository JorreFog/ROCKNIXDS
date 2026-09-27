#!/bin/sh
# DraStic launcher on the RG DS (installed as /storage/.config/drastic/drastic; start_drastic.sh runs it).
# Default: libdsflip. DraStic draws straight into both panels' scanout buffers (KMS), which needs sway
# stopped, so the game runs in a detached systemd unit that stops ES+sway and brings them back afterwards.
# ES's DraStic "shader" and "microphone sensitivity" choices (DSHOOK_SHADER, DSHOOK_MIC_THRESH, set by
# start_drastic.sh) are passed on to libdsflip.
# Off switch: touch /storage/.config/drastic/nodsflip (or DSFLIP=0) -> the previous launcher
# (drastic.dvsync: drastouch touch/shaders + dvsync pacing under sway).
D=/storage/.config/drastic
if [ "${DSFLIP:-1}" != "0" ] && [ ! -e $D/nodsflip ] && [ -f $D/dsflip/libdsflip.so ] && \
   systemd-run --unit=dsflip-game --collect --setenv=DSHOOK_SHADER="${DSHOOK_SHADER:-none}" \
     --setenv=DSHOOK_MIC_THRESH="${DSHOOK_MIC_THRESH:-0}" \
     -p ExecStopPost=$D/dsflip/restore.sh -p TimeoutStopSec=10 \
     $D/dsflip/session.sh "$@" >/dev/null 2>&1; then
    exec sleep 86400        # stopping ES (from the unit) ends this, start_drastic.sh and gptokeyb too
fi
exec $D/drastic.dvsync "$@"
