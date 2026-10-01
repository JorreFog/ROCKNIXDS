#!/bin/sh
# threadsample.sh: run ON the device in the background before a game (nohup setsid sh threadsample.sh &). Waits for
# DraStic, then once a second writes to /tmp/dsflip-threads2.log: the CPU frequency, /proc/stat per CPU, a few
# interrupt counters, and for every DraStic thread its comm, schedstat (run ns, run-queue wait ns, timeslices),
# state, (junk), CPU and scheduling policy. Read-only: safe during real play. This is how the Plus's scheduling
# collisions were found (2026-10-01: the main thread runnable-and-waiting 10% of the time on every core).
# Analyse: per-thread cpu% = d(run)/dt/1e7, rqwait% = d(wait)/dt/1e7; the "prio" field is junk (busybox $16).
OUT=/tmp/dsflip-threads2.log
: > $OUT
for i in $(seq 1 1800); do P=$(pidof drastic.real) && break; sleep 1; done
[ -n "$P" ] || exit 0
echo "# game pid $P start $(date +%s.%N)" >> $OUT
while [ -d /proc/$P ]; do
    printf "T %s freq %s\n" "$(date +%s.%N)" "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)" >> $OUT
    grep "^cpu" /proc/stat | sed "s/^/S /" >> $OUT
    grep -E "^ *(13|23|29|32|58|63|66|81|IPI0|IPI1|IPI5):" /proc/interrupts | sed "s/^/I /" >> $OUT
    for t in /proc/$P/task/*; do
        [ -r $t/schedstat ] || continue
        read run wait slices < $t/schedstat
        set -- $(cat $t/stat 2>/dev/null | sed "s/.*) //")   # fields from state (3) on: $1=state ... $16=prio $37=cpu $39=policy
        printf "D %s %s %s %s %s %s %s %s\n" "$(cat $t/comm)" "$run" "$wait" "$slices" "$1" "$16" "${37}" "${39}" >> $OUT
    done
    sleep 1
done
echo "# end $(date +%s.%N)" >> $OUT
