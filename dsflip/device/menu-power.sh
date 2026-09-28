#!/bin/sh
# ROCKNIXDS: the CPU clock while EmulationStation's menus are showing.
#
# ROCKNIX leaves the CPU governor at "performance" (all cores at 1992 MHz) in the menus: it doesn't apply one at
# boot, and its launcher switches back to performance after every game. system.cpugovernor is the default for GAMES,
# not for the menus. The menus need a fraction of that (measured 2026-09-28, the DS game list, 60 s: schedutil
# averaged 763 MHz instead of 1992 and the SoC ran cooler), so this puts the menus on schedutil. Games still get
# their own governor from ROCKNIX's per-system settings when they start.
# Runs at boot (autostart hook), after every game ES launches (ES's game-end scripts) and after a DS session
# (restore.sh; ES is stopped during those, so its game-end never fires).
# Off: touch /storage/.config/rocknixds-menu-performance (the menus keep ROCKNIX's performance governor).
[ -e /storage/.config/rocknixds-menu-performance ] && exit 0
C=/sys/devices/system/cpu/cpufreq/policy0
grep -qw schedutil $C/scaling_available_governors 2>/dev/null && echo schedutil > $C/scaling_governor
exit 0
