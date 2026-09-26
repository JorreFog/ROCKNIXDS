// kmstest: KMS bring-up for libdsflip (plan tasks 4 + 5), no DraStic involved.
//  1. lists planes (type, formats, possible CRTCs)
//  2. TEST_ONLY: which planes accept 512x384 -> 640x480 scaling (RG16 and XR24)
//  3. modesets both DSI CRTCs with RGB565 dumb buffers and flips a moving test pattern on both
//     in ONE atomic commit per vblank for KMSTEST_FRAMES frames, logging flip timestamps per CRTC
// Needs DRM master: sway must be stopped. Built as a shared object and run via
//   LD_PRELOAD=./kmstest.so /bin/true      (the constructor does the work, then _exit)
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define W 640
#define H 480
typedef struct { uint32_t fb, handle, pitch; uint64_t size; uint16_t *map; } buf_t;
typedef struct {
    uint32_t conn, crtc, plane, mode_blob, crtc_idx;
    drmModeModeInfo mode;
    buf_t b[3];
    int cur;
    int flips; long long last_us, sum_iv, min_iv, max_iv;
    long long ts[1200];
} panel_t;

static int fd;
static panel_t P[2];
static FILE *lg;

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

static int mkbuf(buf_t *b, uint32_t w, uint32_t h, uint32_t fourcc, uint32_t bpp) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = bpp };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;
    uint32_t hs[4] = { c.handle }, ps[4] = { c.pitch }, os[4] = { 0 };
    if (drmModeAddFB2(fd, w, h, fourcc, hs, ps, os, &b->fb, 0)) return -1;
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    b->map = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    return b->map == MAP_FAILED ? -1 : 0;
}

static void add_plane(drmModeAtomicReq *r, uint32_t plane, uint32_t crtc, uint32_t fb,
                      uint32_t sw, uint32_t sh, uint32_t cw, uint32_t ch) {
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "FB_ID"), fb);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), crtc);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_X"), 0);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_Y"), 0);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_W"), (uint64_t)sw << 16);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_H"), (uint64_t)sh << 16);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_X"), 0);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_Y"), 0);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_W"), cw);
    drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_H"), ch);
}
static void add_modeset(drmModeAtomicReq *r, panel_t *p) {
    drmModeAtomicAddProperty(r, p->conn, prop(p->conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), p->crtc);
    drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID"), p->mode_blob);
    drmModeAtomicAddProperty(r, p->crtc, prop(p->crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
}

static long long now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }

static void on_flip(int f, unsigned seq, unsigned sec, unsigned usec, unsigned crtc, void *u) {
    (void)f; (void)seq; (void)u;
    for (int i = 0; i < 2; i++) if (P[i].crtc == crtc) {
        panel_t *p = &P[i];
        long long t = sec * 1000000LL + usec;
        if (p->flips < 1200) p->ts[p->flips] = t;
        if (p->last_us) {
            long long iv = t - p->last_us; p->sum_iv += iv;
            if (!p->min_iv || iv < p->min_iv) p->min_iv = iv;
            if (iv > p->max_iv) p->max_iv = iv;
        }
        p->last_us = t; p->flips++;
    }
}

static void pattern(uint16_t *m, uint32_t pitch, int frame, int panel) {
    int bar = (frame * 4) % W;
    for (int y = 0; y < H; y++) {
        uint16_t *row = (uint16_t *)((char *)m + y * pitch);
        for (int x = 0; x < W; x++) {
            uint16_t c = (uint16_t)(((x >> 4) & 31) | (((y >> 3) & 63) << 5) | ((panel ? 24 : 6) << 11));
            if (x >= bar && x < bar + 16) c = 0xFFFF;
            row[x] = c;
        }
    }
}

__attribute__((constructor)) static void run(void) {
    const char *lp = getenv("KMSTEST_LOG");
    lg = fopen(lp ? lp : "/storage/dsflip/logs/kmstest.log", "w");
    if (!lg) lg = stderr;
    setvbuf(lg, 0, _IOLBF, 0);
    int frames = getenv("KMSTEST_FRAMES") ? atoi(getenv("KMSTEST_FRAMES")) : 600;

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || drmSetMaster(fd)) { fprintf(lg, "FAIL: open/master (is sway running?)\n"); _exit(1); }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) { fprintf(lg, "FAIL: no atomic\n"); _exit(1); }

    drmModeRes *res = drmModeGetResources(fd);
    drmModePlaneRes *pres = drmModeGetPlaneResources(fd);

    /* 1. planes */
    for (uint32_t i = 0; i < pres->count_planes; i++) {
        drmModePlane *pl = drmModeGetPlane(fd, pres->planes[i]);
        char fm[512] = ""; size_t n = 0;
        for (uint32_t k = 0; k < pl->count_formats && n < sizeof fm - 8; k++)
            n += snprintf(fm + n, sizeof fm - n, "%.4s ", (char *)&pl->formats[k]);
        fprintf(lg, "plane %u type=%llu possible_crtcs=0x%x zpos=%llu formats: %s\n", pl->plane_id,
                (unsigned long long)propval(pl->plane_id, DRM_MODE_OBJECT_PLANE, "type"), pl->possible_crtcs,
                (unsigned long long)propval(pl->plane_id, DRM_MODE_OBJECT_PLANE, "zpos"), fm);
        drmModeFreePlane(pl);
    }

    /* connectors: DSI-1, DSI-2 in connector order; P[0] = the one named in KMSTEST_TOP (default DSI-2) */
    int np = 0;
    for (int i = 0; i < res->count_connectors && np < 2; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c->connection != DRM_MODE_CONNECTED || !c->count_modes) { drmModeFreeConnector(c); continue; }
        panel_t *p = &P[np];
        p->conn = c->connector_id; p->mode = c->modes[0];
        drmModeEncoder *e = drmModeGetEncoder(fd, c->encoder_id ? c->encoder_id : c->encoders[0]);
        for (int k = 0; k < res->count_crtcs; k++)
            if (e->possible_crtcs & (1u << k) && (np == 0 || res->crtcs[k] != P[0].crtc)) { p->crtc = res->crtcs[k]; p->crtc_idx = k; break; }
        fprintf(lg, "connector %u type=%u-%u crtc=%u mode %s %dx%d@%d clock=%d\n", c->connector_id, c->connector_type,
                c->connector_type_id, p->crtc, p->mode.name, p->mode.hdisplay, p->mode.vdisplay, p->mode.vrefresh, p->mode.clock);
        drmModeFreeEncoder(e); drmModeFreeConnector(c);
        drmModeCreatePropertyBlob(fd, &p->mode, sizeof p->mode, &p->mode_blob);
        np++;
    }
    /* primary plane per CRTC: prefer type=1 (primary) that can drive it */
    for (int i = 0; i < np; i++)
        for (uint32_t k = 0; k < pres->count_planes && !P[i].plane; k++) {
            drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
            if ((pl->possible_crtcs & (1u << P[i].crtc_idx)) && propval(pl->plane_id, DRM_MODE_OBJECT_PLANE, "type") == 1 &&
                (i == 0 || pl->plane_id != P[0].plane)) P[i].plane = pl->plane_id;
            drmModeFreePlane(pl);
        }
    fprintf(lg, "panels: [0] conn %u crtc %u plane %u | [1] conn %u crtc %u plane %u\n",
            P[0].conn, P[0].crtc, P[0].plane, P[1].conn, P[1].crtc, P[1].plane);
    if (np < 2) { fprintf(lg, "FAIL: %d connected panels\n", np); _exit(1); }

    /* 2. TEST_ONLY scaling probes: 512x384 -> 640x480 on every plane usable on panel 0's CRTC */
    buf_t s16, s32;
    if (mkbuf(&s16, 512, 384, DRM_FORMAT_RGB565, 16) || mkbuf(&s32, 512, 384, DRM_FORMAT_XRGB8888, 32)) { fprintf(lg, "FAIL: dumb 512x384\n"); _exit(1); }
    for (uint32_t k = 0; k < pres->count_planes; k++) {
        drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
        for (int pi = 0; pi < 2; pi++) {
            if (!(pl->possible_crtcs & (1u << P[pi].crtc_idx))) continue;
            for (int f = 0; f < 2; f++) {
                drmModeAtomicReq *r = drmModeAtomicAlloc();
                add_modeset(r, &P[pi]);
                add_plane(r, pl->plane_id, P[pi].crtc, f ? s32.fb : s16.fb, 512, 384, W, H);
                int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
                fprintf(lg, "scale-test plane %u crtc %u %s 512x384->640x480: %s\n", pl->plane_id, P[pi].crtc,
                        f ? "XR24" : "RG16", ret ? strerror(-ret) : "OK");
                drmModeAtomicFree(r);
            }
        }
        drmModeFreePlane(pl);
    }

    /* 3. triple-buffered RGB565 on both panels, one commit for both per vblank */
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 3; k++)
            if (mkbuf(&P[i].b[k], W, H, DRM_FORMAT_RGB565, 16)) { fprintf(lg, "FAIL: dumb %d/%d\n", i, k); _exit(1); }
    /* disable every other plane on our CRTCs (sway's config may leave them bound) */
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (uint32_t k = 0; k < pres->count_planes; k++) {
        uint32_t id = pres->planes[k];
        if (id == P[0].plane || id == P[1].plane) continue;
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
        drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
    }
    for (int i = 0; i < 2; i++) {
        pattern(P[i].b[0].map, P[i].b[0].pitch, 0, i);
        add_modeset(r, &P[i]);
        add_plane(r, P[i].plane, P[i].crtc, P[i].b[0].fb, W, H, W, H);
    }
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
    drmModeAtomicFree(r);
    fprintf(lg, "modeset both: %s\n", ret ? strerror(-ret) : "OK");
    if (ret) _exit(1);

    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    long long t0 = now_us(), draw_us = 0, commit_us = 0; int busy = 0;
    for (int f = 1; f <= frames; f++) {
        long long a = now_us();
        for (int i = 0; i < 2; i++) { P[i].cur = (P[i].cur + 1) % 3; pattern(P[i].b[P[i].cur].map, P[i].b[P[i].cur].pitch, f, i); }
        long long b = now_us(); draw_us += b - a;
        r = drmModeAtomicAlloc();
        for (int i = 0; i < 2; i++)
            drmModeAtomicAddProperty(r, P[i].plane, prop(P[i].plane, DRM_MODE_OBJECT_PLANE, "FB_ID"), P[i].b[P[i].cur].fb);
        ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, 0);
        drmModeAtomicFree(r);
        commit_us += now_us() - b;
        if (ret) { busy++; continue; }
        /* wait until both CRTCs have reported this flip */
        int want0 = P[0].flips + 1, want1 = P[1].flips + 1;
        while (P[0].flips < want0 || P[1].flips < want1) {
            struct pollfd pf = { .fd = fd, .events = POLLIN };
            if (poll(&pf, 1, 100) <= 0) { fprintf(lg, "timeout waiting for flip at frame %d\n", f); break; }
            drmHandleEvent(fd, &ev);
        }
    }
    long long el = now_us() - t0;
    for (int i = 0; i < 2; i++)
        fprintf(lg, "panel %d: flips=%d avg interval %.1f us (min %lld max %lld)\n", i, P[i].flips,
                P[i].flips > 1 ? (double)P[i].sum_iv / (P[i].flips - 1) : 0.0, P[i].min_iv, P[i].max_iv);
    int n = P[0].flips < P[1].flips ? P[0].flips : P[1].flips; if (n > 1200) n = 1200;
    long long dmin = 1 << 30, dmax = -(1 << 30), dsum = 0;
    for (int k = 0; k < n; k++) { long long d = P[1].ts[k] - P[0].ts[k]; dsum += d; if (d < dmin) dmin = d; if (d > dmax) dmax = d; }
    fprintf(lg, "flip phase panel1-panel0: avg %lld us min %lld max %lld\n", n ? dsum / n : 0, dmin, dmax);
    fprintf(lg, "frames=%d elapsed=%.2f s -> %.2f fps; per frame: draw %.2f ms (CPU write to WC dumb x2) commit %.3f ms; busy=%d\n",
            frames, el / 1e6, frames * 1e6 / el, draw_us / 1e3 / frames, commit_us / 1e3 / frames, busy);
    _exit(0);
}
