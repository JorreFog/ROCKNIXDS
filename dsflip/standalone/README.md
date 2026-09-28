# libdsflip for any RG DS firmware

libdsflip makes DraStic draw straight into the Anbernic RG DS's two panels: no compositor, no GL, no copy. DraStic's
hires 3D (2x) holds 60 fps longer under load (a 3D stress ROM at its heaviest level: 50 fps instead of 39
through SDL), touch works on the bottom panel, and it adds GPU
shaders, a pacing audio pump, the microphone and RetroAchievements (softcore). It is one `LD_PRELOAD` library;
DraStic itself is unchanged. It comes from [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), where it is the
default DraStic launcher. This package is for other firmwares. On ROCKNIX, use ROCKNIXDS's `install.sh`.

MIT licensed ([LICENSE](LICENSE), third-party parts in [THIRD_PARTY.md](THIRD_PARTY.md)): ship it in your firmware,
change it, fork it.

## Requirements

| | |
|---|---|
| Device | Anbernic RG DS (RK3568, two 640x480 DSI panels). The top panel is connector `DSI-2`, the bottom touchscreen the Goodix on `fe5e0000.i2c` |
| DraStic | r2.5.2.2, aarch64 (the build ROCKNIX ships as `drastic-sa`) |
| System | glibc 2.38 or newer, libdrm, KMS with atomic modesetting; `libasound.so.2` (ALSA device `default`) for the audio pump, else DraStic's SDL audio |
| Optional | libcurl (RetroAchievements), libEGL + libGLESv2 with dma-buf import (shaders). Tested with ARM's Mali driver (`/usr/lib/mali`); Mesa's surfaceless EGL is supported in the code but untested |
| Access | DRM master on the display device while the game runs (root, or the seat's session) |

## Use

```
tar xzf libdsflip-<version>-aarch64.tar.gz
libdsflip-<version>-aarch64/dsflip-run /path/to/drastic /path/to/game.nds
```

Put that in place of your DraStic launch command. `dsflip-run` gets the display from your frontend, runs DraStic with
the library, waits for it and gives the display back. It exits with DraStic's status. Settings go in `dsflip.conf`
beside it (start from `dsflip.conf.example`) or in the environment.

### Getting the display: `DSFLIP_HANDOVER`

Only one program can drive the panels. Pick how your frontend lets go:

| Value | What happens | Use when |
|---|---|---|
| `auto` (default) | `cmd` if `DSFLIP_STOP_CMD` is set, else `vt` if a compositor (sway, weston, labwc, cage, gamescope, kwin, X) runs, else `none` | |
| `none` | Nothing | Your frontend is a KMS program that exits or drops the display while games run |
| `vt` | Switches the console to VT `DSFLIP_VT` (12) and back afterwards. A compositor started through seatd or logind releases the display on the switch and takes it back after | Your frontend runs under a Wayland compositor. The fastest way back to the menu: nothing restarts |
| `cmd` | Runs `DSFLIP_STOP_CMD` before the game and `DSFLIP_START_CMD` after it | Anything else: stop and start your compositor or frontend service |

`vt` traps found on ROCKNIX: the new VT must stay in text mode and unblanked, or the kernel powers the panels down
under the game (`dsflip-run` handles both). A frontend that tears down its GL context for the game may fail to
re-create it after the switch; keep its window during games.

If libdsflip can't take the display, `dsflip-run` hands the display back and starts DraStic the ordinary way
(`DSFLIP_FALLBACK=0` to give up instead).

### Exit hotkeys

DraStic runs as its own binary name (`drastic`), so firmware hotkeys that `killall drastic` keep working;
`dsflip-run` then restores the display and exits with status 137.

## The contract

What your integration can rely on:

- **Verdict file** `DSFLIP_STATE` (default `/tmp/dsflip-state`): libdsflip writes `ready` once it has both panels, or
  `passthrough: <reason>` when it gives up (DraStic then keeps running, but draws nothing). It decides within ~6 s:
  up to 3 s waiting for the display, up to 3 s for a shader. Delete the file before starting DraStic.
- **Log** `DSFLIP_LOG` (default `logs/dsflip.log` here): one per session, the previous three kept as `.1`..`.3`.
  Frame pacing, drops, shader, audio and RetroAchievements lines. `logs/run.log` is `dsflip-run`'s own.
- **Environment:**

| Variable | Default | |
|---|---|---|
| `SDL_VIDEODRIVER` | `dummy` (set by `dsflip-run`) | Required: SDL must not open a window or the display |
| `DSFLIP_SHADER` | none | A shader from `shaders/` by name, e.g. `ds-crisp`; none = zero-copy |
| `DSFLIP_SHADER_DIR` | | Another folder of `.frag` shaders (checked first) |
| `DSFLIP_CARD` | first `/dev/dri/card*` with two DSI panels | The display device |
| `DSFLIP_TOP` | `DSI-2` | The connector that shows the DS top screen |
| `DSFLIP_TOUCH` | `fe5e0000.i2c` | The bottom touchscreen's device path fragment |
| `DSFLIP_TOUCH_INVERT` | none | `x`, `y` or `xy` |
| `DSFLIP_DATA` | `data/` here | RetroAchievements login token and badge cache |
| `DSFLIP_RA_USER`, `DSFLIP_RA_PASSWORD` | | RetroAchievements account; the password is used once, then the token |
| `DSFLIP_RA_SOUND` | none | An `.ogg` played on unlocks |
| `DSFLIP_FONT` | DejaVu Sans / Liberation Sans | Font for the pop-ups (TTF/OTF) |
| `DSHOOK_MIC_THRESH` | 0 (off) | Microphone sensitivity: 0.03 high, 0.15 medium, 0.3 low |
| `DSFLIP_AUDIO_PUMP` | 1 | 0 = DraStic's own SDL audio (the pump fixes stutter from SDL's uneven drain) |

- **Shaders** are GLSL ES 1.0 fragment shaders with the inputs DraStic's stock shaders use (`u_texture`,
  `u_texture_size`, `u_output_size`, `v_texcoord`; see the top of `dsflip/shader.c` in the source).
  A shader that draws the DS screen into part of the panel declares it (`// dsflip-viewport: x y w h`) and touch
  follows.
- **GPU clock:** with a shader the GPU draws every frame; ROCKNIXDS keeps it at 400 MHz or more
  (`simple_ondemand`, `min_freq`), and at 800 MHz for `ds-fsr`. Without a shader it idles. Clocking is left to
  your firmware.

## Updates

`dsflip-update` replaces this folder's files with the newest stable release from GitHub (keeps `dsflip.conf`,
`data/` and `logs/`); `dsflip-update --check` only reports. Or poll
`https://api.github.com/repos/JorreFog/ROCKNIXDS/releases/latest` yourself for the asset named
`libdsflip-<version>-aarch64.tar.gz`.

## Problems

Look at `logs/dsflip.log` first, then open an issue at https://github.com/JorreFog/ROCKNIXDS/issues with it attached
and your firmware's name and version.
