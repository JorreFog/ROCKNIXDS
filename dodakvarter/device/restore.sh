#!/bin/sh
# Gives the display back after Döda Kvarter: ROCKNIXDS's own restore (dsflip/device/restore.sh: governors, the VT,
# sway with its outputs checked, ES, the display controller's storm check, the notice) when it's installed, else
# the essentials. Safe to run twice (the session and the unit's ExecStopPost both call it).
R=/storage/.config/drastic/dsflip/restore.sh
[ -x $R ] && exec $R
if [ -f /tmp/dsflip-vt ]; then chvt "$(cat /tmp/dsflip-vt)"; rm -f /tmp/dsflip-vt; fi
if [ -f /tmp/dsflip-gpu-governor ]; then cat /tmp/dsflip-gpu-governor > /sys/class/devfreq/fde60000.gpu/governor 2>/dev/null; rm -f /tmp/dsflip-gpu-governor /tmp/dsflip-gpu-min; fi
if [ -s /tmp/dsflip-cpu-governor ]; then cat /tmp/dsflip-cpu-governor > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null; rm -f /tmp/dsflip-cpu-governor; fi
systemctl is-active -q sway.service || systemctl start sway.service
systemctl is-active -q essway.service || systemctl start essway.service
exit 0
