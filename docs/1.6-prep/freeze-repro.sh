#!/bin/sh
# freeze-repro.sh <rom-substring> [runs] [old|new]: the 2026-10-05 freeze's conditions, again and again, over ssh.
#
# Each run: start the game, let it play 25 s, quit it with the exit hotkey's signal (SIGUSR1: libdsflip saves a resume
# state), start it again (the resume state loads at frame 120, about 2 s in), with RetroAchievements unreachable
# (both logins fail without an answer, as they did then), and watch for 40 s. A run that stalls is the freeze: what
# every DraStic thread is doing goes to the report (libdsflip's own [stall] dump, plus gdb backtraces), then the game
# is ended. The RG DS Plus's CPU placement (session.sh, DSFLIP_PIN) is on, as in a normal session.
#   old: DSFLIP_SPREAD_THREADS=0, new threads inherit their creator's CPUs (the behaviour up to 1.5.13)
#   new: the 1.6 default, new threads start on every CPU
# Report: /storage/freeze-repro-<date>.txt. Restores the routes and the test flags on exit; the resume states it made
# are deleted, and one the player had for the game is put back (the game's own saves are not touched).
D=/storage/.config/drastic/dsflip
ROM=$(ls /storage/roms/nds/*.nds /storage/roms/nds/*.zip 2>/dev/null | grep -i -- "$1" | head -n1); [ -n "$ROM" ] || { echo "no rom matching $1"; exit 1; }
RUNS=${2:-5}; MODE=${3:-new}
systemctl is-active -q dsflip-game && { echo "a game is running: leaving it alone"; exit 2; }
REP=/storage/freeze-repro-$(date +%Y%m%d-%H%M%S).txt
GAME=$(basename "$ROM"); RSTATE="/storage/roms/savestates/nds/${GAME%.*}.resume.dss"
NETS="104.16.0.0/12 172.64.0.0/13"      # Cloudflare, where retroachievements.org lives
cleanup() {
    for n in $NETS; do ip route del unreachable $n 2>/dev/null; done
    systemctl unset-environment DSFLIP_SPREAD_THREADS DSFLIP_STALL_QUIT 2>/dev/null
    rm -f /tmp/rocknixds-testing /tmp/rocknixds-testing-resume "$RSTATE"
    [ -f "$RSTATE.freeze-repro" ] && mv "$RSTATE.freeze-repro" "$RSTATE"
}
trap cleanup EXIT; trap 'exit 1' HUP INT TERM
[ -f "$RSTATE" ] && mv "$RSTATE" "$RSTATE.freeze-repro"
touch /tmp/rocknixds-testing /tmp/rocknixds-testing-resume   # a test launch (no play stats), resume on all the same
[ "$MODE" = old ] && systemctl set-environment DSFLIP_SPREAD_THREADS=0
systemctl set-environment DSFLIP_STALL_QUIT=0               # keep a stalled DraStic alive for the backtraces
log() { echo "$*" | tee -a $REP; }
idle() { i=0; while ! curl -s -m 1 localhost:1234/isIdle | grep -q true && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; }
launch() {
    idle
    curl -s -m 5 -X POST --data-binary "$ROM" -o /dev/null localhost:1234/launch
    i=0; while ! systemctl is-active -q dsflip-game && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
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
    timeout 90 gdb -p $P -batch -ex "set pagination off" -ex "thread apply all bt 16" 2>&1 |
        grep -v '^\[New LWP\|^warning\|^Reading\|^$\|^\[Thread debugging\|^Using host\|No such file' | cut -c1-210 | head -n 220 >> $REP
    grep -a 'CPU placement\|power profile\|resuming' $D/last-session.log | tail -n 5 >> $REP
}
log "freeze-repro: $GAME, $RUNS runs, mode $MODE (ROCKNIXDS $(cat /storage/.config/rocknixds-version 2>/dev/null), libdsflip $(head -c 200 $D/dsflip.log | sed -n 's/.*libdsflip //p' | head -n1))"
stalls=0
for r in $(seq 1 $RUNS); do
    rm -f "$RSTATE"
    launch; sleep 25
    killall -USR1 drastic 2>/dev/null; gone                 # the exit hotkey with resume on: save, quit
    [ -f "$RSTATE" ] || { log "run $r: no resume state was saved (resume on quit off for this game?): stopping"; break; }
    for n in $NETS; do ip route add unreachable $n 2>/dev/null; done
    launch
    n=0 zero=0 stalled=0
    while [ $n -lt 40 ]; do
        sleep 1; n=$((n+1))
        l=$(grep -a 'present/s' $D/dsflip.log | tail -n1 | cut -c10-30)
        case "$l" in *"present/s=0.0"*) zero=$((zero+1)) ;; *) zero=0 ;; esac
        grep -aq '^\[stall\] no frame' $D/dsflip.log && stalled=1
        # a library without the stall watch (1.5.13): six seconds of no frames after it had shown some
        [ $zero -ge 6 ] && grep -aq 'present/s=[1-9]' $D/dsflip.log && ! grep -aq '^\[menu\] open' $D/dsflip.log && stalled=1
        [ $stalled = 1 ] && break
    done
    resumed=$(grep -ac '^\[resume\] resumed\|loading the resume state' $D/dsflip.log)
    if [ $stalled = 1 ]; then stalls=$((stalls+1)); dump $r; else log "run $r: ran 40 s after the resume load (resume lines: $resumed), $(grep -a 'present/s' $D/dsflip.log | tail -n1 | cut -c10-30)"; fi
    for n in $NETS; do ip route del unreachable $n 2>/dev/null; done
    P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; gone
done
log "freeze-repro: $stalls of $RUNS runs stalled (mode $MODE). Report: $REP"
