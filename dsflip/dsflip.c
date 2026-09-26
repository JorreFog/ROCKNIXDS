// libdsflip.so: DraStic's two DS screens scanned out directly on the RG DS's two DSI panels.
//
// DraStic renders each DS screen into an SDL streaming texture (ARGB8888, 256x192 or 512x384)
// via SDL_LockTexture/UnlockTexture, draws both with SDL_RenderCopy, then SDL_RenderPresent.
// This hook hands DraStic *DRM dumb buffers* on Lock (XRGB8888 has the same memory layout as
// SDL's ARGB8888), so DraStic writes straight into scanout memory: no upload, no GL, no copy.
// The VOP2 display controller scales each buffer to 640x480 in hardware.
//
// DraStic's menu is one 800x480 RGB565 streaming texture filled with SDL_UpdateTexture; it is
// copied into an RGB565 dumb buffer and shown on the top panel (bottom panel black).
//
// A presenter thread owns the atomic commits: both panels' new framebuffers go in ONE commit
// (same emulated frame on both screens), mailbox-style: RenderPresent never blocks, a frame that
// is superseded before the next vblank is dropped, and a panel with no new frame keeps its buffer.
//
// Touch: with SDL's dummy video driver nothing delivers the touchscreen, so a thread reads the
// bottom panel's evdev node and injects mouse events (in DraStic's logical coordinates, inside
// the rectangle the bottom screen was last drawn to) through SDL_PollEvent.
//
// Requirements: DRM master (sway stopped), SDL_VIDEODRIVER=dummy. Env:
//   DSFLIP_TOP=DSI-2               connector that shows the DS top screen
//   DSFLIP_TOUCH=fe5e0000.i2c      bottom touchscreen (i2c-5; calibrated: raw == panel pixels)
//   DSFLIP_TOUCH_INVERT=x/y/xy     axes to invert (default none)
//   DSFLIP_LOG=path                stats log (default /storage/dsflip/logs/dsflip.log)
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <errno.h>
#include <glob.h>
#include <signal.h>
#include <stdarg.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <math.h>
#include <sys/stat.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

typedef struct { int x, y, w, h; } SDL_Rect;
#define NBUF 4
#define FMT_ARGB8888 0x16362004u         /* SDL_PIXELFORMAT_* */
#define FMT_RGB565   0x15151002u
enum { FREE, WRITING, WRITTEN, READY, QUEUED, SCANOUT };
enum { K_SCREEN, K_MENU, K_BLACK };

typedef struct { uint32_t fb, handle, pitch, w, h; uint64_t size; void *map; int state; } dbuf;
typedef struct {                        /* one DraStic texture we scan out */
    void *tex; int kind, w, h;
    dbuf b[NBUF]; int nb;
    int writing, written;               /* index being written / last completed write, -1 if none */
} stex;
typedef struct {                        /* one panel */
    uint32_t conn, crtc, crtc_idx, plane, mode_blob;
    drmModeModeInfo mode;
    uint32_t p_fb, p_crtc, p_sx, p_sy, p_sw, p_sh, p_cx, p_cy, p_cw, p_ch;
    dbuf *ready, *queued, *scan;
    long long last_flip;
} panel;

static int fd = -1, efd = -1, ok;
static FILE *lg;
static panel P[2];                      /* [0] = top, [1] = bottom */
static stex T[6]; static int nt;
static stex blk;                        /* black 640x480 buffer for an unused panel */
static int menu_hw;                     /* VOP2 scales the 800x480 menu itself */
/* spinlock, not pthread_mutex_t: the desktop's x86 glibc headers give the mutex the wrong size for
 * aarch64. Critical sections are a few loads/stores (plus one non-blocking commit ioctl). */
static int mu, tmu;
static void lock(int *l) { while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) sched_yield(); }
static void unlock(int *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
static int logical_w = 512, logical_h = 192;
static stex *pending_route[2];          /* routes recorded by RenderCopy during this frame */
static SDL_Rect route_dst[2]; static int touch_rect_ok;   /* touch only while a DS screen is on the bottom panel */
static void *window;
static int cursor_log;                  /* DSFLIP_CURSOR_LOG=1: log where DraStic draws its 32x32 stylus cursor */
/* stats */
static int st_present, st_commit, st_drop, st_busy, st_flips[2], st_touch;
static long long st_t0, st_iv_max[2];

#define LOG(...) do { if (lg) fprintf(lg, __VA_ARGS__); } while (0)
#define REAL(ret, fn, ...) static ret (*real)(__VA_ARGS__); if (!real) real = dlsym(RTLD_NEXT, #fn)

static long long now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }

static uint32_t prop(uint32_t obj, uint32_t type, const char *name) {
    drmModeObjectProperties *pr = drmModeObjectGetProperties(fd, obj, type);
    uint32_t id = 0;
    for (uint32_t i = 0; pr && i < pr->count_props && !id; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, pr->props[i]);
        if (p && !strcmp(p->name, name)) id = p->prop_id;
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(pr);
    return id;
}
static uint64_t propval(uint32_t obj, uint32_t type, const char *name) {
    drmModeObjectProperties *pr = drmModeObjectGetProperties(fd, obj, type);
    uint64_t v = 0;
    for (uint32_t i = 0; pr && i < pr->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, pr->props[i]);
        if (p && !strcmp(p->name, name)) v = pr->prop_values[i];
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(pr);
    return v;
}
static int mkbuf(dbuf *b, uint32_t w, uint32_t h, uint32_t fourcc, uint32_t bpp) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = bpp };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size; b->w = w; b->h = h;
    uint32_t hs[4] = { c.handle }, ps[4] = { c.pitch }, os[4] = { 0 };
    if (drmModeAddFB2(fd, w, h, fourcc, hs, ps, os, &b->fb, 0)) return -1;
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    b->map = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (b->map == MAP_FAILED) return -1;
    memset(b->map, 0, c.size);
    b->state = FREE;
    return 0;
}
static void freebuf(dbuf *b) {
    if (!b->map) return;
    munmap(b->map, b->size); drmModeRmFB(fd, b->fb);
    struct drm_mode_destroy_dumb d = { .handle = b->handle }; drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    memset(b, 0, sizeof *b);
}

static void plane_props(panel *p) {
    uint32_t o = DRM_MODE_OBJECT_PLANE, id = p->plane;
    p->p_fb = prop(id, o, "FB_ID"); p->p_crtc = prop(id, o, "CRTC_ID");
    p->p_sx = prop(id, o, "SRC_X"); p->p_sy = prop(id, o, "SRC_Y"); p->p_sw = prop(id, o, "SRC_W"); p->p_sh = prop(id, o, "SRC_H");
    p->p_cx = prop(id, o, "CRTC_X"); p->p_cy = prop(id, o, "CRTC_Y"); p->p_cw = prop(id, o, "CRTC_W"); p->p_ch = prop(id, o, "CRTC_H");
}
static void add_fb(drmModeAtomicReq *r, panel *p, dbuf *b) {
    drmModeAtomicAddProperty(r, p->plane, p->p_fb, b->fb);
    drmModeAtomicAddProperty(r, p->plane, p->p_crtc, p->crtc);
    drmModeAtomicAddProperty(r, p->plane, p->p_sx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sw, (uint64_t)b->w << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_sh, (uint64_t)b->h << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_cx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cw, p->mode.hdisplay);
    drmModeAtomicAddProperty(r, p->plane, p->p_ch, p->mode.vdisplay);
}

#include "font8.h"
void ra_frame(void);

void dsflip_log(const char *fmt, ...) {
    if (!lg) return;
    va_list ap; va_start(ap, fmt); vfprintf(lg, fmt, ap); va_end(ap);
}

/* ---------- presenter ---------- */
static int pending_mask;                /* bit i: a flip event is still due for panel i's CRTC */

/* Pacing. DraStic finishes frames on its own 60.000 Hz clock, not in step with the panels' vblank. Showing each
 * frame at the very next vblank ("immediate") makes the vblank itself the cut-off: when DraStic's presents drift
 * into that phase, per-frame timing jitter puts two frames into one refresh (one dropped) and none into the next
 * (one repeated). "latch" mode instead measures where in the refresh cycle presents arrive (circular average,
 * tracking the slow drift between the two clocks) and commits the newest frame at the opposite phase, so the
 * cut-off sits as far from the presents as possible. DSFLIP_PACING=immediate restores the old behaviour. */
static int pacing_latch = 1, tfd = -1;
static long long vbl_ref;               /* a recent top-panel vblank (flip timestamp, CLOCK_MONOTONIC us) */
static double period = 16666.7;         /* measured refresh period, us */
static double ph_c, ph_s;               /* EMA of cos/sin of the present phase */
static int ph_n, ph_hist[16], st_late;  /* samples; histogram of present phases (logged); commits that missed */
static double latch_off = 14000;        /* current latch point, us after vblank */
static long long ready_since;           /* when the oldest uncommitted frame arrived */

static void release(dbuf *b) { if (b && b != &blk.b[0]) b->state = FREE; }

/* toast: a small overlay plane on the top panel (RetroAchievements pop-ups), committed together with the
 * game frames. Rendered into one of two buffers, never the one being scanned out. */
#define TOAST_W 640
#define TOAST_H 56
static uint32_t tp_plane, tp_fb, tp_crtc, tp_sx, tp_sy, tp_sw, tp_sh, tp_cx, tp_cy, tp_cw, tp_ch;
static dbuf toast[2];
static int toast_cur, toast_shown, toast_want, toast_dirty;
static long long toast_until;

static void add_toast(drmModeAtomicReq *r) {
    if (toast_want) {
        drmModeAtomicAddProperty(r, tp_plane, tp_fb, toast[toast_cur].fb);
        drmModeAtomicAddProperty(r, tp_plane, tp_crtc, P[0].crtc);
        drmModeAtomicAddProperty(r, tp_plane, tp_sx, 0);
        drmModeAtomicAddProperty(r, tp_plane, tp_sy, 0);
        drmModeAtomicAddProperty(r, tp_plane, tp_sw, (uint64_t)TOAST_W << 16);
        drmModeAtomicAddProperty(r, tp_plane, tp_sh, (uint64_t)TOAST_H << 16);
        drmModeAtomicAddProperty(r, tp_plane, tp_cx, 0);
        drmModeAtomicAddProperty(r, tp_plane, tp_cy, 6);
        drmModeAtomicAddProperty(r, tp_plane, tp_cw, TOAST_W);
        drmModeAtomicAddProperty(r, tp_plane, tp_ch, TOAST_H);
    } else {
        drmModeAtomicAddProperty(r, tp_plane, tp_fb, 0);
        drmModeAtomicAddProperty(r, tp_plane, tp_crtc, 0);
    }
}

static void try_commit(void) {         /* called with mu held */
    if (pending_mask) return;
    int mask = 0;
    for (int i = 0; i < 2; i++) if (P[i].ready) mask |= 1 << i;
    int with_toast = tp_plane && toast_dirty;
    if (with_toast) mask |= 1;          /* the toast plane lives on the top panel's CRTC */
    if (!mask) return;
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (int i = 0; i < 2; i++) if (P[i].ready) add_fb(r, &P[i], P[i].ready);
    if (with_toast) add_toast(r);
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, P);
    drmModeAtomicFree(r);
    if (ret) {
        st_busy++;
        if (ret != -EBUSY) {           /* rejected config: drop the frame rather than retry forever */
            LOG("[dsflip] commit rejected: %s%s\n", strerror(-ret), with_toast ? " (toasts disabled)" : "");
            if (with_toast) { tp_plane = 0; toast_dirty = 0; }
            for (int i = 0; i < 2; i++) if (P[i].ready) { release(P[i].ready); P[i].ready = 0; }
        }
        return;
    }
    for (int i = 0; i < 2; i++) if (P[i].ready) { P[i].queued = P[i].ready; P[i].queued->state = QUEUED; P[i].ready = 0; }
    if (with_toast) { toast_dirty = 0; toast_shown = toast_want; }
    pending_mask = mask;                /* one flip event per CRTC in the commit */
    st_commit++;
}

static void on_flip(int f, unsigned seq, unsigned sec, unsigned usec, unsigned crtc, void *u) {
    (void)f; (void)seq; (void)u;
    lock(&mu);
    for (int i = 0; i < 2; i++) if (P[i].crtc == crtc) {
        pending_mask &= ~(1 << i);
        long long t = sec * 1000000LL + usec;
        if (i == 0) {
            if (vbl_ref) {
                double iv = (double)(t - vbl_ref), k = floor(iv / period + 0.5);
                if (k >= 1 && k <= 4 && fabs(iv / k - period) < 300) period += 0.02 * (iv / k - period);
            }
            vbl_ref = t;
        }
        if (!P[i].queued) continue;     /* toast-only flip on this CRTC */
        if (P[i].last_flip && t - P[i].last_flip > st_iv_max[i]) st_iv_max[i] = t - P[i].last_flip;
        P[i].last_flip = t; st_flips[i]++;
        if (P[i].scan != P[i].queued) release(P[i].scan);
        P[i].scan = P[i].queued; P[i].scan->state = SCANOUT; P[i].queued = 0;
    }
    if (!pending_mask && (!pacing_latch || ph_n < 120)) try_commit();
    unlock(&mu);
}

static void toast_text(uint32_t *px, int pitch, int x, int y, const char *t, uint32_t col, int maxc) {
    int n = 0;
    for (; *t && n < maxc; t++, n++) {
        unsigned char c = (unsigned char)*t;
        if (n == maxc - 1 && t[1]) c = '~';          /* truncated */
        if (c < 32 || c > 126) c = '?';
        const unsigned char *g = font8[c - 32];
        for (int gy = 0; gy < 8; gy++)
            for (int gx = 0; gx < 8; gx++)
                if (g[gy] & (0x80 >> gx))
                    for (int sy = 0; sy < 2; sy++) {
                        uint32_t *row = (uint32_t *)((char *)px + (y + gy * 2 + sy) * pitch);
                        row[x + n * 16 + gx * 2] = row[x + n * 16 + gx * 2 + 1] = col;
                    }
    }
}

/* public: show a two-line pop-up on the top panel for ms milliseconds (thread-safe) */
void dsflip_toast(const char *l1, const char *l2, uint32_t accent, int ms) {
    if (!ok || !tp_plane) return;
    lock(&mu); int next = toast_shown ? !toast_cur : toast_cur; unlock(&mu);
    dbuf *b = &toast[next];
    uint32_t *px = b->map; int pitch = (int)b->pitch;
    uint32_t bg = 0xff161b26, edge = 0xff000000 | accent;
    for (int y = 0; y < TOAST_H; y++) {
        uint32_t *row = (uint32_t *)((char *)px + y * pitch);
        for (int x = 0; x < TOAST_W; x++) {
            uint32_t c = bg;
            if (x < 6 || y >= TOAST_H - 2) c = edge;                  /* accent bar + underline */
            if ((x < 2 || x >= TOAST_W - 2) && (y < 2 || y >= TOAST_H - 2)) c = 0;   /* soft corners */
            row[x] = c;
        }
    }
    toast_text(px, pitch, 18, 8, l1 ? l1 : "", 0xff000000 | accent, 38);
    toast_text(px, pitch, 18, 30, l2 ? l2 : "", 0xffffffff, 38);
    lock(&mu);
    toast_cur = next; toast_want = 1; toast_dirty = 1; toast_until = now_us() + (long long)ms * 1000;
    unlock(&mu);
    uint64_t one = 1; if (write(efd, &one, 8) < 0) {}
    }

/* SIGUSR2: dump what each panel scans out to <logdir>/scan<i>.raw (header "w h pitch bpp\n" + pixels) */
static volatile int want_dump, touch_dumps;   /* DSFLIP_TOUCH_DUMP=N: dump the bottom screen at the first N touch-downs */
static int tdump_x, tdump_y, tdump_n;
static void on_usr2(int sig) { (void)sig; want_dump = 1; }
static void dump_scan(void) {
    for (int i = 0; i < 2; i++) {
        dbuf *b = P[i].scan; if (!b || !b->map) continue;
        char fn[96]; snprintf(fn, sizeof fn, "/storage/dsflip/logs/scan%d.raw", i);
        FILE *f = fopen(fn, "wb"); if (!f) continue;
        fprintf(f, "%u %u %u %u\n", b->w, b->h, b->pitch, (uint32_t)(b->pitch / b->w * 8));
        fwrite(b->map, 1, (size_t)b->pitch * b->h, f); fclose(f);
    }
    if (tp_plane && toast_shown) {
        dbuf *b = &toast[toast_cur];
        FILE *f = fopen("/storage/dsflip/logs/toast.raw", "wb");
        if (f) { fprintf(f, "%u %u %u 32\n", b->w, b->h, b->pitch); fwrite(b->map, 1, (size_t)b->pitch * b->h, f); fclose(f); }
    }
    LOG("[dsflip] dumped scanout buffers\n");
}

static void dump_touch(void) {         /* bottom panel's scanout + the DS coordinate we sent */
    dbuf *b = P[1].scan; if (!b || !b->map) return;
    char fn[96]; snprintf(fn, sizeof fn, "/storage/dsflip/logs/touch%d.raw", tdump_n);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "%u %u %u %u %d %d\n", b->w, b->h, b->pitch, (uint32_t)(b->pitch / b->w * 8), tdump_x, tdump_y);
    fwrite(b->map, 1, (size_t)b->pitch * b->h, f); fclose(f);
}

static void arm_latch(void) {         /* with mu held: next latch point after now */
    if (tfd < 0 || !vbl_ref) return;
    if (ph_n >= 120) {
        double mean = atan2(ph_s, ph_c) / (2 * M_PI) * period;          /* where presents cluster */
        double l = fmod(mean + period / 2 + 2 * period, period);        /* opposite phase */
        double lo = 800, hi = period - 2500;                             /* the commit must land before vblank */
        if (l < lo || l > hi) {         /* pick the allowed edge that is farther (circularly) from the presents */
            double m = fmod(mean + 2 * period, period);
            double dlo = fabs(remainder(lo - m, period)), dhi = fabs(remainder(hi - m, period));
            l = dlo > dhi ? lo : hi;
        }
        latch_off = l;
    }
    long long now = now_us();
    double k = ceil(((double)(now - vbl_ref) - latch_off + 300) / period);
    long long at = vbl_ref + (long long)(k * period + latch_off);
    struct itimerspec its = { { 0, 0 }, { at / 1000000, (at % 1000000) * 1000 } };
    timerfd_settime(tfd, TFD_TIMER_ABSTIME, &its, 0);
}

static void *presenter(void *a) {
    (void)a;
    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    struct pollfd pf[3] = { { .fd = fd, .events = POLLIN }, { .fd = efd, .events = POLLIN }, { .fd = tfd, .events = POLLIN } };
    long long st10 = now_us();
    for (;;) {
        if (poll(pf, tfd >= 0 ? 3 : 2, 50) < 0) continue;
        if (pf[0].revents & POLLIN) drmHandleEvent(fd, &ev);
        if (pf[1].revents & POLLIN) {
            uint64_t v; if (read(efd, &v, 8) < 0) {}
            lock(&mu);
            if (!pacing_latch || ph_n < 120) try_commit();              /* immediate (and while learning) */
            else arm_latch();
            unlock(&mu);
        }
        if (tfd >= 0 && (pf[2].revents & POLLIN)) {                        /* latch point reached */
            uint64_t v; if (read(tfd, &v, 8) < 0) {}
            lock(&mu);
            if (pending_mask) st_late++; else try_commit();
            if (P[0].ready || P[1].ready) arm_latch();
            unlock(&mu);
        }
        if (pacing_latch && ph_n >= 120) {                                /* safety net: never sit on a frame */
            lock(&mu);
            if ((P[0].ready || P[1].ready) && !pending_mask && now_us() - ready_since > 2 * (long long)period) { st_late++; try_commit(); }
            unlock(&mu);
        }
        if (toast_want && now_us() > toast_until) {   /* hide an expired toast (commits even without a new frame) */
            lock(&mu); toast_want = 0; toast_dirty = 1; try_commit(); unlock(&mu);
        }
        if (want_dump) { want_dump = 0; lock(&mu); dump_scan(); unlock(&mu); }
        if (tdump_n < touch_dumps && tdump_x >= 0) { lock(&mu); dump_touch(); unlock(&mu); tdump_n++; tdump_x = -1; }
        long long t = now_us();
        if (t - st10 >= 10000000) {                                        /* pacing summary every 10 s */
            lock(&mu);
            char h[64]; for (int i = 0; i < 16; i++) h[i] = ph_hist[i] > 99 ? '#' : ph_hist[i] > 30 ? '+' : ph_hist[i] > 5 ? '.' : ' ';
            h[16] = 0; memset(ph_hist, 0, sizeof ph_hist);
            double mean = fmod(atan2(ph_s, ph_c) / (2 * M_PI) * period + period, period);
            LOG("[pace] mode=%s period=%.1f us, presents at %.0f us after vblank (spread R=%.2f) [%s], latch at %.0f us, late=%d\n",
                pacing_latch && ph_n >= 120 ? "latch" : "immediate", period, mean, sqrt(ph_c * ph_c + ph_s * ph_s), h, latch_off, st_late);
            st_late = 0; st10 = t;
            unlock(&mu);
        }
        if (t - st_t0 >= 1000000) {
            lock(&mu);
            LOG("[dsflip] present/s=%.1f commits=%d dropped=%d busy=%d flips top=%d bot=%d max-iv top=%lld bot=%lld us touch=%d\n",
                st_present * 1e6 / (t - st_t0), st_commit, st_drop, st_busy, st_flips[0], st_flips[1], st_iv_max[0], st_iv_max[1], st_touch);
            st_present = st_commit = st_drop = st_busy = st_flips[0] = st_flips[1] = st_touch = 0; st_iv_max[0] = st_iv_max[1] = 0; st_t0 = t;
            unlock(&mu);
        }
    }
    return 0;
}

/* ---------- touch ---------- */
/* SDL2 event layouts (SDL_events.h): motion {type,timestamp,windowID,which,state,x,y,xrel,yrel},
 * button {type,timestamp,windowID,which,u8 button,state,clicks,pad,x,y}. SDL_Event is 56 bytes. */
typedef struct { uint32_t type, ts, win, which; uint8_t button, state, clicks, pad; int32_t x, y; } btn_ev;
typedef struct { uint32_t type, ts, win, which, state; int32_t x, y, xrel, yrel; } mot_ev;
typedef union { uint32_t type; btn_ev b; mot_ev m; uint8_t pad[56]; } sdl_ev;
#define QN 128
#define EV_BARRIER 0xFFFFu                 /* queue marker: SDL_PollEvent reports "no event" once (ends DraStic's poll loop) */
static sdl_ev tq[QN]; static int tq_head, tq_tail;
static int touch_inv_x = 0, touch_inv_y = 0;

extern uint32_t SDL_GetTicks(void);
extern uint32_t SDL_GetWindowID(void *);

static void push_ev(sdl_ev *e) {
    lock(&tmu);
    int n = (tq_head + 1) % QN;
    if (n != tq_tail) { tq[tq_head] = *e; tq_head = n; }
    unlock(&tmu);
}
/* DraStic ignores the absolute x/y of mouse events: it moves its stylus by the RELATIVE deltas (xrel/yrel),
 * 1:1 in DS pixels, clamped to the bottom screen, starting from the centre (measured by logging where it
 * draws its 32x32 cursor). So we track its stylus position and send exact deltas. On every touch-down we
 * first pin it to (0,0) with a large negative move, then move to the target: no drift can accumulate. */
static int sty_x = -1, sty_y = -1;
static void stylus_move(int tx, int ty, int down, int resync) {
    sdl_ev e; uint32_t wid = window ? SDL_GetWindowID(window) : 1, ts = SDL_GetTicks();
    if (resync || sty_x < 0) {
        memset(&e, 0, sizeof e);
        e.m.type = 0x400; e.m.ts = ts; e.m.win = wid; e.m.state = 0; e.m.x = 0; e.m.y = 0; e.m.xrel = -4096; e.m.yrel = -4096;
        push_ev(&e); sty_x = 0; sty_y = 0;
        /* DraStic sums a frame's deltas and clamps once, so the move away from (0,0) must land in the next frame */
        memset(&e, 0, sizeof e); e.type = EV_BARRIER; push_ev(&e);
    }
    if (tx == sty_x && ty == sty_y) return;
    memset(&e, 0, sizeof e);
    e.m.type = 0x400; e.m.ts = ts; e.m.win = wid; e.m.state = down ? 1 : 0;
    e.m.x = tx; e.m.y = ty; e.m.xrel = tx - sty_x; e.m.yrel = ty - sty_y;
    push_ev(&e); sty_x = tx; sty_y = ty;
}

static void touch_emit(int down_change, int down, int x, int y, int xmax, int ymax) {
    /* the bottom panel shows exactly the bottom DS screen: panel pixels -> DS pixels is a straight rescale */
    if (!touch_rect_ok) return;
    int rx = x, ry = y;
    if (touch_inv_x) x = xmax - x;      /* raw touch is already aligned with the panel on the RG DS */
    if (touch_inv_y) y = ymax - y;
    int lx = (int)((long long)x * 256 / (xmax + 1));
    int ly = (int)((long long)y * 192 / (ymax + 1));
    sdl_ev e;
    uint32_t wid = window ? SDL_GetWindowID(window) : 1, ts = SDL_GetTicks();
    if (lx > 255) lx = 255;
    if (ly > 191) ly = 191;
    stylus_move(lx, ly, down, down_change && down);
    if (down_change && down) {
        LOG("[touch] down raw %d,%d -> ds %d,%d\n", rx, ry, lx, ly);
        if (tdump_n < touch_dumps) { tdump_y = ly; tdump_x = lx; }
    }
    if (down_change) {
        memset(&e, 0, sizeof e);
        e.b.type = down ? 0x401 : 0x402; e.b.ts = ts; e.b.win = wid; e.b.button = 1; e.b.state = down ? 1 : 0; e.b.clicks = 1;
        e.b.x = lx; e.b.y = ly;
        push_ev(&e);
    }
    st_touch++;
}

/* test hook: lines "x y" written to /tmp/dsflip-tap inject a tap with exactly those mouse coordinates */
static void *tap_fifo_thread(void *a) {
    (void)a;
    unlink("/tmp/dsflip-tap"); mkfifo("/tmp/dsflip-tap", 0600);
    for (;;) {
        FILE *f = fopen("/tmp/dsflip-tap", "r"); if (!f) return 0;
        int x, y;
        while (fscanf(f, "%d %d", &x, &y) == 2) {
            sdl_ev e; uint32_t wid = window ? SDL_GetWindowID(window) : 1;
            stylus_move(x, y, 1, 1);
            memset(&e, 0, sizeof e); e.b.type = 0x401; e.b.ts = SDL_GetTicks(); e.b.win = wid; e.b.button = 1; e.b.state = 1; e.b.clicks = 1; e.b.x = x; e.b.y = y; push_ev(&e);
            usleep(150000);
            memset(&e, 0, sizeof e); e.b.type = 0x402; e.b.ts = SDL_GetTicks(); e.b.win = wid; e.b.button = 1; e.b.x = x; e.b.y = y; push_ev(&e);
            LOG("[tap] injected %d,%d\n", x, y);
        }
        fclose(f);
    }
    return 0;
}

static void *touch_thread(void *a) {
    const char *want = a;
    glob_t g; char path[64] = "";
    if (glob("/sys/class/input/event*", 0, 0, &g)) return 0;
    for (size_t i = 0; i < g.gl_pathc && !*path; i++) {
        char dev[300], real_[512]; snprintf(dev, sizeof dev, "%s/device", g.gl_pathv[i]);
        if (realpath(dev, real_) && strstr(real_, want)) snprintf(path, sizeof path, "/dev/input/%s", strrchr(g.gl_pathv[i], '/') + 1);
    }
    globfree(&g);
    int tfd = *path ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    if (tfd < 0) { LOG("[dsflip] no touchscreen matching %s\n", want); return 0; }
    int ax[6] = { 0 }, ay[6] = { 0 };
    ioctl(tfd, 0x80184540 + 0x35, ax); ioctl(tfd, 0x80184540 + 0x36, ay);   /* EVIOCGABS(ABS_MT_POSITION_X/Y) */
    int xmax = ax[2] > 0 ? ax[2] : 639, ymax = ay[2] > 0 ? ay[2] : 479;
    LOG("[dsflip] touch %s range %dx%d\n", path, xmax + 1, ymax + 1);
    struct { long s, us; uint16_t type, code; int32_t value; } ev[32];
    int x = 0, y = 0, down = 0, was_down = 0;
    for (;;) {
        ssize_t n = read(tfd, ev, sizeof ev);
        if (n <= 0) break;
        for (int i = 0; i < (int)(n / sizeof ev[0]); i++) {
            if (ev[i].type == 3 && (ev[i].code == 0x35 || ev[i].code == 0)) x = ev[i].value;
            else if (ev[i].type == 3 && (ev[i].code == 0x36 || ev[i].code == 1)) y = ev[i].value;
            else if (ev[i].type == 1 && ev[i].code == 0x14a) down = ev[i].value != 0;   /* BTN_TOUCH */
            else if (ev[i].type == 0 && ev[i].code == 0) {                                /* SYN_REPORT */
                if (down || was_down) touch_emit(down != was_down, down, x, y, xmax, ymax);
                was_down = down;
            }
        }
    }
    LOG("[dsflip] touch read ended\n");
    return 0;
}

int SDL_PollEvent(void *e) {
    REAL(int, SDL_PollEvent, void *);
    if (tq_head != tq_tail) {
        lock(&tmu);
        if (tq_head != tq_tail) {
            if (tq[tq_tail].type == EV_BARRIER) { tq_tail = (tq_tail + 1) % QN; unlock(&tmu); return 0; }
            if (e) { memcpy(e, &tq[tq_tail], sizeof tq[0]); tq_tail = (tq_tail + 1) % QN; }
            unlock(&tmu);
            return 1;
        }
        unlock(&tmu);
    }
    return real(e);
}

/* ---------- init ---------- */
__attribute__((constructor)) static void init(void) {
    const char *lp = getenv("DSFLIP_LOG");
    lg = fopen(lp ? lp : "/storage/dsflip/logs/dsflip.log", "w");
    if (lg) setvbuf(lg, 0, _IOLBF, 0);
    const char *top = getenv("DSFLIP_TOP"); if (!top) top = "DSI-2";

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || drmSetMaster(fd)) { LOG("[dsflip] no DRM master (sway running?) -> passthrough\n"); return; }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) { LOG("[dsflip] no atomic -> passthrough\n"); return; }
    drmModeRes *res = drmModeGetResources(fd);
    drmModePlaneRes *pres = drmModeGetPlaneResources(fd);
    int np = 0; uint32_t used_crtc = 0;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c->connection != DRM_MODE_CONNECTED || !c->count_modes || np == 2) { drmModeFreeConnector(c); continue; }
        char name[32]; snprintf(name, sizeof name, "DSI-%u", c->connector_type_id);
        panel *p = &P[strcmp(name, top) ? 1 : 0];
        if (p->conn) p = &P[p == &P[0] ? 1 : 0];
        p->conn = c->connector_id; p->mode = c->modes[0];
        drmModeEncoder *e = drmModeGetEncoder(fd, c->encoder_id ? c->encoder_id : c->encoders[0]);
        for (int k = 0; k < res->count_crtcs; k++)
            if ((e->possible_crtcs & (1u << k)) && !(used_crtc & (1u << k))) { p->crtc = res->crtcs[k]; p->crtc_idx = k; used_crtc |= 1u << k; break; }
        drmModeFreeEncoder(e);
        LOG("[dsflip] %s conn %u crtc %u -> %s\n", name, c->connector_id, p->crtc, p == &P[0] ? "top" : "bottom");
        drmModeFreeConnector(c);
        drmModeCreatePropertyBlob(fd, &p->mode, sizeof p->mode, &p->mode_blob);
        np++;
    }
    for (int i = 0; i < 2; i++)
        for (uint32_t k = 0; k < pres->count_planes && !P[i].plane; k++) {
            drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
            if ((pl->possible_crtcs & (1u << P[i].crtc_idx)) && propval(pl->plane_id, DRM_MODE_OBJECT_PLANE, "type") == 1)
                P[i].plane = pl->plane_id;
            drmModeFreePlane(pl);
        }
    if (np < 2 || !P[0].plane || !P[1].plane) { LOG("[dsflip] need 2 panels with primary planes -> passthrough\n"); return; }
    for (int i = 0; i < 2; i++) plane_props(&P[i]);

    /* modeset both with the black buffer; other planes off */
    blk.kind = K_BLACK; blk.nb = 1; blk.w = P[1].mode.hdisplay; blk.h = P[1].mode.vdisplay;
    if (mkbuf(&blk.b[0], blk.w, blk.h, DRM_FORMAT_XRGB8888, 32)) { LOG("[dsflip] dumb alloc failed\n"); return; }
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (uint32_t k = 0; k < pres->count_planes; k++) {
        uint32_t id = pres->planes[k];
        if (id == P[0].plane || id == P[1].plane) continue;
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
    }
    for (int i = 0; i < 2; i++) {
        panel *p = &P[i];
        drmModeAtomicAddProperty(r, p->conn, prop(p->conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), p->crtc);
        drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID"), p->mode_blob);
        drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
        add_fb(r, p, &blk.b[0]);
        p->scan = &blk.b[0];
    }
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
    drmModeAtomicFree(r);
    if (ret) { LOG("[dsflip] modeset failed: %s -> passthrough\n", strerror(-ret)); return; }

    /* can the top plane scale the 800x480 menu down itself? */
    dbuf t; memset(&t, 0, sizeof t);
    if (!mkbuf(&t, 800, 480, DRM_FORMAT_RGB565, 16)) {
        r = drmModeAtomicAlloc(); add_fb(r, &P[0], &t);
        menu_hw = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_TEST_ONLY, 0) == 0;
        drmModeAtomicFree(r); freebuf(&t);
    }
    LOG("[dsflip] menu scaling: %s\n", menu_hw ? "hardware" : "CPU nearest");

    /* toast plane: a free overlay plane that can go on the top panel's CRTC with ARGB8888 */
    for (int k = 0; k < 2; k++) if (mkbuf(&toast[k], TOAST_W, TOAST_H, DRM_FORMAT_ARGB8888, 32)) { toast[0].map = 0; break; }
    for (uint32_t k = 0; k < pres->count_planes && !tp_plane && toast[0].map; k++) {
        drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
        uint32_t id = pl->plane_id;
        int fmt = 0; for (uint32_t f = 0; f < pl->count_formats; f++) if (pl->formats[f] == DRM_FORMAT_ARGB8888) fmt = 1;
        if (id != P[0].plane && id != P[1].plane && fmt && (pl->possible_crtcs & (1u << P[0].crtc_idx)) &&
            propval(id, DRM_MODE_OBJECT_PLANE, "type") == 0) {
            uint32_t o = DRM_MODE_OBJECT_PLANE;
            tp_plane = id; tp_fb = prop(id, o, "FB_ID"); tp_crtc = prop(id, o, "CRTC_ID");
            tp_sx = prop(id, o, "SRC_X"); tp_sy = prop(id, o, "SRC_Y"); tp_sw = prop(id, o, "SRC_W"); tp_sh = prop(id, o, "SRC_H");
            tp_cx = prop(id, o, "CRTC_X"); tp_cy = prop(id, o, "CRTC_Y"); tp_cw = prop(id, o, "CRTC_W"); tp_ch = prop(id, o, "CRTC_H");
            toast_want = 1; toast_cur = 0;
            drmModeAtomicReq *t = drmModeAtomicAlloc(); add_toast(t);
            if (drmModeAtomicCommit(fd, t, DRM_MODE_ATOMIC_TEST_ONLY, 0)) tp_plane = 0;
            drmModeAtomicFree(t); toast_want = 0;
        }
        drmModeFreePlane(pl);
    }
    LOG("[dsflip] toast plane: %u\n", tp_plane);
    signal(SIGUSR2, on_usr2);
    const char *pm = getenv("DSFLIP_PACING");
    if (pm && !strcmp(pm, "immediate")) pacing_latch = 0;
    if (pacing_latch) tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd < 0) pacing_latch = 0;                                   /* no timer: never wait for a latch */
    LOG("[dsflip] pacing: %s\n", pacing_latch ? "latch (adaptive)" : "immediate");
    tdump_x = -1; if (getenv("DSFLIP_TOUCH_DUMP")) touch_dumps = atoi(getenv("DSFLIP_TOUCH_DUMP"));
    efd = eventfd(0, EFD_CLOEXEC);
    st_t0 = now_us();
    pthread_t th; pthread_create(&th, 0, presenter, 0);
    const char *inv = getenv("DSFLIP_TOUCH_INVERT");        /* "x", "y", "xy" or unset/"none" */
    if (inv) { touch_inv_x = strchr(inv, 'x') != 0; touch_inv_y = strchr(inv, 'y') != 0; }
    const char *tp = getenv("DSFLIP_TOUCH");
    pthread_create(&th, 0, touch_thread, (void *)(tp ? tp : "fe5e0000.i2c"));
    cursor_log = getenv("DSFLIP_CURSOR_LOG") != 0;
    if (getenv("DSFLIP_TAP_FIFO")) pthread_create(&th, 0, tap_fifo_thread, 0);
    ok = 1;
    LOG("[dsflip] ready: top plane %u, bottom plane %u\n", P[0].plane, P[1].plane);
}

/* ---------- SDL interception ---------- */
static struct { void *t; int w, h; } other[16]; static int nother;

static stex *find(void *t) { for (int i = 0; i < nt; i++) if (T[i].tex == t) return &T[i]; return 0; }

void *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags) {
    REAL(void *, SDL_CreateWindow, const char *, int, int, int, int, uint32_t);
    void *win = real(title, x, y, w, h, flags);
    if (!window) window = win;
    return win;
}

void *SDL_CreateTexture(void *rn, uint32_t fmt, int access, int w, int h) {
    REAL(void *, SDL_CreateTexture, void *, uint32_t, int, int, int);
    void *t = real(rn, fmt, access, w, h);
    int screen = fmt == FMT_ARGB8888 && ((w == 256 && h == 192) || (w == 512 && h == 384));
    int menu = fmt == FMT_RGB565 && w >= 256 && h >= 192;
    if (!ok || !t || access != 1 || !(screen || menu)) {
        if (t && nother < 16) { other[nother].t = t; other[nother].w = w; other[nother].h = h; nother++; }
        return t;
    }
    lock(&mu);
    stex *s = find(t);
    if (!s) for (int i = 0; i < nt && !s; i++) if (!T[i].tex) s = &T[i];
    if (!s && nt < 6) s = &T[nt++];
    if (s) {
        for (int k = 0; k < s->nb; k++)   /* recycled slot: free buffers that aren't on screen */
            if (s->b[k].state == FREE || s->b[k].state == WRITTEN || s->b[k].state == WRITING) freebuf(&s->b[k]);
            else memset(&s->b[k], 0, sizeof s->b[k]);   /* still scanning out: leak it rather than tear it down */
        s->tex = t; s->kind = screen ? K_SCREEN : K_MENU; s->w = w; s->h = h; s->writing = s->written = -1;
        s->nb = screen ? NBUF : 3;
        int bw = w, bh = h;
        if (!screen && !menu_hw) { bw = P[0].mode.hdisplay; bh = P[0].mode.vdisplay; }
        for (int k = 0; k < s->nb; k++)
            if (mkbuf(&s->b[k], bw, bh, screen ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_RGB565, screen ? 32 : 16)) {
                LOG("[dsflip] alloc %dx%d failed\n", bw, bh); s->tex = 0; break;
            }
        LOG("[dsflip] %s texture %p %dx%d -> %d dumb buffers %dx%d\n", screen ? "screen" : "menu", t, w, h, s->nb, bw, bh);
    }
    unlock(&mu);
    return t;
}

void SDL_DestroyTexture(void *t) {
    REAL(void, SDL_DestroyTexture, void *);
    lock(&mu);
    stex *s = find(t);
    if (s) s->tex = 0;
    unlock(&mu);
    real(t);
}

static int take_free(stex *s) {        /* with mu held */
    for (int k = 0; k < s->nb; k++) if (s->b[k].state == FREE) return k;
    return s->written >= 0 ? s->written : 0;   /* can't happen with enough buffers */
}

int SDL_LockTexture(void *t, const SDL_Rect *rc, void **px, int *pitch) {
    REAL(int, SDL_LockTexture, void *, const SDL_Rect *, void **, int *);
    stex *s = ok ? find(t) : 0;
    if (!s || s->kind != K_SCREEN || rc) return real(t, rc, px, pitch);
    lock(&mu);
    int k = take_free(s);
    s->b[k].state = WRITING; s->writing = k;
    unlock(&mu);
    *px = s->b[k].map; *pitch = (int)s->b[k].pitch;
    return 0;
}

static void finish_write(stex *s) {    /* with mu held */
    if (s->written >= 0 && s->b[s->written].state == WRITTEN) s->b[s->written].state = FREE;  /* never presented */
    s->b[s->writing].state = WRITTEN; s->written = s->writing; s->writing = -1;
}

void SDL_UnlockTexture(void *t) {
    REAL(void, SDL_UnlockTexture, void *);
    stex *s = ok ? find(t) : 0;
    if (!s || s->writing < 0) { real(t); return; }
    lock(&mu); finish_write(s); unlock(&mu);
}

int SDL_UpdateTexture(void *t, const SDL_Rect *rc, const void *pixels, int pitch) {
    REAL(int, SDL_UpdateTexture, void *, const SDL_Rect *, const void *, int);
    stex *s = ok ? find(t) : 0;
    if (!s || s->kind != K_MENU || rc) return real(t, rc, pixels, pitch);
    lock(&mu);
    int k = take_free(s);
    s->b[k].state = WRITING; s->writing = k;
    unlock(&mu);
    dbuf *b = &s->b[k];
    const uint8_t *src = pixels;
    if (b->w == (uint32_t)s->w && b->h == (uint32_t)s->h) {
        for (int y = 0; y < s->h; y++) memcpy((uint8_t *)b->map + y * b->pitch, src + y * pitch, s->w * 2);
    } else {                            /* nearest-neighbour downscale into the panel-sized buffer */
        static int xmap[2048];
        for (uint32_t x = 0; x < b->w; x++) xmap[x] = (int)(x * s->w / b->w);
        uint16_t row[2048];
        for (uint32_t y = 0; y < b->h; y++) {
            const uint16_t *sr = (const uint16_t *)(src + (y * s->h / b->h) * pitch);
            for (uint32_t x = 0; x < b->w; x++) row[x] = sr[xmap[x]];
            memcpy((uint8_t *)b->map + y * b->pitch, row, b->w * 2);
        }
    }
    lock(&mu); finish_write(s); unlock(&mu);
    return 0;
}

int SDL_RenderSetLogicalSize(void *rn, int w, int h) {
    REAL(int, SDL_RenderSetLogicalSize, void *, int, int);
    if (w > 0 && h > 0) { logical_w = w; logical_h = h; }
    return real(rn, w, h);
}

int SDL_RenderCopy(void *rn, void *t, const SDL_Rect *src, const SDL_Rect *dst) {
    REAL(int, SDL_RenderCopy, void *, void *, const SDL_Rect *, const SDL_Rect *);
    stex *s = ok ? find(t) : 0;
    if (!s) {                          /* other draws (overlays, stylus cursor) are not shown */
        if (cursor_log && dst)
            for (int i = 0; i < nother; i++) if (other[i].t == t && other[i].w == 32) {
                static int n; if (n++ % 10 == 0) LOG("[cursor] dst %d,%d %dx%d logical %dx%d\n", dst->x, dst->y, dst->w, dst->h, logical_w, logical_h);
            }
        return ok ? 0 : real(rn, t, src, dst);
    }
    if (s->kind == K_MENU) { pending_route[0] = s; pending_route[1] = &blk; return 0; }
    SDL_Rect d = dst ? *dst : (SDL_Rect){ 0, 0, logical_w, logical_h };
    int bottom = logical_w > logical_h ? d.x >= logical_w / 2 : d.y >= logical_h / 2;
    pending_route[bottom] = s; route_dst[bottom] = d;
    return 0;
}

int SDL_RenderClear(void *rn) {
    REAL(int, SDL_RenderClear, void *);
    return ok ? 0 : real(rn);
}

void SDL_RenderPresent(void *rn) {
    REAL(void, SDL_RenderPresent, void *);
    if (!ok) { real(rn); return; }
    long long tp = now_us();
    lock(&mu);
    st_present++;
    if (vbl_ref) {                       /* where in the refresh cycle did this frame arrive? */
        double ph = fmod((double)(tp - vbl_ref), period); if (ph < 0) ph += period;
        double ang = ph / period * 2 * M_PI, a = ph_n < 120 ? 1.0 / (ph_n + 1) : 0.01;
        ph_c += a * (cos(ang) - ph_c); ph_s += a * (sin(ang) - ph_s); ph_n++;
        ph_hist[(int)(ph / period * 16) & 15]++;
    }
    touch_rect_ok = pending_route[1] && pending_route[1]->kind == K_SCREEN;

    for (int i = 0; i < 2; i++) {
        stex *s = pending_route[i]; pending_route[i] = 0;
        if (!s) continue;
        dbuf *b;
        if (s->kind == K_BLACK) {
            b = &blk.b[0];
            if (P[i].scan == b || P[i].queued == b || P[i].ready == b) continue;
        } else {
            if (s->written < 0 || s->b[s->written].state != WRITTEN) continue;
            b = &s->b[s->written]; s->written = -1; b->state = READY;
        }
        if (P[i].ready) { release(P[i].ready); st_drop++; }     /* mailbox: newest wins */
        else if (!P[0].ready && !P[1].ready) ready_since = tp;
        P[i].ready = b;
    }
    unlock(&mu);
    uint64_t one = 1; if (write(efd, &one, 8) < 0) {}
    ra_frame();
}
