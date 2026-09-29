#!/bin/sh
# kmsrun.sh <cmd...> -- run a command with DRM master: stop ES+sway, run, always restart them.
# Launch via: systemd-run --unit=dsflip-kms --collect /storage/dsflip/kmsrun.sh ...
L=/storage/dsflip/logs/kmsrun.log
echo "$(date) start: $*" > $L
S=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)     # panels off first (see session.sh)
[ -n "$S" ] && XDG_RUNTIME_DIR=/var/run/0-runtime-dir swaymsg -s "$S" output '*' power off >/dev/null 2>&1
systemctl stop essway.service sway.service >>$L 2>&1
sleep 1
timeout ${KMSRUN_TIMEOUT:-150} "$@" >>$L 2>&1; echo "exit $?" >>$L
systemctl start sway.service >>$L 2>&1
sleep 2
systemctl start essway.service >>$L 2>&1
echo "$(date) restored" >>$L
