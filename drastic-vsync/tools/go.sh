#!/bin/sh
# go.sh start <hires> [env...] | stop <tag> | key "<keys>" | shot <name>
export XDG_RUNTIME_DIR=/var/run/0-runtime-dir WAYLAND_DISPLAY=wayland-1
SW="swaymsg -s $(ls /var/run/0-runtime-dir/sway-ipc.*.sock | head -1)"
CFG=/storage/.config/system/configs/system.cfg
case $1 in
start) HR=$2; shift 2
  [ -f /tmp/system.cfg.gobak ] || cp $CFG /tmp/system.cfg.gobak
  sed -i "s/^nds.hires_3d=.*/nds.hires_3d=$HR/" $CFG
  ( set -a; . /etc/profile >/dev/null 2>&1; export DVSYNC_LOG=1 "$@"; set +a; setsid /usr/bin/start_drastic.sh "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" </dev/null >/tmp/go.out 2>&1 & )
  sleep 12
  $SW seat seat0 attach "4660:22136:claude-vkbd" >/dev/null; $SW '[app_id="drastic"] focus' >/dev/null
  cp /tmp/system.cfg.gobak $CFG; rm /tmp/system.cfg.gobak ;;
stop) P=$(pidof drastic.real); kill -USR1 $P; sleep 2; pkill -9 drastic; sleep 2; cp /tmp/dvsync.log /tmp/go-$2.log ;;
key) echo "$2" > /tmp/vkbd ;;
shot) grim -o DSI-2 /tmp/top.png; grim -o DSI-1 /tmp/bot.png ;;
esac
