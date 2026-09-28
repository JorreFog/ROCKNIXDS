# ROCKNIXDS 1.4 plan

1.4 is meant to be a big update. Order agreed on 2026-09-27: check suspend first (it could reorder everything),
then screen modes and touch in ES's menus, then the RetroAchievements update, then a prototype of near-instant
switching, then CI builds. The in-game microphone check needs a play session on the device (battery tuning: section 7). Carried over from `plan-1.3.md`: B6, B7, B13, B14, I5 (debug switch, texture
alloc fallback), I7 (video previews, unplayed look), I8 (hires warning, release assets), I9 (ES patch, CI).

**Priority from 2026-09-28: optimization first** (sections 6 and 7: GPU cost of the shaders, then CPU, battery and
heat across the device), before anything else. The standalone libdsflip package is parked on branch
`standalone-wip` (built, not yet tested on the device).

Already on `beta` for 1.4: **ds-fsr**, AMD FSR 1.0 (EASU) fast enough for both panels (see its header).

## 1. Suspend during a game — works (2026-09-27)

Tested with the RTC alarm (`echo +20 > /sys/class/rtc/rtc0/wakealarm; systemctl suspend`, the same path as the
power key: logind `HandlePowerKey=suspend` + ROCKNIX's `system-sleep/sleep` hook), HeartGold at 2× with a shader:
the device slept ~20 s (deep), the game resumed at 60 fps with 2 frames dropped at resume, the audio pump had no
underruns or xruns, both backlights came back (1332), the touchscreen was not re-created (the reopen from 1.3
wasn't needed), and quitting brought ES back. Traps when reading the logs: CLOCK_MONOTONIC and dmesg stop in deep
sleep, so libdsflip sees only a ~3 s gap (the sleep hook's work) and journald can print suspend entry and exit
milliseconds apart. The mali_kbase stack trace in dmesg comes from an idle suspend too (ROCKNIX's, harmless).

## 2a. Screen modes — ds-fsr and ds-integer (2026-09-27)

- **ds-fsr** (on `beta` since d721645): FSR 1.0 EASU at 4.75 ms per panel; the GPU is pinned to 800 MHz for it.
- **ds-integer**: each screen at exactly 2×, centred (64/48 px borders) in a procedural bezel (recessed well, lit
  edge, the theme's grey). Verified exact on a real frame: at 2× the 512×384 window equals DraStic's buffer 1:1, at
  1× every DS pixel is a 2×2 block. 2.0 ms per panel (ds-crisp 1.96). A shader now declares where it draws the DS
  screen (`// dsflip-viewport: 64 48 512 384`), `shader.c` reads it, and libdsflip maps touch into that rectangle:
  tested with the tap hook, (64,48)→DS 0,0, (575,431)→255,191, (320,240)→128,96; taps on the bezel (even 1 px
  outside) are ignored, a touch that slides off the screen is clamped to its edge.

## 2b. Touch in ES's menus — works (2026-09-27)

ROCKNIX's dual-screen sway lines attach both Goodix touchscreens to seat1 (and make seat1 the fallback) once ES's
window exists. ES (SDL2 Wayland) binds wl_touch on both seats, but with the devices only on seat1 it received no
touch events at all (WAYLAND_DEBUG=client); with them only on seat0, none either; attached to both, every touch
arrives, at the right position (the bottom panel is x 640..1280 of ES's 1920 canvas via ROCKNIX's calibration
matrix). So `sway-config.theme` keeps ROCKNIX's seat1 lines and adds `seat seat0 attach` for the touchscreens.
ES's own touch model then works: swipe scrolls the carousel, a tap opens the current selection (anywhere, by ES's
design), taps pick menu rows. Tested with `touchtap.py`, which writes a real touch into the evdev node.
Games are unaffected (sway is stopped; libdsflip reads the touchscreen itself).

## 3. RetroAchievements

- **DSi-enhanced ROMs** were already verified in 1.3 (Black 2: its set loads, "1 of 187 unlocked").
- **Pop-ups redone (2026-09-27):** `dsflip/ui.c` renders cards (badge or game icon, coloured first line, title) and
  a progress pill in the DSi font (Liberation Sans without the theme), premultiplied ARGB into the 640x72 overlay
  plane, on its own thread; pop-ups queue. Badges are prefetched after the game loads (HeartGold: 137 of 137 in a
  few seconds) and cached, so an unlock shows its badge at once. rc_client's PROGRESS_INDICATOR_SHOW/UPDATE/HIDE
  drive the pill. Checked on the device with `DSFLIP_UI_DEMO=1` and scanout + overlay dumps.
- **Drop-in animation:** each commit shows the card's bottom rows at the panel's top edge (SRC_Y/SRC_H and CRTC_H
  change, no scaling), eased out over 280 ms in and eased in over 200 ms out; the presenter commits the overlay every
  frame while it moves. Logged on the device: 4, 12, 23, 32 ... 71, 72 rows in; 72 ... 21, 10, 4, off out; no commit
  rejected, 60 fps.
- **DTCM (2026-09-27):** DraStic (r2.5.2.2) backs the DS address space with one shared-memory file,
  `/dev/shm/drastic_mapped_memory.dat` (open as an fd, unlinked), mapped view by view at a fixed base: main RAM at
  file offset 0, ITCM 0x400000 (32 KB, mirrored), shared WRAM 0x408000, DTCM 0x410000 (16 KB) at the game's CP15
  address (HeartGold 0x027E0000; the RAM mirror there is split around it). The file's bytes at 0x410000 equal the
  game's view at 0x027E0000 (checked). ra.c maps its own read-only view from DraStic's fd before the set loads
  and serves RA 0x1000000-0x1003FFF from it (`DSFLIP_RA_TEST` logs live DTCM reads through `read_memory`). Not yet
  seen: an actual unlock of a DTCM achievement (no such set at hand).
- **Hardcore:** not possible, DraStic's savestates, cheats and fast-forward can't be locked from outside.

## 4. Near-instant switching — opt-in `fast-switch` (2026-09-27)

A VT switch instead of stopping ES and sway: `chvt 12` makes seatd disable sway's session, which releases DRM
master in ~80 ms (verified with a SET_MASTER probe); `chvt` back gives it to sway again in ~7 ms, and ES, which
was waiting for the launch command, carries on. What it took:
- **The console blanks the panels** on the new VT: in KD_GRAPHICS the framebuffer console reports blank=4 and the
  panels stay dark although libdsflip flips at 60 fps (confirmed by eye). Keeping tty12 in text mode and writing 0
  to `/sys/class/graphics/fb0/blank` after the switch keeps them lit.
- **ES must keep its window** (`HideWindow=false`): with the default, ES tears its renderer down for the game, and
  its GL re-init after the VT round trip failed (`glGenTextures failed`, SIGSEGV, systemd restart). With the window
  kept: 0 restarts over 5 games. After the switch back, sway may have shrunk ES's floating window to 640x480;
  restore.sh sets it to 1920x480 at 0,0 again.
- **Buttons pressed during the game don't replay in ES** (tested: 3x right and 2x A mid-game, ES unchanged after).
- **Result** (`tools/switchtime.sh`, 3 cycles): quit → ES answering 1.2 s, visible 1.6-1.8 s (was 4.3 s); launch
  unchanged (3.4 s, ROCKNIX's scripts dominate).

Opt-in because HideWindow is global: with it off, other systems' emulators may show ES's loading screen on the
screen they don't use (untested). `fast-switch on|off|status` sets both; uninstall undoes it.

## 5. CI builds (2026-09-27)

`.github/workflows/build.yml` builds libdsflip.so on every push to `dsflip/` (beta, main, PRs, on demand): a Debian
trixie container (glibc 2.41, the same as ROCKNIX 20260901), clang + lld, an arm64 sysroot from the packages
`build.sh` names, then checks the result is AArch64, needs at most the device's glibc (it needs 2.38) and exports
the SDL hooks, and keeps it as an artifact. The CI-built library passed the smoke test on the device. The patched
ES isn't built in CI: it needs ROCKNIX's full build system (hours of toolchain per run).

## Bug round (2026-09-27): issue #3 and Reddit reports

- **RetroAchievements menu "Unauthenticated" (401)** (#3): the patched ES had no developer keys (ROCKNIX compiles
  them into its own ES from CI secrets). `es-rgds-devkeys.patch` reads them at run time from
  `/usr/bin/emulationstation`; nothing is redistributed. Menu verified on the device; ScreenScraper works again too.
- **"1 of N achievements" on unplayed games** (#3): RA adds a pseudo-achievement ("Warning: Unknown Emulator",
  id 101000001) for clients it doesn't know and reports it unlocked. `ra-fetch.py` skips ids >= 101000001 like
  rcheevos; the media tool now always refreshes the strip.
- **"Last played" wrong for DS** (#3): ES is stopped during a game, so `playstats.py` does ES's post-game update
  (play count, time played, last played) into the game's recovery file, as ES does. Not in fast-switch mode.
- **No unlock sound** (#3): libdsflip plays the sound picked in ES's RetroAchievements settings (stb_vorbis,
  mixed into the audio pump). The default is "none", as for RetroArch.
- **Scraper keyboard stretched** (Reddit): stock ES, which 1.3 ran on every other ROCKNIX release. The patched ES
  now runs wherever its libraries and symbols resolve (dynamic loader check).
- **Other themes stretched over both screens** (Reddit): the launcher forced a 1920x480 canvas. Other themes now
  get stock ROCKNIX's layout; `theme-changed.sh` restarts ES when the choice switches.
- **DQ4 screens flip in battles** (Reddit): not changed. `stressrom/dsswap.nds` toggles the DS screen-swap bit;
  libdsflip follows it exactly like the hardware (DraStic moves the pictures between its per-position textures).
  So DQ4 swapping screens for battles is the game's own behaviour, which stock DraStic shows too. Worth confirming
  with the tester (`touch /storage/.config/drastic/nodsflip` runs stock DraStic's display path).

## 6. Shaders: as little GPU as possible (requested 2026-09-28, not started)

Goal: every shader at the lowest GPU time and clock it can have without changing its picture, so the GPU can sit
at its lowest clock (less heat, longer battery). Method, per shader: `shtest SRC=<real frame> PIPE=1` for GPU time
per panel, a pixel diff against the current version (identical, or a stated tolerance), then a play session's
drops/s and the GPU's average clock (devfreq trans_stat) before and after.
- **Skip unchanged frames:** when DraStic presents the same picture again (menus, pauses, 30 fps games), reuse the
  last output instead of shading it again. Likely the biggest single saving.
- **Static borders:** ds-integer's bezel never changes; draw it once per buffer and shade only the 512x384 screen
  (scissor), instead of the whole 640x480 panel every frame.
- **Per-shader work:** tap positions computed in the vertex shader (varyings) instead of per pixel, mediump
  wherever the diff stays exact (highp only where it was measured to matter, see ds-fsr's positions), fewer
  texture reads (bilinear taps that fetch 2 texels at once), no branches in the hot path.
- **The stock names** (lcd3x, lcd1x-nds-color, sharp-bilinear...) come from ROCKNIX's libdrastouch at run time
  and can't be edited here; faster look-alikes of our own could replace them, checked against the originals.
- **Then the clock:** re-run 1.3's I6 sweep; with cheaper shaders the 400 MHz floor may drop to 200-300 MHz.

**Measured 2026-09-28** (`tools/shaders.sh`: ms per 640x480 panel at 800 MHz, real frames, copy mode as shipped,
draws queued back to back):

| shader | 2x | 1x | | shader | 2x | 1x |
|---|---|---|---|---|---|---|
| null (pass-through) | 1.97 | 1.03 | | sharp-bilinear | 1.93 | 1.02 |
| ds-crisp | 1.95 | 1.09 | | lcd1x-nds-color | 2.07 | 2.03 |
| ds-crisp-color | 1.96 | 1.65 | | lcd3x | 2.01 | 1.10 |
| ds-grid | 1.91 | 1.39 | | scanlines | 1.87 | 1.11 |
| ds-grid-color | 2.50 | 2.48 | | sharp-shimmerless | 1.96 | 1.09 |
| ds-grid-2x | 1.88 | 1.01 | | quilez | 1.83 | 1.09 |
| ds-integer | 1.91 | 1.06 | | ds-fsr | 5.28 | 5.27 |

Almost every shader costs what the pass-through costs: the time is the **upload** of DraStic's frame
(glTexSubImage2D; ~1 ms more at 2x than 1x) and writing the panel, not the shader's math. Importing DraStic's buffer
instead (dma-buf, `DSFLIP_SHADER_COPY=0`, already in libdsflip): null 2x 1.90 -> 0.69 ms, ds-crisp 1.96 -> 0.75,
1x 1.06 -> 0.63; ds-fsr unchanged (5.3: its math). In a real session (HeartGold 2x, ds-crisp, copy mode) the shader
thread alone took 17% of a core, mostly that upload, and the GPU sat at its 400 MHz floor throughout.

**Import mode is now the default** (23e2e85). With the upload gone (tools/shaders.sh, import, 800 MHz), the floor is
~0.7 ms per panel and the real costs show: ds-crisp 0.76, sharp-bilinear 0.79, lcd3x 0.86, ds-integer 0.92,
ds-grid-2x 0.98, ds-grid 1.38, ds-crisp-color 1.65, lcd1x-nds-color 2.03, ds-grid-color 2.48, ds-fsr 5.29.
(Absolute times drift up to ~20% between runs; compare variants within one run.)
- **ds-grid:** the LCD mask was ~0.45 ms of it. "Is there a DS pixel boundary inside this output pixel" is
  ceil(p/scale)*scale < p+1, for all four lines as one vec4: bit-identical output on real frames (and for every
  pixel at scales 2.5-7.5, with a reciprocal 1 ulp off), 1.11 -> 0.99 ms.
- **The -color shaders:** fp32 arithmetic was the limit, not pow() (a highp polynomial version took the same
  time). mediump with both gamma curves as polynomials: ds-crisp-color 1.33 -> 1.06, ds-grid-color ~2.2 -> 1.9 ms
  (same run); real frames differ by <= 2 levels, >1 level on <= 0.007% of pixels. HeartGold 2x with ds-grid-color:
  GPU average 600 -> 548 MHz, drops unchanged (0.09/s). They are now generated by tools/gen-color-shaders.py.
- **GPU floor:** 400 MHz stays. ds-crisp and lcd1x-nds-color were as smooth at 200, ds-grid-color dropped more in
  one run (0.70/s); the heavy shaders average 530-600 MHz whatever the floor, and at these clocks the GPU is likely
  at its lowest voltage anyway, so a lower floor would save little.

## 7. CPU, battery and heat across the whole device (requested 2026-09-28, not started)

Goal: the same games and menus for less CPU, less power and a cooler device. Replaces the "battery tuning" note at
the top. Measure first, per scene, with numbers that can be compared: CPU per thread (/proc/<pid>/task/*/stat),
cpufreq time_in_state, GPU devfreq trans_stat, SoC temperature (thermal_zone*), and battery power
(voltage_avg x current_avg, on battery, charger unplugged: the gauge's percentage is unreliable). Baseline scenes:
ES idle in the game list, HeartGold walking at 1x and 2x (zero-copy and a shader), a RetroArch system.
- **Our own threads in DraStic sessions:** presenter, audio pump (RT timer every 256 samples), touch reader,
  shader worker, RA and UI threads: wake-ups per second and CPU each; no polling where an event can wait.
- **DraStic itself:** the CPU sweep 1.3 did for the GPU (governor, min/max clock, which cores), keeping drops at 0.
- **ES and the menus:** idle CPU/GPU in the game list (animations, the carousel, the clock and battery widgets'
  redraws); ES should draw nothing when nothing changes.
- **Other systems** (RetroArch cores, PPSSPP ...): ROCKNIX's per-system governor settings; only where a measured
  saving holds without drops, and as settings, not patches to ROCKNIX.
- **Idle and background:** services and timers that wake the device while playing (journald, network scans).

**Measured 2026-09-28** (`tools/power.sh`: HeartGold 2x, no shader, walking, 60 s per setting; battery current
on a weak charger, so read the trend):

| CPU | CPU used (of 400%) | busiest DraStic thread | drops/s | battery | SoC |
|---|---|---|---|---|---|
| performance, 1992 MHz (ROCKNIX's nds setting) | 121% | 46% | 0.07 | -123..-178 mA | 48 C |
| schedutil (averaged 1771 MHz) | 138% | 51% | 0.10 | -154 mA | 47 C |
| capped 1608 MHz | 127% | 50% | 0.10 | -99 mA | 47 C |
| capped 1416 MHz | 146% | 56% | 0.07 | -66 mA | 46 C |
| capped 1104 MHz | 183% | 66% | 0.13 | -40 mA | 43 C |

Our own threads are small (presenter 1.8%, ALSA writer 0.9%, pump and touch < 0.5%); the audio path through
PipeWire costs ~9% of a core (a `data-loop` thread inside DraStic + pipewire's own). A fixed cap is too blunt for
heavier games, so libdsflip gets its own governor (`cpugov.c`): the lowest clock at which DraStic's busiest thread
stays under 65%, up at once, down after 2 s.

**Done so far (2026-09-28, on `beta`):**
- `cpugov.c` (see above), tuned on real play: average load alone let single frames run late (fixed ds-crisp caps:
  1416 MHz 0.24-0.44 drops/s, 1104 MHz 0.26-0.90, vs 0.00-0.04 at 1992), so it also watches the heaviest frame, the
  frame rate over 1 s and the queue's drops, and keeps away for 30 s from a clock that dropped a frame. Result:
  ds-crisp 1660 MHz average at 0.04 drops/s, no shader 1729 MHz at 0.09; the 3D stress ROM's heavy levels 51.4 fps
  (fixed 1992: 51.0), where it goes to 1992 and stays. Heavier games get the full clock, lighter ones less.
- Tuned again after a smoke test dropped 2.93 frames/s at 816 MHz in a still scene (frames dropped although the
  heaviest frame was only ~40% of a refresh): any drop now moves up a step, a clock that keeps dropping is banned
  30 s doubling to 10 min, and the floor is 1104 MHz. HeartGold 2x walking, 90 s: no shader 1622 MHz average at 0.04
  drops/s, ds-crisp 1778 MHz at 0.07 (fixed 1992: 0.07-0.09 and 0.00-0.04). Smoke tests: 0.04 drops/s.
- Shaders import DraStic's frames (no upload), see section 6.
- The menus: ROCKNIX leaves the CPU at `performance` in ES (nothing applies a governor at boot, and its launcher
  sets performance after every game; `system.cpugovernor` is the games' default, not the menus'). `menu-power.sh`
  puts the menus on schedutil at boot, after every ES game (game-end script) and after DS sessions (restore.sh).
  Verified: after a launch the governor read performance in the hook and schedutil after it.
  `touch /storage/.config/rocknixds-menu-performance` keeps ROCKNIX's behaviour.

- **Display interrupt storm (found 2026-09-28):** stopping sway for a DS game while its buffers were still being
  scanned out made the display controller read freed memory (`rk_iommu ... Page fault`), and video port 1 then
  looped on `POST_BUF_EMPTY`: ~84,000 interrupts/s on CPU 0, ~60% of a core, in games AND the menus, until the
  next full modeset (it survived later games and sway restarts). Seen once in ~40 sway stops. session.sh now
  switches the panels off through sway before stopping it; restore.sh counts the display controller's interrupts
  after a session and power-cycles the panels if they storm (swaymsg power off/on cleared it: 84,618/s -> 93/s).

- **The idle menu (2026-09-28):** ES + sway used ~45% of a core in a menu nobody touched. The theme's four looping
  animations (background drift, START frame pulses, logo glow) kept ES drawing 60 frames a second and sway
  compositing them; ES's power saver ("default") would still draw 25 a second. Now the animations stop after a few
  cycles (they restart on navigation), the power saver is "enhanced", and es-rgds-powersaver.patch wakes ES each
  minute for the clock (it was frozen otherwise) and polls input per frame instead of per millisecond (SDL can't
  block with a gamepad open). Idle menu: CPU 101% -> 39% of 400, average clock 1389 -> ~800 MHz, GPU 400 -> 200 MHz,
  SoC 43 -> 38 C, ~150 mA less. Then audio: ES kept a stream open (silence; SDL2 streams silence even when paused)
  and PipeWire processed it, ~13% of a core, with the sink RUNNING (amplifier on). The patch closes ES's audio after
  60 s without input (reopening measured 63-102 ms, so not at the 3 s standby: that delayed the first press while
  browsing). Idle menu now: 13% of 400 at ~530 MHz average, sink SUSPENDED.

- **Game audio (2026-09-28):** PipeWire ran its graph every 256 samples at 48 kHz (DraStic's ALSA periods of 7.5 ms
  set the quantum) and resampled DraStic's 44.1 kHz in every cycle: ~9% of a core. ROCKNIX's volume keys change the
  PipeWire sink's (soft) volume, so bypassing PipeWire would lose them. session.sh now forces PipeWire's graph to
  44.1 kHz for the session (pw-metadata clock.force-rate, reset by restore.sh): audio threads 9.3% -> 4.9% at 1608
  MHz, pump steady (ring 1280..1792), drops 0.00-0.04. Bigger ALSA periods (60 ms buffer) cut wake-ups further but
  made the drain bursty (drops 0.18/s) and add latency: not taken. Switching the rate mid-stream made the drain
  uneven for the rest of the session, which is why it's set before DraStic starts.

- **ROCKNIX's battery LED monitor (2026-09-28):** a bash loop that started ~15 processes a second ($(cat) for sysfs,
  awk via get_setting twice per poll, subshells, sleep): 6.5% of a core with its children, half of the idle menu's
  remaining CPU. battery-led-status is the same monitor (LEDs, thresholds, 1 s poll, overrides) reading sysfs with
  bash's read, re-reading system.cfg only when it changed and waiting in read -t: 0.4% (the rest is the fuel gauge's
  I2C reads). A systemd drop-in in /storage/.config/system.d points the service at it; it runs ROCKNIX's own
  script instead whenever that differs from the version it follows (checked by md5 at start; tested).

**Still open:** skipping unchanged frames in shader mode, ds-fsr (5.3 ms per panel), other
systems' governors (no ROMs on the test device).

Found on the way: ROCKNIX's `powerstate` service re-applies a GPU profile whenever the battery status flips
(charger plugged/unplugged, or a weak charger flapping), which overrode the session's GPU clock mid-game; session.sh
now puts it back (7e2ff35). ES idle in the menu: CPU at 1992 MHz by the `performance` setting (schedutil: 763 MHz
average, 1 C cooler), PipeWire and ES's audio threads run while nothing plays, and the GPU sits at 800 MHz on
battery with `gpuperf=performance`.

## Proposal: libdsflip as a standalone package (Reddit request, for other firmwares)

libdsflip is already separable: one LD_PRELOAD library for DraStic r2.5.2.2 (aarch64, glibc >= 2.38), built in CI,
with the top panel's connector set by `DSFLIP_TOP`. What another firmware needs, as a release asset
`libdsflip-<version>-aarch64.tar.gz`: `libdsflip.so`, the shaders, a minimal `dsflip-run` that gives it the
display (stop the compositor or switch VT, run DraStic with the library preloaded, restore) and a short contract:
KMS with two connectors, DRM master, the verdict file (`/tmp/dsflip-state`), the log, the environment variables.
Updates: the firmware polls GitHub's releases API for the latest asset. ROCKNIX users keep `install.sh`
(`--no-theme` already installs libdsflip without the theme).
