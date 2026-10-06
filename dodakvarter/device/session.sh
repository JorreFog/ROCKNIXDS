#!/bin/sh
# Döda Kvarter's session (systemd unit dodakvarter-game, outside ES's process tree): takes the display from sway,
# runs the game on the bare panels, then gives everything back. If the unit is stopped, its ExecStopPost
# (restore.sh) does the giving back. Modelled on ROCKNIXDS's DS session (dsflip/device/session.sh).
D=/storage/.config/rocknixds/dodakvarter
LOG=$D/data/session.log
NOTICE=/tmp/dsflip-notice           # restore.sh (dsflip's) shows this in ES once ES is back
mkdir -p $D/data
T0=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - T0) / 1000000 )); }
{
  echo "$(date) start"
  kill -9 $(pidof gptokeyb) 2>/dev/null
  if [ -e /storage/.config/drastic/dsflip/vt-switch ] && systemctl is-active -q sway.service; then
    # fast-switch: sway and ES stay up; switching the console away from sway's VT makes seatd release the display
    fgconsole > /tmp/dsflip-vt 2>/dev/null || echo 1 > /tmp/dsflip-vt
    python3 -c 'import fcntl, os; fcntl.ioctl(os.open("/dev/tty12", os.O_RDWR), 0x4B3A, 0)' 2>/dev/null   # KDSETMODE KD_TEXT
    chvt 12
    echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null
    echo "$(ms) ms: display released by VT switch"
  else
    systemctl stop essway.service sway.service
    echo "$(ms) ms: ES and sway stopped"
  fi
  # Nothing is drawn on the GPU in this game (the CPU draws every pixel and the display controller scans it out):
  # the Mali can sit at its lowest clock. The CPU follows the load (schedutil): a frame is a few ms of work.
  # restore.sh (dsflip's) puts both back from these files.
  GPU=/sys/class/devfreq/fde60000.gpu
  if [ -f $GPU/governor ]; then
    cat $GPU/governor > /tmp/dsflip-gpu-governor; cat $GPU/min_freq > /tmp/dsflip-gpu-min 2>/dev/null
    echo powersave > $GPU/governor 2>/dev/null
  fi
  CPU=/sys/devices/system/cpu/cpufreq/policy0
  [ -f /tmp/dsflip-cpu-governor ] || cat $CPU/scaling_governor > /tmp/dsflip-cpu-governor 2>/dev/null
  grep -qw schedutil $CPU/scaling_available_governors 2>/dev/null && echo schedutil > $CPU/scaling_governor 2>/dev/null
  # ROCKNIX's exit hotkey runs killall $(cat /tmp/.process-kill-data): TERM lets the game save and quit
  echo "-TERM dodakvarter" > /tmp/.process-kill-data
  rm -f $NOTICE
  trap 'kill -TERM $P 2>/dev/null; sleep 0.5; kill -9 $P 2>/dev/null; echo "$(date) stopped by the unit"; exit 0' TERM INT
  cd $D
  XDG_RUNTIME_DIR=/var/run/0-runtime-dir DK_DATA=$D/data ./dodakvarter --backend kms >> $D/data/game.out 2>&1 &
  P=$!
  wait $P; rc=$?
  echo "$(ms) ms: the game exited: $rc"
  case $rc in
    0|143) ;;                                       # quit from the menu, or the exit hotkey
    3) echo "Döda Kvarter couldn't take over the screens. Its log: $D/data/dodakvarter.log" > $NOTICE ;;
    *) echo "Döda Kvarter stopped unexpectedly (exit status $rc). Its log: $D/data/dodakvarter.log" > $NOTICE ;;
  esac
  echo "-9 drastic" > /tmp/.process-kill-data       # the hotkey's usual target again
  $D/restore.sh
  echo "$(date) restored"
} >> $LOG 2>&1
