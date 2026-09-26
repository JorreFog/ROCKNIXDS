#!/bin/sh
# stocktouch.sh <secs>: stock sway + drastouch HeartGold session; logs raw bottom-touch evdev (evtest)
# next to the mouse-down coordinates drastouch hands DraStic (probe), to derive the stock mapping.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic
export XDG_RUNTIME_DIR=/var/run/0-runtime-dir WAYLAND_DISPLAY=wayland-1
cd $CFGD
( SDL_VIDEODRIVER=wayland SDL_VIDEO_DISPLAY_PRIORITY=DSI-2,DSI-1 SDL_TOUCH_MOUSE_EVENTS=0 DSHOOK_SHADER=lcd3x \
  DSPROBE_LOG=$L/stock.probe.log LD_PRELOAD=$D/libdsprobe.so:$CFGD/libdrastouch.so setsid ./drastic.real \
  "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" </dev/null >$L/stock.out 2>&1 & )
sleep 8
$D/vk.sh restart; sleep 1; $D/vk.sh key hold:l:0.6
for n in 1 2; do timeout $1 evtest /dev/input/event$n > $L/stock-ev$n.log 2>&1 & done
sleep $1
pkill drastic.real; pkill -f vkbd.py
