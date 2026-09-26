// libdsflip.so v1: DraStic's two DS screens scanned out directly on the RG DS's two DSI panels.
//
// DraStic renders each DS screen into an SDL streaming texture (ARGB8888, 256x192 or 512x384)
// via SDL_LockTexture/UnlockTexture, draws both with SDL_RenderCopy, then SDL_RenderPresent.
// This hook hands DraStic *DRM dumb buffers* on Lock (XRGB8888 has the same memory layout as
// SDL's ARGB8888), so DraStic writes straight into scanout memory: no upload, no GL, no copy.
// The VOP2 display controller scales each buffer to 640x480 in hardware.
//
// A presenter thread owns the atomic commits: both panels' new framebuffers go in ONE commit
// (same emulated frame on both screens), mailbox-style: RenderPresent never blocks, a frame that
// is superseded before the next vblank is dropped, and a panel with no new frame keeps its buffer.
//
// Requirements: DRM master (sway stopped), SDL_VIDEODRIVER=dummy. Env:
//   DSFLIP_TOP=DSI-2        connector that shows DraStic's left half (the DS top screen)
//   DSFLIP_LOG=path         stats log (default /storage/dsflip/logs/dsflip.log)
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
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

typedef struct { int x, y, w, h; } SDL_Rect;
#define NBUF 4
enum { FREE, WRITING, WRITTEN, READY, QUEUED, SCANOUT };

typedef struct { uint32_t fb, handle, pitch; uint64_t size; void *map; int state; } dbuf;
typedef struct {                        /* one DraStic screen texture */
    void *tex; int w, h;
    dbuf b[NBUF];
    int writing, written;               /* index being written / last completed write, -1 if none */
} stex;
typedef struct {                        /* one panel */
    uint32_t conn, crtc, crtc_idx, plane, mode_blob;
    drmModeModeInfo mode;
    uint32_t p_fb, p_crtc, p_sx, p_sy, p_sw, p_sh, p_cx, p_cy, p_cw, p_ch;
    stex *src;                          /* texture currently routed here */
    dbuf *ready, *queued, *scan;
    long long last_flip;
} panel;

static int fd = -1, efd = -1, ok;
static FILE *lg;
static panel P[2];                      /* [0] = top (DraStic's left half), [1] = bottom */
static stex T[4]; static int nt;
/* spinlock, not pthread_mutex_t: the desktop's x86 glibc headers give the mutex the wrong size for
 * aarch64. Critical sections are a few loads/stores (plus one non-blocking commit ioctl). */
static int mu;
static void lock(int *l) { while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) sched_yield(); }
static void unlock(int *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
static int logical_w = 512;
static stex *pending_route[2];          /* routes recorded by RenderCopy during this frame */
/* stats */
static int st_present, st_commit, st_drop, st_busy, st_flips[2];
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
static int mkbuf(dbuf *b, uint32_t w, uint32_t h) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;
    uint32_t hs[4] = { c.handle }, ps[4] = { c.pitch }, os[4] = { 0 };
    if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, hs, ps, os, &b->fb, 0)) return -1;
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    b->map = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (b->map == MAP_FAILED) return -1;
    memset(b->map, 0, c.size);
    b->state = FREE;
    return 0;
}

static void plane_props(panel *p) {
    uint32_t o = DRM_MODE_OBJECT_PLANE, id = p->plane;
    p->p_fb = prop(id, o, "FB_ID"); p->p_crtc = prop(id, o, "CRTC_ID");
    p->p_sx = prop(id, o, "SRC_X"); p->p_sy = prop(id, o, "SRC_Y"); p->p_sw = prop(id, o, "SRC_W"); p->p_sh = prop(id, o, "SRC_H");
    p->p_cx = prop(id, o, "CRTC_X"); p->p_cy = prop(id, o, "CRTC_Y"); p->p_cw = prop(id, o, "CRTC_W"); p->p_ch = prop(id, o, "CRTC_H");
}
static void add_fb(drmModeAtomicReq *r, panel *p, dbuf *b, int w, int h) {
    drmModeAtomicAddProperty(r, p->plane, p->p_fb, b->fb);
    drmModeAtomicAddProperty(r, p->plane, p->p_crtc, p->crtc);
    drmModeAtomicAddProperty(r, p->plane, p->p_sx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sw, (uint64_t)w << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_sh, (uint64_t)h << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_cx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cw, p->mode.hdisplay);
    drmModeAtomicAddProperty(r, p->plane, p->p_ch, p->mode.vdisplay);
}

/* ---------- presenter ---------- */
static int commit_pending;

static void try_commit(void) {         /* called with mu held */
    if (commit_pending) return;
    int any = 0;
    for (int i = 0; i < 2; i++) if (P[i].ready) any = 1;
    if (!any) return;
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (int i = 0; i < 2; i++) if (P[i].ready) add_fb(r, &P[i], P[i].ready, P[i].src->w, P[i].src->h);
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, P);
    drmModeAtomicFree(r);
    if (ret) { st_busy++; return; }
    for (int i = 0; i < 2; i++) if (P[i].ready) { P[i].queued = P[i].ready; P[i].queued->state = QUEUED; P[i].ready = 0; }
    int n = 0; for (int i = 0; i < 2; i++) if (P[i].queued) n++;
    commit_pending = n;
    st_commit++;
}

static void on_flip(int f, unsigned seq, unsigned sec, unsigned usec, unsigned crtc, void *u) {
    (void)f; (void)seq; (void)u;
    lock(&mu);
    for (int i = 0; i < 2; i++) if (P[i].crtc == crtc && P[i].queued) {
        long long t = sec * 1000000LL + usec;
        if (P[i].last_flip && t - P[i].last_flip > st_iv_max[i]) st_iv_max[i] = t - P[i].last_flip;
        P[i].last_flip = t; st_flips[i]++;
        if (P[i].scan) P[i].scan->state = FREE;
        P[i].scan = P[i].queued; P[i].scan->state = SCANOUT; P[i].queued = 0;
        if (commit_pending > 0) commit_pending--;
    }
    if (!commit_pending) try_commit();
    unlock(&mu);
}

static void *presenter(void *a) {
    (void)a;
    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    struct pollfd pf[2] = { { .fd = fd, .events = POLLIN }, { .fd = efd, .events = POLLIN } };
    for (;;) {
        if (poll(pf, 2, 1000) < 0) continue;
        if (pf[0].revents & POLLIN) drmHandleEvent(fd, &ev);
        if (pf[1].revents & POLLIN) {
            uint64_t v; if (read(efd, &v, 8) < 0) {}
            lock(&mu); try_commit(); unlock(&mu);
        }
        long long t = now_us();
        if (t - st_t0 >= 1000000) {
            lock(&mu);
            LOG("[dsflip] present/s=%.1f commits=%d dropped=%d busy=%d flips top=%d bot=%d max-iv top=%lld bot=%lld us\n",
                st_present * 1e6 / (t - st_t0), st_commit, st_drop, st_busy, st_flips[0], st_flips[1], st_iv_max[0], st_iv_max[1]);
            st_present = st_commit = st_drop = st_busy = st_flips[0] = st_flips[1] = 0; st_iv_max[0] = st_iv_max[1] = 0; st_t0 = t;
            unlock(&mu);
        }
    }
    return 0;
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

    /* modeset both with a black 640x480 buffer; other planes off */
    static dbuf black[2];
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (uint32_t k = 0; k < pres->count_planes; k++) {
        uint32_t id = pres->planes[k];
        if (id == P[0].plane || id == P[1].plane) continue;
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
    }
    for (int i = 0; i < 2; i++) {
        panel *p = &P[i];
        if (mkbuf(&black[i], p->mode.hdisplay, p->mode.vdisplay)) { LOG("[dsflip] dumb alloc failed\n"); return; }
        drmModeAtomicAddProperty(r, p->conn, prop(p->conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), p->crtc);
        drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID"), p->mode_blob);
        drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
        add_fb(r, p, &black[i], p->mode.hdisplay, p->mode.vdisplay);
    }
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
    drmModeAtomicFree(r);
    if (ret) { LOG("[dsflip] modeset failed: %s -> passthrough\n", strerror(-ret)); return; }
    efd = eventfd(0, EFD_CLOEXEC);
    st_t0 = now_us();
    pthread_t th; pthread_create(&th, 0, presenter, 0);
    ok = 1;
    LOG("[dsflip] ready: top plane %u, bottom plane %u\n", P[0].plane, P[1].plane);
}

/* ---------- SDL interception ---------- */
static stex *find(void *t) { for (int i = 0; i < nt; i++) if (T[i].tex == t) return &T[i]; return 0; }

void *SDL_CreateTexture(void *rn, uint32_t fmt, int access, int w, int h) {
    REAL(void *, SDL_CreateTexture, void *, uint32_t, int, int, int);
    void *t = real(rn, fmt, access, w, h);
    /* SDL_PIXELFORMAT_ARGB8888 = 0x16362004, streaming access = 1, DS screen sizes only */
    if (ok && t && access == 1 && fmt == 0x16362004 && ((w == 256 && h == 192) || (w == 512 && h == 384))) {
        lock(&mu);
        stex *s = find(t);
        if (!s) {                       /* reuse a slot of a destroyed texture, else a new one */
            for (int i = 0; i < nt && !s; i++) if (!T[i].tex) s = &T[i];
            if (!s && nt < 4) s = &T[nt++];
        }
        if (s) {
            for (int k = 0; k < NBUF; k++) if (s->b[k].map && (s->w != w || s->h != h)) {
                /* size changed (hires toggle): old buffers leak on purpose if still on screen */
                if (s->b[k].state == FREE) { munmap(s->b[k].map, s->b[k].size); drmModeRmFB(fd, s->b[k].fb); }
                memset(&s->b[k], 0, sizeof s->b[k]);
            }
            s->tex = t; s->w = w; s->h = h; s->writing = s->written = -1;
            for (int k = 0; k < NBUF; k++) if (!s->b[k].map && mkbuf(&s->b[k], w, h)) { LOG("[dsflip] alloc %dx%d failed\n", w, h); s->tex = 0; break; }
            LOG("[dsflip] screen texture %p %dx%d -> %d dumb buffers\n", t, w, h, NBUF);
        }
        unlock(&mu);
    }
    return t;
}

void SDL_DestroyTexture(void *t) {
    REAL(void, SDL_DestroyTexture, void *);
    lock(&mu);
    stex *s = find(t);
    if (s) { s->tex = 0; for (int i = 0; i < 2; i++) if (P[i].src == s) P[i].src = 0; }
    unlock(&mu);
    real(t);
}

int SDL_LockTexture(void *t, const SDL_Rect *rc, void **px, int *pitch) {
    REAL(int, SDL_LockTexture, void *, const SDL_Rect *, void **, int *);
    stex *s = ok ? find(t) : 0;
    if (!s || rc) return real(t, rc, px, pitch);
    lock(&mu);
    int k;
    for (k = 0; k < NBUF; k++) if (s->b[k].state == FREE) break;
    if (k == NBUF) {                    /* can't happen with 4 buffers; recycle the oldest written */
        k = s->written >= 0 ? s->written : 0;
    }
    s->b[k].state = WRITING; s->writing = k;
    unlock(&mu);
    *px = s->b[k].map; *pitch = (int)s->b[k].pitch;
    return 0;
}

void SDL_UnlockTexture(void *t) {
    REAL(void, SDL_UnlockTexture, void *);
    stex *s = ok ? find(t) : 0;
    if (!s || s->writing < 0) { real(t); return; }
    lock(&mu);
    if (s->written >= 0 && s->b[s->written].state == WRITTEN) s->b[s->written].state = FREE;  /* never presented */
    s->b[s->writing].state = WRITTEN; s->written = s->writing; s->writing = -1;
    unlock(&mu);
}

int SDL_RenderSetLogicalSize(void *rn, int w, int h) {
    REAL(int, SDL_RenderSetLogicalSize, void *, int, int);
    if (w > 0) logical_w = w;
    return real(rn, w, h);
}

int SDL_RenderCopy(void *rn, void *t, const SDL_Rect *src, const SDL_Rect *dst) {
    REAL(int, SDL_RenderCopy, void *, void *, const SDL_Rect *, const SDL_Rect *);
    stex *s = ok ? find(t) : 0;
    if (!s) return ok ? 0 : real(rn, t, src, dst);   /* v1: other draws (menus/overlays) are not shown */
    int half = dst && dst->x >= logical_w / 2;
    pending_route[half] = s;
    return 0;
}

int SDL_RenderClear(void *rn) {
    REAL(int, SDL_RenderClear, void *);
    return ok ? 0 : real(rn);
}

void SDL_RenderPresent(void *rn) {
    REAL(void, SDL_RenderPresent, void *);
    if (!ok) { real(rn); return; }
    lock(&mu);
    st_present++;
    for (int i = 0; i < 2; i++) {
        stex *s = pending_route[i]; pending_route[i] = 0;
        if (!s || s->written < 0) continue;
        dbuf *b = &s->b[s->written];
        if (b->state != WRITTEN) continue;
        if (P[i].ready) { P[i].ready->state = FREE; st_drop++; }     /* mailbox: newest wins */
        P[i].ready = b; b->state = READY; P[i].src = s;
        s->written = -1;
    }
    unlock(&mu);
    uint64_t one = 1; if (write(efd, &one, 8) < 0) {}
}
