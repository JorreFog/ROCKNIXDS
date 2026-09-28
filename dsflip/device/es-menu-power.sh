#!/bin/sh
# ROCKNIXDS: ES runs this when it starts (installed in scripts/start: after ROCKNIX's autostart has applied its
# governor at boot) and after every game (scripts/game-end: ROCKNIX's launcher has just set performance).
[ -x /storage/.config/drastic/dsflip/menu-power.sh ] && /storage/.config/drastic/dsflip/menu-power.sh
exit 0
