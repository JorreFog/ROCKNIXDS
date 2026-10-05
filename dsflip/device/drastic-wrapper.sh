#!/bin/sh
# DraStic launcher on the RG DS (installed as /storage/.config/drastic/dsflip/drastic-wrapper.sh; start_drastic.sh
# runs /storage/.config/drastic/drastic, drastic-launch, which execs this without LD_PRELOAD).
# Default: libdsflip. DraStic draws straight into both panels' scanout buffers (KMS), which needs sway
# stopped, so the game runs in a detached systemd unit that stops ES+sway and brings them back afterwards.
# ES's DraStic "shader" and "microphone sensitivity" choices (DSHOOK_SHADER, DSHOOK_MIC_THRESH, set by
# start_drastic.sh) are passed on to libdsflip.
# Off switch: touch /storage/.config/drastic/nodsflip (or DSFLIP=0) -> the previous launcher
# (drastic.dvsync: drastouch touch/shaders + dvsync pacing under sway).
D=/storage/.config/drastic
# VT mode (fast switching, on by default since 1.5.13; dsflip/fast-switch off turns it off): ES and sway stay up; the
# session switches the console to another VT so seatd takes the display from sway, and back afterwards. The menu is
# back about a second after a quit, whatever the size of the library: nothing restarts and nothing is reloaded.
# The patched ES keeps its window and renderer for such a launch (es-rgds-keepwindow.patch: a DS game run by DraStic
# while $D/dsflip/vt-switch exists) and says so with RGDS_ES_KEEPS_WINDOW=1 in our environment; only then is this
# path taken. Stock ES, or the patched one with fast switching off, tears its window down for the game, and its GL
# re-init after a VT switch fails: those launches take the stop/start path below, so both sides always agree. ES is
# simply waiting for this command here, so wait for the game instead of being stopped.
if [ "${DSFLIP:-1}" != "0" ] && [ ! -e $D/nodsflip ] && [ -f $D/dsflip/libdsflip.so ] && [ "$RGDS_ES_KEEPS_WINDOW" = 1 ]; then
    # VT mode only: do not fall through into the non-VT path (that stops ES/sway) if the unit fails.
    systemd-run --wait --unit=dsflip-game --collect --setenv=DSHOOK_SHADER="${DSHOOK_SHADER:-none}" \
      --setenv=DSHOOK_MIC_THRESH="${DSHOOK_MIC_THRESH:-0}" --setenv=DSFLIP_VT=1 \
      -p ExecStopPost=$D/dsflip/restore.sh -p TimeoutStopSec=10 \
      $D/dsflip/session.sh "$@" >/dev/null 2>&1
    exit $?
fi
if [ "${DSFLIP:-1}" != "0" ] && [ ! -e $D/nodsflip ] && [ -f $D/dsflip/libdsflip.so ] && \
   systemd-run --unit=dsflip-game --collect --setenv=DSHOOK_SHADER="${DSHOOK_SHADER:-none}" \
     --setenv=DSHOOK_MIC_THRESH="${DSHOOK_MIC_THRESH:-0}" \
     -p ExecStopPost=$D/dsflip/restore.sh -p TimeoutStopSec=10 \
     $D/dsflip/session.sh "$@" >/dev/null 2>&1; then
    exec sleep 86400        # stopping ES (from the unit) ends this, start_drastic.sh and gptokeyb too
fi
# Not exec: start_drastic.sh's "kill -9 $(pidof gptokeyb)" afterwards runs with libdrastouch preloaded, so with the
# mic on pidof crashes and gptokeyb outlives the game. Here LD_PRELOAD is gone (drastic-launch), so stop it here.
$D/drastic.dvsync "$@"
r=$?
kill -9 $(pidof gptokeyb) 2>/dev/null
exit $r
