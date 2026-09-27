# ROCKNIXDS 1.3: bugs found in 1.2 and what to build next

Written 2026-09-27 after a code review of v1.2 (commit `73b2ea2`): every script and C file was read, the
device's logs from real sessions were checked (`dsflip.log`, `last-session.log`, ES's log), and the beta
testers' report ([#1](https://github.com/JorreFog/ROCKNIXDS/issues/1)) was taken into account. Each item says
where the problem is, how it was found, and what 1.3 should do about it.

Severity: **high** = users hit it or lose data, **medium** = real but rare or cosmetic-with-consequences,
**low** = cleanliness.

---

## Part 1: bugs in 1.2

### B1. Uninstall throws away settings changed after the install (high) — DONE

`install.sh --uninstall` copies back **every** file it backed up at install time
([`install.sh:74`](../install.sh)): `es_settings.cfg`, `system.cfg`, the sway config and the DraStic launcher,
whole. Anything the user changed afterwards is lost: the RetroAchievements login, other emulators' settings, ES
options, the ES theme choice for other themes. Found by reading the uninstall block; confirmed by the
`backup_once` list.

**Plan:** make the undo surgical. At install time record only the keys we change and their previous values in
`$BACKUP/undo.env` (`nds.hires_3d`, ES `ThemeSet`/`FullScreenMenu`/`GameTransitionStyle`, the launcher file).
Uninstall puts those keys back and leaves the rest of each file alone. Keep the whole-file copies as a last
resort (`--uninstall --restore-files`). Test: install, change an unrelated setting, uninstall, check it survived.

### B2. Stopping the game unit leaves both screens black (high) — DONE

`session.sh` runs in `systemd-run --unit=dsflip-game` with the default `KillMode=control-group`
([`drastic-wrapper.sh:11`](../dsflip/device/drastic-wrapper.sh)). `systemctl stop dsflip-game` kills the shell
together with DraStic, so the lines that restart sway and ES and restore the GPU governor never run. The
installer's own uninstall does exactly this stop ([`install.sh:69`](../install.sh)) and happens to restart sway
afterwards, but anything else that stops the unit (a future "quit game" from ES, a low-battery shutdown path,
a user on ssh) ends with black panels and the GPU pinned at `performance`. Found by reading; the unit shows
`KillMode=control-group` on the device.

**Plan:** pass `-p ExecStopPost=/storage/.config/drastic/dsflip/restore.sh` (restores the governor, starts
sway and ES) and `-p KillMode=mixed` so DraStic gets the signal first. Test: `systemctl stop dsflip-game`
mid-game must bring the menu back within the usual ~5 s.

### B3. libdsflip gives up after 3 s if it can't take the display (medium) — DONE

`session.sh` sleeps 0.5 s after stopping sway, starts DraStic, and after 3 s kills it if the log says
`passthrough` ([`session.sh:29`](../dsflip/device/session.sh)). If sway is slow to release DRM master (it
happens on a busy system) the user gets a black screen, then the menu again, with no game and no message.

**Plan:** in `init()` retry `drmSetMaster` for up to 2 s (every 50 ms) before deciding on passthrough; in
`session.sh` show why it aborted (a toast is impossible there, so write a line to `last-session.log` and let
the ES launcher show a popup on return, see I8). Test: start a game while sway is being restarted.

### B4. Real-time presenter + spin lock can stall (medium) — DONE

`mu` is a spin lock built on `sched_yield` ([`dsflip.c:93`](../dsflip/dsflip.c)). The presenter runs
`SCHED_FIFO` 10 and the audio pump `SCHED_FIFO` 20, while DraStic's main thread (which takes `mu` in
Lock/Unlock/Present) and the RetroAchievements HTTP threads run at normal priority. If a normal-priority holder
is preempted on a core and the presenter starts spinning on that same core, `sched_yield` never hands the CPU
to the holder; the kernel's RT throttle breaks the deadlock after up to 950 ms. The 10 s `[lock]` lines on the
device show 0 contentions, so it is rare, but each hit would be a visible freeze. The reason for not using
`pthread_mutex_t` (the x86 headers gave it the wrong size) no longer applies: the build uses a real aarch64
sysroot since RetroAchievements was added.

**Plan:** replace `mu` and `tmu` with `pthread_mutex_t` initialised with `PTHREAD_PRIO_INHERIT`; keep the
contention diagnostics (they can wrap `pthread_mutex_trylock`). Test: the `[lock]` line still reports, a 90 s
HeartGold run shows the same drop rate as before.

### B5. Recreating a screen texture while a buffer is on screen corrupts buffer tracking (medium) — DONE

When DraStic recreates a screen texture (the hires toggle in its menu does this), the recycled slot's buffers
in `READY`/`QUEUED`/`SCANOUT` state are zeroed and immediately re-allocated by `mkbuf`
([`dsflip.c:1069`](../dsflip/dsflip.c)), while `P[i].ready/queued/scan` still point at the same struct. A `READY`
one goes into the next commit with `fb = 0` (rejected commit, frames dropped); a `SCANOUT` one is later
`release()`d to `FREE` while DraStic may already be writing into the new buffer at that slot. In practice the
toggle happens from DraStic's menu, when the screen buffers are already off screen, so it is hard to hit.

**Plan:** never reuse a struct that the panels still reference: move such buffers to a small graveyard list
and free them from `on_flip` once no panel points at them. Test: toggle hires from the menu 20 times during a
game with `DSFLIP_LOG` at debug; no "commit rejected" lines.

### B6. A new pop-up can draw into the buffer still being scanned out (low)

`dsflip_toast` picks `toast[toast_cur]` when `toast_shown` is 0 ([`dsflip.c:397`](../dsflip/dsflip.c)). Right
after a toast expires, the "hide" commit is issued (`toast_shown = 0`) but its flip hasn't landed, so the plane
still shows that buffer; a toast arriving in that window (two unlocks in a row) writes into it: one torn frame
of a disappearing pop-up.

**Plan:** track which buffer the pending commit references and always pick the other one.

### B7. Shader start race decides the wrong buffer type (low)

`SDL_CreateTexture` chooses memory buffers (upload path) or dumb buffers (dma-buf import) from `shader_on`,
and `init()` waits at most 3 s for the shader worker ([`dsflip.c:985`](../dsflip/dsflip.c)). If libmali takes
longer on a cold start, the first textures get the import path, which is the one the copy mode was added to
avoid (the GPU IOMMU mapping interrupts every core), and the log still says "upload from memory".

**Plan:** make the wait bounded but authoritative (`SDL_CreateTexture` waits for `shader_done`), and log which
path each texture actually got.

### B8. The microphone is off unless the ES setting is changed (medium, documentation) — DONE (README)

`DSHOOK_MIC_THRESH` comes from ES's *Nintendo DS › microphone sensitivity*; its default is unset, so the
wrapper passes 0 and the log says `[mic] off`. That is stock ROCKNIX behaviour, but the 1.2 README and release
notes read as if blowing into the mic just works. Found in the device's `dsflip.log`.

**Plan:** README and release notes say to set the sensitivity (medium is a good start). Consider a default of
`medium` when the setting is unset (`DSFLIP_MIC_DEFAULT` in the wrapper) after checking it causes no false
presses in a quiet room.

### B9. The game description clips a line through the glyphs (low, visible) — DONE 2026-09-27 (fades)

`md_description` is 120 px tall at a ~22 px line height ([`theme-rgds.xml:689`](../dii-ess-aye/overlay/theme-rgds.xml)):
5.4 lines, and the last visible line is cut mid-glyph (visible on Pokémon Black 2's bubble).

**Plan:** size the box to whole lines (4 lines, `size` 0.2333 0.1833) or draw a 20 px fade at the bottom of the
bubble over the text (an image with a gradient at zIndex 58).

### B10. Our copy of `es_features.cfg` hides ROCKNIX's future changes (medium, verify) — DONE 2026-09-27

When the user has no `/storage/.config/emulationstation/es_features.cfg`, the installer copies the system one
there and adds the ds-* shader entries ([`install.sh`](../install.sh), the `.esf-created` flag). ES prefers the
user copy, so features ROCKNIX adds in a later release (new cores, new options) won't appear until uninstall.
On the reviewer's device the copies already differ only by our five lines. Verify ES's lookup order first
(`es_features.cfg` in `ROCKNIX/emulationstation-next`).

**Plan:** if ES supports a user overlay file, use it; otherwise regenerate the user copy at every boot from the
current system file plus our entries (in the autostart hook), so a ROCKNIX update flows through.

**Done:** ES does load `es_features_*.cfg` overlays (`CustomFeatures::loadAdditionnalFeatures`), but it appends
emulators and features without merging, so an overlay would add a second "shader" row. So `dsflip/device/
es-features.sh` runs from the installer and from its own autostart hook (`rocknixds-es-features`, before ES
starts): a copy the installer created is rebuilt from ROCKNIX's file whenever that file's md5 changes (previous
copy kept as `.rocknixds-old`); a copy that predates the install only gets our entries. Our choices go at the end
of the drastic-sa core's shader option, found by structure rather than after one particular choice, and the file
is only rewritten when its content changes. Tested: first build, rerun, a simulated ROCKNIX update (new emulator
and a new DraStic choice both flow in), a user's own copy (only reordered), a file without the option (untouched).

### B11. No version in the logs or the RetroAchievements user agent (low) — DONE

`ra.c` still reports `dsflip/1.0` ([`ra.c:299`](../dsflip/ra.c)) and `dsflip.log` has no header line, so a bug
report can't tell which build produced it. Tester reports in #1 needed exactly this.

**Plan:** a `VERSION` file at the repo root, baked into `libdsflip.so` (`-DDSFLIP_VERSION=`), written as the
first log line and into the user agent; `install.sh` writes it to `/storage/.config/rocknixds-version` and
prints it with `--version`.

### B12. `dsflip.log` is overwritten every launch (medium, the tester lost their evidence) — DONE

The log opens with `"w"` ([`dsflip.c:854`](../dsflip/dsflip.c)). The beta tester attached a log that only
covered their last game, so the RetroAchievements question in #1 couldn't be answered.

**Plan:** rotate: `dsflip.log` → `dsflip.log.1` … `.3` at start, and let `session.sh` append the ROM name and
exit code to a `sessions.log` that is never overwritten. The README's reporting instructions then just say
"attach `dsflip.log*`".

### B17. A transient crash sticks the device on stock ES until reboot (medium) — DONE 2026-09-27

Found while testing: `start_es_rgds.sh` counts crashes in `/tmp/es-rgds-fails` and falls back to stock ES at 2, but
never resets the count, so two quick crashes from a one-off cause (here: sway came up with no outputs during
testing, so ES crash-looped for a few seconds) left the device on stock ES until reboot. Stock ES ignores the
theme's patched features, most visibly the clock `<format>`, so the main menu's date rendered as the time.

**Fixed:** the launcher resets the counter after a session that ran at least 120 s (proof the patched binary is
fine); two genuinely quick crashes in a row still fall back. Verified: after clearing the stale count the patched
ES runs and the date shows again.

### B13. The patched ES fails to load an embedded image (low, verify)

ES's log shows once per start: `Could not initialize texture from memory, invalid data! (file path:
:/scroll_gradient.png)`. It is a resource compiled into the ES binary, used by text lists. This theme has no
text lists on screen, but ES's own menus may lose the list fade. Check whether stock ES logs the same; if it is
our build, the resource blob was miscompiled.

### B14. "Last played Unknown" / "Time played Unknown" (low, visible) — tried, see note

The home card prints ES's literal "Unknown" for a system never played
([`theme-rgds.xml:298`](../dii-ess-aye/overlay/theme-rgds.xml), `:308`). Also "0 played" and "None played" are
both possible depending on the binding.

**Plan:** hide the two lines when the value is "Unknown" (`<visible>` on the binding) and print "Not played
yet" once instead. **Note (2026-09-27):** wrapping either binding in an expression, even quoted, makes ES print
the expression text literally; the fix needs an ES-side binding (e.g. `{system:lastPlayedDate:short}` or a
`played` boolean in `es-rgds-bindings-clock.patch`).

### B15. Upgrades overwrite the pre-dsflip launcher backup (low) — DONE

`dsflip/device/install.sh:21` copies the current launcher to `drastic.pre-dsflip.bak` on every run; after the
first upgrade the "backup" is our own wrapper. The top-level installer's `backup_once` keeps the real original,
so uninstall still works, but the file is misleading.

**Plan:** create the `.bak` only if it doesn't exist.

---

## Part 2: improvements for 1.3

### I1. One command for the game art (the biggest gap users see) — DONE 2026-09-27 (`rocknixds-media.py`)

Cart scans, 3D boxes, label art and the RetroAchievements strip are what make the theme, and today they need
desktop Python, Pillow, a 108 MB LaunchBox `Metadata.zip` and hand-run pushes. The device has Python 3.14 but
no Pillow, so rendering has to stay on a PC or move into a tiny pure-Python renderer.

**Plan:** `scrape/rocknixds-media.py --device <ip>`, run on a PC: lists the games through ES's API over ssh,
fetches covers/snaps/titles from libretro-thumbnails (No-Intro name match with a fuzzy fallback), cart scans
from a small JSON index of LaunchBox "Cart - Front" URLs for DS that we generate and ship in the repo (so nobody
downloads the 108 MB zip), renders box3d/labelart/ra_panel, and uploads everything through the API. Ships a
`--only-new` mode and a dry run. On the device, `ra-fetch.py` keeps working alone for the strip's numbers, with
a pure-Python PNG writer (zlib is there) so the strip can refresh after each session without a PC. Stretch:
call the refresh from `session.sh` when a game exits, so the "N of M" is always current.

### I2. Faster switch into and out of a game — step 1 DONE 2026-09-27

Today about 5 s each way, mostly fixed sleeps and ES's own startup. Plan in three steps, measure each:

1. Replace the sleeps in `session.sh` with readiness polls (DRM master available; sway's socket present; ES's
   `isIdle`), and start DraStic the moment sway has released the display.
2. Keep ES's process alive across the game: it already destroys and recreates its window, so the cost is the
   window and theme reload, not the process. Check whether `essway.service` can stay up while sway is restarted
   underneath it (SDL's Wayland backend reconnect), or whether ES must be stopped.
**Step 1 result (2026-09-27, `tools/switchtime.sh`, 3 cycles each):**

| | before | after |
|---|---|---|
| launch request → DraStic's first frame | ~4.8 s | 3.5 s |
| exit hotkey → ES menu visible | ~6 s, then the panels froze 2+ s | 4.3 s |

What the time was: gptokeyb (in ES's unit) took 1.1 s to die on the stop's TERM, and the stop waited for it:
`session.sh` now kills it first (it's unused in a session). The 0.5 s sleep became libdsflip's own 10 ms master
retry. ROCKNIX's `es_settings` spent 1.35 s on 64 single-variable `systemctl import-environment` calls: the
launcher batches them into one (identical environment, 27 ms). The launcher's `swaymsg reload` after revealing
ES blocked sway for 2.1-2.5 s: it now runs the config's ES `exec_always` lines directly. Polls went from 0.5 to
0.1 s, and `restore.sh` starts sway and ES together (the launcher waits for sway's outputs). The 1 s wait between
ES's API answering and the reveal stays: at 0-0.3 s its top panel is still black (screenshots). The rest of the
launch, 2.3 s from the request to our unit, is ES and ROCKNIX's `runemu.sh`/`start_drastic.sh` (~140 awk/sed
forks for settings), outside this project.

3. Stretch: don't stop sway at all. wlroots can lease outputs to a DRM client (`wlr-drm-lease-v1`) but only
   outputs marked non-desktop; a small sway patch that flips both DSI outputs to non-desktop while a game runs
   would let libdsflip take a lease in ~100 ms and hand it back on exit. Prototype on the device before
   committing to it.

### I3. DraStic's menu on the bottom screen, with touch — DONE 2026-09-27 (bottom screen; touch impossible)

The 800×480 menu goes to the top panel and the bottom panel is black; touch is disabled in the menu
(`touch_rect_ok` is only set when a DS screen is on the bottom). DraStic's menu is built for touch.

**Plan:** show the menu on the bottom panel (hardware-scaled, as today) and map touch to menu coordinates
(`x * 800/640`), keep the top panel showing the last game frame. Test with the load/save state dialogs.

**Done:** the menu is routed to the bottom panel; the top plane is left alone, so it keeps the last game frame.
Touch cannot work: the premise was wrong. Disassembling drastic.real (r2.5.2.2, aarch64) shows the menu's input
loop (the SDL_PollEvent loop at 0x8b9c0) dispatches only SDL key (0x300/0x301), joystick axis (0x600), hat
(0x602) and button (0x603/0x604) events; mouse (0x400..0x402) and finger (0x700..) events fall through. The
binary does not even import SDL_GetMouseState. Injecting mouse events (relative stylus deltas, absolute menu-space
and absolute DS-space coordinates) was tried and ignored, as expected. Translating taps into d-pad presses by
reading the menu texture is not worth it: the menu's rows are about 8 px tall at 800×480.

### B16. Achievements are disabled at load because the RAM scan hasn't finished (high) — DONE 2026-09-27

Confirmed on the device with Pokémon Black 2: the log fills with `[rc] Disabled achievement NNN. Invalid address
000BA8` right after login. rc_client validates every achievement's memory addresses when the game loads
(`begin_identify_and_load_game`), but libdsflip finds DS main RAM on a separate thread that only starts at frame
90 and can finish after the load, so `read_memory` returns 0 for all of them and rc_client permanently disables
the set. This is almost certainly the tester's "achievements were not able to be achieved" in
[#1](https://github.com/JorreFog/ROCKNIXDS/issues/1): RetroAchievements looks logged in and identifies the game,
but nothing can ever unlock.

**Fixed:** login now only marks the client logged in; `ra_frame` starts the game load once the RAM scan has found
main memory (or after a ~15 s fallback), so rc_client validates against real memory. Verified on Black 2: 0 disabled
achievements, "1 of 187 unlocked" read from memory, where before the whole set was disabled.

### I4. RetroAchievements: the DTCM region and progress indicators

- **DTCM:** rcheevos maps the DS's 16 KB data TCM after main RAM (`consoleinfo.c`, "Nintendo DS"); sets that use
  it can't unlock here. DraStic keeps it in a separate buffer whose location the game chooses. Plan: find it the
  way main RAM was found: locate DraStic's ARM9 CP15 DTCM base variable (readable from the emulated register)
  and the 16 KB buffer that follows the same lifetime as main RAM; log candidates first, like `[ra] DS RAM
  candidate`.
- **Progress pop-ups:** rc_client already raises `RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW/HIDE`;
  show a small "3/5" indicator in the toast plane.
- **Nicer pop-ups:** render the toast text with the theme's DSi font (stb_truetype, the font file is on the
  device) instead of the 8×8 pixel font, and show the achievement badge (already downloaded by ra-fetch).
- **Verify DSi-enhanced ROMs** (Black 2 is one): rcheevos hashes DS and DSi through one path, and the device log
  shows HeartGold identified; run Black 2 once and confirm `[ra] game …` appears.

### I5. libdsflip robustness

- **DONE 2026-09-27:** Reopen the touch device if the read ends ([`dsflip.c:832`](../dsflip/dsflip.c)), for suspend/resume.
- A `DSFLIP_DEBUG=1` that adds per-frame lines, off by default, instead of the many one-off env switches.
- Handle a `SDL_CreateTexture` allocation failure by falling back to passthrough for that texture instead of
  leaving `s->tex = 0` with a half-built slot.

### I6. Cooler shader mode

`session.sh` pins the GPU governor to `performance` (800 MHz) whenever a shader is on
([`session.sh:20`](../dsflip/device/session.sh)). The shader pass takes ~2.5 ms per screen at 800 MHz, and since
1.2 the GPU work is off the timing path, so a lower clock may cost nothing. **Plan:** keep `simple_ondemand`
and raise `min_freq` to 400 MHz, then 300, measuring drops and SoC temperature over 5 min of HeartGold with
lcd1x+nds-color; pick the lowest that holds 0.1 drops/s.

### I7. Theme

- B9 and B14 above.
- **DONE 2026-09-27** (horizontal marquee after 1.5 s; ES was cutting them to "..."; the system name too).
  Long titles: "Pokemon HeartGold Version" fits the bubble; longer names will overflow the 24 px title. Plan:
  `autoScroll` horizontal on the title, or shrink to 20 px when longer than ~26 characters (an ES `size`
  expression).
- Touch in ES menus (known issue since 1.0): the sway config maps the Goodix touchscreen to `DSI-2` only for
  DraStic's window and attaches it to `seat1` for ES; ES then sees touches on the wrong output. Plan: trace with
  `libinput debug-events` and `swaymsg -t get_seats`, then fix the `map_to_output` for the ES window in
  `sway-config.theme`.
- Game list: show the video preview when one is scraped (the `md_video` element exists; the media tool in I1
  can fetch libretro-thumbnails' videos where available).
- **DONE 2026-09-27 (reported by Joar):** the tray behind the main menu's icon row was 600 px wide while the row
  spans x8..632, so the outer sockets hung over its ends. It is now a full-width band (`carousel_tray.svg`).
- An "unplayed" cartridge look (slightly desaturated label) so the carousel shows what's new at a glance.

### I8. Installer and upgrades

- B1, B10, B15 above.
- **Pre-flight check (DONE 2026-09-27, at run time in the launcher, so a later ROCKNIX update is covered too):** compare `/etc/os-release` `OS_VERSION` with the ROCKNIX build the patched ES was made
  against (`20260901` today). On a mismatch, skip the patched ES with a clear message instead of letting the
  launcher discover two crashes at boot; the theme still works on stock ES.
- **`--no-dsflip` with hires on** makes the stock path slow: warn, or turn hires off unless `--hires` is given.
- **Report problems from the launcher (DONE 2026-09-27, from `restore.sh` so it works without the theme too):** when `last-session.log` ends with an abort (B3) or a DraStic crash,
  `start_es_rgds.sh` shows an ES popup (`curl localhost:1234/messagebox`) with the reason.
- **Release assets instead of binaries in git:** `emulationstation-rgds` (10 MB) and `libdsflip.so` are
  committed on every rebuild. Publish them as release assets and have `install.sh` download the tagged release
  (`RGDS_BRANCH=beta` keeps working for testers by pointing at the latest pre-release). Keeps the repo small
  and lets testers verify checksums.

### I9. Code health

- **`dsflip.c` carries dead experiments:** the clock lock, PLL, `SDL_Delay` wake tracking, A/B legacy pacing,
  CPU pinning and RT scheduling are all off by default and documented as worse. Move them behind
  `#ifdef DSFLIP_EXPERIMENTS` (history keeps them) so the pacing code that ships is the ~400 lines that matter.
  **DONE 2026-09-27:** removed rather than `#ifdef`'d (history keeps them; the header names 5d67d79 as the last
  commit with them): the clock lock + PLL with its `gettimeofday`/`SDL_Delay` hooks, wake tracking and audio
  rate branch, CPU pinning, SCHED_FIFO for DraStic's threads, A/B legacy pacing (it re-read a /tmp file every
  10 s), the fixed-latch test mode and the audio chunk-size experiment. dsflip.c 1307 -> 1156 lines. Every
  removed path was off by default, so behaviour is unchanged: smoke test in shader and zero-copy mode, 60.0
  presents/s and 0 drops each, the latch still settling opposite the presents.
- **`es-rgds-uiwidth.patch` is 209 KB over 60+ files**, which makes rebasing onto a newer ROCKNIX ES a chore.
  Investigate a one-point change: every GUI reads its width through `Renderer::getScreenWidth()`; an
  `ES_UI_WIDTH` override in that helper (with the carousel views exempt) may replace most of the patch.
- **Build in CI:** a GitHub Actions job that builds `libdsflip.so` with clang and a Debian arm64 sysroot, and
  the ES binary in the ROCKNIX build container, so what ships is reproducible from the source in the repo.
- **DONE (1.3-dev):** **A smoke test:** `tools/smoke.sh <device-ip>`: install from the checkout, launch a ROM through ES's API,
  read `dsflip.log` for `ready`, 60 presents/s and 0 drops over 30 s, kill DraStic, check ES is back. This is
  what the 1.2 release was verified with by hand; make it one command and run it before every release.

---

## Status

Done on `beta` (2026-09-27): B1, B2, B3, B4, B5, B8 (docs), B11, B12, B15; `tools/smoke.sh` (I9's smoke test)
exists and passes on the device: 60 presents/s, ~0.07 drops/s, audio verified at the sink monitor and through the
speaker via the mic, and the game-launch/quit cycle. **B16** (RetroAchievements disabled at load, the likely cause
of the tester's report) was found and fixed the same day. I6 (cooler shaders) is now tunable by env; choosing a
lower default needs a heavy-gameplay temperature sweep. **B17** (a transient crash stuck the device on stock ES,
which dropped the patched clock format so the date showed as the time) was found and fixed too.

Later the same day: **I1** (`dii-ess-aye/scrape/rocknixds-media.py`, every DS game's art and text in one command),
**B9** (description fades) and **I3** (DraStic's menu on the bottom panel; touch in it is impossible, see I3).

## Suggested order

1. B1, B2, B12, B11, B15 (installer and logs: small, and they protect users and bug reports).
2. B4, B5, B3 (libdsflip correctness), then I6 (cooler shaders) since its measurements reuse the same runs.
3. I1 (media tool): the feature most testers will notice.
4. B9, B14, I7 theme items; B8 docs.
5. I2 step 1 and 2, I3 (switching and the menu), I4 (RetroAchievements).
6. I8, I9 (release assets, CI, smoke test) before tagging 1.3.

## Release checklist for 1.3

- `tools/smoke.sh` passes on a fresh install and on an upgrade from 1.2.
- Uninstall on a device with settings changed after install keeps those settings (B1).
- `systemctl stop dsflip-game` mid-game restores the menu (B2).
- 90 s HeartGold at 2× with lcd1x+nds-color: ≤ 0.1 drops/s, SoC temperature logged (I6).
- `dsflip.log` starts with the version line and the previous session's log still exists (B11, B12).
- README: microphone sensitivity note (B8), the media tool (I1), updated known issues.
