#!/bin/sh
# ROCKNIXDS: the CPU clock while EmulationStation's menus are showing.
#
# ROCKNIX runs the menus at "performance" (all cores at 1992 MHz): its boot applies system.cpugovernor (which is also
# the default for games) as the very last step of its autostart, after any user hook, and its launcher switches to
# performance after every game. The menus need a fraction of that (measured 2026-09-28, the DS game list, 60 s:
# schedutil averaged 763 MHz instead of 1992 and the SoC ran cooler), so this puts the menus on schedutil. Games still
# get their own governor from ROCKNIX's per-system settings when they start.
# Runs when ES starts (ES's start scripts: after ROCKNIX's autostart, so at boot it isn't overridden), after every
# game ES launches (game-end scripts) and after a DS session (restore.sh; ES is stopped during those).
# Off: touch /storage/.config/rocknixds-menu-performance (the menus keep ROCKNIX's performance governor).
[ -e /storage/.config/rocknixds-menu-performance ] && exit 0
C=/sys/devices/system/cpu/cpufreq/policy0
grep -qw schedutil $C/scaling_available_governors 2>/dev/null && echo schedutil > $C/scaling_governor
exit 0
