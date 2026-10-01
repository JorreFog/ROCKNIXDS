#!/bin/sh
# switchtime.sh: how long switching into and out of a DS game takes on a device, step by step. Run from a PC.
#
#   tools/switchtime.sh <device-ip> [cycles] [rom-substring]
#
# Each cycle launches a game through ES's HTTP API and polls (every 20 ms, on the device) for the milestones of the
# switch: our game unit starting, ES and sway gone, libdsflip's verdict, DraStic's first screen texture. It then
# quits the way ROCKNIX's exit hotkey does (kill -9) and times the way back: the unit ending, sway with outputs,
# ES answering its API, and ES's window visible again (the theme keeps it hidden in sway's scratchpad until then).
# Times are seconds since the launch request / since the kill. Needs ES idle and no game running.
# RGDS_SSH: an ssh wrapper if you use an askpass (as for smoke.sh).
set -u
IP=${1:?usage: switchtime.sh <device-ip> [cycles] [rom-substring]}
N=${2:-3}
ROMPAT=${3:-}
SSH=${RGDS_SSH:-"ssh -o ConnectTimeout=5 root@$IP"}
$SSH "N=$N ROMPAT='$ROMPAT' sh -s" <<'EOF'
D=/storage/.config/drastic/dsflip; RT=/var/run/0-runtime-dir
ROM=$(ls /storage/roms/nds/*.nds 2>/dev/null | grep -i -- "$ROMPAT" | head -n1)
[ -n "$ROM" ] || { echo "no ROM matching '$ROMPAT'"; exit 1; }
curl -s -m 2 localhost:1234/isIdle | grep -q true || { echo "ES isn't idle"; exit 1; }
systemctl is-active -q dsflip-game && { echo "a game is running"; exit 1; }
echo "ROM: $ROM"
touch /tmp/rocknixds-testing; trap 'rm -f /tmp/rocknixds-testing' EXIT   # no play stats for these launches
now() { date +%s%N; }
sec() { echo $(( ($(now) - T0) / 1000000 )) | awk '{ printf "%6.2f", $1 / 1000 }'; }
# poll for a condition (up to 30 s), record when it became true
until_() { i=0; while ! eval "$1" && [ $i -lt 1500 ]; do sleep 0.02; i=$((i + 1)); done; eval "$1" && sec || echo "   n/a"; }
sock() { ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1; }
outputs() { S=$(sock); [ -n "$S" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -t get_outputs 2>/dev/null | grep -q '"active": true'; }
es_visible() {
    S=$(sock); [ -n "$S" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -t get_tree 2>/dev/null | python3 -c '
import json, sys
def walk(n):
    if n.get("app_id") == "emulationstation" and n.get("visible"): sys.exit(0)
    for c in n.get("nodes", []) + n.get("floating_nodes", []): walk(c)
walk(json.load(sys.stdin)); sys.exit(1)'
}
printf '%-5s | %-7s %-7s %-7s %-7s | %-7s %-7s %-7s %-7s\n' cycle unit sway-off ready texture unit-off outputs ES-api ES-shown
k=1
while [ $k -le $N ]; do
    rm -f /tmp/dsflip-state
    T0=$(now)
    curl -s -X POST --data-binary "$ROM" localhost:1234/launch >/dev/null
    a=$(until_ 'systemctl is-active -q dsflip-game')
    b=$(until_ '! pidof sway >/dev/null || [ "$(cat /sys/class/tty/tty0/active)" != tty1 ]')   # sway stopped, or (VT mode) the console left its VT
    c=$(until_ 'grep -q . /tmp/dsflip-state 2>/dev/null')
    d=$(until_ 'grep -q "screen texture" $D/dsflip.log 2>/dev/null')
    sleep 6
    T0=$(now); KT=$(cut -d' ' -f1 /proc/uptime)
    kill -9 $(pidof drastic || pidof drastic.real) 2>/dev/null
    # the way back, all four polled together: each is recorded when it first holds (one after the other, a milestone
    # couldn't be seen before the previous one, e.g. ES answering before the game unit had ended)
    e= f= g= h=; i=0
    while { [ -z "$e" ] || [ -z "$f" ] || [ -z "$g" ] || [ -z "$h" ]; } && [ $i -lt 600 ]; do
        [ -z "$e" ] && ! systemctl is-active -q dsflip-game && e=$(sec)
        [ -z "$f" ] && outputs && f=$(sec)
        [ -z "$g" ] && curl -s -m 1 localhost:1234/isIdle 2>/dev/null | grep -q true && g=$(sec)
        [ -z "$h" ] && [ -n "$f" ] && es_visible && h=$(sec)
        sleep 0.02; i=$((i + 1))
    done
    printf '%-5s | %-7s %-7s %-7s %-7s | %-7s %-7s %-7s %-7s\n' $k $a $b $c $d ${e:-n/a} ${f:-n/a} ${g:-n/a} ${h:-n/a}
    grep -E '^[0-9]+ ms:' $D/last-session.log | tail -n 2 | sed 's/^/        session.sh: /'
    echo "        killed at [$KT] (uptime); the session's own steps:"
    awk -v kt="$KT" '/^\[[0-9.]+\] / { u = substr($1, 2, length($1) - 2) + 0; if (u >= kt) printf "        %+6.2f s  %s\n", u - kt, substr($0, length($1) + 2) }' $D/last-session.log | tail -n 12
    awk -v kt="$KT" '/^\[[0-9.]+\] / { u = substr($1, 2, length($1) - 2) + 0; if (u >= kt) printf "        %+6.2f s  ES launcher: %s\n", u - kt, substr($0, length($1) + 2) }' /tmp/es-rgds-launch.log 2>/dev/null
    sleep 3
    k=$((k + 1))
done
EOF
