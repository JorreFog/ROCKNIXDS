#!/bin/sh
# The ROCKNIXDS Store's session (systemd unit rocknixds-store, outside ES's process tree): takes the display from sway,
# runs the Store on the bare panels, then gives everything back, ES starting again with the tiles of what was
# installed. If the unit is stopped, its ExecStopPost (restore.sh) does the giving back. Döda Kvarter's session, less
# the fast-switch path.
D=/storage/.config/rocknixds/store
LOG=$D/data/session.log
NOTICE=/tmp/dsflip-notice           # restore.sh (dsflip's) shows this in ES once ES is back
mkdir -p $D/data
{
  echo "$(date) start"
  kill -9 $(pidof gptokeyb) 2>/dev/null
  systemctl stop essway.service sway.service
  # ROCKNIX's exit hotkey runs killall $(cat /tmp/.process-kill-data): TERM lets the Store finish its screen and quit
  echo "-TERM store" > /tmp/.process-kill-data
  rm -f $NOTICE
  trap 'kill -TERM $P 2>/dev/null; sleep 0.5; kill -9 $P 2>/dev/null; echo "$(date) stopped by the unit"; exit 0' TERM INT
  while :; do
    cd $D       # each time: an update of the Store swaps the folder, and the old one (this cwd) is gone
    # RNDS_STORE_NO_ES: ES is already stopped, the package manager leaves it alone. After the Store updates itself
    # it exits with 75 and the new one starts here.
    XDG_RUNTIME_DIR=/var/run/0-runtime-dir DK_DATA=$D/data DK_LOG_NAME=store.log RNDS_STORE_NO_ES=1 \
        RNDS_STORE_CLI=$D/rocknixds-store ./store --backend kms "$@" >> $D/data/store.out 2>&1 &
    P=$!
    wait $P; rc=$?
    echo "$(date) the Store exited: $rc"
    [ $rc = 75 ] && [ -x $D/store ] || break
    echo "$(date) updated to $(cat $D/VERSION 2>/dev/null): starting it"
  done
  case $rc in
    0|143) ;;
    3) echo "The Store couldn't take over the screens. Its log: $D/data/store.log" > $NOTICE ;;
    *) echo "The Store stopped unexpectedly (exit status $rc). Its log: $D/data/store.log" > $NOTICE ;;
  esac
  echo "-9 drastic" > /tmp/.process-kill-data       # the hotkey's usual target again
  $D/restore.sh
  echo "$(date) restored"
} >> $LOG 2>&1
