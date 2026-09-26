#!/bin/sh
# vk.sh restart | key "<keys>" -- fresh uinput keyboard (empty queue) attached to seat0 and focused on DraStic
export XDG_RUNTIME_DIR=/var/run/0-runtime-dir
SW="swaymsg -s $(ls /var/run/0-runtime-dir/sway-ipc.*.sock | head -1)"
case $1 in
restart)
  pkill -f vkbd.py; rm -f /tmp/vkbd; sleep 0.3
  nohup setsid python3 /storage/drastic-vsync/vkbd.py </dev/null >/tmp/vkbd.out 2>&1 &
  i=0; while [ ! -p /tmp/vkbd ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
  sleep 0.5
  $SW seat seat0 attach "4660:22136:claude-vkbd" >/dev/null
  $SW '[app_id="drastic"] focus' >/dev/null ;;
key) echo "$2" > /tmp/vkbd ;;
esac
