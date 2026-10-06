// plat_kms.c: the RG DS's two panels straight through DRM/KMS, the way SuperDrastic drives them (no compositor,
// no GL): a dumb buffer pair per panel, each panel's new frame in an atomic commit of its own, paced by the top
// panel's flip events (the two panels refresh out of step: see k_present).
// Each frame is drawn at the logical size and scaled up by whole pixels on the CPU (sharp pixels, the panel's own
// resolution: 2x for 640x480, 3x for the RG DS Plus' 1024x768).
//
// Needs DRM master: ES and sway stopped (device/session.sh), or the console switched away from sway's VT.
// Env: DK_CARD=/dev/dri/cardN, DK_TOP=DSI-2 (the connector that is the top screen), DK_ROTATE_BOTTOM=1 (spare).
#define _GNU_SOURCE
#include "plat.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

void evdev_init(int pw[2], int ph[2], int scale[2], int ox[2], int oy[2]);
void evdev_poll(Input *in);
void evdev_shutdown(void);

typedef struct { uint32_t fb, handle, pitch; uint64_t size; uint32_t *map; } Buf;
typedef struct {
    uint32_t conn, crtc, crtc_idx, plane, mode_blob;
    drmModeModeInfo mode;
    uint32_t p_fb, p_crtc, p_sx, p_sy, p_sw, p_sh, p_cx, p_cy, p_cw, p_ch, c_crtc, c_mode, c_active;
    Buf b[2]; int front;
    int w, h, scale, ox, oy, lw, lh;
    uint64_t last_hash; int have_frame;
} Panel;

static int fd = -1, pending;
static Panel P[2];
static uint32_t *rowbuf;

/* pacing, counted for DK_PROFILE (k_pace): a panel that got a new frame at two presents in a row must flip at two
 * refreshes in a row; every refresh in between showed the old frame again (a late frame: a stutter) */
static struct {
    unsigned presents, idle, flips[2], late[2], busy[2], both;
    double wait, scale, commit;             /* seconds in each part of k_present */
} K;
static unsigned flip_seq[2]; static double flip_t[2];
static double frame_clock;                  /* k_frame_clock: the top panel's flip this present waited for */
static int in_prev[2]; static unsigned seq_prev[2];
static double now_s(void) { return plat_now(); }
static double commit_at[2], flip_prev_t[2]; static int trace_late = -1;
static double tr_enter, tr_waited, tr_hash0, tr_scaled0, tr_exit, tr_prev_exit;   /* the last present, for the late report */
static void count_flip(int i) {
    K.flips[i]++;
    if (in_prev[i] && flip_seq[i] - seq_prev[i] > 1 && flip_seq[i] - seq_prev[i] < 600) {
        K.late[i] += flip_seq[i] - seq_prev[i] - 1;
        if (trace_late < 0) trace_late = getenv("DK_PROFILE") && atoi(getenv("DK_PROFILE")) > 1;
        if (trace_late) plat_log("kms: %s frame late by %u: committed %.2f ms after the flip before it (the panel's clock), flips %.2f ms apart; "
                                 "that present: left the one before at %.2f, entered %.2f, waited until %.2f, hashed %.2f, scaled %.2f, left %.2f",
                                 i ? "bottom" : "top", flip_seq[i] - seq_prev[i] - 1, (commit_at[i] - flip_prev_t[i]) * 1000, (flip_t[i] - flip_prev_t[i]) * 1000,
                                 (tr_prev_exit - flip_prev_t[i]) * 1000, (tr_enter - flip_prev_t[i]) * 1000, (tr_waited - flip_prev_t[i]) * 1000,
                                 (tr_hash0 - flip_prev_t[i]) * 1000, (tr_scaled0 - flip_prev_t[i]) * 1000, (tr_exit - flip_prev_t[i]) * 1000);
    }
    seq_prev[i] = flip_seq[i]; in_prev[i] = 1; flip_prev_t[i] = flip_t[i];
}
/* the bottom panel's refresh after the top's, from their last flips (ms) */
static double panel_phase(void) {
    double hz = P[0].mode.vrefresh ? P[0].mode.vrefresh : 60, d = fmod(flip_t[1] - flip_t[0], 1 / hz);
    if (d < 0) d += 1 / hz;
    return flip_t[0] > 0 && flip_t[1] > 0 ? d * 1000 : -1;
}

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

static int mkbuf(Buf *b, uint32_t w, uint32_t h) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) return -1;
    b->handle = c.handle; b->pitch = c.pitch; b->size = c.size;
    uint32_t hs[4] = { c.handle }, ps[4] = { c.pitch }, os[4] = { 0 };
    if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, hs, ps, os, &b->fb, 0)) return -1;
    struct drm_mode_map_dumb m = { .handle = c.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    b->map = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (b->map == MAP_FAILED) { b->map = 0; return -1; }
    memset(b->map, 0, c.size);
    return 0;
}
static void freebuf(Buf *b) {
    if (!b->map) return;
    munmap(b->map, b->size); drmModeRmFB(fd, b->fb);
    struct drm_mode_destroy_dumb d = { .handle = b->handle }; drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    memset(b, 0, sizeof *b);
}

static void add_plane(drmModeAtomicReq *r, Panel *p, Buf *b) {
    drmModeAtomicAddProperty(r, p->plane, p->p_fb, b->fb);
    drmModeAtomicAddProperty(r, p->plane, p->p_crtc, p->crtc);
    drmModeAtomicAddProperty(r, p->plane, p->p_sx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_sw, (uint64_t)p->w << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_sh, (uint64_t)p->h << 16);
    drmModeAtomicAddProperty(r, p->plane, p->p_cx, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cy, 0);
    drmModeAtomicAddProperty(r, p->plane, p->p_cw, p->w);
    drmModeAtomicAddProperty(r, p->plane, p->p_ch, p->h);
}

/* the first card with two connected DSI panels (card0 on ROCKNIX) */
static int open_card(void) {
    const char *c = getenv("DK_CARD");
    if (c && *c) return open(c, O_RDWR | O_CLOEXEC);
    glob_t g; int found = -1;
    if (glob("/dev/dri/card*", 0, 0, &g)) return open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    for (size_t i = 0; i < g.gl_pathc && found < 0; i++) {
        int f = open(g.gl_pathv[i], O_RDWR | O_CLOEXEC); if (f < 0) continue;
        drmModeRes *res = drmModeGetResources(f); int n = 0;
        for (int k = 0; res && k < res->count_connectors; k++) {
            drmModeConnector *cn = drmModeGetConnector(f, res->connectors[k]);
            if (cn && cn->connection == DRM_MODE_CONNECTED && cn->count_modes) n++;
            drmModeFreeConnector(cn);
        }
        drmModeFreeResources(res);
        if (n >= 2) found = f; else close(f);
    }
    globfree(&g);
    return found >= 0 ? found : open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
}

/* the display controller's interrupts (an underrun storm after sway let go: see SuperDrastic's dsflip.c) */
static long long vop_irqs(void) {
    FILE *f = fopen("/proc/interrupts", "r"); if (!f) return -1;
    char line[512]; long long n = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, ".vop")) continue;
        char *p = strchr(line, ':'); if (!p) break;
        n = 0; p++;
        for (;;) { char *e; long long v = strtoll(p, &e, 10); if (e == p) break; n += v; p = e; }
        break;
    }
    fclose(f);
    return n;
}

/* Each commit carries its number, and only the event of the commit still waited for counts: an event that came after
 * its wait was given up (a panel coming on after the modeset can take a while) must not stand for the next flip, or
 * the next commit finds the panel busy. */
static unsigned commit_no[2];
static void on_flip(int f, unsigned seq, unsigned sec, unsigned usec, unsigned crtc, void *u) {
    (void)f;
    for (int i = 0; i < 2; i++) if (P[i].crtc == crtc) {
        if ((uintptr_t)u != commit_no[i]) continue;      /* a stale one */
        pending &= ~(1 << i);
        flip_seq[i] = seq; flip_t[i] = sec + usec * 1e-6;
        count_flip(i);
    }
}
/* until the panels in `mask` have flipped (events of the other panel are taken as they come). give_up: after
 * timeout_ms without an event the flips count as done (a lost event must not hang the game); without it the wait
 * just ends, the flips still on their way */
static void wait_flips(int mask, int timeout_ms, int give_up) {
    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    double end = plat_now() + timeout_ms / 1000.0;
    while (pending & mask) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        int left = give_up ? timeout_ms : (int)((end - plat_now()) * 1000 + 0.999);
        int r = left > 0 ? poll(&pf, 1, left) : 0;
        if (r <= 0) { if (give_up) { pending &= ~mask; for (int i = 0; i < 2; i++) if (mask & (1 << i)) flip_t[i] = 0; } break; }
        drmHandleEvent(fd, &ev);
    }
}
/* the events that are already there */
static void take_flips(void) {
    drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
    struct pollfd pf = { .fd = fd, .events = POLLIN };
    while (pending && poll(&pf, 1, 0) > 0) drmHandleEvent(fd, &ev);
}

static int k_init(PlatInfo *info) {
    fd = open_card();
    if (fd < 0) { plat_log("kms: no display device"); return -1; }
    int master = -1;
    for (int k = 0; k < 300 && (master = drmSetMaster(fd)) != 0; k++) usleep(10000);   /* seatd may still be letting go */
    if (master) { plat_log("kms: the display stayed busy (no DRM master)"); close(fd); fd = -1; return -1; }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) { plat_log("kms: no atomic modesetting"); close(fd); fd = -1; return -1; }
    drmModeRes *res = drmModeGetResources(fd);
    drmModePlaneRes *pres = drmModeGetPlaneResources(fd);
    if (!res || !pres) { plat_log("kms: no resources"); close(fd); fd = -1; return -1; }
    const char *top = getenv("DK_TOP"); if (!top) top = "DSI-2";
    int np = 0; uint32_t used = 0;
    memset(P, 0, sizeof P);
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c || c->connection != DRM_MODE_CONNECTED || !c->count_modes || np == 2) { drmModeFreeConnector(c); continue; }
        char name[32]; snprintf(name, sizeof name, "%s-%u", c->connector_type == DRM_MODE_CONNECTOR_DSI ? "DSI" : "CONN", c->connector_type_id);
        Panel *p = &P[strcmp(name, top) ? 1 : 0];
        if (p->conn) p = &P[p == &P[0] ? 1 : 0];
        p->conn = c->connector_id; p->mode = c->modes[0];
        for (int m = 0; m < c->count_modes; m++) if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { p->mode = c->modes[m]; break; }
        drmModeEncoder *e = drmModeGetEncoder(fd, c->encoder_id ? c->encoder_id : c->encoders[0]);
        for (int k = 0; e && k < res->count_crtcs; k++)
            if ((e->possible_crtcs & (1u << k)) && !(used & (1u << k))) { p->crtc = res->crtcs[k]; p->crtc_idx = (uint32_t)k; used |= 1u << k; break; }
        drmModeFreeEncoder(e);
        plat_log("kms: %s %dx%d@%d -> %s", name, p->mode.hdisplay, p->mode.vdisplay, p->mode.vrefresh, p == &P[0] ? "top" : "bottom");
        drmModeFreeConnector(c);
        drmModeCreatePropertyBlob(fd, &p->mode, sizeof p->mode, &p->mode_blob);
        np++;
    }
    for (int i = 0; i < 2; i++)
        for (uint32_t k = 0; k < pres->count_planes && !P[i].plane; k++) {
            drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
            if (pl && (pl->possible_crtcs & (1u << P[i].crtc_idx)) && propval(pl->plane_id, DRM_MODE_OBJECT_PLANE, "type") == DRM_PLANE_TYPE_PRIMARY)
                P[i].plane = pl->plane_id;
            drmModeFreePlane(pl);
        }
    if (np < 2 || !P[0].plane || !P[1].plane || !P[0].crtc || !P[1].crtc) {
        plat_log("kms: need two panels with primary planes (found %d)", np);
        drmModeFreeResources(res); drmModeFreePlaneResources(pres); drmDropMaster(fd); close(fd); fd = -1;
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        Panel *p = &P[i];
        uint32_t o = DRM_MODE_OBJECT_PLANE, id = p->plane;
        p->p_fb = prop(id, o, "FB_ID"); p->p_crtc = prop(id, o, "CRTC_ID");
        p->p_sx = prop(id, o, "SRC_X"); p->p_sy = prop(id, o, "SRC_Y"); p->p_sw = prop(id, o, "SRC_W"); p->p_sh = prop(id, o, "SRC_H");
        p->p_cx = prop(id, o, "CRTC_X"); p->p_cy = prop(id, o, "CRTC_Y"); p->p_cw = prop(id, o, "CRTC_W"); p->p_ch = prop(id, o, "CRTC_H");
        p->c_crtc = prop(p->conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
        p->c_mode = prop(p->crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID"); p->c_active = prop(p->crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE");
        p->w = p->mode.hdisplay; p->h = p->mode.vdisplay;
        /* whole-pixel scale: as big as fits 320x240 */
        p->scale = MAX(1, MIN(p->w / 320, p->h / 240));
        p->lw = p->w / p->scale; p->lh = p->h / p->scale;
        p->ox = (p->w - p->lw * p->scale) / 2; p->oy = (p->h - p->lh * p->scale) / 2;
        for (int k = 0; k < 2; k++) if (mkbuf(&p->b[k], (uint32_t)p->w, (uint32_t)p->h)) {
            plat_log("kms: dumb buffer alloc failed"); drmDropMaster(fd); close(fd); fd = -1; return -1;
        }
    }
    /* modeset both panels with a black frame, every other plane off; once more if the controller is storming */
    int ret = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        drmModeAtomicReq *r = drmModeAtomicAlloc();
        for (uint32_t k = 0; k < pres->count_planes; k++) {
            uint32_t id = pres->planes[k];
            if (id == P[0].plane || id == P[1].plane) continue;
            drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
            drmModeAtomicAddProperty(r, id, prop(id, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
        }
        for (int i = 0; i < 2; i++) {
            Panel *p = &P[i];
            drmModeAtomicAddProperty(r, p->conn, p->c_crtc, p->crtc);
            drmModeAtomicAddProperty(r, p->crtc, p->c_mode, p->mode_blob);
            drmModeAtomicAddProperty(r, p->crtc, p->c_active, 1);
            add_plane(r, p, &p->b[0]);
            p->front = 0;
        }
        ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
        drmModeAtomicFree(r);
        if (ret) break;
        if (attempt) break;
        long long a = vop_irqs(); usleep(30000); long long b = vop_irqs();
        if (!(a >= 0 && b - a > 100)) break;
        plat_log("kms: display controller interrupt storm (%lld in 30 ms): panels off and on", b - a);
        r = drmModeAtomicAlloc();
        for (int i = 0; i < 2; i++) drmModeAtomicAddProperty(r, P[i].crtc, P[i].c_active, 0);
        drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
        drmModeAtomicFree(r);
    }
    drmModeFreeResources(res); drmModeFreePlaneResources(pres);
    if (ret) { plat_log("kms: modeset failed: %s", strerror(-ret)); for (int i = 0; i < 2; i++) { freebuf(&P[i].b[0]); freebuf(&P[i].b[1]); } drmDropMaster(fd); close(fd); fd = -1; return -1; }
    rowbuf = malloc((size_t)MAX(P[0].w, P[1].w) * 4);
    info->top_w = P[0].lw; info->top_h = P[0].lh; info->bot_w = P[1].lw; info->bot_h = P[1].lh;
    info->scale = P[0].scale; info->panel_w = P[0].w; info->panel_h = P[0].h;
    info->hz = P[0].mode.vrefresh ? P[0].mode.vrefresh : 60; info->backend = "kms";
    int pw[2] = { P[0].w, P[1].w }, ph[2] = { P[0].h, P[1].h }, sc[2] = { P[0].scale, P[1].scale }, ox[2] = { P[0].ox, P[1].ox }, oy[2] = { P[0].oy, P[1].oy };
    evdev_init(pw, ph, sc, ox, oy);
    return 0;
}

/* scale a logical frame into a panel buffer: build each row once in cached memory, then copy it `scale` times
 * (the dumb buffer is write-combined: only ever write it, in long runs) */
static void upscale(Panel *p, Buf *b, const Surf *s) {
    int k = p->scale, w = MIN(s->w, p->lw), h = MIN(s->h, p->lh);
    for (int y = 0; y < h; y++) {
        const uint32_t *src = s->px + (size_t)y * s->pitch;
        uint32_t *r = rowbuf;
        if (k == 2) { for (int x = 0; x < w; x++) { uint32_t c = src[x]; r[0] = c; r[1] = c; r += 2; } }
        else if (k == 3) { for (int x = 0; x < w; x++) { uint32_t c = src[x]; r[0] = c; r[1] = c; r[2] = c; r += 3; } }
        else for (int x = 0; x < w; x++) for (int j = 0; j < k; j++) *r++ = src[x];
        for (int j = 0; j < k; j++)
            memcpy((uint8_t *)b->map + (size_t)(p->oy + y * k + j) * b->pitch + (size_t)p->ox * 4, rowbuf, (size_t)w * k * 4);
    }
}

/* every pixel, two at a time (a single changed pixel always changes it: xor and an odd multiply are both one to one) */
static uint64_t surf_hash(const Surf *s) {
    uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < s->h; y++) {
        const uint32_t *r = s->px + (size_t)y * s->pitch;
        int x = 0;
        for (; x + 1 < s->w; x += 2) { uint64_t v; memcpy(&v, r + x, 8); h = (h ^ v) * 1099511628211ull; }
        if (x < s->w) h = (h ^ r[x]) * 1099511628211ull;
    }
    return h;
}

/* A panel's new frame, alone in its commit. Each panel flips at its own refresh, and the two are out of step (on an
 * RG DS Plus the bottom one 9.9 ms after the top one). Together in one commit, the game waited for the later flip
 * and had what was left of the top panel's 16.7 ms for the next frame's commit: about once a second it came too
 * late, and the top screen showed a frame twice (measured: 7-13 late frames in 600). */
static int commit_panel(int i) {
    Panel *p = &P[i];
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    add_plane(r, p, &p->b[!p->front]);
    int ret = drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, (void *)(uintptr_t)(commit_no[i] + 1));
    drmModeAtomicFree(r);
    if (ret == 0) { commit_no[i]++; p->front = !p->front; pending |= 1 << i; commit_at[i] = now_s(); return 0; }
    if (ret == -EBUSY) { static int busy; if (!busy++) plat_log("kms: the %s panel was still busy with its last frame: this one waits", i ? "bottom" : "top"); return ret; }
    static int warned;
    if (!warned++) plat_log("kms: commit failed (%s panel): %s", i ? "bottom" : "top", strerror(-ret));
    p->have_frame = 0;                                 /* drawn again at the next present */
    return ret;
}

static void k_present(Surf *top, Surf *bot) {
    double t0 = now_s();
    /* The top screen (the game) sets the pace: its last frame must be on the panel before the next goes out. The
     * bottom screen follows when it can; while only it changes (a menu), it paces. */
    frame_clock = 0;
    if (pending & 1) { wait_flips(1, 1000, 1); frame_clock = flip_t[0]; }
    else if (pending & 2) wait_flips(2, 1000, 1);
    take_flips();
    double t1 = now_s();
    tr_prev_exit = tr_exit; tr_enter = t0; tr_waited = t1;
    Surf *s[2] = { top, bot };
    int sent = 0, failed = 0;
    for (int i = 0; i < 2; i++) {
        Panel *p = &P[i];
        uint64_t h = surf_hash(s[i]);
        if (i == 0) tr_hash0 = now_s();
        if (p->have_frame && h == p->last_hash) { in_prev[i] = 0; continue; }   /* an unchanged screen keeps its buffer */
        /* its last frame isn't up yet (the bottom panel, when its refresh comes just after the top's): a moment for
         * it, else this frame of it goes out at the next present */
        if (pending & (1 << i)) wait_flips(1 << i, 3, 0);
        if (pending & (1 << i)) { K.busy[i]++; continue; }
        double u0 = now_s();
        upscale(p, &p->b[!p->front], s[i]);
        double u1 = now_s();
        if (i == 0) tr_scaled0 = u1;
        K.scale += u1 - u0;
        int ret = commit_panel(i);
        if (ret == 0) { p->last_hash = h; p->have_frame = 1; sent |= 1 << i; }
        else if (ret == -EBUSY) K.busy[i]++;             /* its frame goes out at the next present */
        else failed = 1;
        K.commit += now_s() - u1;
    }
    K.presents++; K.wait += t1 - t0;
    if (sent == 3) K.both++;
    if (failed) usleep(16000);
    else if (!sent && !pending) { K.idle++; usleep(8000); }   /* nothing changed: still pace roughly */
    tr_exit = now_s();
}

/* DK_PROFILE's line: the counts since the last call */
static const char *k_pace(void) {
    static char b[260];
    double n = K.presents ? K.presents : 1;
    snprintf(b, sizeof b, "; presents %u (%u with nothing new), top %u flips %u late, bottom %u flips %u late %u put off, both screens %u (bottom panel %.1f ms after top); "
             "waiting %.2f ms, scaling %.2f ms, commit %.2f ms",
             K.presents, K.idle, K.flips[0], K.late[0], K.flips[1], K.late[1], K.busy[1], K.both, panel_phase(),
             K.wait / n * 1000, K.scale / n * 1000, K.commit / n * 1000);
    memset(&K, 0, sizeof K);
    return b;
}

static double k_frame_clock(void) { return frame_clock; }

static void k_poll(Input *in) { evdev_poll(in); }

static void k_shutdown(void) {
    if (fd < 0) return;
    wait_flips(3, 1000, 1);
    evdev_shutdown();
    /* leave the panels black; the menu's compositor does its own modeset when it starts */
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    for (int i = 0; i < 2; i++) { memset(P[i].b[!P[i].front].map, 0, P[i].b[!P[i].front].size); add_plane(r, &P[i], &P[i].b[!P[i].front]); }
    drmModeAtomicCommit(fd, r, 0, 0);
    drmModeAtomicFree(r);
    drmDropMaster(fd);
    close(fd); fd = -1;
}

static int k_audio(int rate, void (*mix)(int16_t *, int)) { return alsa_start(rate, mix); }

const Backend backend_kms = { k_init, k_poll, k_present, k_shutdown, k_audio, k_pace, k_frame_clock };
