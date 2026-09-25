#!/bin/sh
# hr.sh <tag> <hires 0|1> <secs> [extra env...]  -- run HeartGold via start_drastic.sh, dump dvsync log to /tmp/hr-<tag>.log
TAG=$1 HR=$2 SECS=$3; shift 3
CFG=/storage/.config/system/configs/system.cfg
cp $CFG /tmp/system.cfg.hrbak
sed -i "s/^nds.hires_3d=.*/nds.hires_3d=$HR/" $CFG
( set -a; . /etc/profile >/dev/null 2>&1; export DVSYNC_LOG=1 "$@"; set +a; setsid /usr/bin/start_drastic.sh "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" </dev/null >/tmp/hr.out 2>&1 & )
sleep $SECS
P=$(pidof drastic.real)
grep -E "^(hires_3d|threaded_3d)" /storage/.config/drastic/config/drastic.cfg | tr '\n' ' '
echo "gpu cur=$(cat /sys/class/devfreq/fde60000.gpu/cur_freq) cpu=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)"
top -b -n1 | grep -E "drastic" | head -3
kill -USR1 $P; sleep 2
pkill -9 drastic; sleep 3
cp /tmp/dvsync.log /tmp/hr-$TAG.log
cp /tmp/system.cfg.hrbak $CFG
