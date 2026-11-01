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
# start_drastic.sh ends with `kill -9 $(pidof gptokeyb)`, and that line is the script's exit status. runemu.sh
# keeps 0 (and 137/143) and turns every other status into 1, and ES records play count, time played and last
# played only when the launch exits 0 (FileData::launchGame). session.sh kills gptokeyb as the game starts (it
# isn't used, and it used to add a second to the way back), so that kill finds nothing and fails: a DS game that
# was quit normally comes back as a failed launch and the stats stay "never played" (issues #50, #62, #88). GBA
# goes through RetroArch, which exits 0, so only DS was affected. pidof matches the executable's own name, so a
# copy of python named gptokeyb is what the kill finds. It sleeps 30 s at most; the kill reaps it at once. A
# gptokeyb that is still running is left to that kill.
hold_gptokeyb_for_launcher() {
  pidof gptokeyb >/dev/null 2>&1 && return 0
  command -v python3 >/dev/null 2>&1 || return 0
  py=$(readlink -f "$(command -v python3)") || return 0
  [ -f "$py" ] || return 0
  rm -rf /tmp/rocknixds-gptokeyb-hold.* 2>/dev/null
  dir=$(mktemp -d /tmp/rocknixds-gptokeyb-hold.XXXXXX) || return 0
  cp -f "$py" "$dir/gptokeyb" || { rm -rf "$dir"; return 0; }
  # the copy's path is not python's, so the standard library is found from its real prefix
  PYTHONHOME=$(dirname "$(dirname "$py")") "$dir/gptokeyb" -c 'import time; time.sleep(30)' >/dev/null 2>&1 &
  i=0
  while ! pidof gptokeyb >/dev/null 2>&1 && [ "$i" -lt 50 ]; do sleep 0.02; i=$((i + 1)); done
  pidof gptokeyb >/dev/null 2>&1
}
# host test: spawn the hold and return, without starting a game
[ "$ROCKNIXDS_WRAPPER_HOLD_TEST" = 1 ] && { hold_gptokeyb_for_launcher; exit $?; }
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
    ec=$?
    hold_gptokeyb_for_launcher || true
    exit $ec
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
hold_gptokeyb_for_launcher || true
exit $r
