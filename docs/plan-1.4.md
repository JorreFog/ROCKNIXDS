# ROCKNIXDS 1.4 plan

1.4 is meant to be a big update. Order agreed on 2026-09-27: check suspend first (it could reorder everything),
then screen modes and touch in ES's menus, then the RetroAchievements update, then a prototype of near-instant
switching, then CI builds. Battery tuning (a CPU sweep like 1.3's GPU one) and the in-game microphone check need
play sessions on the device. Carried over from `plan-1.3.md`: B6, B7, B13, B14, I5 (debug switch, texture
alloc fallback), I7 (video previews, unplayed look), I8 (hires warning, release assets), I9 (ES patch, CI).

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
