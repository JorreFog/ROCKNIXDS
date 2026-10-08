#!/bin/sh
# Gives the display back after the Store: ROCKNIXDS's own restore (dsflip/device/restore.sh: governors, the VT, sway
# with its outputs checked, ES, the notice) when it's installed, else the essentials. Safe to run twice (the session
# and the unit's ExecStopPost both call it).
R=/storage/.config/drastic/dsflip/restore.sh
[ -x $R ] && exec $R
systemctl is-active -q sway.service || systemctl start sway.service
systemctl is-active -q essway.service || systemctl start essway.service
exit 0
