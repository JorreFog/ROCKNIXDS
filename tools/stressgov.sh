#!/bin/sh
# stressgov.sh <tag> <secs> [VAR=value...]   (runs ON the device with the display free, via kmsrun.sh)
# The 3D stress ROM (stressrom/, dsstress-ramp.nds: level 1 to 10, one level per 300 emulated frames) through the
# installed libdsflip at 2x with the CPU governor "performance" as ROCKNIX sets it for DS games. Log:
# logs/sg-<tag>.log: presents per second and the CPU governor's decisions, to compare cpugov against a fixed clock.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic CFG=$CFGD/config/drastic.cfg
TAG=$1 SECS=$2; shift 2
C=/sys/devices/system/cpu/cpufreq/policy0
OLD_CG=$(cat $C/scaling_governor) OLD_CMAX=$(cat $C/scaling_max_freq)
echo performance > $C/scaling_governor
cp $CFG /tmp/drastic.cfg.sg; sed -i "s/^hires_3d = .*/hires_3d = 1/" $CFG
cd $CFGD
( export SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/sg-$TAG.log "$@"
  LD_PRELOAD=$CFGD/dsflip/libdsflip.so exec ./drastic.real $D/roms/dsstress-ramp.nds >$L/sg-$TAG.out 2>&1 ) &
P=$!
sleep 8; mv /tmp/drastic.cfg.sg $CFG
sleep $SECS
kill $P; sleep 1; kill -9 $P 2>/dev/null
echo $OLD_CG > $C/scaling_governor; echo $OLD_CMAX > $C/scaling_max_freq
