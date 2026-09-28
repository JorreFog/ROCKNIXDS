#!/bin/sh
# hgpower.sh <tag> <secs> [VAR=value...]   (runs ON the device, with the display free: see tools/power.sh)
#
# One power/CPU/GPU measurement of a DS game session, as the player gets it: the INSTALLED libdsflip, GPU clock set
# the way session.sh sets it, HeartGold from savestate 0, walking (walker.py). An isolated copy of the ROM, the
# savestate and the save (HGtest.*) is used, so the real save is never written.
# Options: DSFLIP_SHADER=<name> (default none), CPUGOV=<governor> (default: as configured), CPUMAX=<kHz>,
#          GPUGOV=<governor> / GPUMIN=<Hz> (override session.sh's choice), HIRES=0 (1x), any DSFLIP_* variable.
# Writes /storage/dsflip/probe/<tag>.json (powerprobe) and <tag>.txt (that plus the frame stats).
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic P=/storage/dsflip/probe
TAG=$1 SECS=$2; shift 2
mkdir -p $P $D/roms
SS=/storage/roms/savestates/nds
[ -f $D/roms/HGtest.nds ] || cp "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" $D/roms/HGtest.nds
cp "$SS/Pokemon - HeartGold Version (USA)_0.dss" $SS/HGtest_0.dss
cp "/storage/roms/nds/Pokemon - HeartGold Version (USA).dsv" /storage/roms/nds/HGtest.dsv
SHADER=none CPUGOV= CPUMAX= GPUGOV= GPUMIN= HIRES=
for a in "$@"; do case $a in
  DSFLIP_SHADER=*) SHADER=${a#*=} ;; CPUGOV=*) CPUGOV=${a#*=} ;; CPUMAX=*) CPUMAX=${a#*=} ;;
  GPUGOV=*) GPUGOV=${a#*=} ;; GPUMIN=*) GPUMIN=${a#*=} ;; HIRES=*) HIRES=${a#*=} ;;
esac; done
C=/sys/devices/system/cpu/cpufreq/policy0 G=/sys/class/devfreq/fde60000.gpu
OLD_CG=$(cat $C/scaling_governor) OLD_CMAX=$(cat $C/scaling_max_freq) OLD_GG=$(cat $G/governor) OLD_GMIN=$(cat $G/min_freq)
[ -n "$CPUGOV" ] && echo $CPUGOV > $C/scaling_governor
[ -n "$CPUMAX" ] && echo $CPUMAX > $C/scaling_max_freq
# session.sh's GPU choice (keep in sync with dsflip/device/session.sh)
case $SHADER in
  none|bilinear) GOV=powersave; MIN= ;;
  ds-fsr) GOV=performance; MIN= ;;
  *) GOV=simple_ondemand; MIN=400000000 ;;
esac
[ -n "$GPUGOV" ] && GOV=$GPUGOV; [ -n "$GPUMIN" ] && MIN=$GPUMIN
echo $GOV > $G/governor; [ -n "$MIN" ] && echo $MIN > $G/min_freq
CFG=$CFGD/config/drastic.cfg
[ "$HIRES" = 0 ] && { cp $CFG /tmp/drastic.cfg.hgpower; sed -i "s/^hires_3d = .*/hires_3d = 0/" $CFG; }
cd $CFGD
( export SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/hp-$TAG.log DSFLIP_SHADER=$SHADER "$@"
  LD_PRELOAD=$CFGD/dsflip/libdsflip.so exec ./drastic.real $D/roms/HGtest.nds >$L/hp-$TAG.out 2>&1 ) &
PID=$!
# ROCKNIX's powerstate re-applies a GPU profile when the charger status flips: keep this run's choice (as session.sh)
( while kill -0 $PID 2>/dev/null; do sleep 2; [ "$(cat $G/governor)" = $GOV ] || echo $GOV > $G/governor; done ) &
sleep 8; [ -f /tmp/drastic.cfg.hgpower ] && mv /tmp/drastic.cfg.hgpower $CFG
python3 $D/padkey.py 312 0.6                 # load state 0 (L2 held)
sleep 3; python3 $D/padkey.py 304 0.2; sleep 1; python3 $D/padkey.py 304 0.2; sleep 1   # B B: out of the save dialog
python3 $D/walker.py $((SECS + 10)) &
W=$!
sleep 10                                       # settle: clocks, temperatures
M=$(wc -l < $L/hp-$TAG.log)
python3 $D/powerprobe.py $SECS $TAG > $P/$TAG.txt 2>&1
# frames over the probe window: presents/s and drops/s
tail -n +$((M + 1)) $L/hp-$TAG.log | grep present/s | awk -v t=$TAG '
  { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] } n++; p += v["present/s"]; d += v["dropped"] }
  END { if (n) printf "frames: %d s, %.1f presents/s, %.2f drops/s\n", n, p / n, d / n }' >> $P/$TAG.txt
kill $W 2>/dev/null
kill $PID; sleep 1; kill -9 $PID 2>/dev/null
echo $OLD_CG > $C/scaling_governor; echo $OLD_CMAX > $C/scaling_max_freq
echo $OLD_GG > $G/governor; echo $OLD_GMIN > $G/min_freq
rm -f /storage/roms/nds/HGtest.dsv $SS/HGtest_0.dss
cat $P/$TAG.txt
