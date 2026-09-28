#!/bin/sh
# ROCKNIXDS: ES runs this when it starts (installed in scripts/start: after ROCKNIX's autostart has applied its
# governor at boot) and after every game (scripts/game-end: ROCKNIX's launcher has just set performance).
# once ES is idle, so ES's own start and the way back from a game run at the full clock; from a transient unit
# because ES may wait for this script
[ -x /storage/.config/drastic/dsflip/menu-power.sh ] && \
    systemd-run --collect --quiet /storage/.config/drastic/dsflip/menu-power.sh --after-es-idle >/dev/null 2>&1
exit 0
