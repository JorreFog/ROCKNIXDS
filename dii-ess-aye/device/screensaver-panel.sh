#!/bin/sh
# ROCKNIXDS: the bottom panel's backlight off while the screensaver runs (es-rgds-screensaver.patch puts the screensaver
# on the top panel and leaves the bottom one black, but lit). ES runs this from scripts/screensaver-start (backlight
# off), and from scripts/screensaver-stop and scripts/wake (back on). It only turns back on a backlight it turned off
# itself: on a one-panel theme the launcher keeps the bottom panel off (start_es_rgds.sh), and that stays as it is.
# scripts/start and scripts/game-start restore it too, so a crash or a game launched from the screensaver never leaves
# the panel dark.
# Not `swaymsg output DSI-1 power off`, the launcher's way: ES's window lives on DSI-1, and with that output off sway
# stops sending it frame callbacks, so ES stops drawing and the slideshow on the top panel freezes too. The panel keeps
# scanning out black; its backlight is most of what it draws.
# The bottom panel's backlight is the one its DSI panel node points to (dsi@fe060000 is DSI-1, the bottom panel, on
# the RK3566); backlight0 if the device tree doesn't say.
MARK=/tmp/rocknixds-screensaver-backlight-off
bottom_backlight() {
    want=$(od -An -tx4 /proc/device-tree/dsi@fe060000/panel@0/backlight 2>/dev/null)
    for b in /sys/class/backlight/*; do
        n=/proc/device-tree/${b##*/}
        [ -n "$want" ] && [ "$(od -An -tx4 $n/phandle 2>/dev/null)" = "$want" ] && { echo $b; return; }
    done
    echo /sys/class/backlight/backlight0
}
B=$(bottom_backlight)
[ -w $B/bl_power ] || exit 0

case "$0" in
    */screensaver-start/*)
        # only a backlight that is on now (bl_power 0): a panel the launcher switched off stays as it is
        if [ "$(cat $B/bl_power)" = 0 ]; then
            echo 4 > $B/bl_power && echo $B > $MARK
        fi
        ;;
    *)
        if [ -f $MARK ]; then
            echo 0 > "$(cat $MARK)/bl_power"
            rm -f $MARK
        fi
        ;;
esac
exit 0
