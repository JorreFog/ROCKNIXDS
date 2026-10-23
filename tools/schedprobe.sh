#!/bin/sh
# schedprobe.sh [secs]   (runs ON the device; tools/irqtest.sh runs it first and keeps its output)
#
# What the running kernel does about ticks, preemption, idle and interrupts, read from the device itself (ROCKNIX's
# config in its repo can be newer than what's installed): the CONFIG_ lines that matter (/proc/config.gz), the
# scheduler's features and slice, the clock source, the cpuidle states, every interrupt with its affinity and the CPU
# it is actually delivered to, the threaded interrupt handlers, and <secs> (default 10) of interrupts per second per
# CPU with whatever is running (the menu, or a game).
SECS=${1:-10}
DBG=/sys/kernel/debug/sched
[ -d $DBG ] || mount -t debugfs none /sys/kernel/debug 2>/dev/null
echo "== kernel"; uname -a; cat /proc/cmdline
echo "== config"
zcat /proc/config.gz 2>/dev/null | grep -E '^(# )?CONFIG_(HZ|HZ_[0-9]+|NO_HZ[A-Z_]*|HZ_PERIODIC|PREEMPT[A-Z_]*|HIGH_RES_TIMERS|SCHED_HRTICK|SCHED_AUTOGROUP|SCHED_CLASS_EXT|UCLAMP_TASK|CPU_IDLE_GOV_[A-Z]+|IRQ_FORCED_THREADING|RCU_NOCB_CPU|RCU_LAZY|IRQ_TIME_ACCOUNTING|VIRT_CPU_ACCOUNTING_GEN|CPU_FREQ_GOV_[A-Z]+)[= ]' \
    || echo "(no /proc/config.gz)"
echo "== clocksource"; cat /sys/devices/system/clocksource/clocksource0/current_clocksource
echo "== scheduler"
tr ' ' '\n' < $DBG/features 2>/dev/null | grep -iE 'hrtick|wakeup|run_to_parity|place|delay' | tr '\n' ' '; echo
for f in base_slice_ns migration_cost_ns nr_migrate; do [ -f $DBG/$f ] && echo "$f $(cat $DBG/$f)"; done
echo "sched_autogroup_enabled $(cat /proc/sys/kernel/sched_autogroup_enabled 2>/dev/null)"
echo "sched_rt_runtime_us $(cat /proc/sys/kernel/sched_rt_runtime_us 2>/dev/null)"
echo "== cpufreq"; for f in scaling_governor scaling_cur_freq scaling_max_freq; do echo "$f $(cat /sys/devices/system/cpu/cpufreq/policy0/$f)"; done
echo "== cpuidle"
echo "driver $(cat /sys/devices/system/cpu/cpuidle/current_driver 2>/dev/null) governor $(cat /sys/devices/system/cpu/cpuidle/current_governor 2>/dev/null)"
for s in /sys/devices/system/cpu/cpu0/cpuidle/state[0-9]*; do
    [ -d "$s" ] && echo "${s##*/} $(cat $s/name) latency=$(cat $s/latency)us residency=$(cat $s/residency)us disable=$(cat $s/disable)"
done
echo "== irqbalance"; pidof irqbalance >/dev/null && echo running || echo "not running"
echo "== interrupts: affinity -> effective"
echo "default_smp_affinity $(cat /proc/irq/default_smp_affinity)"
for d in /proc/irq/[0-9]*; do
    n=${d##*/}
    name=$(grep "^ *$n:" /proc/interrupts | sed 's/.*  //')
    echo "$n aff=$(cat $d/smp_affinity_list 2>/dev/null) eff=$(cat $d/effective_affinity_list 2>/dev/null) $name"
done
echo "== threaded handlers"; ps -eo pid,cls,rtprio,psr,comm 2>/dev/null | grep -E ' irq/' || ps | grep ' \[irq/'
echo "== interrupts per second, $SECS s"
cat /proc/interrupts > /tmp/schedprobe.a; sleep $SECS; cat /proc/interrupts > /tmp/schedprobe.b
awk -v s=$SECS 'NR == FNR { if (FNR > 1) { k = $1; for (i = 2; i <= 5; i++) a[k, i] = $i }; next }
    FNR == 1 { next }
    { k = $1; line = ""; tot = 0
      for (i = 2; i <= 5; i++) { r = ($i - a[k, i]) / s; tot += r; line = line sprintf(" %8.0f", r) }
      if (tot >= 1) { $1 = $2 = $3 = $4 = $5 = ""; printf "%-6s%s  %s\n", k, line, $0 } }' /tmp/schedprobe.a /tmp/schedprobe.b
rm -f /tmp/schedprobe.a /tmp/schedprobe.b
