# ROCKNIXDS 1.5 plan

Carried over from 1.4 (see [plan-1.4.md](plan-1.4.md) and the [optimization report](optimization-1.4.md)).

## 1. Starting and quitting games (first)

1.4 starts a game as fast as 1.3 (first frame 3.47 s after the launch request, 1.3: 3.44 s), but the way back to the
menu is ~0.8 s slower: ES answers its API 3.51 s after the game ends (1.3: 2.65 s) and is visible at 5.26 s (1.3:
4.47 s). Known so far: it isn't the menu governor hook (ES starts in ~1.7 s with or without it), the CPU clock limit
(session.sh lifts it the moment DraStic exits) or the PipeWire rate reset (~20 ms). Next: time each step of
restore.sh and ES's start on 1.3's and 1.4's ES binaries (`tools/switchtime.sh`), then look at the launch chain
itself (ROCKNIX's runemu.sh and start_drastic.sh take ~2.3 s before our unit starts).

## 2. Power, continued

- **ROCKNIX's `powerstate` service**: a bash loop polling every 2 s with `cat`, `awk` on the battery's whole uevent
  (I2C reads) and `sleep`, ~3% of a core, more on battery (it looks settings up with `awk` on every pass after the
  first 40 s). Same treatment as `battery-led-status`: a fork-free copy behind a systemd drop-in that hands back to
  ROCKNIX's script if that ever changes (md5 79dcb5ee1f43876d5c1dff429f87b36b), including `ledcontrol discharging`
  done in-process when the battery is above 97%.
- **The CPU governor's first minute**: it finds its level by trying lower clocks, and each clock that turns out too
  low costs 2-4 frames once (a 30 s smoke test showed 0.20 drops/s, a 90 s run 0.02-0.07). Ideas: descend more
  slowly, start from the last session's level for the same game, or learn per game.
- **Skipping unchanged frames with a shader**: needs a way to know a frame didn't change without reading DraStic's
  uncached buffers back.
- **ds-fsr** (5.3 ms per panel at 800 MHz, 4.3 in the 1.4 comparison session): a two-pass version that analyses each
  source pixel once.
- **Other systems**: ROCKNIX's per-system governors for RetroArch cores, measured with free homebrew ROMs.

## 3. 816 MHz at 2×, 60 fps

1.4's governor stops at 1104 MHz. The 816 MHz smoke test (HeartGold, 2×, still dialog) dropped 2.93 frames/s even
though the heaviest frame was only ~40% of a refresh, so the floor was raised and the note was "the step to 1104
saves little". The logs say what those drops actually were: every one is `drop-q`, and each one lands on a 33 ms
flip interval. DraStic's presents at 2× already alternate short and long (~13 / ~21 ms); at a lower clock the two
phase clusters spread further apart, a frame misses its latch, DraStic (paced by the audio callback) catches up
with a burst, and the third frame overflows the two-slot queue. Average CPU time was never the limit.

What changed, so 816 can be tried instead of banned up front:

- **Unchanged frames are not drops.** When the queue is full, the new frame is compared with the newest waiting
  one (every 8th row, outside the presenter's lock — a dumb buffer is uncached). The same picture is discarded
  and counted as `dup=`, not `dropped=` and not `dsflip_queue_drops`, so a still scene no longer forces the clock
  up. If the two frames already waiting match each other, the older one is the one discarded. With
  `DSFLIP_QUEUE=2` (three waiting frames) a new frame that matches the newest is skipped even before the queue
  is full, so a burst of repeats doesn't sit two refreshes behind. `DSFLIP_DUPCHECK=0` turns the compare off.
  The steady state is unchanged: the compare only runs once two frames are already waiting, or the queue is full.
- **One more slot, opt-in.** `DSFLIP_QUEUE=0` mailbox, `1` (default) one extra frame, `2` two extra frames. The
  default stays one: a producer that runs a hair fast would otherwise sit at the deeper cap all the time.
- **A finer audio tick, without finer ALSA writes.** DraStic releases its next frame when the audio callback has
  drained a chunk, so the pump chunk is the granularity of that pacing. `DSFLIP_PUMP_CHUNK=64` is ~1.5 ms instead
  of ~5.8. The ALSA writer stays at 256 samples (`DSFLIP_ALSA_CHUNK`); the echo gate's history grows so it still
  covers ~186 ms. Default chunk stays 256 — the old chunk-size experiment changed the write size too, and measured
  worse.
- **Governor floor is 816 MHz.** A counted drop still bans that clock (30 s, doubling, up to 10 min) and steps up.
  `DSFLIP_CPU_MIN=1104000` restores the 1.4 floor.

Not measured on the device in this change. 816 and 1104 may share the RK3566's minimum CPU voltage; dynamic power
still scales with the clock, but if the regulator doesn't move, the saving is the small one 1.4 already noted.
Read it during a capped run (`/sys/class/regulator/regulator.*/name` and `microvolts`, the one named like
`vdd_cpu`). Then, charger unplugged, HeartGold at 2×, with `DSFLIP_CPUGOV=0` so the cap stays put:

```sh
tools/power.sh t816     90 CPUGOV=performance CPUMAX=816000 DSFLIP_CPUGOV=0
tools/power.sh t816q2   90 CPUGOV=performance CPUMAX=816000 DSFLIP_CPUGOV=0 DSFLIP_QUEUE=2
tools/power.sh t816p64  90 CPUGOV=performance CPUMAX=816000 DSFLIP_CPUGOV=0 DSFLIP_PUMP_CHUNK=64
tools/power.sh t816both 90 CPUGOV=performance CPUMAX=816000 DSFLIP_CPUGOV=0 DSFLIP_QUEUE=2 DSFLIP_PUMP_CHUNK=64
tools/power.sh t1104    90 CPUGOV=performance CPUMAX=1104000 DSFLIP_CPUGOV=0
```

Keep a run when `present/s` stays at 60 and `dropped=` / `drop-q=` stay at the 1992 MHz noise floor; `dup=` may be
high in a still scene and that is the point. The same four with the governor left on (no `CPUMAX`, no
`DSFLIP_CPUGOV=0`) show whether it actually stays at 816 or bans it after a real drop. If 816 holds the frame
rate but `vdd_cpu` matches 1104 and the battery current doesn't move, put the floor back.

## 4. Parked branches

- `standalone-wip`: libdsflip as a package for other firmwares (built, not tested on the device).
- `plus-alpha`: the RG DS Plus port (pre-release v1.4-plus-alpha.1, untested on hardware); rebase onto 1.4 when the
  hardware is at hand.
