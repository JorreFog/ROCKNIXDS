#!/bin/sh
# stall-checks.sh <rom-substring>: 1.6's handling of a stuck game (docs/handoff-local.md, section A, steps 2-5),
# checked unattended over ssh in about 5 minutes. Any DS game works; it is started through ES's API, so ES must be up.
#   1. The stall watch. DraStic's main thread stops 20 s in (DSFLIP_STALL_TEST): libdsflip's thread dump ~5 s later,
#      the game ended ~20 s after that, and the notice for ES.
#   2. The exit hotkey on a stalled game (after the dump): the game ends at once.
#   3. The exit hotkey on a stuck game before the dump, resume on: it ends ~3 s later, without a resume state.
#   4. A normal quit still saves and the next start resumes; a second press during the save quits at once.
#   5. DraStic frozen solid (SIGSTOP: libdsflip's threads too): session.sh ends it 15-25 s later, with its notice.
# One PASS/FAIL/SKIP line per check; the log lines behind each go to /storage/stall-checks-<date>.txt. The game's own
# saves are not touched, and a resume state the player had for the game is put back. dsflip.log ends up holding the
# last check's game. ES shows the notices of checks 1 and 5 once it's done: close them with A.
D=/storage/.config/drastic/dsflip
ROM=$(ls /storage/roms/nds/*.nds /storage/roms/nds/*.zip 2>/dev/null | grep -i -- "${1:?usage: stall-checks.sh <rom-substring>}" | head -n1)
[ -n "$ROM" ] || { echo "no rom matching $1"; exit 1; }
systemctl is-active -q dsflip-game && { echo "a game is running: leaving it alone"; exit 2; }
REP=/storage/stall-checks-$(date +%Y%m%d-%H%M%S).txt
GAME=$(basename "$ROM"); RSTATE="/storage/roms/savestates/nds/${GAME%.*}.resume.dss"
SLOG=$D/last-session.log
cleanup() {
    systemctl unset-environment DSFLIP_STALL_TEST 2>/dev/null
    P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P
    rm -f /tmp/rocknixds-testing /tmp/rocknixds-testing-resume "$RSTATE"
    [ -f "$RSTATE.stall-checks" ] && mv "$RSTATE.stall-checks" "$RSTATE"
}
trap cleanup EXIT; trap 'exit 1' HUP INT TERM
[ -f "$RSTATE" ] && mv "$RSTATE" "$RSTATE.stall-checks"
touch /tmp/rocknixds-testing /tmp/rocknixds-testing-resume   # test launches (no play stats), resume on all the same
log() { echo "$*" | tee -a $REP; }
lines() { echo "--- $1" >> $REP; grep -a "$2" $D/dsflip.log | cut -c1-200 | head -n 12 >> $REP; }
session() { tail -n +$((SL + 1)) $SLOG 2>/dev/null; }       # last-session.log since this check's launch
idle() { i=0; while ! curl -s -m 1 localhost:1234/isIdle | grep -q true && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; }
# launch [stall-at-s]: the game through ES; true once libdsflip has the panels (P = DraStic, S0 = when it was asked)
launch() {
    if [ -n "$1" ]; then systemctl set-environment DSFLIP_STALL_TEST=$1; else systemctl unset-environment DSFLIP_STALL_TEST; fi
    idle
    SL=$(cat $SLOG 2>/dev/null | wc -l)
    rm -f /tmp/dsflip-state                                  # session.sh deletes it too; libdsflip writes its verdict
    S0=$(date +%s)
    curl -s -m 5 -X POST --data-binary "$ROM" -o /dev/null localhost:1234/launch
    i=0; while [ ! -s /tmp/dsflip-state ] && [ $i -lt 200 ]; do sleep 0.1; i=$((i+1)); done
    P=$(pidof drastic || pidof drastic.real)
    grep -q '^ready' /tmp/dsflip-state 2>/dev/null && [ -n "$P" ]
}
alive() { [ -d /proc/$P ] && ! grep -q '^State:[[:space:]]*Z' /proc/$P/status 2>/dev/null; }
# dead_within <s>: DraStic exits within s seconds; T = how long it took, in tenths of a second
dead_within() { i=0; while alive && [ $i -lt $(($1 * 10)) ]; do sleep 0.1; i=$((i+1)); done; T=$i; ! alive; }
secs() { echo "$(($1 / 10)).$(($1 % 10)) s"; }
# until_log <s> <pattern>: waits for a dsflip.log line
until_log() { i=0; while ! grep -aq "$2" $D/dsflip.log && [ $i -lt $(($1 * 10)) ]; do sleep 0.1; i=$((i+1)); done; grep -aq "$2" $D/dsflip.log; }
unit_done() { i=0; while systemctl is-active -q dsflip-game && [ $i -lt 300 ]; do sleep 0.1; i=$((i+1)); done; }
pass=0 fail=0 skip=0
result() {
    case $1 in ok) pass=$((pass+1)); log "PASS $2" ;; skip) skip=$((skip+1)); log "SKIP $2" ;; *) fail=$((fail+1)); log "FAIL $2" ;; esac
}
log "stall-checks: $GAME (ROCKNIXDS $(cat /storage/.config/rocknixds-version 2>/dev/null))"

# ---- 1. the stall watch ---------------------------------------------------------------------------------------------
if launch 20; then
    if ! grep -aq 'stall watch:' $D/dsflip.log; then
        log "FAIL this libdsflip has no stall watch (not 1.6's): $(grep -a 'libdsflip' $D/dsflip.log | head -n1 | cut -c1-120)"
        exit 1
    fi
    lines "1: libdsflip's setup" 'stall watch:\|\[resume\] quit-with-save\|save-state control'
    dead_within 70; t=$(($(date +%s) - S0)); unit_done
    lines "1: the stall" '\[stall\] DSFLIP_STALL_TEST\|\[stall\] no frame\|\[stall\] still no frame'
    echo "    ($(grep -ac '\[stall\] thread' $D/dsflip.log) thread lines in the dump)" >> $REP
    session | grep 'notice:' >> $REP
    ok=ok
    grep -aq '\[stall\] no frame from DraStic for' $D/dsflip.log || ok=no
    grep -aq '\[stall\] still no frame after 20 s: ending the game' $D/dsflip.log || ok=no
    [ $t -ge 35 ] && [ $t -le 55 ] || ok=no
    session | grep -q 'notice: The game stopped responding (no picture for 20 seconds)' || ok=no
    result $ok "1 stall watch: the thread dump, then the game ended $t s after the start (expected ~40 s), its notice for ES"
else
    result no "1 stall watch: the game didn't start ($(cat /tmp/dsflip-state 2>/dev/null))"
fi

# resume on quit, as checks 2-4 need it: on for this game (ES), and DraStic's save state on a joystick button
RES=ok
grep -aq '\[resume\] quit-with-save on SIGUSR1' $D/dsflip.log || RES="resume on quit is off for this game (ES's per-game setting)"
grep -aq "save-state control isn't a joystick button" $D/dsflip.log && RES="DraStic's save state isn't on a joystick button"

# ---- 2. the exit hotkey on a stalled game ---------------------------------------------------------------------------
if [ "$RES" != ok ]; then result skip "2 exit hotkey on a stalled game: $RES"
elif launch 20 && until_log 40 '\[stall\] no frame'; then
    killall -USR1 drastic; dead_within 10; unit_done
    ok=ok; [ $T -le 15 ] || ok=no; [ -f "$RSTATE" ] && ok=no
    result $ok "2 exit hotkey on a stalled game: ended in $(secs $T) (expected at once), no resume state"
    rm -f "$RSTATE"
else
    result no "2 exit hotkey on a stalled game: no stall within 40 s ($(cat /tmp/dsflip-state 2>/dev/null))"
    P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; unit_done
fi

# ---- 3. the exit hotkey on a stuck game, before the dump --------------------------------------------------------
if [ "$RES" != ok ]; then result skip "3 exit hotkey before the dump: $RES"
elif launch 20 && until_log 35 'DSFLIP_STALL_TEST: DraStic'; then
    sleep 0.3; killall -USR1 drastic; dead_within 10; unit_done       # the dump would come 5 s after the stop
    lines "3: the quit" '\[resume\] quit requested\|\[stall\] no frame'
    ok=ok; [ $T -ge 25 ] && [ $T -le 45 ] || ok=no
    grep -aq "quit requested, but DraStic didn't take it within" $D/dsflip.log || ok=no
    grep -aq '\[stall\] no frame' $D/dsflip.log && ok=no
    [ -f "$RSTATE" ] && ok=no
    result $ok "3 exit hotkey before the dump: ended in $(secs $T) (expected ~3 s), before the dump, no resume state"
    rm -f "$RSTATE"
else
    result no "3 exit hotkey before the dump: the test stop didn't come within 35 s"
    P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; unit_done
fi

# ---- 4. a normal quit still saves, and the game resumes --------------------------------------------------------
if [ "$RES" != ok ]; then result skip "4 a normal quit saves: $RES"
elif launch; then
    sleep 30; killall -USR1 drastic; dead_within 15; unit_done
    lines "4: the quit" '\[resume\]'
    ok=ok; grep -aq '\[resume\] saved in' $D/dsflip.log && [ -f "$RSTATE" ] || ok=no
    result $ok "4a one press: saved and quit in $(secs $T)"
    if [ $ok = ok ] && launch; then
        sleep 12
        ok=ok; grep -aq '\[resume\] resumed\|loading the resume state' $D/dsflip.log || ok=no
        result $ok "4b the next start resumes"
        rm -f "$RSTATE"; sleep 8
        # the second press as soon as DraStic has started the save (its next frame): out at once, no resume state
        killall -USR1 drastic; until_log 3 '\[resume\] quit requested: saving'; killall -USR1 drastic
        dead_within 15; unit_done
        lines "4c: the quit" '\[resume\] quit\|\[resume\] saved'
        if grep -aq '\[resume\] saved in' $D/dsflip.log; then
            result skip "4c second press during the save: the save was done before it ($(secs $T))"
        else
            ok=ok; [ $T -le 10 ] || ok=no; [ -f "$RSTATE" ] && ok=no
            result $ok "4c second press during the save: ended in $(secs $T) (expected at once), no resume state"
        fi
    else
        result skip "4b/4c: need 4a"
        P=$(pidof drastic || pidof drastic.real); [ -n "$P" ] && kill -9 $P; unit_done
    fi
    rm -f "$RSTATE"
else
    result no "4 a normal quit saves: the game didn't start"
fi

# ---- 5. DraStic frozen solid, ended from outside ----------------------------------------------------------------
if launch; then
    sleep 12; kill -STOP $P; dead_within 40; unit_done
    session | grep 'wedged\|notice:' >> $REP
    ok=ok; [ $T -ge 120 ] && [ $T -le 350 ] || ok=no      # 15 checks ~1 s apart, slower on a busy handheld
    session | grep -q "presenter hasn't run for 15 s: DraStic is wedged" || ok=no
    session | grep -q 'notice: The game stopped responding and was closed' || ok=no
    result $ok "5 frozen solid (SIGSTOP): ended by session.sh after $(secs $T) (expected ~15-25 s), its notice for ES"
else
    result no "5 frozen solid: the game didn't start"
fi
log "stall-checks: $pass passed, $fail failed, $skip skipped. Report: $REP"
