# 1.6 prep: hand-off to the cloud agents (2026-10-06)

## The goal

The owner's words (2026-10-05): "We are going to prepare a 1.6 version and this update will be the biggest and probably
the last large update. This one needs to have everything (except online play) working, it needs to look good in every
aspect, all features needs to work."

So 1.6 is one release for both handhelds (RG DS = `main` line, RG DS Plus = `plus-beta` line). Polish and "every
feature works" count as release criteria. Online play (Nintendo WFC) is parked and is **not** part of 1.6.

## Where things are

| What | Where |
| --- | --- |
| This branch | ROCKNIXDS `1.6-prep`, cut from `claude/tender-volta-a9nkpk-plus` (your 1.5.13 work ported to the Plus line, 38141e3) |
| RG DS twin of 1.5.13 | `claude/tender-volta-a9nkpk` (cut from `main`) |
| libdsflip used on the Plus | SuperDrastic branch `1.6-prep` = main 930c4c5 (0.5.0-beta.1, in-game menu) + your 4 patch commits (ds-fsr 3x, WFC, WFC fixes, mic tunables) + the parked WFC work-in-progress as one commit |
| Device test notes for 1.5.13 | `docs/handoff-local.md` |

Not yet committed anywhere before this branch, now included here:
- `dsflip/device/session.sh`, `restore.sh`: fixes for the two fast-switching glitches (see task 2). **Never run on a
  device in this exact form** except the first runs listed below.
- `dii-ess-aye/scrape/rocknixds-media.py`: no crash when the library has no DS games (ES answers 404).
- `dii-ess-aye/es-rgds-dsfirst.patch`: ES patch "the DS is listed before it has games; collection names capitalised".
  Written 2026-10-04, **not** in `tools/build-es.sh`'s PATCHES yet: it touches the same area as `es-rgds-emptylibrary`;
  check whether it is still needed, rebase it, then add it.
- `docs/1.6-prep/rgds-font-fix.patch`: the Pixel font fix (#34: digits 5, 2, Z, B redrawn) for the **RG DS** line. The
  Plus line has it already (af8badd). Apply it to the `main`-based branch.
- `docs/1.6-prep/ra-offline-repro.sh`: device script used for task 1.

## Tasks, in priority order

### 1. Game start froze, the Plus had to be restarted (found 2026-10-05 18:07, device time)

Pokemon Black Version 2, started from the menu with a **resume state** loaded, froze on start; the owner had to hard
reset. libdsflip's log of that run:
- `[resume] ... resuming this session`, `[cpugov] savestate: 1992 MHz`
- `[rc] Login failed:` / `[ra] token login failed (), trying the password` / `[ra] login failed:` (empty error text;
  the network was up; one second earlier Platinum logged in fine with the token)
- `[ui] overlay: ` (an empty toast line), two present stats lines, then `present/s=0.0 commits=0 ... flips 0` for the
  rest of the log: DraStic stopped presenting frames. A touch was logged, so the input thread lived.
- `[cpugov] savestate done` did come, so DraStic read the state.

Tried since: HeartGold, fresh start (no resume), RetroAchievements made unreachable with routes: **no freeze**, 60 fps,
the login failure toast showed. So "RA offline" alone is not it.

What to look at:
- The resume load (`resume.c`: `want_load` at frame 120, load button held 40 frames, `dsflip_hold`) together with the
  RA login failure toast (`ra.c on_login_password` -> `dsflip_toast` with an empty `err`, from the dsf-http thread) and
  `ui.c` (its mutex `mx`, condvar, `ui_start()`). A toast from a non-DraStic thread while a load is under way is the
  prime suspect: check every path for a lock-order problem or a toast with an empty string.
- Why the token login failed with an empty error once (rcheevos result / curl error not passed through). Log curl's
  error and the HTTP status.
- Why a userspace stall needed a hard reset: session.sh's watchdog (`kill_pending`, 5 s) should bring the menu back
  when DraStic hangs. Check that the exit hotkey and the watchdog work while DraStic is alive but not presenting
  (it is not in state D then). If nothing can detect it, add a "no present for N s after ready" check.
- Reproduce: Black 2 (or any game) with a resume state + RA login failing (wrong password in system.cfg is easier
  than blocking routes). `ra-offline-repro.sh` collects thread states and gdb backtraces when presents stop (gdb is
  on the device).

### 2. Fast-switching glitches (owner report, 1.5.13 test build)

a) A black "empty terminal" after the start animation: session.sh switches to tty12, whose cursor showed. Fix in this
   branch: tty12 cleared, cursor hidden. Do not use KD_GRAPHICS on tty12 (the kernel then ignores VT switches back;
   the device hung).
b) The last frame of the start animation stays on the bottom screen 1-2 s after quitting: the panels went back to sway
   at +0.9 s, ES was blocked until +3.7 s. Fix in this branch: restore.sh hands the panels back from a transient unit
   (`dsflip-vtback`) once ES's main thread is back in its loop (`/proc/<pid>/syscall` 115 or 73, not wait4 260); the
   second restore.sh run (ExecStopPost) exits at once. Measured on the Plus: menu shown ~1.7 s after the quit, no stale
   frame. One run logged "ES did not come back in 3 s" (fell back correctly). Port to the RG DS line (its restore.sh
   differs: no `floating_maximum_size` dance).

### 3. Menu (ES) memory runaway

At 17:38 on 2026-10-05 the kernel OOM-killed ES at 713 MB RSS (it idles at ~180 MB; the Plus has 975 MB and no swap);
the device was unreachable for minutes before. Not reproduced in ~10 game round trips or 7 idle minutes afterwards.
Leads: `ViewController::launch` for the rnds theme calls `reloadAllGames(w, false)` after **every** game
(es-rgds-rnds.patch) — check it frees the old views and the rnds engine's textures/cache (`RndsUI` cache `evict()` only
counts entries per group, not bytes). Two launch requests 0.15 s apart started the game twice: guard against a second
launch while one is pending. A watch script (`esmemlog.sh` idea: RSS every 30 s to /storage/es-mem.log) helped.

### 4. Open GitHub issues (JorreFog/ROCKNIXDS)

- Can be closed (reporter confirmed fixed): #24, #28, #33; #37 (fixed in 1.5.7); #25 went away by itself.
- #26 microphone: no longer crashes, but blowing does nothing. The in-game menu has a Microphone page with a live meter
  and a "blow" action (holds the fake-mic key 3 s). Needs to make a mic game react.
- #27 Pixel theme QOL: battery icon next to the percentage, and the rest of the reporter's list.
- #30 keyboard help text overflowing.
- #31 RetroAchievements hash mismatch.
- #32 View Game Media image size.
- #34 Save State Manager display (font part fixed: digits; check the rest).
- #35 Manual Scrape text clipping.
- #36 Tools / Music Player shown in the per-system advanced configuration.
- #42 saves in their own folder per game (suggestion).
- #43 front-end music not playing.
- #44 option to show the DS cartridge icon instead of box art (suggestion).

### 5. Online play

Parked. Do not ship the ES "wfc dns" option visible, and keep the WFC hook off by default. Findings for later
(device-verified): the hook only ever saw register **reads**; DraStic's recompiled ARM7 stores to 0x04xxxxxx go
straight to `store_io_register_arm7_8/16/32` (0x10fa0 / 0x117f0 / 0x12120), so those handlers must be patched; slots
are sent via W_TXBUF_LOCn bit 15; DraStic's firmware has zero wifi tables and MAC 00:01:02:03:04:05 on every device.
Code: SuperDrastic `1.6-prep`, last commit.

## Rules from the owner

- One issue at a time, batched into one release; no release, tag or push to `main` / `plus-beta` without the owner.
- Say plainly what is untested on a device. The RG DS (non-Plus) is untested for everything since 1.5.8.
- Everything must look right on both panels (top 1024x768 on the Plus, 640x480 class on the RG DS): check text
  clipping, alignment and the Pixel style on every screen you touch.

## What we want back from you

1. Per task: what you changed, on which branch, and the exact device checks the owner (or the local agent) must do,
   added to `docs/handoff-local.md` like before.
2. For task 1: your best explanation with code references, the fix, and a way to reproduce it that the local agent can
   run over ssh.
3. Both lines kept in step: every fix on the Plus branch also on the RG DS branch (and the reverse), or a note why not.
4. A draft of the 1.6 release notes (`docs/releases/v1.6.md`, `v1.6-plus.md`) listing what is verified and what is not.
5. Do not bump VERSION to 1.6 or build release packages; the owner decides when.
