# High-res DraStic at the 816 MHz step

Research for getting 2× (hires 3D, 512×384) play down from the 1104 MHz floor toward the RK3566's next OPP. Nothing in here was re-run on the device. The numbers are the 1.4 measurements, the 1.5 beta session script, and SuperDrastic `0.3.0-beta.1` (`cpugov.c`, `dsflip.c`, `audio.c`, and the release notes).

## Answer

816 MHz is the step to aim at. There is no 800 MHz CPU OPP on this SoC; the table the governor already walks is 408, 600, **816**, 1104, 1416, 1608, 1800, 1992. 600 MHz cannot hold HeartGold walking at 2×: that scene's busiest thread is already 66% of one core at 1104, which is about 121% of one core at 600. 816 is the only OPP below the floor that the average still fits in.

The floor itself is not proof the raster misses at 816. It was set in 1.4 after a **still** HeartGold scene dropped 2.93 frames/s at 816 while the heaviest main-thread frame was only ~40% of a refresh. That run had the one-frame queue and no hold. SuperDrastic 0.3 exists because of that kind of drop: a deeper queue plus a hold when the queue is full. Its release measured Black 2 walking at fixed 1104 (0.73 → 0.13 hitches/s) and fixed 1416 (0.11–3.2 → 0.07). **816 was not remeasured.** Until that pin exists, more libraries and more code are guesses.

No new library speeds this up. DraStic's raster is closed source. The work that can still move the clock is around it, in this order:

1. Pin 816 with the 0.3 queue and read the log. This may already be the whole win for walking and dialogs.
2. If the raster is actually late, give DraStic a cached framebuffer and copy once into the dumb scanout buffer. The zero-copy path hands DraStic write-combined memory today, and a software blender that reads that memory back pays DRAM latency on every blended pixel.
3. If frames are late while the cores are not full, pin the DRAM frequency for the session. It has never been measured.
4. Only then teach the governor to use 816 as a floor inside a range. As it stands it will not stay there: the default floor is hardcoded at 1104000, and any drop still steps the clock up.

Heavy scenes stay at 1416–1992. The stress ROM's heaviest levels are ~51 fps at 1992 MHz, so 60 fps at 816 is a target for typical play (dialog, walking, ordinary battles), not for that ROM.

## The budget

One refresh is 16.67 ms. Both screens at 2× are 512×384 XRGB8888: 786 KB each, 1.57 MB per frame, 94 MB/s at 60 fps. The VOP2 scales 512×384 to 640×480 in hardware. That scale is free and stays free.

HeartGold 2×, walking, no shader, clock capped, 60 s (`docs/plan-1.4.md`):

| CPU cap | CPU used (of 400%) | busiest DraStic thread | drops/s |
|---|---|---|---|
| 1992 | 121% | 46% | 0.07 |
| 1608 | 127% | 50% | 0.10 |
| 1416 | 146% | 56% | 0.07 |
| 1104 | 183% | 66% | 0.13 |
| 816 | not in that table | — | 2.93 in a still scene (separate smoke, one-frame queue) |

Scale the 1104 walking row by 1104/816 = 1.35, which is right only for work that is CPU-bound. Memory-bound work scales less, so this is the pessimistic one:

- Busiest thread: 66% → **89%** of one core. About 11% of a core left on that thread.
- Average frame on that thread: 0.66 × 16.67 ms = 11.0 ms at 1104, **14.9 ms at 816**.
- Whole process: 183% → about 248% of 400%, two and a half cores. The other cores have room. This is not a four-core problem.
- Same scene at 600: 66% × 1104/600 = **121%** of one core. The average frame does not fit, before jitter.

The tail is tighter than the average. The governor's own comment records that at 1104 the average was 66% and single main-thread frames still ran over 16.7 ms of CPU time. Those frames, scaled the same way, are ~23 ms at 816: one refresh late. A queue of depth 2 holds one extra frame (16.7 ms of cover). Depth 3 holds two. Battery mode already accepts depth 3 at 1104. Depth is the first tool, a higher clock the second.

`dsflip.c` also records that 2× frame arrivals alternate, measured around **13 ms / 21 ms**. A mailbox shows only the newest and hitches. The queue turns that alternation into one frame per refresh. A 21 ms interval that grows with the clock (21 × 1.35 ≈ 28 ms) is one late frame, which depth 2 covers, as long as the next interval is short. If **both** halves grow so the average sits above 16.7 ms, the queue fills and stays over budget. The `[raw]` interval line is how you tell those apart.

Black 2 is heavier than HeartGold. With the 0.3 queue it still hitches 0.13/s at fixed 1104 and 0.07/s at fixed 1416. 816 for Black 2 is the hard case. HeartGold walking is the case the arithmetic says can fit.

## Why the floor is 1104

`cpugov.c` defaults `DSFLIP_CPU_MIN` to 1104000:

```c
/* 816 MHz dropped frames at 2x even in a still scene; the step to 1104 saves little */
fmin_ = getenv("DSFLIP_CPU_MIN") ? atoi(getenv("DSFLIP_CPU_MIN")) : 1104000;
```

The evidence for that comment is one smoke: 2.93 drops/s, still HeartGold, heaviest main-thread frame ~40% of a refresh (`docs/plan-1.4.md`, `docs/optimization-1.4.md`). 40% of 16.67 ms is 6.7 ms of CPU on the presenting thread. The raster of that scene finished with most of the refresh unused. The drops were the queue, the latch, or time the thread spent off-CPU.

That matches what 0.3 later wrote down. The hold exists because the queue fills and then every frame that arrives before the previous one has been shown is a drop, "and at low clocks frames come early more often" (the comment on `SDL_RenderPresent`, and the 0.3.0-beta.1 release notes). A still scene is exactly where the raster is cheap and the arrivals are what the queue sees. 2.93/s is a drop every few frames, which is that overflow, not a raster that needs 35% more clock.

The "heaviest frame" number is CPU time on the presenting thread (`CLOCK_THREAD_CPUTIME_ID` between presents), not wall time. It includes DRAM stalls while that thread is on-core. It does not include time spent waiting on the audio callback, on the queue hold, or in another DraStic thread. So:

- A long `[raw]` interval with a small heaviest-frame number means the main thread was waiting or a worker did the work.
- A long interval with a heaviest frame near or over 100% of a refresh means the raster (or the memory it stalls on) missed.

The 816 smoke published only the second number, and only for a still scene. Walking at 816 has never been logged.

## What 0.3 changed, and what it left

`DSFLIP_QUEUE` is 0..3. Each waiting frame is one refresh of input latency and one late frame of cover. `DSFLIP_QUEUE_WAIT` holds `SDL_RenderPresent` when the queue is full, up to N ms, instead of dropping. The audio ring target is 1536 samples, about 35 ms, which is what absorbs that hold. Profiles in `session.sh`:

| Profile | Queue | Wait | CPU max the script exports |
|---|---|---|---|
| performance | 1 | 0 | (hardware) |
| balanced | 2 | 20 ms | 1416 MHz |
| battery | 3 | 20 ms | 1104 MHz |

The release says the wait is meant for a capped clock, and that the governor does not suit it yet. Two behaviors still throw 816 away the moment it is tried inside a range:

- Any drop does `want = fit(cur + 1)`, including a light drop. The 816 smoke is the reason that branch exists. With a queue, that drop is often the thing the queue was added to absorb.
- `fps < 58.5` does the same. Holds bunch presents into the next window, so a healthy hold can look like "slow" and step the clock up.
- The memory file is `<rom>.<shader>.<1x|2x>` with no queue depth. A strike earned on the one-frame queue bans that clock for the deep queue too. Bans are not consulted on the way up.

`fit()` also will not pick 816 while `fmin` is 1104000. A test that only sets `scaling_max_freq` to 816000, and leaves the governor on, climbs back off 816 on the first window: `fmin` is 1104000, so the chosen clock is at least that, and it is above `cur`.

Pinning means all of these at once: `scaling_max_freq` written to 816000 before DraStic starts (so `cur` is already 816), `DSFLIP_CPU_MIN=816000`, `DSFLIP_CPU_MAX=816000`. With min and max equal, `fit(cur + 1)` cannot leave 816. `DSFLIP_CPUGOV=0` plus the sysfs write is the same pin, and it skips the memory file.

`hgpower.sh` can do this. It writes `CPUMAX` to `scaling_max_freq` and exports any `DSFLIP_*` argument into DraStic's environment. It does not default the queue or the cap, and its summary line adds up `present/s` and `dropped` only. `dropped` mixes source overruns, queue drops and buffer exhaustion. The hitch the 0.3 measurements quote is `repeat` (refreshes that showed no new frame). Read the log.

## Where the time goes

Zero-copy, no shader, is the path that has to get to 816. Shaders are extra.

DraStic locks a screen texture and receives the mmap of a DRM dumb buffer (`mkbuf`). Cached `aligned_alloc` buffers (`mkmem`) are used only when a shader is on and `DSFLIP_SHADER_COPY=1`. The presenting thread's work besides DraStic's own raster is small: the 1.4 split puts the presenter at 1.8% of a core, the ALSA writer at 0.9%, the pump and touch under 0.5%. Audio through PipeWire at 44.1 kHz is about 5% of a core at 1608 MHz (about 10% at 816, same work, slower clock), on its own threads.

Those audio threads are `SCHED_FIFO` priority 20. The presenter is `SCHED_FIFO` 10. DraStic's threads are normal. The pump therefore preempts the raster every chunk (256 samples, 5.8 ms). That preemption is a few tens of microseconds if the callback only copies samples. It was tried the other way, with DraStic's own threads at `SCHED_FIFO`, and it was worse (removed in 1.3; the last commit that has it is `5d67d79`).

The GPU is idle in this mode: `session.sh` puts it on `powersave` at 200 MHz. ds-crisp after the dma-buf import is ~0.57 ms of GPU per panel and a shader thread at 5.5% of a core, and it still raised the 1.4 average clock from 1445 to 1567 MHz. ds-fsr is 4.3–5.3 ms per panel, about 9 ms for both, and the session pins the GPU at 800 MHz for it. The GPU work can overlap the raster, but the extra DRAM traffic and the shader thread are what lifted the clock for a shader that is ten times cheaper. ds-fsr is outside this budget.

Background was the other measured slice: ROCKNIX services ~3.2% of a core during 1.4 games, and `powerstate` was most of a 2-second bash loop. 1.5 already replaced that loop (131 → 8 ticks per 60 s). What remains is bursty work: journald, Wi-Fi, a RetroAchievements request. At 89% occupancy a burst on the raster core is a late frame. It is not the steady 2.93/s.

Fast-switch VT mode keeps ES and sway up during the game. Leave it off for any 816 run. Sway was 15–23% of a core on the old path; even a quieter ES is a core you do not have to give away.

## Ranked work

### 1. Pin 816 with the queue that shipped to fix this

No new code. One session, HeartGold, then Black 2 if HeartGold holds.

On the device, display free, from `hgpower.sh`'s own rules (it refuses to start over a running game):

```sh
# still uses the installed library. CPUGOV left unset: cpugov stays on, but min=max keeps it at 816.
tools/hgpower.sh hg-816-q3 90 \
  CPUMAX=816000 \
  DSFLIP_CPU_MIN=816000 DSFLIP_CPU_MAX=816000 \
  DSFLIP_QUEUE=3 DSFLIP_QUEUE_WAIT=20 \
  DSFLIP_CPUGOV_MEMORY=0 DSFLIP_CPUGOV_LOG=1
```

Then the same with `DSFLIP_QUEUE=2`, then `GAME=b2`. No shader. Mic sensitivity left off. Fast-switch off.

Pass, for that scene: `present/s` between 59 and 60, `repeat` at or under the 1104 Black 2 number (0.13/s), `drop-q` and `drop-src` near zero, `[audio]` underruns zero, pump still ~44100 Hz. Holds are expected once the queue is full. A longest hold glued to 20 ms, with `present/s` sagging or the ring underrunning, means DraStic is chronically behind and the hold is hiding it.

While it runs, record one sample of:

```sh
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq
ls /sys/class/devfreq
for d in /sys/class/devfreq/*; do echo "$d $(cat $d/cur_freq 2>/dev/null) $(cat $d/governor 2>/dev/null)"; done
```

The GPU node is `fde60000.gpu`. Anything else in that list (a `dmc` node in particular) is the DRAM clock from section 4. Also keep the `[cpugov] window` lines, the `[raw]` interval buckets, and `[queue]`.

`repeat` is not in `hgpower.sh`'s summary. Take it from `hp-*.log` until the summary learns it:

```text
[dsflip] present/s=… dropped=… drop-src=… drop-q=… drop-buf=… repeat=…
[raw] … intervals(ms) 0-4:… 4-8:… 8-12:… 12-16:… 16-20:… 20-24:… 24+:…
[queue] depth … avg … | DraStic held N times, … ms in total, longest …
```

### 2. Read the log before changing code

| What the 90 s log shows | What it means | Next step |
|---|---|---|
| Passes on HeartGold walking and on Black 2 walking | The 1.4 floor was the one-frame queue. 816 already works for those scenes. | Section 5, so the governor is allowed to sit there. Stop. |
| HeartGold passes, Black 2's `repeat` climbs, `[raw]` 24+ grows, busiest thread near 100%, heaviest frame over ~80% | Black 2's raster does not fit at 816. HeartGold can use 816; Black 2 should step up. | Section 5 with a range, not a pin. Cached FB (section 3) is what could pull Black 2 down. |
| `present/s` stays ~60 but `repeat` is high, intervals still clustered at 13/21, heaviest frame well under 80% | Same bug as the still-scene smoke: early frames, not a slow raster. | Try queue 3 if you ran 2. If 3 already failed this way, the hold is not covering the bunching; look at `[pace]` spread and `[late]` before any framebuffer work. |
| Intervals in the 24+ bucket, heaviest frame high | The presenting thread itself missed. WC framebuffer and DRAM are the suspects. | Section 3, then section 4. |
| Intervals in the 24+ bucket, heaviest frame low, some other DraStic thread hot | A worker is the raster. The main-thread number will keep lying. | Same as a hot raster: section 3. The governor already tracks the busiest thread (`HIGH` 0.85, `TARGET` 0.72). |
| Intervals in the 24+ bucket, every DraStic thread cool, `[lock]` max wait high | The presenting thread blocked on `mu`. | The presenter's critical section is the suspect, not the clock. |
| Rare 24+ spikes, threads cool, `[audio]` trim swinging or underruns | Scheduling, idle, or the audio clock. | Section 6. Not a framebuffer project. |

### 3. Cached framebuffer, if the raster is the thing that is late

Dumb buffers are mapped write-combined. `docs/drastic-2x-plan.md` already says to write them sequentially and never read them back. DraStic's 3D blender cannot follow that rule if it alpha-blends into the color buffer: a blend is a read-modify-write. A write-combined load does not hit a cache. It is a DRAM round trip per read, and at 816 MHz that round trip is a larger share of the frame than it is at 1104.

Whether DraStic actually does that to the pointer we hand it is unmeasured. Two shapes are possible:

- It rasterizes straight into the locked pointer. That is the point of the zero-copy path, and it is the case where WC reads hurt.
- It rasterizes into its own buffer and copies out at unlock. The locked pointer then sees a sequential store, which write-combined memory is good at, and a shadow copy would only add cost.

There is a probe that does not need a new mode. `DSFLIP_SHADER_COPY=1` with any cheap shader makes screen textures `mkmem` (cached). The upload runs on the shader thread, not on DraStic's. Compare, at the same pinned clock, the busiest DraStic thread and the heaviest-frame percentage against the zero-copy run. Ignore the shader thread and the GPU.

```sh
tools/hgpower.sh hg-816-copy 90 \
  CPUMAX=816000 \
  DSFLIP_CPU_MIN=816000 DSFLIP_CPU_MAX=816000 \
  DSFLIP_QUEUE=3 DSFLIP_QUEUE_WAIT=20 \
  DSFLIP_CPUGOV_MEMORY=0 \
  DSFLIP_SHADER=ds-crisp DSFLIP_SHADER_COPY=1
```

Run the no-shader pin next to it at the same temperature. If DraStic's own threads drop by more than the copy you would add, the WC mapping is taxing the raster.

The copy you would add, if the probe wins, is a mode such as `DSFLIP_CACHED_FB=1` inside SuperDrastic, not a new library:

- DraStic locks the cached buffer, as `mkmem` already allocates.
- On unlock, one NEON copy streams into a dumb scanout buffer. Sequential stores, never reads of the dumb mapping, then a store barrier before the buffer is queued so the VOP2 sees the writes.
- The dumb buffer is what gets scanned out, and what a shader imports. The mode must not require a shader. Coupling it to `DSFLIP_SHADER_COPY` would mix the raster result with a GPU pass.
- Size is 1.57 MB per frame. A streaming copy on an A55 is on the order of 0.5–2 ms depending on how fast stores to write-combined memory actually retire. Measure it; do not budget it as free. It has to be cheaper than the WC reads it removes, on the scene that failed the pin.
- Eight cached buffers plus the dumb set is a few megabytes. Irrelevant.
- The open 1.5 item "skip unchanged frames" is blocked because reading the dumb buffer back is costlier than shading it. A cached shadow makes a checksum cheap, and then a static screen can skip the GPU pass. That is a shader saving. It does not make the raster faster. Build it only after the copy exists.

If the probe shows no difference, DraStic is already streaming stores (or copying out of an internal buffer). Do not add the blit. A kernel change to map the same GEM cached for the CPU and write-combined for scanout is the step after a blit that wins on CPU and loses on the 1–2 ms copy. `DRM_IOCTL_MODE_MAP_DUMB` takes no cache flag. It wants either a second mapping plus `DMA_BUF_IOCTL_SYNC`, or a Rockchip buffer flag the running kernel may not have. Confirm on the device only after the userspace copy has earned it.

### 4. DRAM frequency, if the cores are not full and the frames are still late

The CPU OPP and the DMC OPP are separate. A governor that watches CPU idle can drop DRAM while a memory-bound raster is running, and the symptom looks like "needs a higher CPU clock." Nothing in this repo reads a DMC node. The 1.4 power logs record CPU cpufreq and `fde60000.gpu` only.

If section 1's devfreq listing shows a memory controller, repeat the pin with that device's `min_freq` set to its highest OPP for the session, and put it back in `restore.sh` the way the GPU governor already is. If `repeat` and the 24+ bucket fall with no change in DraStic's code, the floor belongs in `session.sh` for DS sessions. If the listing has only the GPU, there is nothing to pin and this inch is closed.

This is a shell change either way. It is not a library.

### 5. Let the governor sit at 816 once a scene has earned it

Do this after the pin, and only for scenes the pin actually held. The governor is what makes 816 the clock in ordinary play instead of a test.

- Default `fmin` becomes 816000, and the comment that freezes the 1.4 smoke goes with it. Keep stepping up on a CPU-bound miss: busiest thread over `TARGET` (0.72), or a frame over `BLAME_PEAK` (0.80). Those are the Black 2 and stress-ROM cases.
- A light drop, and `fps < 58.5` caused by holds, must not step up while the queue depth is greater than 1 and the hold is what absorbed the frame. Otherwise the first early frame leaves 816 and the floor is back.
- Write `scaling_max_freq` to the profile cap before DraStic starts, so `cur` begins at the cap. Today `cur` starts at whatever sysfs holds (1992 under `performance`) and walks down one OPP every 2 s, and a drop during that walk cancels the walk.
- Put the queue depth in the memory file name (`<rom>.<shader>.<1x|2x>.qN`). Strikes from the one-frame queue must not ban 816 for the three-frame queue.
- A profile that should stay low is a range whose floor is 816, not a hard pin. HeartGold walking can sit at 816 while a heavy scene climbs. A hard pin at 816 makes the stress ROM worse than it is at 1992 (~51 fps there already). Battery can cap the climb (1104 or 1416) the way it caps at 1104 today.

600 MHz stays out of the high-res walking goal. It can be a later step-down for a dialog whose busiest thread at 816 is under ~70% (so the same work stays under one core at 600). That is the governor doing its job after 816 is a legal floor. It is not a separate project, and it is not the number to promise for 2× overworlds.

Latency: depth 3 is up to 50 ms of queue on top of the ~35 ms audio ring. Battery mode already chose that at 1104. Prefer depth 2 wherever the pin says it passes.

### 6. Audio, interrupts, idle, background

Only if section 2 points here.

- **Audio chunk.** 256 samples is 5.8 ms. A larger ALSA buffer was measured at 0.18 drops/s and removed, because DraStic's pacing follows the drain. The chunk-size experiment was removed in 1.3 with the other losers. Retry a smaller chunk (128) only if `[raw]` lines up with the chunk and the pin is otherwise clean. The ±2% trim clamp is a rail; the log's `trim` ppm is the number. Hundreds of ppm is the loop doing its job. Thousands, swinging, means the ring is being asked to follow a clock the raster is missing.
- **Do not put DraStic back on `SCHED_FIFO` or pin its threads.** Both were measured worse. The presenter stays at FIFO 10, below the pump at 20.
- **Interrupts.** A VOP underrun storm (~84,000/s, ~60% of a core) is already detected and cleared at modeset. Normal VOP traffic is two panels at 60 Hz. If a hitchy 816 log shows cool threads and fat 24+ buckets, sample `/proc/interrupts` across the run and move a noisy line (Wi-Fi, USB, MMC) off the core the raster actually ran on. `isolcpus` plus pinning is the experiment that already lost; affinity for one noisy IRQ is the smaller version.
- **Idle.** If `cpu0/cpuidle` only has the WFI state, leave it. Deeper states are worth disabling for the session only when their exit latency shows up as off-CPU gaps in the same log.
- **`mlock`.** A page fault is off-CPU time, so it looks like a late frame with a cool thread. One `mlockall` at the start of the session is cheap insurance after the pin, not before it. Faults at 2.93/s for a whole still scene would be a stuck mapping, not a cold page.
- **RetroAchievements.** `ra_frame()` runs on the presenting thread after the queue update. If an 816 log is worse with RA logged in than with it off, that hook is part of the frame. Unlikely to be the steady cost. Easy to exclude for one run.
- **Allocator preload** (`jemalloc`, `mimalloc`). DraStic's allocator is inside the binary. An interposing malloc is justified only after a profile shows allocation in the raster. It has a history of breaking emulators that ship their own allocator. Not on the first list.

## Do we need more libraries

No additional library gets the closed-source raster to 816.

| Idea | Verdict |
|---|---|
| Another emulator as a library beside DraStic (melonDS, DSperate) | A different product. melonDS's GL renderer and DSperate's NEON scaler do not speed DraStic's binary. VOP2 already scales. Replacing DraStic drops the touch path, the savestates, RetroAchievements and the session around it. That is a port, not an optimization of this stack. |
| A NEON blit `.so` | The blit is ~50 lines inside SuperDrastic next to `mkmem`. A separate library only adds a lookup. |
| libdrm, libasound, libmali | Already in the process. |
| dma-heap / a cached GEM helper | Userspace copy first. A kernel buffer flag only if the copy wins on raster time and loses on its own milliseconds. |
| ARM Compute Library, KleidiAI | They cannot see DraStic's vertices. |
| A new audio stack past the pump | The pump is the piece that made frames even. Bypassing PipeWire was rejected because the volume keys set PipeWire's volume. |
| 16-bit scanout | DraStic's screen textures are ARGB8888 (`FMT_ARGB8888`). The VOP2 read of both screens is 94 MB/s. Halving that is noise next to a raster. A conversion pass would spend CPU to save the display controller a read. |
| Compiler flags, LTO, PGO on DraStic | There is no source. Flags on SuperDrastic do not move the raster. |

GammaOS Nano's gain was removing the display path. This tree already did that: dumb buffer, one atomic commit for both panels, hardware scale, no GL and no sway on the frame.

## Already measured, leave it

From 1.3 and 1.4, each of these lost on device or tied a higher clock for no gain:

- Clock lock and a PLL hooked over `gettimeofday` / `SDL_Delay`.
- CPU pinning, and `SCHED_FIFO` on DraStic's threads.
- A/B legacy pacing, a fixed latch, and the audio chunk-size trial (the paths were deleted; history keeps them at `5d67d79`).
- `schedutil` for DS games: average 1771 MHz, 0.10 drops/s, worse than the custom governor.
- A fixed cap with no queue, as the way to run every game. Light games waste the cap, heavy ones hitch. The governor stays; its floor is what this document moves.
- Bigger audio buffers (60 ms): ~0.18 drops/s. DraStic follows the burstier drain.
- Bypassing PipeWire.
- GPU floor under 400 MHz with shaders. 200 and 300 MHz did not help; heavy shaders sit at 530–600 MHz anyway.
- 16-bit `pow()` in the colour shaders: slower than the 32-bit polynomial (1.72 vs 1.65 ms).
- Uploading frames for shaders. dma-buf import is the default. `DSFLIP_SHADER_COPY=1` is the probe in section 3, not a mode to ship as the default.

The spin lock on `sched_yield` is already gone. Priority-inheriting mutexes replaced it after a presenter at FIFO could spin for up to ~1 s on a preempted holder. Do not bring the spin back.

## What 816 will not do

- The stress ROM's heavy levels. They are ~51 fps at 1992. The governor's job there is to reach 1992 and stay.
- ds-fsr at 2×. It does not look better than ds-crisp at 2× (`docs/filters-1.5.md`) and it costs 8–9× the GPU time. It is a 1× filter.
- A shader-by-default 816 profile. No shader is the mode whose budget closes. ds-crisp is a second pin, after the no-shader pin passes, and it loses if the shader thread or the extra DRAM traffic pushes `repeat` back up.
- 600 MHz overworlds at 2×, on the 1104 walking measurement. Dialogs can be revisited only after 816 walking is real.
- A smoother game by giving the queue infinite depth. Past 3 the latency is the feature. The hold cap (20 ms) exists so a chronically slow scene drops instead of sliding forever.

## Order on the device

1. Pin HeartGold walking at 816, queue 3, wait 20, no shader. Then queue 2. Log `repeat`, the `[raw]` buckets, `[queue]`, devfreq.
2. Same for Black 2, and for a still HeartGold dialog (the original failure).
3. Stop if those pass. Change the governor's floor and its light-drop rule, key the memory file by queue depth, and write the profile cap to sysfs at session start.
4. If a scene is raster-bound, run the `DSFLIP_SHADER_COPY=1` probe at the same pin. Build `DSFLIP_CACHED_FB` only if DraStic's threads get cheaper.
5. If a DMC node exists and frames are late with cool-to-mid cores, floor it for the session and remeasure.
6. Audio chunk, IRQ affinity, idle and `mlock` only for whatever spikes are left.

The first three steps need no new library and no new code in the raster. They are the measurement the 1104 floor was set without.
