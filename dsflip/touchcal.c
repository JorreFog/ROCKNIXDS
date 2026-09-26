// touchcal: touchscreen calibration for libdsflip. Draws 5 crosshairs, one at a time, on the bottom
// panel via KMS and records the raw evdev position the user taps for each. The log line
//   target <px>,<py> raw <rx>,<ry>
// gives the panel-pixel -> raw mapping. Run like kmstest (needs DRM master, sway stopped):
//   LD_PRELOAD=./touchcal.so /bin/true
// Env: TOUCHCAL_DEV=fe5c0000.i2c (bottom touch), TOUCHCAL_OUT=DSI-1 (bottom panel), TOUCHCAL_LOG.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static int fd;
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

static void draw(uint32_t *m, int pitch, int w, int h, int tx, int ty, int done) {
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((char *)m + y * pitch);
        for (int x = 0; x < w; x++) row[x] = 0x101820;
    }
    for (int i = 0; i < 5; i++)                  /* progress dots along the top */
        for (int y = 8; y < 16; y++) for (int x = 280 + i * 16; x < 288 + i * 16; x++)
            ((uint32_t *)((char *)m + y * pitch))[x] = i < done ? 0x40ff40 : 0x404040;
    for (int d = -30; d <= 30; d++)
        for (int t = -1; t <= 1; t++) {
            if (tx + d >= 0 && tx + d < w && ty + t >= 0 && ty + t < h) ((uint32_t *)((char *)m + (ty + t) * pitch))[tx + d] = 0xffffff;
            if (ty + d >= 0 && ty + d < h && tx + t >= 0 && tx + t < w) ((uint32_t *)((char *)m + (ty + d) * pitch))[tx + t] = 0xffffff;
        }
    for (int y = ty - 3; y <= ty + 3; y++) for (int x = tx - 3; x <= tx + 3; x++)
        ((uint32_t *)((char *)m + y * pitch))[x] = 0xff2020;   /* red centre */
}

__attribute__((constructor)) static void run(void) {
    const char *lp = getenv("TOUCHCAL_LOG");
    lg = fopen(lp ? lp : "/storage/dsflip/logs/touchcal.log", "w");
    if (!lg) lg = stderr;
    setvbuf(lg, 0, _IOLBF, 0);
    const char *want = getenv("TOUCHCAL_DEV"); if (!want) want = "fe5c0000.i2c";
    const char *out = getenv("TOUCHCAL_OUT"); if (!out) out = "DSI-1";

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || drmSetMaster(fd)) { fprintf(lg, "FAIL: DRM master\n"); _exit(1); }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    drmModeRes *res = drmModeGetResources(fd);
    drmModePlaneRes *pres = drmModeGetPlaneResources(fd);
    uint32_t conn = 0, crtc = 0, plane = 0; int crtc_idx = -1; drmModeModeInfo mode;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        char name[32]; snprintf(name, sizeof name, "DSI-%u", c->connector_type_id);
        if (c->connection == DRM_MODE_CONNECTED && !strcmp(name, out)) {
            conn = c->connector_id; mode = c->modes[0];
            drmModeEncoder *e = drmModeGetEncoder(fd, c->encoder_id ? c->encoder_id : c->encoders[0]);
            for (int k = 0; k < res->count_crtcs; k++) if (e->possible_crtcs & (1u << k)) { crtc = res->crtcs[k]; crtc_idx = k; break; }
            drmModeFreeEncoder(e);
        }
        drmModeFreeConnector(c);
    }
    for (uint32_t k = 0; k < pres->count_planes && !plane; k++) {
        drmModePlane *pl = drmModeGetPlane(fd, pres->planes[k]);
        drmModeObjectProperties *pr = drmModeObjectGetProperties(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE);
        uint64_t type = 0;
        for (uint32_t i = 0; i < pr->count_props; i++) { drmModePropertyRes *p = drmModeGetProperty(fd, pr->props[i]); if (!strcmp(p->name, "type")) type = pr->prop_values[i]; drmModeFreeProperty(p); }
        drmModeFreeObjectProperties(pr);
        if ((pl->possible_crtcs & (1u << crtc_idx)) && type == 1) plane = pl->plane_id;
        drmModeFreePlane(pl);
    }
    int w = mode.hdisplay, h = mode.vdisplay;
    struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
    drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd);
    uint32_t fb, hs[4] = { cd.handle }, ps[4] = { cd.pitch }, os[4] = { 0 };
    drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, hs, ps, os, &fb, 0);
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
    uint32_t *map = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    uint32_t blob; drmModeCreatePropertyBlob(fd, &mode, sizeof mode, &blob);

    /* listen on BOTH Goodix controllers and report which one each tap came from */
    (void)want;
    glob_t g; int tfd[2] = { -1, -1 }; char tname[2][96]; int nt = 0;
    glob("/sys/class/input/event*", 0, 0, &g);
    for (size_t i = 0; i < g.gl_pathc && nt < 2; i++) {
        char dev[300], rp[512], nm[128] = ""; snprintf(dev, sizeof dev, "%s/device/name", g.gl_pathv[i]);
        FILE *f = fopen(dev, "r"); if (f) { if (!fgets(nm, sizeof nm, f)) nm[0] = 0; fclose(f); }
        if (!strstr(nm, "Goodix")) continue;
        snprintf(dev, sizeof dev, "%s/device", g.gl_pathv[i]);
        if (!realpath(dev, rp)) continue;
        char path[64]; snprintf(path, sizeof path, "/dev/input/%s", strrchr(g.gl_pathv[i], '/') + 1);
        tfd[nt] = open(path, O_RDONLY);
        snprintf(tname[nt], sizeof tname[nt], "%s %.40s", path, strstr(rp, "platform/") ? strstr(rp, "platform/") + 9 : rp);
        nt++;
    }
    fprintf(lg, "panel %s %dx%d conn %u crtc %u plane %u; touch A=%s B=%s\n", out, w, h, conn, crtc, plane, tname[0], nt > 1 ? tname[1] : "-");

    int tx[5] = { 60, w - 60, w - 60, 60, w / 2 }, ty[5] = { 60, 60, h - 60, h - 60, h / 2 };
    for (int t = 0; t < 5; t++) {
        draw(map, cd.pitch, w, h, tx[t], ty[t], t);
        drmModeAtomicReq *r = drmModeAtomicAlloc();
        drmModeAtomicAddProperty(r, conn, prop(conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), crtc);
        drmModeAtomicAddProperty(r, crtc, prop(crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID"), blob);
        drmModeAtomicAddProperty(r, crtc, prop(crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
        const char *pn[] = { "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H" };
        uint64_t pv[] = { fb, crtc, 0, 0, (uint64_t)w << 16, (uint64_t)h << 16, 0, 0, w, h };
        for (int k = 0; k < 10; k++) drmModeAtomicAddProperty(r, plane, prop(plane, DRM_MODE_OBJECT_PLANE, pn[k]), pv[k]);
        drmModeAtomicCommit(fd, r, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
        drmModeAtomicFree(r);
        /* wait for a touch release; record the position at release (the settled finger) */
        struct { long s, us; uint16_t type, code; int32_t value; } ev[32];
        /* first finger only (MT slot 0), position at touch-DOWN, taps in the first 0.7 s ignored */
        int x[2] = { -1, -1 }, y[2] = { -1, -1 }, slot[2] = { 0, 0 }, pend[2] = { 0, 0 }, got = -1;
        struct timespec ts0; clock_gettime(CLOCK_MONOTONIC, &ts0);
        for (int d = 0; d < nt; d++) { struct { long s, us; uint16_t type, code; int32_t value; } junk[64]; fcntl(tfd[d], F_SETFL, O_NONBLOCK); while (read(tfd[d], junk, sizeof junk) > 0) {} fcntl(tfd[d], F_SETFL, 0); }
        while (got < 0) {
            struct timespec tn; clock_gettime(CLOCK_MONOTONIC, &tn);
            double el = (tn.tv_sec - ts0.tv_sec) + (tn.tv_nsec - ts0.tv_nsec) / 1e9;
            if (el > 60) break;
            struct pollfd pf[2] = { { .fd = tfd[0], .events = POLLIN }, { .fd = tfd[1], .events = POLLIN } };
            if (poll(pf, nt, 1000) <= 0) continue;
            for (int d = 0; d < nt && got < 0; d++) {
                if (!(pf[d].revents & POLLIN)) continue;
                ssize_t n = read(tfd[d], ev, sizeof ev);
                for (int i = 0; i < (int)(n / sizeof ev[0]); i++) {
                    if (ev[i].type == 3 && ev[i].code == 0x2f) slot[d] = ev[i].value;               /* ABS_MT_SLOT */
                    if (ev[i].type == 3 && ev[i].code == 0x35 && slot[d] == 0) x[d] = ev[i].value;
                    if (ev[i].type == 3 && ev[i].code == 0x36 && slot[d] == 0) y[d] = ev[i].value;
                    if (ev[i].type == 1 && ev[i].code == 0x14a && ev[i].value) pend[d] = 1;
                    if (ev[i].type == 0 && pend[d]) { pend[d] = 0; if (el > 0.7) { got = d; break; } }
                }
            }
        }
        if (got >= 0) {   /* green dot where the tap registered (raw taken as panel pixels), shown briefly */
            for (int yy = y[got] - 5; yy <= y[got] + 5; yy++) for (int xx = x[got] - 5; xx <= x[got] + 5; xx++)
                if (xx >= 0 && xx < w && yy >= 0 && yy < h) ((uint32_t *)((char *)map + yy * cd.pitch))[xx] = 0x20ff20;
            usleep(700000);
        }
        if (got >= 0) fprintf(lg, "target %d,%d raw %d,%d from %c (%s)\n", tx[t], ty[t], x[got], y[got], 'A' + got, tname[got]);
        else fprintf(lg, "target %d,%d timeout\n", tx[t], ty[t]);
        usleep(300000);
    }
    fprintf(lg, "done\n");
    _exit(0);
}
