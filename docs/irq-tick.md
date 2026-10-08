# Interrupts and the kernel's tick

Two questions about the CPU under DraStic that the earlier work didn't cover: does it matter which core takes the
hardware interrupts, and would a faster kernel tick help? The first needs the device to answer, and `tools/irqtest.sh`
is the test for it. The second can mostly be answered from ROCKNIX's kernel configuration (below), and the same test
includes a variant that settles the rest.

## What the kernel is

ROCKNIX's RK3566 kernel (`projects/ROCKNIX/devices/RK3566/linux/linux.aarch64.conf` in ROCKNIX/distribution, `next`
at 2d34256, 2026-10-08: Linux 7.2.7):

| Setting | Value | What it means here |
|---|---|---|
| `CONFIG_HZ` | 250 | one scheduler tick every 4 ms, 4.2 ticks per 60 Hz frame |
| `CONFIG_NO_HZ_IDLE` | y (`NO_HZ_FULL` off) | idle cores stop ticking; a busy core ticks 250 times a second |
| `CONFIG_HIGH_RES_TIMERS` | y | sleeps, timerfd, poll/epoll timeouts end on time, not on the next tick |
| `CONFIG_PREEMPT` | y (full; no `PREEMPT_DYNAMIC`, no `PREEMPT_RT`) | the kernel can be preempted anywhere outside locks |
| `CONFIG_SCHED_HRTICK` | y (the `HRTICK` feature off by default) | the scheduler *can* end slices on a hrtimer instead of the tick |
| `CONFIG_DEBUG_FS` | y | `HRTICK` and `base_slice_ns` can be changed at runtime in `/sys/kernel/debug/sched/` |
| `CONFIG_IKCONFIG_PROC` | y | the device's own config is in `/proc/config.gz` (`tools/schedprobe.sh` prints it) |
| `CONFIG_UCLAMP_TASK`, `CONFIG_SCHED_CLASS_EXT` | off | no utilization clamping, no BPF schedulers |
| cpuidle | menu governor, PSCI; `cpu-sleep` (ROCKNIX patch 1001): exit 120 µs, min residency 1 ms, stops the local timer | |
| Interrupt controller | GICv3, no irqbalance | |

The installed ROCKNIX may be older than `next`. `tools/schedprobe.sh` reads the running kernel's own values, and
`irqtest.sh` saves them in `probe.txt`.

## The tick: what 250 Hz does and doesn't touch

Most of what decides frame pacing doesn't depend on the tick:

- **Timers.** With high-resolution timers, the audio pump's absolute timer, `SDL_Delay`/`nanosleep`, timerfd and
  poll timeouts wake on time at any `HZ`. The presenter waits for page-flip events, which come from the display's own
  interrupt. Normal threads get the default 50 µs timer slack; the SCHED_FIFO presenter and audio pump get none.
- **Wakeups.** A thread that wakes (a frame ready, a vblank) is placed and may preempt at once, without waiting for
  a tick. The SCHED_FIFO threads preempt any normal thread as soon as they wake.
- **The governor's measurements.** `cpugov.c` reads per-thread CPU clocks, which are exact to the nanosecond. The
  tick-sampled user/system split in `/proc` is scaled to the same total.
- **The clock.** `performance` with `scaling_max_freq` doesn't run a tick-driven governor.

What the tick does decide:

- **When a running thread's slice ends,** if another normal thread is waiting for the same core. The fair scheduler's
  slice is 0.75 ms × (1 + log2 4) = 2.25 ms, but without `HRTICK` it is only checked at a tick, so it lasts until the
  next one: up to 4 ms at 250 Hz, against 1 ms at 1000 Hz. That matters only when more threads want to run than
  there are cores. DraStic and libdsflip use about 1.4 cores of the 4 in HeartGold at 2x, so it happens rarely:
  when ROCKNIX's services or DraStic's own threads pile onto one core.
- **Periodic load balancing.** Moving work from a busy core to an idle one at a tick. With cores often idle, most
  placement happens on wakeup and when a core goes idle, not at a tick.
- **Its own cost.** 250 interrupts a second on each busy core. 1000 Hz would mean four times as many ticks.
- **`jiffies` delays in drivers** (`msleep`, `schedule_timeout`) are rounded up to 4 ms. These are in device setup
  and error paths, not in the frame path.

**Conclusion.** A 1000 Hz kernel would change one thing that matters (slice ends when a core is contended) and add
tick overhead on every busy core. The `HRTICK` scheduler feature gives the first, with better precision than any
`HZ`, and can be switched on at runtime. So the test is `hrtick=1`, not a kernel rebuild. If `hrtick=1` makes no
difference, a 1000 Hz kernel won't either. `NO_HZ_FULL` isn't worth trying: it needs a kernel rebuild and a boot
parameter, and it only stops the tick on a core that runs a single thread, which DraStic's main thread doesn't get.
The kernel already uses full preemption.

## Interrupts: where they go

On a GICv3 each interrupt goes to one core: the first online core in its affinity mask. With ROCKNIX's default
masks (all cores) and no irqbalance, the display, touch, I2C, MMC, USB and PMIC interrupts all land on CPU0, and so
do their softirqs. Nothing keeps DraStic's busiest thread off CPU0. Each interrupt there costs the game the handler's
time, plus cache and branch-predictor state. The 1.4 interrupt storm (84,000 a second on one core,
[optimization-1.4](optimization-1.4.md)) showed how much one core's interrupts can cost. In normal play the rate is far
lower; measuring it is the probe's first job. `schedprobe.sh` shows each interrupt's mask (`aff=`) and the core it is
actually delivered to (`eff=`).

## The test

`tools/irqtest.sh <outdir> [rounds] [layout...]` (on a PC, `RGDS_HOST=<ip>`). It runs `schedprobe.sh` first, then for
each round and layout:

- **HeartGold at 2x, walking, 90 s** (`power.sh` → `hgpower.sh`): presents and drops, late flips (seconds whose
  longest flip interval was over 20 ms), the clock libdsflip's governor settled at, battery current, temperature.
- **The 3D stress ROM ramp, 75 s** (`stressgov.sh`): frames per second over the last 30 s (the heaviest levels).

Each run is wrapped in `tools/irqab.sh <tag> <layout>`. It sets up the layout, records `/proc/interrupts`,
`/proc/softirqs`, the CPU time split and the cpuidle entries before and after, and restores everything when the run
ends, is stopped or times out. A run that died before it could restore is cleaned up by the next one. The layout order
rotates each round, so heat and battery drift fall evenly on every layout. The governor forgets between runs
(`DSFLIP_CPUGOV_MEMORY=0`), so no layout inherits what an earlier one taught it.

| Layout | |
|---|---|
| `base` | as ROCKNIX leaves it (the control) |
| `irq=3` | every interrupt that can move on CPU3 |
| `irq=0,app=1-3` | interrupts on CPU0, DraStic's threads kept off it |
| `irq=3,app=0-2` | interrupts on CPU3, DraStic's threads kept off it |
| `hrtick=1` (add it) | slices end on a high-resolution timer: what a faster tick would give the scheduler |
| `slice=<ns>` (add it) | the fair scheduler's base slice |

Items combine, e.g. `irq=3,app=0-2,hrtick=1`. `app=` keeps DraStic off *one* core and leaves the scheduler free on the
rest. It is not the per-thread core pinning that 1.3 measured as worse and removed.

```sh
RGDS_HOST=192.168.1.50 tools/irqtest.sh /tmp/irq 3                       # the four default layouts, ~1 h
RGDS_HOST=192.168.1.50 tools/irqtest.sh /tmp/tick 3 base hrtick=1         # the tick question, ~30 min
python3 tools/irqtest-summary.py /tmp/irq                                 # the tables again
```

How to read the results: drops/s in HeartGold differ by a few hundredths between runs on the same settings (1.4:
0.00-0.04 at full clock). A layout is better only if its drops, late flips or governor clock beat `base` in every round, not just
on average. On the stress ROM, a gain under ~0.5 fps is within the noise. If interrupts on CPU0 turn out to be a few
hundred a second at well under 1% of a core, moving them can't buy much, whatever the tables say.

If a layout wins clearly, it belongs in `session.sh` (set before DraStic starts, restored in `restore.sh`), off by
default for a release so players' logs can confirm it.
