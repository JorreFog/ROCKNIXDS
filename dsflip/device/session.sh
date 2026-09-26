#!/bin/sh
# libdsflip game session (runs as systemd unit dsflip-game, outside ES's process tree).
# Stops ES + sway, runs DraStic with libdsflip on the bare panels, then always brings sway + ES back.
# The exit hotkey (killall -9 drastic) works because DraStic runs through a symlink named "drastic".
D=/storage/.config/drastic
LOG=$D/dsflip/last-session.log
ROM="$1"
{
  echo "$(date) start: $ROM"
  systemctl stop essway.service; systemctl stop sway.service
  sleep 0.5
  # the Mali GPU does nothing in this mode (the display controller scans out DraStic's buffers), so don't
  # keep it at ROCKNIX's "performance" 800 MHz: that's only heat
  GPU=/sys/class/devfreq/fde60000.gpu
  GPU_GOV=$(cat $GPU/governor 2>/dev/null)
  [ -n "$GPU_GOV" ] && echo powersave > $GPU/governor 2>/dev/null
  cd $D
  SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$D/dsflip/dsflip.log \
    LD_PRELOAD=$D/dsflip/libdsflip.so $D/dsflip/drastic "$ROM" > $D/dsflip/drastic.out 2>&1 &
  P=$!
  # libdsflip couldn't take the display (no DRM master etc.): don't leave a black screen
  sleep 3
  if grep -q passthrough $D/dsflip/dsflip.log 2>/dev/null; then echo "passthrough -> abort"; kill -9 $P; fi
  wait $P; echo "drastic exited: $?"
  [ -n "$GPU_GOV" ] && echo "$GPU_GOV" > $GPU/governor 2>/dev/null
  systemctl start sway.service
  sleep 2
  systemctl start essway.service
  echo "$(date) restored"
} >> $LOG 2>&1
