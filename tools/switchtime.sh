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
    b=$(until_ '! pidof sway >/dev/null')
    c=$(until_ 'grep -q . /tmp/dsflip-state 2>/dev/null')
    d=$(until_ 'grep -q "screen texture" $D/dsflip.log 2>/dev/null')
    sleep 6
    T0=$(now)
    kill -9 $(pidof drastic.real) 2>/dev/null
    e=$(until_ '! systemctl is-active -q dsflip-game')
    f=$(until_ 'outputs')
    g=$(until_ 'curl -s -m 1 localhost:1234/isIdle 2>/dev/null | grep -q true')
    h=$(until_ 'es_visible')
    printf '%-5s | %-7s %-7s %-7s %-7s | %-7s %-7s %-7s %-7s\n' $k $a $b $c $d $e $f $g $h
    grep -E '^[0-9]+ ms:' $D/last-session.log | tail -n 2 | sed 's/^/        session.sh: /'
    sleep 3
    k=$((k + 1))
done
EOF
