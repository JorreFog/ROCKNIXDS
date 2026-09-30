#!/bin/sh
# First time the menu comes up, ask whether a performance log may be uploaded when a game quits.
# ES runs this from its start scripts and may wait for it, so the question itself runs beside the menu.
CFG=/storage/.config/system/configs/system.cfg
v=$(grep '^nds\.share_performance_logs=' "$CFG" 2>/dev/null | tail -n1 | cut -d= -f2)
[ -n "$v" ] && exit 0
[ -e /tmp/rocknixds-testing ] && exit 0
[ -f /storage/.config/drastic/dsflip/perf-session.py ] || exit 0
systemd-run --collect --quiet python3 -u /storage/.config/drastic/dsflip/perf-session.py ask >/dev/null 2>&1
exit 0
