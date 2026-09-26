#!/bin/sh
# dsrun.sh <tag> <rom-substring> <hires 0|1> <fastforward 0|1> <secs> [ENV=val ...]
# <rom-substring> matches in /storage/roms/nds; an absolute path (e.g. a dsstress ROM) skips savestate + walking.
# Runs drastic.real with libdsprobe first in the preload chain (stock pacing, no dvsync),
# loads savestate 0, walks left/right, and samples per-thread CPU of DraStic and sway.
# DSRUN_PAD=1: load state + walk via the real gamepad (padkey.py), for KMS runs without sway.
# DSRUN_DUMP=1: SIGUSR2 DraStic at the end (libdsflip dumps both scanout buffers).
# DSPROBE_POST=lib.so goes right after the probe (e.g. libdsflip.so); DSRUN_NOTOUCH=1 drops drastouch.
# Output: /storage/dsflip/logs/<tag>.probe.log and <tag>.cpu.txt
TAG=$1 ROMPAT=$2 HR=$3 FF=$4 SECS=$5; shift 5
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic CFG=$CFGD/config/drastic.cfg
case $ROMPAT in /*) ROM=$ROMPAT; NOPLAY=1 ;; *) ROM=$(ls /storage/roms/nds/*.nds | grep -i "$ROMPAT" | head -1) ;; esac
export XDG_RUNTIME_DIR=/var/run/0-runtime-dir WAYLAND_DISPLAY=wayland-1
SW="swaymsg -s $(ls /var/run/0-runtime-dir/sway-ipc.*.sock | head -1)"


cp $CFG /tmp/drastic.cfg.dsrun
sed -i -e "s/^hires_3d = .*/hires_3d = $HR/" -e "s/^fast_forward = .*/fast_forward = $FF/" $CFG

cd $CFGD
( export SDL_VIDEODRIVER=wayland SDL_VIDEO_DISPLAY_PRIORITY=DSI-2,DSI-1 SDL_TOUCH_MOUSE_EVENTS=0 \
         DSHOOK_SHADER=${DSHOOK_SHADER:-lcd3x} DSPROBE_LOG=$L/$TAG.probe.log "$@"
  TOUCH=$CFGD/libdrastouch.so; [ -n "$DSRUN_NOTOUCH" ] && TOUCH=
  LD_PRELOAD="${DSPROBE_PRE:+$DSPROBE_PRE:}$D/libdsprobe.so${DSPROBE_POST:+:$DSPROBE_POST}${TOUCH:+:$TOUCH}" \
    setsid ./drastic.real "$ROM" </dev/null >$L/$TAG.out 2>&1 & )
sleep 8
[ -z "$NOPLAY" ] && [ -z "$DSRUN_PAD" ] && $D/vk.sh restart     # fresh uinput keyboard: an empty queue, attached to seat0, DraStic focused
cp /tmp/drastic.cfg.dsrun $CFG      # restore early; DraStic has already read it
P=$(pidof drastic.real)
if [ -z "$NOPLAY" ] && [ -n "$DSRUN_PAD" ]; then   # no sway (KMS): drive the real gamepad instead
  python3 $D/padkey.py 312 0.6; sleep 4             # L2 = load state
  ( while :; do python3 $D/padkey.py 546 2; python3 $D/padkey.py 547 2; done ) &
  WALK=$!
  sleep 4
elif [ -z "$NOPLAY" ]; then
  $D/vk.sh key hold:l:0.6; sleep 4   # a short tap doesn't register as load-state
  ( while :; do $D/vk.sh key "hold:left:2 hold:right:2"; sleep 4.5; done ) &
  WALK=$!
  sleep 4
fi

snap() {  # pid -> "tid comm ticks" lines
  for t in /proc/$1/task/*; do
    s=$(cat $t/stat 2>/dev/null) || continue
    c=$(cat $t/comm)
    set -- $(echo "$s" | sed 's/^.*) //')
    echo "$(basename $t) $c $(( ${12} + ${13} ))"
  done
}
SP=$(pidof sway)
snap $P > /tmp/a.d; snap $SP > /tmp/a.s; T0=$(cut -d' ' -f1 /proc/uptime)
sleep $SECS
[ -n "$DSRUN_DUMP" ] && { kill -USR2 $P; sleep 1; }
snap $P > /tmp/b.d; snap $SP > /tmp/b.s; T1=$(cut -d' ' -f1 /proc/uptime)
{
  echo "tag=$TAG rom=$ROM hires=$HR ff=$FF secs=$SECS env=$* temp=$(cat /sys/class/thermal/thermal_zone0/temp) cpu=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) gpu=$(cat /sys/class/devfreq/fde60000.gpu/cur_freq)"
  for w in d s; do
    [ $w = d ] && echo "-- drastic.real threads (% of one core)" || echo "-- sway threads"
    awk -v T="$T0 $T1" 'BEGIN{split(T,t," "); el=t[2]-t[1]} NR==FNR{a[$1]=$3; next} {d=$3-a[$1]; if(d>0){printf "%6s %-16s %5.1f\n",$1,$2,d/el; tot+=d}} END{printf "  total %.1f%%\n", tot/el}' /tmp/a.$w /tmp/b.$w
  done
} > $L/$TAG.cpu.txt
[ -n "$WALK" ] && { kill $WALK 2>/dev/null; [ -z "$DSRUN_PAD" ] && pkill -f vkbd.py; }
kill $P; sleep 1; kill -9 $P 2>/dev/null; sleep 1
cat $L/$TAG.cpu.txt
