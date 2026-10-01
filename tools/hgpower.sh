#!/bin/sh
# hgpower.sh <tag> <secs> [VAR=value...]   (runs ON the device, with the display free: see tools/power.sh)
#
# One power/CPU/GPU measurement of a DS game session, as the player gets it: the INSTALLED libdsflip, GPU clock set
# the way session.sh sets it, HeartGold from savestate 0, walking (walker.py). An isolated copy of the ROM, the
# savestate and the save (HGtest.*) is used, so the real save is never written.
# Options: GAME=hg|b2 (HeartGold, default, or Black 2: B2test.*, a heavier game), DSFLIP_SHADER=<name> (default
#          none), CPUGOV=<governor> (default: as configured), CPUMAX=<kHz>,
#          GPUGOV=<governor> / GPUMIN=<Hz> (override session.sh's choice), HIRES=0 (1x), LIB=<libdsflip.so to test
#          instead of the installed one>, any DSFLIP_* variable.
# Writes /storage/dsflip/probe/<tag>.json (powerprobe) and <tag>.txt (that plus the frame stats).
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic P=/storage/dsflip/probe
TAG=$1 SECS=$2; shift 2
# never alongside a real game: padkey.py and walker.py write into the real gamepad's evdev node, which every reader
# sees, so a player's DraStic would get the load-state press (L2) and the walking (it did once)
if systemctl is-active -q dsflip-game || pidof drastic.real >/dev/null || pidof drastic >/dev/null; then
    echo "hgpower: a game is running: not starting" >&2; exit 3
fi
mkdir -p $P $D/roms
SS=/storage/roms/savestates/nds
GAME=hg; for a in "$@"; do case $a in GAME=*) GAME=${a#*=} ;; esac; done
case $GAME in
  b2) SRC="Pokemon Black Version 2 (DSi Enhanced)" TEST=B2test ;;
  *)  SRC="Pokemon - HeartGold Version (USA)" TEST=HGtest ;;
esac
[ -f $D/roms/$TEST.nds ] || cp "/storage/roms/nds/$SRC.nds" $D/roms/$TEST.nds
cp "$SS/${SRC}_0.dss" $SS/${TEST}_0.dss
cp "/storage/roms/nds/$SRC.dsv" /storage/roms/nds/$TEST.dsv
SHADER=none CPUGOV= CPUMAX= GPUGOV= GPUMIN= HIRES= PWRATE= LIB=$CFGD/dsflip/libdsflip.so
for a in "$@"; do case $a in
  GAME=*) ;;
  DSFLIP_SHADER=*) SHADER=${a#*=} ;; CPUGOV=*) CPUGOV=${a#*=} ;; CPUMAX=*) CPUMAX=${a#*=} ;;
  GPUGOV=*) GPUGOV=${a#*=} ;; GPUMIN=*) GPUMIN=${a#*=} ;; HIRES=*) HIRES=${a#*=} ;; PWRATE=*) PWRATE=${a#*=} ;;
  LIB=*) LIB=${a#*=} ;;
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
# PWRATE=<Hz>: PipeWire's graph forced to that rate for the run (clock.force-rate), reset after. Unset: as the installed
# session.sh does (1.4 forces 44.1 kHz, 1.3 doesn't); PWRATE=0: not forced.
[ -z "$PWRATE" ] && grep -q "clock.force-rate 44100" $CFGD/dsflip/session.sh 2>/dev/null && PWRATE=44100
[ "$PWRATE" = 0 ] && PWRATE=
PWM() { XDG_RUNTIME_DIR=/var/run/0-runtime-dir pw-metadata -n settings 0 clock.force-rate $1 >/dev/null 2>&1; }
[ -n "$PWRATE" ] && PWM $PWRATE
CFG=$CFGD/config/drastic.cfg
[ "$HIRES" = 0 ] && { cp $CFG /tmp/drastic.cfg.hgpower; sed -i "s/^hires_3d = .*/hires_3d = 0/" $CFG; }
# A kill between the edits above and the restore at the end used to leave the GPU governor, PipeWire rate
# or hires_3d changed. Put them back on any exit.
restore_limits() {
  [ -n "$GW" ] && kill $GW 2>/dev/null
  [ -f /tmp/drastic.cfg.hgpower ] && mv /tmp/drastic.cfg.hgpower $CFG
  echo "$OLD_CG" > $C/scaling_governor 2>/dev/null
  echo "$OLD_CMAX" > $C/scaling_max_freq 2>/dev/null
  echo "$OLD_GG" > $G/governor 2>/dev/null
  echo "$OLD_GMIN" > $G/min_freq 2>/dev/null
  [ -n "$PWRATE" ] && PWM 0
}
trap restore_limits EXIT INT TERM
cd $CFGD
( export SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/hp-$TAG.log DSFLIP_SHADER=$SHADER "$@"
  LD_PRELOAD=$LIB exec ./drastic.real $D/roms/$TEST.nds >$L/hp-$TAG.out 2>&1 ) &
PID=$!
# ROCKNIX's powerstate re-applies a GPU profile when the charger status flips: keep this run's choice (as session.sh)
( while kill -0 $PID 2>/dev/null; do sleep 2; [ "$(cat $G/governor)" = $GOV ] || echo $GOV > $G/governor; done ) &
GW=$!
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
restore_limits
trap - EXIT INT TERM
# the audio pump's lines over the run (ring range, rate trim, underruns)
grep "\[audio\] pump [0-9]" $L/hp-$TAG.log | tail -n 4 >> $P/$TAG.txt
rm -f /storage/roms/nds/$TEST.dsv $SS/${TEST}_0.dss
cat $P/$TAG.txt
