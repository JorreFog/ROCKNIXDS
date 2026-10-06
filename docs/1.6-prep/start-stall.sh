#!/bin/sh
# start-stall.sh <rom-substring> [runs] [seconds]: a game started fresh, again and again, watched for a stall.
#
# On 2026-10-06 a Pokemon HeartGold started from the menu on an RG DS Plus (1.6's library, no resume state) showed no
# frame from about 2 s in: libdsflip's stall watch put up its card and ended the game 20 s later, as it should, but the
# log of that run was gone before anyone read it. This starts the game through ES, lets it run [seconds] (15), and
# ends it; a run in which libdsflip reports a stall gets what freeze-repro.sh collects (libdsflip's [stall] dump, every
# thread's state and CPUs, gdb backtraces) in /storage/start-stall-<date>.txt, and that run's dsflip.log is kept next
# to it. No resume state is made or loaded; one the player had is put back afterwards.
D=/storage/.config/drastic/dsflip
ROM=$(ls /storage/roms/nds/*.nds /storage/roms/nds/*.zip 2>/dev/null | grep -i -- "${1:?usage: start-stall.sh <rom-substring> [runs] [seconds]}" | head -n1)
[ -n "$ROM" ] || { echo "no rom matching $1"; exit 1; }
RUNS=${2:-30}; SECS=${3:-15}
systemctl is-active -q dsflip-game && { echo "a game is running: leaving it alone"; exit 2; }
REP=/storage/start-stall-$(date +%Y%m%d-%H%M%S).txt
GAME=$(basename "$ROM"); RSTATE="/storage/roms/savestates/nds/${GAME%.*}.resume.dss"
cleanup() {
    systemctl unset-environment DSFLIP_STALL_QUIT 2>/dev/null
    [ -n "$HIDE" ] && kill $HIDE 2>/dev/null
    rm -f /tmp/rocknixds-testing /tmp/dsflip-hold
    [ -f "$RSTATE.start-stall" ] && mv "$RSTATE.start-stall" "$RSTATE"
}
trap cleanup EXIT; trap 'exit 1' HUP INT TERM
[ -f "$RSTATE" ] && mv "$RSTATE" "$RSTATE.start-stall"
touch /tmp/rocknixds-testing                                # test launches: no play stats, no resume
( while kill -0 $$ 2>/dev/null; do rm -f /tmp/dsflip-notice.[0-9]* 2>/dev/null; sleep 0.1; done ) & HIDE=$!   # as freeze-repro.sh
systemctl set-environment DSFLIP_STALL_QUIT=0               # keep a stalled DraStic alive for the backtraces
log() { echo "$*" | tee -a $REP; }
idle() {                                                     # as in freeze-repro.sh
    i=0; while { systemctl is-active -q dsflip-game || systemctl is-active -q dsflip-vtback; } && [ $i -lt 300 ]; do sleep 0.1; i=$((i+1)); done
    EP=$(pidof emulationstation | cut -d' ' -f1); i=0; ok=0
    while [ $ok -lt 15 ] && [ $i -lt 300 ]; do
        sc=; read sc _ < /proc/$EP/syscall 2>/dev/null
        case "$sc" in 260|"") ok=0 ;; *) ok=$((ok+1)) ;; esac
        sleep 0.1; i=$((i+1))
    done
    i=0; while ! curl -s -m 1 localhost:1234/isIdle | grep -q true && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done
}
gone() { i=0; while systemctl is-active -q dsflip-game && [ $i -lt 200 ]; do sleep 0.1; i=$((i+1)); done; }
dump() {
    P=$(pidof drastic || pidof drastic.real)
    log "=== run $1: DraStic (pid $P) stopped presenting. libdsflip's lines:"
    grep -a '^\[stall\]\|^\[resume\]\|^\[ra\]\|^\[rc\]\|^\[ui\]\|^\[cpugov\] savestate' $D/dsflip.log | tail -n 80 >> $REP
    log "=== threads: tid name state cpu-ticks allowed-cpus | syscall"
    for t in /proc/$P/task/*; do
        echo "${t##*/} $(cat $t/comm) $(sed 's/.*) //' $t/stat | awk '{print $1, $12+$13}') $(grep Cpus_allowed_list $t/status | cut -f2) | $(cut -c1-60 $t/syscall 2>/dev/null)"
    done >> $REP
    log "=== backtraces"
    touch /tmp/dsflip-hold          # gdb stops every thread: session.sh would take that for a wedge after 15 s
    timeout 90 gdb -p $P -batch -ex "set pagination off" -ex "thread apply all bt 16" 2>&1 |
        grep -v '^\[New LWP\|^warning\|^Reading\|^$\|^\[Thread debugging\|^Using host\|No such file' | cut -c1-210 | head -n 260 >> $REP
    rm -f /tmp/dsflip-hold
    grep -a 'CPU placement\|power profile\|3D renderer' $D/last-session.log | tail -n 4 >> $REP
    cp $D/dsflip.log "${REP%.txt}-run$1-dsflip.log"; cp $D/drastic.out "${REP%.txt}-run$1-drastic.out" 2>/dev/null
}
log "start-stall: $GAME, $RUNS runs of $SECS s (ROCKNIXDS $(cat /storage/.config/rocknixds-version 2>/dev/null))"
stalls=0 bad=0
for r in $(seq 1 $RUNS); do
    idle
    rm -f /tmp/dsflip-state
    curl -s -m 5 -X POST --data-binary "$ROM" -o /dev/null localhost:1234/launch
    i=0; while [ ! -s /tmp/dsflip-state ] && [ $i -lt 200 ]; do sleep 0.1; i=$((i+1)); done
    if ! grep -q '^ready' /tmp/dsflip-state 2>/dev/null; then
        bad=$((bad+1)); log "run $r: the game didn't start ($(cat /tmp/dsflip-state 2>/dev/null))"
        P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; gone; continue
    fi
    n=0 stalled=0
    while [ $n -lt $SECS ]; do
        sleep 1; n=$((n+1))
        grep -aq '^\[stall\] no frame' $D/dsflip.log && { stalled=1; break; }
        pidof drastic >/dev/null || pidof drastic.real >/dev/null || break
    done
    if [ $stalled = 1 ]; then stalls=$((stalls+1)); log "run $r: STALLED $n s in"; dump $r
    else echo "run $r: ok, $(grep -a 'present/s' $D/dsflip.log | tail -n1 | cut -c10-25)"; fi
    P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; gone
done
log "start-stall: $stalls of $RUNS runs stalled, $bad didn't start. Report: $REP"
