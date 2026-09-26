# DraStic at 2× Internal Resolution on ROCKNIX (Anbernic RG DS)

**Implementation plan: bringing GammaOS Nano's DraStic-nano techniques to ROCKNIX's `drastic-sa`**

> Status: design and execution plan, September 2026. Nothing here has been run on-device yet. Every phase starts with a measurement and ends with a go/no-go gate, so effort only goes where the numbers say it helps.

Target device: Anbernic RG DS. The SoC is 4× Cortex-A55 with a Mali-G52 2EE (Anbernic lists it as RK3568; ROCKNIX runs it as RK3566). It has two 640×480 DSI panels, exposed by ROCKNIX as `card0-DSI-1` and `card0-DSI-2`.

---

## 0. TL;DR

- **GammaOS Nano has no special upscaler.** It runs DraStic's built-in **Hi-res 3D** mode, which rasterizes 3D at 512×384 instead of 256×192.
- **It fits in the frame budget by removing overhead around the emulator:**
  - no GL or compositor in the frame path;
  - zero-copy scanout straight to both panels via DRM/KMS;
  - a presentation loop decoupled from the emulation thread;
  - a stripped-down OS.
- **DraStic's 3D is software-rendered on the CPU.** On four A55 cores, every millisecond spent uploading, compositing and flipping frames is a millisecond the 3D raster threads don't get.
- **The display path is the part we can port.** The plan is an `LD_PRELOAD` library, `libdsflip.so`, that:
  - intercepts DraStic's SDL2 video calls;
  - scales the two DS screens on the CPU with NEON (or in the display controller);
  - page-flips them directly onto the two DSI panels, so GL never runs in the DraStic process.
- **Reference numbers from the same hardware** (DSperate author, RG DS):
  - SDL2's KMSDRM "software" renderer is a hidden GLES path costing **13.4 ms per frame**;
  - flipping your own buffer costs **0.15 ms**;
  - emulation + present went from **17.9 ms to 6.4 ms** per frame.
- **Phase 2 is the decisive measurement.** It measures ROCKNIX's current display overhead before any heavy work. If that overhead turns out small, the display path isn't the bottleneck and the plan changes (§14).

---

## 1. Goal and success criteria

"2×" here means DraStic's `hires_3d = 1` (double internal 3D resolution) running at full speed. It does **not** mean doubling emulation speed.

| Metric | Target |
|---|---|
| Emulated FPS (DraStic's counter) | ≥ 59.5 average in every title of the benchmark set (§13) with `hires_3d = 1` |
| Present interval p99 | ≤ 20 ms (no visible hitches) |
| Dual-panel consistency | The same emulated frame is shown on top and bottom (no inter-screen tearing) |
| Input latency | No worse than current ROCKNIX |
| Failure mode | Any failure falls back to the stock SDL path. Never a black screen. |

Hi-res only affects 3D. 2D layers are simply doubled when composited with the 3D image, so 2D-only games look the same. They just have to hold 60 fps with the larger buffers.

---

## 2. What GammaOS Nano actually does

Sources: the GammaOS Next changelog (v1.3–v1.4.1), the GammaOS Nano wiki and the drastic-nano source docs (see References).

| Technique | GammaOS implementation | Portable to ROCKNIX? |
|---|---|---|
| Emulator core | Loads Android DraStic's `libdrastic` (from the r2.6.0.4a app) in-process into a native runner. There is no Java app on the DRM path. | Only via a bionic loader (Appendix A) |
| Dual-screen defaults | Hi-res 3D on, Threaded 3D on, edge marking disabled, Frame Sync on | **Yes.** The same keys exist in the Linux `drastic.cfg` (§6). |
| Presentation | NEON blit into buffers that are scanned out zero-copy via DRM PRIME. Flip work dropped from 3 ms to 150 µs. Triple buffering, per-CRTC page-flip pacing. | **Yes.** This is the core of this plan (§8–10). |
| Decoupled render loop | Rendering runs independently of DraStic's free-running emulation thread, driven by vblank | **Yes** (§8.5) |
| Frameskip | Done by the frontend (skip upload and shading, but still flip every vblank), because that libdrastic build ignores its own frameskip fields | Optional. The Linux build honours its own frameskip. |
| In-memory patches | Skip the blocking OpenSL audio init, fix a BG-layer bug, re-assert a patch covering 13 "GPU fast-path" flags in DraStic's master state | Specific to the Android core |
| OS diet | Minimal Android boot that skips ~120 system services, plus RAM reclaim while a game runs | Partially (§11). ROCKNIX is already lean. |
| Opt-in render options | Half resolution, 16-bit (RGB565) composition, vsync lock | 16-bit scanout: **yes** (§8.3, §9) |

Independent confirmation of the principle comes from DSperate, a GPL-3 DS emulator built and measured on the RG DS. Its README reports two things:

- the SDL2/GLES present path is a dominant cost on this SoC;
- GL driver threads compete with the emulation and raster threads for the four cores.

---

## 3. The current ROCKNIX pipeline

### Known

- **Official support.** The RG DS is supported in the official `RK3566 Specific` image.
- **Display outputs.** ROCKNIX exposes two connected DRM connectors, `card0-DSI-1` and `card0-DSI-2`. `fb0` is 640×480.
- **Session and emulator.** ROCKNIX on RK3566 runs a Wayland session (sway). `drastic-sa` is the standalone Linux DraStic binary, driven through SDL2.
- **Existing SDL2 hook.** Since June 2026, `drastic-sa` has a pixel-shader path implemented as an SDL2 hook (sharp-shimmerless, quilez, lcd1x-nds-color, LCD3X). That hook is the natural integration point, or at least the pattern to copy.
- **Config keys.** The Linux DraStic config has `hires_3d`, `threaded_3d` and `disable_edge_marking`.
- **Hi-res output size.** With hi-res on, Linux DraStic outputs 512×384 per screen instead of 256×192 (per steward-fu's port notes).

### Unknown (Phase 0 answers these)

1. How DraStic's window maps onto the two outputs: one window spanning both, one per output, or a combined output?
2. Which SDL renderer is used (`opengles2`, `software`, …), whether vsync is on, and whether DraStic calls `SDL_RenderPresent` or `SDL_GL_SwapWindow`.
3. The texture sizes and pixel formats DraStic uploads, and whether it uses `SDL_UpdateTexture` or `Lock/UnlockTexture`.
4. Whether sway scans out DraStic's buffers directly or composites them. It can't scan out directly if an output is rotated or transformed.
5. How ROCKNIX's existing SDL2 hook is loaded: `LD_PRELOAD`, or a replacement `libSDL2`.
6. How touch reaches DraStic: SDL mouse events, `SDL_GetMouseState` polling, or finger events.

---

## 4. Architecture

### Current (probable)

```
DraStic emu thread ──► 3D raster threads ──► 2 screen buffers (512×384, CPU memory)
        │
        └─► SDL_UpdateTexture ─► GLES texture upload ─► SDL_RenderCopy (GL draw + shader pass)
               ─► eglSwapBuffers ─► sway composites (GL, unless direct scanout) ─► KMS flip ─► DSI-1 / DSI-2
```

Every arrow after the screen buffers costs CPU time on the same four A55 cores (GL driver threads, compositor), plus GPU work and memory bandwidth.

### Target

```
DraStic emu thread ──► 3D raster threads ──► 2 screen buffers
        │
        └─► SDL_UpdateTexture ──[libdsflip intercept]──► memcpy to shadow buffer (~0.1 ms)
                                                                  │
                            presenter thread (vblank-driven) ◄────┘
                               ├─ NEON 5:4 area scale 512×384 → 640×480 (or HW plane scaling)
                               ├─ write RGB565 scanout buffers (triple-buffered, per panel)
                               └─ one atomic commit: plane FBs on CRTC(DSI-1) + CRTC(DSI-2)
```

GL never runs in the DraStic process: with the KMS backend, SDL runs with the `dummy` video driver. Touch is read from evdev by the hook and injected into SDL.

### Modules of `libdsflip.so`

| Module | Responsibility |
|---|---|
| `intercept.c` | `dlsym(RTLD_NEXT)` wrappers for the SDL2 video, render and mouse calls DraStic uses |
| `compose.c` | A tiny CPU "renderer" that turns DraStic's `RenderCopy` calls into blits onto two panel canvases |
| `scale_neon.c` | Fixed-ratio scalers (5:4 and 5:2): area/sharp, nearest, LCD grid |
| `backend_kms.c` | DRM/KMS: connectors, CRTCs, planes, dumb buffers, atomic commits, flip events |
| `backend_wl.c` | Wayland: `zwp_linux_dmabuf_v1` buffers on fullscreen surfaces per output (keeps sway running) |
| `input.c` | Maps evdev touch to DraStic window coordinates and delivers it as SDL events plus an `SDL_GetMouseState` override |
| `stats.c` | Frame timing (p50/p99), per-stage cost, logging to `/storage/dsflip/logs` |

Backends sit behind one small interface (`present(frame)`, `wait_vblank()`), like drastic-nano's pluggable display backend.

---

## 5. Phase 0 — Recon and baseline (½ day)

Everything runs over SSH (default login `root` / `rocknix`). Keep all work in `/storage/dsflip/`, because `/usr` is a read-only squashfs.

```sh
mkdir -p /storage/dsflip/logs

# 1. How is DraStic launched?
ls /usr/bin | grep -i -E 'drastic|nds'
grep -il drastic /usr/bin/*.sh 2>/dev/null        # find the start script
# Read it fully: env vars, LD_PRELOAD, working dir, config path, shader option handling.

# 2. Start a 3D game from EmulationStation, then:
P=$(pidof drastic)
tr '\0' '\n' < /proc/$P/environ | grep -E '^(SDL_|WAYLAND|XDG_|LD_)'
awk '{print $6}' /proc/$P/maps | grep '\.so' | sort -u      # which libSDL2, hook libs, Mesa
for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/comm)"; done   # thread names
top -H -b -d 1 -n 5 -p $P > /storage/dsflip/logs/threads-baseline.txt

# 3. Display topology
swaymsg -t get_outputs            # names, modes, *transform*, positions of both DSI outputs
swaymsg -t get_tree | grep -B2 -A12 -i drastic   # window geometry: which output(s) it covers
cat /sys/kernel/debug/dri/0/state 2>/dev/null | head -120   # planes/CRTCs in use (if debugfs is mounted)

# 4. Config
find /storage -name 'drastic.cfg' 2>/dev/null   # likely under /storage/.config/drastic/
```

Record the answers in `/storage/dsflip/logs/recon.md`:

- start script path and the exact command line;
- `SDL_VIDEODRIVER`, `SDL_RENDER_DRIVER` and the preload chain;
- output transforms (anything other than `normal` means sway must composite, which is an extra GL pass);
- DraStic's window geometry relative to the two outputs;
- thread list with CPU% per thread.

**Baseline benchmark:** run the §13 set with current settings (note the `hires_3d` value) and log DraStic's FPS counter.

**Gate 0:** none. Always continue to Phase 1.

---

## 6. Phase 1 — Settings parity and system tuning (1 evening)

Match GammaOS's dual-screen defaults in `drastic.cfg`, using the key names from the Linux build:

```ini
hires_3d = 1              ; 2× internal 3D (512×384)
threaded_3d = 1           ; multi-threaded rasterizer (turn off per game if it glitches)
disable_edge_marking = 1  ; skips an extra 3D pass; minor visual difference
frameskip_type = 0        ; verify in DraStic's menu that 0 = none on this build
show_frame_counter = 1    ; for measurement only
```

System side:

```sh
# CPU governor (RK356x has one CPU cluster, policy0)
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors
echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor

# Devfreq devices (GPU, and DMC if present): note the current governors
for d in /sys/class/devfreq/*; do echo "$d $(cat $d/governor)"; done

# Thermals while playing (throttling wipes out any gain on a fanless A55)
watch -n1 'cat /sys/class/thermal/thermal_zone*/temp; cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq'
```

A/B test ROCKNIX's shader hook: run the benchmark with shader set to none, then with the shader you like. A GLES shader pass costs GPU time and GL driver CPU time on the same cores.

**Gate 1:** if every title in §13 already holds 60 fps with these settings, stop here and ship a per-game config. Otherwise continue.

---

## 7. Phase 2 — Instrument the display path (1 day)

Build a logging-only preload, `libdsprobe.so`, that times every SDL video call DraStic makes. It answers the unknowns in §3 and, most importantly, **how many milliseconds per frame the current display path costs**.

### `probe.c`

```c
// libdsprobe.so: logs and times DraStic's SDL2 video calls. Changes no behaviour.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <SDL2/SDL.h>

#define WINDOW    300   /* frames per summary line */
#define LOG_FIRST 12    /* log the first N calls of each kind in detail */

static FILE *lg;
static uint64_t last_present_ns;
static double iv[WINDOW];                        /* present-to-present intervals, ms */
static double c_upd, c_lock, c_copy, c_pres;     /* accumulated cost, ms */
static int frames, n_upd, n_lock, n_copy, n_swap, n_mouse;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static double ms_since(uint64_t t0) { return (double)(now_ns() - t0) / 1e6; }
static int tid(void) { return (int)syscall(SYS_gettid); }

static void fmt_rect(char *b, size_t n, const SDL_Rect *r) {
    if (r) snprintf(b, n, "%d,%d %dx%d", r->x, r->y, r->w, r->h);
    else   snprintf(b, n, "full");
}

__attribute__((constructor)) static void probe_init(void) {
    const char *p = getenv("DSPROBE_LOG");
    lg = fopen(p ? p : "/storage/dsflip/logs/probe.log", "w");
    if (!lg) lg = stderr;
    setvbuf(lg, NULL, _IOLBF, 0);
    fprintf(lg, "[probe] loaded pid=%d\n", getpid());
}

#define REAL(fn) \
    static __typeof__(fn) *real_##fn; \
    if (!real_##fn) real_##fn = (__typeof__(fn) *)dlsym(RTLD_NEXT, #fn)

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    REAL(SDL_CreateWindow);
    SDL_Window *win = real_SDL_CreateWindow(title, x, y, w, h, flags);
    const char *drv = SDL_GetCurrentVideoDriver();
    fprintf(lg, "[win] CreateWindow '%s' %dx%d flags=0x%x driver=%s tid=%d\n",
            title ? title : "", w, h, (unsigned)flags, drv ? drv : "?", tid());
    return win;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, int index, Uint32 flags) {
    REAL(SDL_CreateRenderer);
    SDL_Renderer *r = real_SDL_CreateRenderer(win, index, flags);
    SDL_RendererInfo info;
    if (r && SDL_GetRendererInfo(r, &info) == 0)
        fprintf(lg, "[ren] CreateRenderer name=%s flags=0x%x requested=0x%x vsync=%d\n",
                info.name, (unsigned)info.flags, (unsigned)flags,
                !!(info.flags & SDL_RENDERER_PRESENTVSYNC));
    return r;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h) {
    REAL(SDL_CreateTexture);
    SDL_Texture *t = real_SDL_CreateTexture(r, fmt, access, w, h);
    fprintf(lg, "[tex] CreateTexture %p fmt=%s access=%d %dx%d\n",
            (void *)t, SDL_GetPixelFormatName(fmt), access, w, h);
    return t;
}

int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *px, int pitch) {
    REAL(SDL_UpdateTexture);
    uint64_t t0 = now_ns();
    int ret = real_SDL_UpdateTexture(t, rect, px, pitch);
    c_upd += ms_since(t0);
    if (n_upd++ < LOG_FIRST) {
        char rb[48]; fmt_rect(rb, sizeof rb, rect);
        fprintf(lg, "[upd] tex=%p rect=%s pitch=%d src=%p tid=%d\n",
                (void *)t, rb, pitch, px, tid());
    }
    return ret;
}

int SDL_LockTexture(SDL_Texture *t, const SDL_Rect *rect, void **px, int *pitch) {
    REAL(SDL_LockTexture);
    uint64_t t0 = now_ns();
    int ret = real_SDL_LockTexture(t, rect, px, pitch);
    c_lock += ms_since(t0);
    if (n_lock++ < LOG_FIRST) {
        char rb[48]; fmt_rect(rb, sizeof rb, rect);
        fprintf(lg, "[lock] tex=%p rect=%s pitch=%d tid=%d\n",
                (void *)t, rb, pitch ? *pitch : -1, tid());
    }
    return ret;
}

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst) {
    REAL(SDL_RenderCopy);
    uint64_t t0 = now_ns();
    int ret = real_SDL_RenderCopy(r, t, src, dst);
    c_copy += ms_since(t0);
    if (n_copy++ < LOG_FIRST) {
        char sb[48], db[48]; fmt_rect(sb, sizeof sb, src); fmt_rect(db, sizeof db, dst);
        fprintf(lg, "[copy] tex=%p src=%s dst=%s\n", (void *)t, sb, db);
    }
    return ret;
}

int SDL_RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst,
                     const double angle, const SDL_Point *center, const SDL_RendererFlip flip) {
    REAL(SDL_RenderCopyEx);
    uint64_t t0 = now_ns();
    int ret = real_SDL_RenderCopyEx(r, t, src, dst, angle, center, flip);
    c_copy += ms_since(t0);
    if (n_copy++ < LOG_FIRST) {
        char sb[48], db[48]; fmt_rect(sb, sizeof sb, src); fmt_rect(db, sizeof db, dst);
        fprintf(lg, "[copyex] tex=%p src=%s dst=%s angle=%.0f flip=%d\n",
                (void *)t, sb, db, angle, (int)flip);
    }
    return ret;
}

void SDL_GL_SwapWindow(SDL_Window *w) {
    REAL(SDL_GL_SwapWindow);
    if (n_swap++ < LOG_FIRST)
        fprintf(lg, "[gl] SDL_GL_SwapWindow called (raw GL path?) tid=%d\n", tid());
    real_SDL_GL_SwapWindow(w);
}

Uint32 SDL_GetMouseState(int *x, int *y) {
    REAL(SDL_GetMouseState);
    n_mouse++;
    return real_SDL_GetMouseState(x, y);
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

void SDL_RenderPresent(SDL_Renderer *r) {
    REAL(SDL_RenderPresent);
    uint64_t t0 = now_ns();
    real_SDL_RenderPresent(r);
    uint64_t t1 = now_ns();
    c_pres += (double)(t1 - t0) / 1e6;
    if (last_present_ns) iv[frames % WINDOW] = (double)(t1 - last_present_ns) / 1e6;
    last_present_ns = t1;
    if (++frames % WINDOW == 0) {
        double s[WINDOW];
        memcpy(s, iv, sizeof s);
        qsort(s, WINDOW, sizeof s[0], cmp_d);
        fprintf(lg, "[stats] frames=%d interval p50=%.2f p99=%.2f max=%.2f ms | per frame: "
                    "upd=%.2f lock=%.2f copy=%.2f present=%.2f ms | mousepoll=%d tid=%d\n",
                frames, s[WINDOW / 2], s[(WINDOW * 99) / 100], s[WINDOW - 1],
                c_upd / WINDOW, c_lock / WINDOW, c_copy / WINDOW, c_pres / WINDOW,
                n_mouse, tid());
        c_upd = c_lock = c_copy = c_pres = 0;
        n_mouse = 0;
    }
}
```

### Build and run

Build on a PC; ROCKNIX has no compiler on the device:

```sh
aarch64-linux-gnu-gcc -O2 -fPIC -shared -I/path/to/SDL2/include \
    -o libdsprobe.so probe.c -ldl
scp libdsprobe.so root@<rgds-ip>:/storage/dsflip/
```

Any aarch64 glibc toolchain older than ROCKNIX's glibc works. Once you package it (§12), the ROCKNIX build tree is the cleanest option.

To run it:

1. Copy DraStic's start script to `/storage/dsflip/start_probe.sh`.
2. Prepend `LD_PRELOAD=/storage/dsflip/libdsprobe.so${LD_PRELOAD:+:$LD_PRELOAD}`. Put the probe **first**, so its timings include ROCKNIX's shader hook, which sits further down the chain.
3. Launch the same ROM from SSH, or temporarily point EmulationStation at the copy.

### Reading `probe.log`

| Observation | Meaning |
|---|---|
| `upd + copy + present` average ≥ 4 ms | The display path is a real bottleneck, so Phase 3 is worth it |
| `present` average ≈ 16 ms and interval p50 ≈ 16.7 ms | DraStic paces itself on a vsync-blocking present. Use FIFO pacing (§8.5). |
| `present` average ≪ 16 ms | DraStic paces on audio or a timer. Mailbox pacing is safe. |
| `[gl] SDL_GL_SwapWindow` lines appear | DraStic draws with raw GL. Intercept `glTexSubImage2D` instead (harder; §14). |
| Textures are 512×384 or 256×192, `RGB565` | Expected. The scalers in §9 assume RGB565 input. |
| One texture holds both screens | Crop by layout in `compose.c` |
| `mousepoll` > 0 | DraStic polls mouse state, so `input.c` must override `SDL_GetMouseState` |

Also capture per-thread CPU with and without the probe loaded. If you can get a `perf` binary onto the device, take one `perf record -g` as well.

**Gate 2:**

- **Display path ≥ ~3 ms per frame, or GL/compositor threads visibly busy:** go to Phase 3.
- **Display path < ~2 ms per frame and raster threads pegged:** the bottleneck is DraStic's CPU rendering itself. Skip to §11 (scheduling) and consider Appendix A or B.

---

## 8. Phase 3 — `libdsflip.so`: the presenter (1–2 weeks)

### 8.1 Interception strategy

Wrap these calls via `dlsym(RTLD_NEXT)`, so the hook chains with ROCKNIX's existing SDL2 hook:

- **Object tracking:** `SDL_CreateWindow`, `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyTexture`. The window size is DraStic's logical canvas; textures are layers.
- **Pixel capture:** `SDL_UpdateTexture`, or `SDL_LockTexture` + `SDL_UnlockTexture`. Copy the pixels into our own per-texture shadow buffer with a plain `memcpy` (about 0.1 ms for 512×384 RGB565).
- **Drawing:** `SDL_RenderClear`, `SDL_RenderCopy`, `SDL_RenderCopyEx`, `SDL_RenderFillRect`, `SDL_SetRenderDrawColor`, `SDL_SetTextureBlendMode`, `SDL_SetTextureAlphaMod`. Record them into a per-frame draw list and **do not** forward them to the real renderer.
- **Present:** `SDL_RenderPresent` hands the draw list to the presenter, then returns according to the pacing mode.
- **Readback:** `SDL_RenderReadPixels` is served from our composed canvas, in case DraStic takes screenshots through SDL.
- **Mouse:** `SDL_GetMouseState`, plus `SDL_PollEvent`/`SDL_PeepEvents` only if injection needs them.

Only implement what Phase 2 showed DraStic actually calls. On any unrecognized call or unsupported texture format, switch to **passthrough mode**: forward everything to real SDL, release the display, and log why.

An alternative with the same effect is a custom SDL2 video driver, which is what steward-fu did for the Miyoo Mini. It's more invasive to package, so `LD_PRELOAD` is preferred.

### 8.2 The composer

DraStic draws in its own window coordinate space, for example 640×960 if its layout stacks the screens. The composer does three things:

1. **Maps window space to panels** using the topology found in Phase 0, e.g. rows 0–479 go to DSI-1 and rows 480–959 to DSI-2. The mapping is configurable in `dsflip.ini`.
2. **Handles each `RenderCopy(tex, srcrect, dstrect)`:**
   - If `srcrect` is a whole DS screen (256×192 or 512×384) and `dstrect` exactly covers a panel, it takes the **fast path**: a fixed-ratio NEON scaler writes straight into that panel's back buffer.
   - Otherwise it takes a generic nearest/bilinear blit with alpha, used for menus and overlays.
3. **Stays cheap:** during gameplay that is just two fast-path blits per frame.

Optional improvement: ignore DraStic's layout and always put screen 0 on DSI-1 and screen 1 on DSI-2 at full size. On a dual-panel device, DraStic's layout options stop mattering.

### 8.3 Backend B: KMS direct (recommended first, because it shows the upper bound)

This backend needs DRM master, so sway must not be running during the DS session (§8.7). SDL runs with `SDL_VIDEODRIVER=dummy`, so SDL never touches DRM and the hook owns it outright.

The alternative is to keep `kmsdrm` and borrow SDL's DRM fd via `SDL_GetWindowWMInfo`, as DSperate does. In that case SDL has already set a mode on one CRTC, and you must stop SDL from flipping.

Skeleton, linked with `-ldrm`:

```c
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>

typedef struct { uint32_t fb_id, handle, pitch; size_t size; uint16_t *map; } kbuf;

typedef struct {
    uint32_t conn_id, crtc_id, plane_id, mode_blob;
    drmModeModeInfo mode;
    kbuf buf[3];
    int front, pending, back;            /* triple-buffer indices */
    uint32_t p_fb;                       /* plane FB_ID property id (others looked up at init) */
    uint64_t last_flip_us;               /* for the frame-sync log */
} kpanel;

static int drm_fd;
static kpanel panel[2];                  /* [0] = DSI-1 (top), [1] = DSI-2 (bottom) */

static uint32_t prop_id(uint32_t obj, uint32_t type, const char *name) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd, obj, type);
    uint32_t id = 0;
    for (uint32_t i = 0; props && i < props->count_props && !id; i++) {
        drmModePropertyRes *p = drmModeGetProperty(drm_fd, props->props[i]);
        if (p && !strcmp(p->name, name)) id = p->prop_id;
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return id;
}

static int make_dumb(kbuf *b, uint32_t w, uint32_t h, uint32_t fourcc, uint32_t bpp) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = bpp };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;
    uint32_t handles[4] = { c.handle }, pitches[4] = { c.pitch }, offsets[4] = { 0 };
    if (drmModeAddFB2(drm_fd, w, h, fourcc, handles, pitches, offsets, &b->fb_id, 0)) return -1;
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    b->map = mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, m.offset);
    return b->map == MAP_FAILED ? -1 : 0;
}

int kms_init(void) {
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0 || drmSetMaster(drm_fd)) return -1;     /* fails while sway holds master */
    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    /* 1. drmModeGetResources → keep the two connected DSI connectors
     *    (DSI-1 = top, DSI-2 = bottom; make the order configurable).
     * 2. Per connector: preferred mode; a free CRTC from the encoder's possible_crtcs;
     *    a primary plane whose possible_crtcs includes that CRTC and whose
     *    IN_FORMATS lists DRM_FORMAT_RGB565.
     * 3. drmModeCreatePropertyBlob(mode) → mode_blob; look up property ids with prop_id().
     * 4. make_dumb() ×3 per panel at the mode size, DRM_FORMAT_RGB565, bpp 16.
     * 5. One atomic commit with DRM_MODE_ATOMIC_ALLOW_MODESET setting, per panel:
     *    CONNECTOR.CRTC_ID, CRTC.MODE_ID, CRTC.ACTIVE=1 and the plane's
     *    FB_ID / CRTC_ID / SRC_X,Y,W,H (16.16 fixed point) / CRTC_X,Y,W,H → buf[0]. */
    return 0;
}

/* Queue one composed frame on BOTH panels in a single commit (frame sync, §10). */
int kms_flip_both(void) {
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    for (int i = 0; i < 2; i++)
        drmModeAtomicAddProperty(req, panel[i].plane_id, panel[i].p_fb,
                                 panel[i].buf[panel[i].back].fb_id);
    int ret = drmModeAtomicCommit(drm_fd, req,
                                  DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req);
    return ret;   /* -EBUSY: a previous flip is still pending, so skip this vblank */
}

static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec,
                    unsigned crtc_id, void *user) {
    /* Find the panel with this crtc_id, rotate front/pending/back,
     * store sec*1e6+usec in last_flip_us for the sync log. */
}

void kms_wait_flips(void) {
    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    /* poll(drm_fd) + drmHandleEvent(drm_fd, &ev) until both CRTCs have reported. */
}
```

Notes:

- **Buffer access:** dumb buffers are mapped write-combined. Write them sequentially and never read them back. The scaler reads from cached shadow buffers and writes each output row once.
- **Pixel format:** RGB565 scanout halves memory bandwidth compared with XRGB8888; GammaOS offers the same thing as "16-Bit Layout". Check that the chosen plane lists `RG16` in `IN_FORMATS`.
- **Panel orientation:** if the DSI panels are natively portrait, use the plane `rotation` property only if a `TEST_ONLY` commit accepts it. Otherwise rotate inside the scaler, which writes rows anyway.

### 8.4 Backend C: hardware plane scaling (try it; it might be free)

The RK356x display controller (VOP2) can scale on some window types. Test whether an atomic `TEST_ONLY` commit accepts `SRC 512×384 → CRTC 640×480` on the plane you use.

- **If it does,** the presenter just copies DraStic's buffer into the dumb buffer (about 0.1 ms) and the display controller does the scaling.
- **The filter is fixed** (roughly bilinear, soft), so keep the NEON path for sharp and LCD filters.

Treat this as unverified until `TEST_ONLY` says yes on the device.

### 8.5 Pacing (decoupled presenter)

The presenter thread runs on vblank (flip-complete events), independently of the emulation thread.

- **Triple buffering per panel:** `front` (scanning out), `pending` (committed, waiting for flip) and `back` (being written).
- **`SDL_RenderPresent` publishes the newest composed frame** with an atomic pointer swap, then does one of two things:
  - `DSFLIP_PACING=mailbox`: return immediately. Use this if DraStic paces on audio or a timer.
  - `DSFLIP_PACING=fifo`: block until the next flip event. This emulates a vsync-blocking present at a 0.15 ms cost instead of a GL swap. Use it if Phase 2 showed DraStic relies on present blocking.
- **If no new frame arrived by vblank,** don't commit. Never stall.

### 8.6 Input (touch)

With `SDL_VIDEODRIVER=dummy`, SDL no longer receives mouse or touch input from the panel, so the hook provides it:

1. **Find the touch device.** Look for the bottom panel's touch device with `grep -A5 -i touch /proc/bus/input/devices`.
2. **Read and map it.** A thread reads `ABS_MT_POSITION_X/Y` and `BTN_TOUCH`, then maps panel coordinates to DraStic window coordinates for the bottom screen (the inverse of the composer mapping).
3. **Deliver it both ways**, because we won't know which one DraStic uses until Phase 2:
   - push `SDL_MOUSEMOTION` and `SDL_MOUSEBUTTONDOWN/UP` with `SDL_PushEvent`;
   - override `SDL_GetMouseState` to return the tracked position and buttons.

Gamepad input is unaffected, because SDL's joystick subsystem reads evdev regardless of the video driver. Re-test ROCKNIX's hotkey and exit handling.

### 8.7 Session handling for the KMS backend

Stopping the compositor from a process that EmulationStation launched also kills the launcher, because ES runs inside that session. The launcher must therefore hand off to a detached systemd unit.

`/storage/dsflip/launch.sh`, used instead of DraStic's start script while testing:

```sh
#!/bin/sh
exec systemd-run --unit=dsflip-session --collect /bin/sh /storage/dsflip/session.sh "$1"
```

`/storage/dsflip/session.sh`, which runs outside the ES/compositor process tree:

```sh
#!/bin/sh
ROM="$1"
COMP_UNIT="CHANGE_ME"   # find with: systemctl list-units --type=service | grep -i -E 'sway|essway|emustation'
LOG=/storage/dsflip/logs/session-$(date +%Y%m%d-%H%M%S).log
GOV_FILE=/sys/devices/system/cpu/cpufreq/policy0/scaling_governor
OLD_GOV=$(cat "$GOV_FILE")

systemctl stop "$COMP_UNIT"
sleep 0.5                                   # let it drop DRM master
echo performance > "$GOV_FILE"

export SDL_VIDEODRIVER=dummy                # the hook owns the display
export DSFLIP_BACKEND=kms DSFLIP_FILTER=area DSFLIP_PACING=fifo
export LD_PRELOAD="/storage/dsflip/libdsflip.so${LD_PRELOAD:+:$LD_PRELOAD}"
cd /storage/.config/drastic                 # verify DraStic's expected working dir during recon
./drastic "$ROM" >>"$LOG" 2>&1
unset LD_PRELOAD SDL_VIDEODRIVER

echo "$OLD_GOV" > "$GOV_FILE"
systemctl start "$COMP_UNIT"                # ES comes back with the session
```

Also check that audio still works with the compositor stopped (PipeWire should run as its own service; verify). The cost of this approach is a few seconds on launch and exit for the compositor restart. Measure it; if it's unacceptable, move to Backend A.

### 8.8 Backend A: Wayland zero-copy (keeps sway and ES running)

This uses the same composer and scaler with a different output:

1. **Connect:** open the hook's own Wayland connection with `wl_display_connect(NULL)`. `WAYLAND_DISPLAY` is already set.
2. **Bind globals:** `wl_compositor`, `xdg_wm_base`, `zwp_linux_dmabuf_v1`, both `wl_output`s and `wl_seat`.
3. **Create surfaces:** one `xdg_toplevel` per output with `set_fullscreen(output)`, and an opaque region covering the whole surface.
4. **Allocate buffers:** allocate scanout-capable buffers with GBM on `/dev/dri/card0` (or from a dma-heap), export them as dmabuf and wrap them with `zwp_linux_buffer_params_v1`.
5. **Pace:** use `wl_surface.frame` callbacks or `wp_presentation` feedback.
6. **Input:** touch arrives on our surfaces via `wl_touch` and goes into the same injection path as §8.6.

sway can scan such a buffer out directly only when all of these hold:

- the window is fullscreen and opaque;
- the format and modifier are accepted by the plane;
- the **output has no transform**.

If Phase 0 shows transformed outputs, Backend A still composites once on the GPU. That's cheaper than today, but not free. Confirm direct scanout in sway's debug log.

**Gate 3:** build a Backend B prototype on one panel with a fixed scaler and measure it against the Phase 2 baseline. Continue if emulated FPS or p99 frame time improves measurably on the heavy titles.

---

## 9. Phase 4 — Scaling and filters in NEON (2–4 days)

Both scaling ratios on this device are small rational numbers, so each filter is a fixed repeating pattern, which suits NEON well:

- **Hi-res on:** 512×384 → 640×480 = **5:4** (4 source pixels → 5 output pixels, on both axes)
- **Hi-res off:** 256×192 → 640×480 = **5:2**

### Area ("sharp-shimmerless") weights

Each output pixel is the average of the source area it covers.

**5:4** (per group of 4 source pixels `s0..s3`):

| Output | Weights |
|---|---|
| d0 | s0 |
| d1 | ¼·s0 + ¾·s1 |
| d2 | ½·s1 + ½·s2 |
| d3 | ¾·s2 + ¼·s3 |
| d4 | s3 |

**5:2** (per group of 2 source pixels): d0 = s0, d1 = s0, d2 = ½·s0 + ½·s1, d3 = s1, d4 = s1.

### Scalar reference (RGB565 → RGB565, separable)

```c
#include <stdint.h>
#include <string.h>

static inline uint16_t mix565(uint16_t a, uint16_t b, int wa /* weight of a, in quarters */) {
    int wb = 4 - wa;
    int r = ((((a >> 11) & 31) * wa) + (((b >> 11) & 31) * wb) + 2) >> 2;
    int g = ((((a >> 5) & 63) * wa) + (((b >> 5) & 63) * wb) + 2) >> 2;
    int bl = (((a & 31) * wa) + ((b & 31) * wb) + 2) >> 2;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void hscale_5_4(const uint16_t *s, uint16_t *d, int sw) {
    for (int x = 0; x < sw; x += 4, s += 4, d += 5) {
        d[0] = s[0];
        d[1] = mix565(s[0], s[1], 1);
        d[2] = mix565(s[1], s[2], 2);
        d[3] = mix565(s[2], s[3], 3);
        d[4] = s[3];
    }
}

/* src: sw×sh RGB565 (sw, sh multiples of 4), pitches in pixels.
 * dst: (sw*5/4)×(sh*5/4). dst may be write-combined scanout memory:
 * it is written strictly sequentially and never read. */
void scale_5_4_area_rgb565(const uint16_t *src, int src_pitch, int sw, int sh,
                           uint16_t *dst, int dst_pitch) {
    uint16_t h[4][640];                      /* cached temp rows, dw <= 640 */
    int dw = sw * 5 / 4;
    for (int y = 0; y < sh; y += 4) {
        for (int k = 0; k < 4; k++) hscale_5_4(src + (y + k) * src_pitch, h[k], sw);
        uint16_t *d = dst + (y * 5 / 4) * dst_pitch;
        memcpy(d, h[0], dw * 2);                               /* d0 = s0          */
        for (int k = 1; k <= 3; k++) {                         /* d1..d3: blends   */
            d += dst_pitch;
            for (int x = 0; x < dw; x++) d[x] = mix565(h[k - 1][x], h[k][x], k);
        }
        d += dst_pitch;
        memcpy(d, h[3], dw * 2);                               /* d4 = s3          */
    }
}
```

### NEON version

1. Load 8 source pixels (two 4-pixel groups) with `vld1q_u16`.
2. Unpack to 5/6/5 lanes with `vshrq_n_u16` / `vandq_u16`.
3. Compute the weighted sums with `vmlaq`, round and repack.
4. Store 10 output pixels.

Unit-test the NEON version bit-for-bit against the scalar version. DSperate does exactly this for its own kernels.

### Other filters in the same framework

- `nearest`: at 5:4, every fourth pixel is duplicated, which is uneven and shimmers. Integer 1× centered (512×384 inside 640×480 with borders) is the clean alternative.
- `lcd`: the area filter plus a darkened seam per DS pixel, similar to ROCKNIX's `lcd1x`/LCD3X shaders, computed in the same pass.
- `hw`: Backend C.

**Budget:** DSperate measured bilinear scaling at 2.5× at about 0.2 ms per frame on the RG DS with NEON. Expect the same order of magnitude here.

---

## 10. Phase 5 — Dual-panel frame sync (1–2 days)

1. **Commit both panels together.** Put both panels' new framebuffers in **one** atomic commit, only after both screens of the same emulated frame are composed. Both panels then show that frame from their next vblank.
2. **Wait for both flips.** Wait for both CRTCs' flip events before the next commit; `page_flip_handler2` reports which CRTC flipped.
3. **Watch the vblank phase.** If the two panels' vblanks are far apart in phase, a combined commit can wait up to a frame for the later one. Log the delta between the two flip timestamps. If it's large, try committing each CRTC separately right after its own vblank, still with same-frame content.

True hardware phase-lock of the two panels is out of scope, since it needs timing control in the display driver. What this achieves matches GammaOS's "Frame Sync": the same frame on both screens.

---

## 11. Phase 6 — CPU scheduling (2–3 days, measurement-driven)

Only keep changes that improve the §13 numbers.

1. **Identify thread roles** from Phase 0 (`comm` names plus CPU%): emulation, 3D raster workers, audio, and our presenter and input threads.
2. **Presenter placement:** the presenter runs at normal priority and does little work (under 1 ms per frame). Try pinning it to the core with the least raster load, and compare with unpinned.
3. **Presenter priority:** try `SCHED_FIFO` at a low priority (e.g. 10) for the presenter only, so flips are never late. Verify that nothing starves audio.
4. **Thermal headroom:** with the GPU now idle, its devfreq should sit at minimum. That means less heat and more thermal headroom for the CPUs. Confirm with thermal logs.
5. **Background services:** check ROCKNIX's background services during DS sessions (Syncthing, Samba, network scans) and pause them if they wake the CPU.

---

## 12. Phase 7 — Packaging into ROCKNIX (2–3 days)

Once everything works from `/storage/dsflip`:

- **Package:**
  - Add the `libdsflip` sources to the `drastic-sa` package, likely `projects/ROCKNIX/packages/emulators/standalone/drastic-sa/` (check the tree). The June 2026 SDL2 shader-hook commits show exactly how hook code is built and installed there.
  - Build with `$CC $CFLAGS -shared -fPIC … -ldrm -lpthread`, plus `-lwayland-client -lgbm` for Backend A.
  - Add `libdrm` to `PKG_DEPENDS_TARGET` and install to `/usr/lib/dsflip/`.
- **Launcher:** in DraStic's start script, prepend `libdsflip.so` to `LD_PRELOAD` when the per-system or per-game setting is on. Keep the existing shader hook working in passthrough mode.
- **Settings:** expose them in EmulationStation the same way the shader selector is exposed:
  - `dsflip` on/off;
  - backend (`kms` / `wayland`);
  - filter (`area` / `nearest` / `lcd` / `hw`);
  - pacing (`fifo` / `mailbox`).
- **Config file:** `/storage/.config/drastic/dsflip.ini` for power users (layout mapping, panel rotation, debug stats).
- **Safety:** if init fails (no DRM master, unexpected format, no frame within 5 seconds), switch to passthrough and write the reason to the log.

---

## 13. Benchmark protocol

Test conditions for every run:

- the same save state per title, placed at a known heavy scene;
- 60 seconds per run;
- device starting cool (below 45 °C);
- same brightness, charger unplugged.

| Title | Why it's in the set |
|---|---|
| Mario Kart DS | 3D-heavy, fast motion |
| New Super Mario Bros. | 2D + 3D mix |
| Pokémon HeartGold/SoulSilver (overworld) | Common, 3D overworld |
| Pokémon Black/White (overworld) | Display capture every frame |
| Golden Sun: Dark Dawn | Very heavy; swaps screens every frame |
| Zelda: Spirit Tracks | 3D + stylus |
| Sonic Rush | Fast 2D/3D |
| Kingdom Hearts 358/2 Days | Heavy 3D |

Record per run:

- DraStic FPS (average and minimum);
- `libdsflip` present interval p50/p99;
- per-stage ms (capture, scale, commit);
- CPU% per thread;
- maximum SoC temperature;
- battery current (`/sys/class/power_supply/*/current_now`, if exposed).

Results table template (`/storage/dsflip/logs/results.md`):

| Title | Config | hires | FPS avg | FPS min | p99 ms | Temp max | Notes |
|---|---|---|---|---|---|---|---|
| | | | | | | | |

Configurations to compare:

1. stock ROCKNIX;
2. Phase 1 settings;
3. Phase 1 + `libdsflip` Backend B;
4. + filters;
5. + scheduling.

If you have GammaOS Nano on a second SD card, run the same states there as a reference column.

---

## 14. Risks, unknowns and fallbacks

| Risk | Mitigation |
|---|---|
| DraStic renders with raw GL, not SDL_Renderer | Intercept `glTexImage2D`/`glTexSubImage2D` + `eglSwapBuffers` instead; the composer and backends stay the same |
| The menu uses blending or calls we don't implement | Generic alpha blit path. Unknown calls switch to passthrough while the menu is open. |
| DraStic paces on present | FIFO pacing mode |
| Stopping sway breaks ES/hotkeys or takes too long | Backend A (Wayland) |
| An output transform prevents direct scanout in Backend A | Rotate in the scaler and set the output transform to `normal`; otherwise accept one GPU composite |
| HW plane scaling unsupported on this VOP2 | NEON scaler (Backend C is only a bonus) |
| The display path turns out cheap (Gate 2 fails) | The remaining cost is DraStic's CPU raster: scheduling (§11), per-game `threaded_3d`/frameskip, or Appendix A/B |
| Thermal throttling erases the gains | Log frequency and temperature every run; the idle GPU helps; consider a per-game governor |
| A drastic-sa update changes the binary | The hook patches no DraStic code, only SDL calls, so it survives binary changes that keep the same SDL usage |

---

## Appendix A — Running the Android DraStic core on ROCKNIX (research track)

This is what GammaOS actually does (in-process `libdrastic`). On Linux it needs a bionic ELF loader:

1. **Extract the core.** Take `lib/arm64-v8a/libdrastic.so` from the r2.6.0.4a APK you own and pin its SHA-256. Any later patches are specific to that exact build.
2. **Inventory it.** Run `readelf -d` for its needed libraries and `nm -D` for its imports and `Java_com_dsemu_drastic_*` JNI exports.
3. **Start from an existing loader.** Use gmloader-next / droidports (JohnnyonFlame), which PortMaster already uses on these handhelds to run Android GameMaker's `libyoyo.so` on Linux. It loads the library, resolves imports to native functions and patches in place; its lineage goes back to the PS Vita so-loader.
4. **Implement the missing pieces:** bionic libc thunks, a fake `JNIEnv` for the calls the Java side normally makes, and an OpenSL ES stub (GammaOS already patches out `slCreateEngine`). Then add audio, video and input glue and reuse libdsflip's backends.
5. **Read GammaOS's `DrasticRunner.cpp`** (Apache-2.0, in `GammaOSNextDistribution-14/frameworks/base/cmds/gammaos-nano/`) for call order, memory-region discovery and the in-memory patches.

Caveats:

- **Licensing:** DraStic was delisted in February 2025, its last build is r2.6.0.4a, and the announced source release never happened. drastic-nano's own code is Apache-2.0, but DraStic itself isn't covered. A public ROCKNIX fork therefore can't ship `libdrastic.so`; users would supply their own APK at install time.
- **Unproven benefit:** nobody has shown that the r2.6 Android core is faster than the Linux binary at identical settings. Benchmark before investing.

---

## Appendix B — DSperate (open-source alternative)

DSperate is a GPL-3 clean-room reimplementation of DraStic's JIT and NEON techniques, built and measured on the RG DS. It has:

- a dual-window mode for dual-panel handhelds;
- zero-copy dmabuf presentation under Wayland and its own page flips under KMSDRM.

A ROCKNIX PR adds it as `dsperate-sa`. It has no 2× internal 3D yet; the closest feature is a "smooth" anti-aliasing option that draws polygon edges at panel resolution.

It's useful to this plan in two ways:

- **Reference code:** its presentation code is a working example of Backends A and B on this exact hardware.
- **Long-term option:** adding a 2× rasterizer to an open emulator may age better than hooking a closed one.

---

## Appendix C — Task list for Claude Code sessions

Ground rules for every session:

- work only under `/storage/dsflip/` on the device;
- never modify stock ROCKNIX files;
- keep `DSFLIP_BACKEND=off` working;
- end every change with a benchmark row in `/storage/dsflip/logs/results.md`.

1. **Recon** (§5): write `recon.md` answering all six unknowns. *Done when:* each unknown has an answer, or "couldn't determine" with the command output attached.
2. **Settings parity** (§6): add baseline and Phase 1 rows to `results.md`. *Gate 1.*
3. **Probe** (§7): build `libdsprobe.so` and collect logs for three titles. *Done when:* the per-frame ms for update, copy and present and the pacing behavior are known. *Gate 2.*
4. **KMS bring-up:** a standalone test program (no DraStic) that sets modes on both DSI CRTCs, flips a test pattern at 60 Hz with RGB565 dumb buffers, and logs flip timestamps per CRTC.
5. **HW scaling test:** a `TEST_ONLY` commit with 512×384 → 640×480 on each candidate plane; record the result.
6. **libdsflip v0:** intercept + passthrough + stats only. It must be invisible to DraStic.
7. **libdsflip v1:** capture + scalar area scaler + Backend B on both panels, with mailbox or FIFO pacing per step 3. *Gate 3.*
8. **Input:** evdev touch injection; test in a stylus game (Spirit Tracks).
9. **NEON scalers:** bit-exact against the scalar versions; add `nearest` and `lcd`.
10. **Frame sync:** a single atomic commit for both CRTCs; log flip deltas.
11. **Session handling:** the systemd-run launcher with compositor stop/start; measure launch and exit time.
12. **Scheduling experiments** (§11).
13. **Backend A**, if step 11's cost is unacceptable.
14. **Package** (§12) into a ROCKNIX build and test the update path.

---

## References

- GammaOS Next changelog (DraStic-nano, DRM direct output, Frame Sync, minimal boot): https://github.com/TheGammaSqueeze/GammaOSNext/wiki/GammaOS-Next-Changelog
- GammaOS Nano wiki, DraStic Nano options and dual-screen defaults: https://thegammasqueeze.github.io/GammaOSNano-WIKI/drastic-nano.html
- drastic-nano architecture (in-process core, DRM/KMS rendering, decoupled render loop, licensing): https://github.com/TheGammaSqueeze/GammaOSNextDistribution-14/blob/develop/frameworks/base/cmds/drastic-nano/RETROACHIEVEMENTS.md
- DSperate (RG DS measurements, KMSDRM/dmabuf presentation, NEON scaling): https://github.com/beebono/DSperate
- DSperate ROCKNIX PR: https://github.com/ROCKNIX/distribution/pull/3343
- ROCKNIX drastic-sa SDL2 shader hook (nightly 2026-06-23): https://github.com/ROCKNIX/distribution-nightly/releases/tag/nightly-20260623
- advanced_drastic (LD_PRELOAD hooking of 64-bit DraStic, runs on ROCKNIX): https://github.com/trngaje/advanced_drastic
- steward-fu NDS port (custom SDL2; hi-res output 512×384): https://github.com/steward-fu/nds
- DraStic's hi-res mode is software-rendered at 512×384: https://drastic-ds.com/viewtopic.php?t=2523
- gmloader / droidports (Android .so libraries on Linux handhelds): https://github.com/JohnnyonFlame/droidports
