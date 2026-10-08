#!/bin/sh
# irqab.sh <tag> <layout> <cmd...>   (runs ON the device; tools/irqtest.sh puts it in front of hgpower.sh/stressgov.sh)
#
# Runs <cmd> with the hardware interrupts (and optionally DraStic's threads, and the scheduler's tick) laid out as
# <layout> says, puts everything back afterwards, and records what the interrupts did during the run.
#
# Why: on this GICv3 every interrupt is delivered to ONE core of its affinity mask, the first online one, so with
# ROCKNIX's default masks (all cores, no irqbalance) the display, touch, I2C, MMC, USB and PMIC interrupts and their
# softirqs all land on CPU0, and DraStic's busiest thread is free to run there too. The 1.4 interrupt storm (84,000/s on
# one core) showed how much one core's interrupts can cost.
#
# <layout>: comma-separated, any of
#   base           change nothing (the control run; interrupts are still recorded)
#   irq=<cpu>      every interrupt that can move goes to <cpu> (and new ones: default_smp_affinity)
#   app=<cpus>     DraStic's threads (drastic.real, every thread, re-applied each second) only on <cpus>, e.g. 1-3
#   hrtick=1       the scheduler's HRTICK feature: slices end on a high-resolution timer instead of the next 4 ms tick
#                  (CONFIG_HZ=250); what a 1000 Hz kernel would give the scheduler, without rebuilding one
#   slice=<ns>     the fair scheduler's base slice (/sys/kernel/debug/sched/base_slice_ns)
# e.g. "irq=3,app=0-2": the interrupts on CPU3, the game on the other three.
#
# Writes /storage/dsflip/probe/<tag>.irq/: layout, moved (the interrupts moved, with their effective CPU),
# {before,after}.{interrupts,softirqs,idle,stat} and secs; tools/irqtest-summary.py reads them.
P=/storage/dsflip/probe
TAG=${1:?tag} LAYOUT=${2:?layout}; shift 2
[ $# -gt 0 ] || { echo "irqab: no command" >&2; exit 2; }
O=$P/$TAG.irq SAVED=/tmp/irqab.saved DBG=/sys/kernel/debug/sched
rm -rf $O; mkdir -p $O
echo "$LAYOUT" > $O/layout

IRQ= APP= HRTICK= SLICE=
for kv in $(echo "$LAYOUT" | tr ',' ' '); do case $kv in
  base) ;;
  irq=*) IRQ=${kv#*=} ;;
  app=*) APP=${kv#*=} ;;
  hrtick=1) HRTICK=1 ;;
  slice=*) SLICE=${kv#*=} ;;
  *) echo "irqab: unknown layout item $kv" >&2; exit 2 ;;
esac; done
case $IRQ in ''|[0-9]) ;; *) echo "irqab: irq= takes one CPU (the GIC delivers to one anyway)" >&2; exit 2 ;; esac

snap() {   # snap <before|after>
    cat /proc/interrupts > $O/$1.interrupts
    cat /proc/softirqs > $O/$1.softirqs
    head -n 1 /proc/stat > $O/$1.stat
    for s in /sys/devices/system/cpu/cpu[0-9]*/cpuidle/state[0-9]*; do
        [ -d "$s" ] || continue
        c=${s%/cpuidle/*}; c=${c##*/}
        echo "$c ${s##*/} $(cat $s/name) $(cat $s/usage) $(cat $s/time)"
    done > $O/$1.idle
    cut -d' ' -f1 /proc/uptime > $O/$1.uptime
}

OLD_DEF= OLD_FEAT= OLD_SLICE= AP=
restore() {
    [ -n "$AP" ] && kill $AP 2>/dev/null
    if [ -f $SAVED ]; then
        while read -r n list; do echo "$list" > /proc/irq/$n/smp_affinity_list 2>/dev/null; done < $SAVED
        rm -f $SAVED
    fi
    [ -n "$OLD_DEF" ] && echo "$OLD_DEF" > /proc/irq/default_smp_affinity 2>/dev/null
    [ -n "$OLD_FEAT" ] && echo "$OLD_FEAT" > $DBG/features 2>/dev/null
    [ -n "$OLD_SLICE" ] && echo "$OLD_SLICE" > $DBG/base_slice_ns 2>/dev/null
    OLD_DEF= OLD_FEAT= OLD_SLICE= AP=
}

# a previous run that died before restoring
if [ -f $SAVED ]; then echo "irqab: restoring the affinities a previous run left"; restore; fi

if [ -n "$HRTICK$SLICE" ] && [ ! -d $DBG ]; then
    mount -t debugfs none /sys/kernel/debug 2>/dev/null
    [ -d $DBG ] || { echo "irqab: no $DBG (debugfs): can't set hrtick/slice" >&2; exit 4; }
fi

if [ -n "$IRQ" ]; then
    : > $SAVED
    for d in /proc/irq/[0-9]*; do
        n=${d##*/}
        echo "$n $(cat $d/smp_affinity_list)" >> $SAVED
        if echo "$IRQ" > $d/smp_affinity_list 2>/dev/null; then
            echo "$n $(cat $d/effective_affinity_list 2>/dev/null)"
        fi
    done > $O/moved
    OLD_DEF=$(cat /proc/irq/default_smp_affinity)
    printf '%x\n' $((1 << IRQ)) > /proc/irq/default_smp_affinity
fi
if [ -n "$HRTICK" ]; then
    grep -qw HRTICK $DBG/features && OLD_FEAT=HRTICK || OLD_FEAT=NO_HRTICK
    echo HRTICK > $DBG/features || { echo "irqab: can't enable HRTICK" >&2; restore; exit 4; }
fi
if [ -n "$SLICE" ]; then
    OLD_SLICE=$(cat $DBG/base_slice_ns)
    echo "$SLICE" > $DBG/base_slice_ns || { echo "irqab: can't set base_slice_ns" >&2; restore; exit 4; }
fi
# what the scheduler runs with, for the record
{ cat $DBG/features 2>/dev/null | tr ' ' '\n' | grep -i hrtick; echo "base_slice_ns $(cat $DBG/base_slice_ns 2>/dev/null)"; } > $O/sched

if [ -n "$APP" ]; then
    # every thread of drastic.real, each second (DraStic starts its threads after the wrapper sees the process)
    python3 - "$APP" > $O/app 2>&1 <<'PY' &
import os, sys, time
def cpus(s):
    out = set()
    for part in s.split(","):
        a, _, b = part.partition("-")
        out.update(range(int(a), int(b or a) + 1))
    return out
want, seen = cpus(sys.argv[1]), set()
while True:
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            if open(f"/proc/{p}/comm").read().strip() != "drastic.real":
                continue
            for t in os.listdir(f"/proc/{p}/task"):
                tid = int(t)
                if os.sched_getaffinity(tid) != want:
                    os.sched_setaffinity(tid, want)
                if tid not in seen:
                    seen.add(tid)
                    print(tid, open(f"/proc/{p}/task/{t}/comm").read().strip(), sorted(want), flush=True)
        except OSError:
            pass
    time.sleep(1)
PY
    AP=$!
fi

trap 'kill $C 2>/dev/null; wait $C; snap after; restore; exit 143' TERM INT
snap before
"$@" &
C=$!
wait $C; RC=$?
snap after
restore
echo "irqab: $TAG ($LAYOUT) done, exit $RC"
exit $RC
