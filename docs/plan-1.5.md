# ROCKNIXDS 1.5 plan

Carried over from 1.4 (see [plan-1.4.md](plan-1.4.md) and the [optimization report](optimization-1.4.md)).

## 1. Starting and quitting games (first)

**Done (2026-09-28):** the old measurement polled the way back one milestone after another, so "ES answers" could
only be seen after the game unit had ended; `switchtime.sh` now polls them together and prints the session's and the
ES launcher's own timestamps (both on the uptime clock). The real timeline after a quit (1.4.0): DraStic gone +0.44 s
(its teardown), play stats +0.30 s (on the path), sway started, outputs +1.64 s, ES answering +3.4 s, visible ~+4.4 s
(a fixed 1 s wait after ES answered). Changes: the play stats are written while sway starts (ES still starts only
once they're written: it reads them), and the patched ES says when its first view is complete (es-rgds-firstview.patch;
the launcher shows the window then). Now: outputs +1.38-1.43 s, ES answering +3.11-3.15 s, window shown +3.84-3.92 s
(the launcher's timestamp; switchtime's own visibility poll lags ~0.5 s behind it). The rest is DraStic's teardown
(0.43 s), sway's start (0.8 s) and ES loading its gamelists and theme (~1.4 s after its window appears).
Test launches (smoke.sh, switchtime.sh) no longer count as plays (/tmp/rocknixds-testing).

1.4 starts a game as fast as 1.3 (first frame 3.47 s after the launch request, 1.3: 3.44 s), but the way back to the
menu is ~0.8 s slower: ES answers its API 3.51 s after the game ends (1.3: 2.65 s) and is visible at 5.26 s (1.3:
4.47 s). Known so far: it isn't the menu governor hook (ES starts in ~1.7 s with or without it), the CPU clock limit
(session.sh lifts it the moment DraStic exits) or the PipeWire rate reset (~20 ms). Next: time each step of
restore.sh and ES's start on 1.3's and 1.4's ES binaries (`tools/switchtime.sh`), then look at the launch chain
itself (ROCKNIX's runemu.sh and start_drastic.sh take ~2.3 s before our unit starts).

## 2. Power, continued

- **Done (2026-09-29): ROCKNIX's `powerstate` service** replaced through a drop-in, like the LED monitor: 131 -> 8
  ticks per 60 s (2.2% -> 0.13% of a core, with its child processes), the system's process starts 3/s -> 0/s. Same
  behaviour: mode changes still run ROCKNIX's GPU profile, ledcontrol and log; `ledcontrol discharging` at a full
  battery is done in-process (checked against ROCKNIX's: the same LEDs); a changed ROCKNIX script or ledcontrol
  is run as is (tested). Was: a bash loop polling every 2 s with `cat`, `awk` on the battery's whole uevent
  (I2C reads) and `sleep`, ~3% of a core, more on battery (it looks settings up with `awk` on every pass after the
  first 40 s). Same treatment as `battery-led-status`: a fork-free copy behind a systemd drop-in that hands back to
  ROCKNIX's script if that ever changes (md5 79dcb5ee1f43876d5c1dff429f87b36b), including `ledcontrol discharging`
  done in-process when the battery is above 97%.
- **Done (2026-09-29): the CPU governor remembers**, per game, shader and resolution, the clocks that dropped frames
  (`dsflip/cpugov/`), so a session starts with them banned for as long as their strikes say (30 s for one, doubling,
  up to 10 min) instead of finding them again by dropping frames, mostly in its first minute. Two minutes at a clock
  or lower without a drop forgive it a strike; drops at the top clock aren't held against anything; nothing is
  stepped down before DraStic's resolution has settled (it starts at 1x and switches to 2x a second later). Tested
  with a seeded memory: bans held from the start, new drops saved, a clock forgiven after 120 s. Test launches
  don't write it (`DSFLIP_CPUGOV_MEMORY=0` under /tmp/rocknixds-testing).
- **Done (2026-09-29): drop storms from the frame pacing.** Measuring the governor turned up the bigger cause of
  dropped frames: minute-long storms (up to 32 drops/s, at the full CPU clock) in ~1 of 10 two-minute runs, 1.3 and
  1.4 alike. The cause: after one late latch, libdsflip committed as soon as the pending flip landed; when that was
  the bottom panel's flip (its vblank comes first when the panels are ~8 ms apart), the commit missed the bottom's
  next vblank, so the next latch was late too: every latch "late" from then on (`late=600` per 10 s in the pace
  log, seen in 2 of 2 runs within 90 s). Each late latch also widened the commit margin (to 4.5-5.3 ms, though
  the top panel never missed: `missed=0`), which squeezed the latch into the window where DraStic's frames
  arrive: the storms. Now a catch-up commit is made only while it still makes both panels' next vblanks, a
  latch that finds this cycle's own commit pending isn't late, and the margin grows only when a commit made at
  the latch missed. HeartGold ds-crisp, 200 s: late latches 0-1 per 10 s all run, margin at 1300 us, 0.02 drops/s
  (before: 1.93 in the run with a storm).
- **Skipping unchanged frames with a shader**: needs a way to know a frame didn't change without reading DraStic's
  uncached buffers back.
- **ds-fsr** (5.3 ms per panel at 800 MHz, 4.3 in the 1.4 comparison session): a two-pass version that analyses each
  source pixel once.
- **Other systems**: ROCKNIX's per-system governors for RetroArch cores, measured with free homebrew ROMs.

## 3. SuperDrastic

**Done (2026-09-29):** SuperDrastic 0.1.0 released; `install.sh` downloads the version pinned in `SUPERDRASTIC`,
checks its sha256 and installs `libsuperdrastic.so` as `libdsflip.so` plus the package's shaders
(`RGDS_SUPERDRASTIC=<tarball>`: a local package). The library source, shaders, `third_party/` and the bring-up tools
left this repo; `tools/padkey.py` and `tools/kmsrun.sh` stayed for the harness, the shader benchmark moved to
SuperDrastic. CI checks the pinned package. Tested on the device: the install from this checkout put the release's
library in place byte for byte, smoke test passed. Found on the way: the RA token was never used when the username's
case differed (fixed in SuperDrastic main, for its next release; bump `SUPERDRASTIC` then).

libdsflip now also lives on its own as [SuperDrastic](https://github.com/JorreFog/SuperDrastic) (libsuperdrastic.so, a generic launcher, the integration guide; tested on the RG DS in all three display handovers). ROCKNIXDS should install SuperDrastic's release instead of carrying its own copy of the library (dsflip/ then keeps only the ROCKNIX session scripts), so fixes land once.

## 5. Added 2026-09-29

- **Done: resume on quit.** The exit hotkey sends SIGUSR1 (session.sh writes `-USR1 drastic` into ROCKNIX's
  /tmp/.process-kill-data); SuperDrastic saves to `<savestates>/<game>.resume.dss` and quits, and the next start
  loads it once. End-to-end on the device through ES and the hotkey's own command: saved in 0.83 s, ES back 3.9 s
  after the press (~0.8 s more than a plain quit), resumed on the next start, slot 0 and the in-game save unchanged.
  In SuperDrastic 0.2.0, which `SUPERDRASTIC` pins (re-tested through the installer: same results).
- **Done: README animation** of both screens (docs/demo_anim.py).
- **Done: does ds-fsr look better?** [filters-1.5.md](filters-1.5.md): at 2× no (same picture, 8-9× the GPU time of
  ds-crisp); at 1× it smooths 3D edges (closest sharp filter to the real 2× render) but rounds pixel art. Next: make
  ds-fsr fall back to the ds-crisp path at 2× and label it "for 1×" in ES.

## 6. Lockdown, updates, themes, monitoring (added 2026-09-29)

- **Done:** es-rgds-lockdown.patch (ES): the settings that break the dual-screen setup, the power handling or
  DraStic are left out (list in the patch; `rocknixds/unlocked` shows them), the theme list offers only
  `rocknixds/themes.allow`, ROCKNIX's OS updater and theme downloader are hidden. The DS system offers ROCKNIXDS's
  DraStic only (es-features.sh edits es_systems.cfg at every boot; uninstall puts ROCKNIX's list back). Verified on
  the device by screenshots of the menus.
- **Done:** ROCKNIXDS updates: *Updates & downloads > ROCKNIXDS* (version, stable/beta channel, automatic check
  switch, check now + install) and rocknixds-update-check.timer (4 min after boot, then every 6 h; one notification
  per update through ES's /notify). install.sh records what it installed (release tag or beta commit).
- **Done:** canvas-ds (toniremi) as a second dual-screen theme at a pinned commit; the launcher and the theme hook
  know both dual-screen themes. Verified on the device, both screens, system view and DS game list: all 9 colour
  schemes (custom is the author's template: its wallpapers say where to put your own), grid and carousel, all 10
  grid sizes, both icon styles, the font sizes, metadata on/off. One adaptation: its game-list image box was 672 px
  wide, 32 px more than the top screen, so DS screenshots (both screens side by side) were cut; the installer caps it
  at 614 px (4:3 images and box art unchanged).
- **Done:** tools/rgds-monitor.py (live stats + per-session logs of real play).

## 4. Parked branches

- `standalone-wip`: superseded by SuperDrastic.
- `plus-alpha`: the RG DS Plus port (pre-release v1.4-plus-alpha.1, untested on hardware); rebase onto 1.4 when the
  hardware is at hand.
