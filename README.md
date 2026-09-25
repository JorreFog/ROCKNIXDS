# RG DS on ROCKNIX: dual-screen theme and DraStic pacing

Customisations for the Anbernic RG DS (RK3566, two 640x480 DSI panels) on ROCKNIX.

## `dii-ess-aye/` — DSi-style EmulationStation theme across both screens

Builds on [beebono/dii-ess-aye](https://github.com/beebono/dii-ess-aye). ES runs on a 1920x480
canvas: the top screen, the bottom screen, and an unused third.

| File | What it is |
|---|---|
| `0001-*.patch`, `0002-*.patch` | Theme changes against upstream @9fd5eee: dark vector reskin, cartridge item template, patched-ES launcher |
| `theme-files/theme-rgds.xml` | The current layout: vector logo + glow, big clock/date home screen, drifting grid, pulsing START, a scroll bar bound to list position, and a preview placeholder |
| `theme-files/start_es_rgds.sh` | Launcher, bind-mounted over `/usr/bin/start_es.sh`. It falls back to stock ES after 2 quick crashes and re-floats the ES window whenever sway makes it fullscreen |
| `gen_skin.py` | Generates every SVG in `assets/images/common`: `python3 gen_skin.py <themedir>` |
| `trace_logo.py`, `rocknix_logo.paths` | The ROCKNIX wordmark traced from the stock PNG into vector paths |
| `es-rgds-uiwidth.patch` | ES: popups, keyboard, sliders and game options sized for one 640px screen (`ES_UI_WIDTH`) |
| `es-rgds-bindings-clock.patch` | ES: `{system:index}/{count}`, `{game:index}/{count}` bindings and a strftime `<format>` on `clock` |
| `emulationstation-rgds` | The built, stripped ES binary with both ES patches applied (ROCKNIX/emulationstation-next bccd715, aarch64) |
| `device/autostart-dii-ess-aye` | `/storage/.config/autostart` hook: puts the launcher bind mount back and restores the theme's sway config, which ROCKNIX's `111-sway-init` overwrites on every boot |
| `device/sway-config.theme` | That sway config |
| `device/apply-60hz-dtb.sh` | Retunes both panels to 60.0013 Hz (DTB porch edit, checked by md5) |

## `drastic-vsync/` — vblank-locked frame pacing for DraStic

`dvsync.c` is an `LD_PRELOAD` shim. It paces DraStic's post-present sleep to the compositor's latch
point, warps `gettimeofday` so DraStic sees exactly 60 fps, and resamples audio to match. It learns
its timing from `wp_presentation` feedback. `drastic-wrapper.sh` installs as `/storage/.config/drastic/drastic`.
Touch `/storage/.config/drastic/nodvsync` (or set `DVSYNC=0`) for stock pacing.

Build:

    clang -target aarch64-linux-gnu -fuse-ld=lld -shared -fPIC -nostdlib -O2 -o libdvsync.so dvsync.c

`tools/` holds the measurement harness: a uinput keyboard (`vkbd.py`), a launch/savestate/walk
benchmark (`bench.sh`, `go.sh`), and analysis (`fps.py`, `rep.py`). The scripts on the desktop reach the
device through `tools/rg`, so set `RGDS_HOST`.

Measured on 60 Hz panels, HeartGold walking test, ~45 s:

| | Stock DraStic | Shim |
|---|---|---|
| Low res | 3 repeated frames, 21 ms latency | 4 repeated frames, 11 ms latency |
| High res | 6 *or* 194 repeated frames (the phase is set by chance at launch) | ~40 (in heavy map transitions), never below 55 fps |
