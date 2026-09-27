// ui.c: the RetroAchievements overlay on the top panel -- pop-up cards (badge, title, text) and a progress pill,
// in the theme's DSi font, drawn into libdsflip's overlay plane on a thread of its own so no game frame waits.
//
// The plane is TOAST_W x TOAST_H ARGB8888 at the top of the top panel, scanned out over the game with per-pixel
// alpha. DRM's default blend mode is premultiplied, so everything here is drawn premultiplied. Pop-ups queue up
// (several achievements can unlock in one frame) and show one after another; the progress pill shows whenever no
// pop-up does. Font: the dii-ess-aye theme's DSi font if installed, else Liberation Sans (stb_truetype reads both);
// badges: PNG files (stb_image), drawn with rounded corners.
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

uint32_t *dsflip_overlay_begin(int *pitch, int *w, int *h);
void dsflip_overlay_end(int show);
void dsflip_log(const char *fmt, ...);

static const char *font_paths[] = {
    "/storage/.config/emulationstation/themes/dii-ess-aye/assets/fonts/dsi_font.otf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
};
static stbtt_fontinfo font;
static int font_ok;

typedef struct { char l1[128], l2[192]; uint32_t accent; int ms; uint8_t *badge; int bw, bh; } popup;
#define QMAX 8
static popup q[QMAX]; static int qn;                  /* pending pop-ups, q[0] is the one showing */
static long long q_until;                              /* when q[0] goes away (0: not shown yet) */
static char prog_text[48]; static uint8_t *prog_badge; static int prog_bw, prog_bh, prog_on;
static int dirty;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int started;

static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }

/* ---------- drawing (premultiplied ARGB) ---------- */
typedef struct { uint32_t *px; int pitch, w, h; } canvas;

static void blend(canvas *c, int x, int y, uint32_t rgb, float a) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    uint32_t *d = (uint32_t *)((char *)c->px + y * c->pitch) + x, o = *d;
    float ia = 1.0f - a;
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * a + ((o >> 16) & 255) * ia + 0.5f);
    uint32_t g = (uint32_t)(((rgb >> 8) & 255) * a + ((o >> 8) & 255) * ia + 0.5f);
    uint32_t b = (uint32_t)((rgb & 255) * a + (o & 255) * ia + 0.5f);
    uint32_t al = (uint32_t)(255.0f * a + (o >> 24) * ia + 0.5f);
    *d = al << 24 | r << 16 | g << 8 | b;
}

/* coverage of a rounded rectangle at pixel (x, y): signed distance, antialiased over one pixel */
static float rrect_cov(float px, float py, float x0, float y0, float x1, float y1, float rad) {
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f - rad, hy = (y1 - y0) * 0.5f - rad;
    float dx = fabsf(px - cx) - hx, dy = fabsf(py - cy) - hy;
    float ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
    float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(dx, dy), 0.0f) - rad;
    return fminf(fmaxf(0.5f - d, 0.0f), 1.0f);
}

static void fill_rrect(canvas *c, int x0, int y0, int x1, int y1, float rad, uint32_t rgb, float alpha) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float cov = rrect_cov(x + 0.5f, y + 0.5f, (float)x0, (float)y0, (float)x1, (float)y1, rad);
            if (cov > 0) blend(c, x, y, rgb, cov * alpha);
        }
}

static void stroke_rrect(canvas *c, int x0, int y0, int x1, int y1, float rad, uint32_t rgb, float alpha) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float outer = rrect_cov(x + 0.5f, y + 0.5f, (float)x0, (float)y0, (float)x1, (float)y1, rad);
            float inner = rrect_cov(x + 0.5f, y + 0.5f, x0 + 1.0f, y0 + 1.0f, x1 - 1.0f, y1 - 1.0f, rad - 1.0f);
            if (outer - inner > 0) blend(c, x, y, rgb, (outer - inner) * alpha);
        }
}

/* an RGBA (straight alpha) image scaled bilinearly into a size x size square with rounded corners */
static void draw_image(canvas *c, const uint8_t *img, int iw, int ih, int x0, int y0, int size, float rad) {
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float cov = rrect_cov(x + 0.5f, y + 0.5f, 0, 0, (float)size, (float)size, rad);
            if (cov <= 0) continue;
            float sx = (x + 0.5f) * iw / size - 0.5f, sy = (y + 0.5f) * ih / size - 0.5f;
            int ix = (int)floorf(sx), iy = (int)floorf(sy); float fx = sx - ix, fy = sy - iy;
            float acc[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < 4; k++) {
                int xx = ix + (k & 1), yy = iy + (k >> 1);
                if (xx < 0) xx = 0; if (yy < 0) yy = 0; if (xx >= iw) xx = iw - 1; if (yy >= ih) yy = ih - 1;
                float w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                const uint8_t *p = img + (yy * iw + xx) * 4;
                float a = p[3] / 255.0f;
                acc[0] += p[0] * a * w; acc[1] += p[1] * a * w; acc[2] += p[2] * a * w; acc[3] += a * w;
            }
            if (acc[3] <= 0) continue;
            uint32_t rgb = (uint32_t)(acc[0] / acc[3]) << 16 | (uint32_t)(acc[1] / acc[3]) << 8 | (uint32_t)(acc[2] / acc[3]);
            blend(c, x0 + x, y0 + y, rgb, acc[3] * cov);
        }
}

/* ---------- text ---------- */
static int utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int cp = *p++, extra = 0;
    if (cp >= 0xF0) { cp &= 7; extra = 3; } else if (cp >= 0xE0) { cp &= 15; extra = 2; } else if (cp >= 0xC0) { cp &= 31; extra = 1; }
    while (extra-- && (*p & 0xC0) == 0x80) cp = cp << 6 | (*p++ & 63);
    *s = (const char *)p;
    return cp;
}

static float text_width(const char *t, float px, int max_cp) {
    float sc = stbtt_ScaleForPixelHeight(&font, px), w = 0; int prev = 0, n = 0;
    for (const char *s = t; *s && n < max_cp; n++) {
        int cp = utf8_next(&s), adv, lsb;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        if (prev) w += stbtt_GetCodepointKernAdvance(&font, prev, cp) * sc;
        w += adv * sc; prev = cp;
    }
    return w;
}

/* draw at baseline (x, y); if wider than maxw, cut with "..." */
static void draw_text(canvas *c, const char *t, float x, int y, float px, uint32_t rgb, float maxw) {
    int n = 0; for (const char *s = t; *s; n++) utf8_next(&s);
    int keep = n, dots = 0;
    if (text_width(t, px, n) > maxw) {
        float dw = text_width("...", px, 3);
        while (keep > 0 && text_width(t, px, keep) + dw > maxw) keep--;
        dots = 1;
    }
    float sc = stbtt_ScaleForPixelHeight(&font, px);
    int prev = 0; const char *s = t;
    for (int i = 0; i < keep + (dots ? 3 : 0); i++) {
        int cp = i < keep ? utf8_next(&s) : '.';
        if (prev) x += stbtt_GetCodepointKernAdvance(&font, prev, cp) * sc;
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        float sub = x - floorf(x);
        stbtt_GetCodepointBitmapBoxSubpixel(&font, cp, sc, sc, sub, 0, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw < 256 && gh < 256) {
            static unsigned char g[256 * 256];
            stbtt_MakeCodepointBitmapSubpixel(&font, g, gw, gh, gw, sc, sc, sub, 0, cp);
            for (int yy = 0; yy < gh; yy++)
                for (int xx = 0; xx < gw; xx++)
                    if (g[yy * gw + xx]) blend(c, (int)floorf(x) + x0 + xx, y + y0 + yy, rgb, g[yy * gw + xx] / 255.0f);
        }
        x += adv * sc; prev = cp;
    }
}

/* ---------- the two layouts ---------- */
static void render_popup(canvas *c, const popup *p) {
    int x0 = 8, y0 = 4, x1 = c->w - 8, y1 = c->h - 4;
    fill_rrect(c, x0, y0, x1, y1, 12, 0x1b1d21, 0.95f);
    stroke_rrect(c, x0, y0, x1, y1, 12, 0x464a52, 1.0f);
    int tx = x0 + 18;
    if (p->badge) { draw_image(c, p->badge, p->bw, p->bh, x0 + 6, y0 + 6, (y1 - y0) - 12, 8); tx = x0 + (y1 - y0) + 8; }
    else fill_rrect(c, x0 + 6, y0 + 10, x0 + 10, y1 - 10, 2, p->accent, 1.0f);   /* accent bar */
    float maxw = (float)(x1 - 14 - tx);
    draw_text(c, p->l1, (float)tx, y0 + 25, 19.0f, p->accent, maxw);
    draw_text(c, p->l2, (float)tx, y0 + 52, 22.0f, 0xf2f3f5, maxw);
}

static void render_pill(canvas *c) {
    float tw = text_width(prog_text, 20.0f, 64);
    int h = 40, bs = prog_badge ? 30 : 0, w = 14 + bs + (bs ? 8 : 0) + (int)ceilf(tw) + 14;
    int x1 = c->w - 8, x0 = x1 - w, y0 = 4, y1 = y0 + h;
    fill_rrect(c, x0, y0, x1, y1, 20, 0x1b1d21, 0.92f);
    stroke_rrect(c, x0, y0, x1, y1, 20, 0x464a52, 1.0f);
    if (prog_badge) draw_image(c, prog_badge, prog_bw, prog_bh, x0 + 8, y0 + 5, bs, 6);
    draw_text(c, prog_text, (float)(x0 + 14 + bs + (bs ? 2 : 0)), y0 + 27, 20.0f, 0xf2f3f5, tw + 2);
}

static void render(void) {                            /* with mx held */
    int pitch, w, h;
    uint32_t *px = dsflip_overlay_begin(&pitch, &w, &h);
    if (!px) return;
    int show = qn > 0 || prog_on;
    if (show) {
        for (int y = 0; y < h; y++) memset((char *)px + y * pitch, 0, (size_t)w * 4);
        canvas c = { px, pitch, w, h };
        if (qn) render_popup(&c, &q[0]); else render_pill(&c);
    }
    dsflip_overlay_end(show);
    dsflip_log("[ui] overlay: %s\n", qn ? q[0].l2 : prog_on ? prog_text : "hidden");
}

static void *ui_thread(void *a) {
    (void)a;
    pthread_mutex_lock(&mx);
    for (;;) {
        long long t = now_ms();
        if (qn && q_until && t >= q_until) {           /* the pop-up's time is up: next one, or the pill, or nothing */
            free(q[0].badge); memmove(q, q + 1, (size_t)(qn - 1) * sizeof *q); qn--; q_until = 0; dirty = 1;
        }
        if (qn && !q_until) { q_until = t + q[0].ms; dirty = 1; }
        if (dirty) { dirty = 0; render(); }
        if (qn) {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
            long long wait = q_until - now_ms(); if (wait < 1) wait = 1;
            ts.tv_sec += wait / 1000; ts.tv_nsec += (wait % 1000) * 1000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&cv, &mx, &ts);
        } else pthread_cond_wait(&cv, &mx);
    }
    return 0;
}

static int ui_start(void) {                           /* with mx held */
    if (started) return font_ok;
    started = 1;
    for (size_t i = 0; i < sizeof font_paths / sizeof *font_paths && !font_ok; i++) {
        FILE *f = fopen(font_paths[i], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *d = n > 0 ? malloc((size_t)n) : 0;
        if (d && fread(d, 1, (size_t)n, f) == (size_t)n && stbtt_InitFont(&font, d, stbtt_GetFontOffsetForIndex(d, 0))) {
            font_ok = 1; dsflip_log("[ui] font %s\n", font_paths[i]);
        } else free(d);
        fclose(f);
    }
    if (!font_ok) { dsflip_log("[ui] no font: pop-ups off\n"); return 0; }
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, ui_thread, 0); pthread_attr_destroy(&at);
    return 1;
}

static uint8_t *load_png(const char *path, int *w, int *h) {
    if (!path || !*path) return 0;
    int n; uint8_t *d = stbi_load(path, w, h, &n, 4);
    if (d && (*w <= 0 || *h <= 0 || *w > 512 || *h > 512)) { stbi_image_free(d); return 0; }
    return d;
}

/* ---------- public ---------- */
/* queue a pop-up; badge: a PNG path or NULL. Thread-safe. */
void ui_popup(const char *l1, const char *l2, const char *badge_png, uint32_t accent, int ms) {
    pthread_mutex_lock(&mx);
    if (!ui_start()) { pthread_mutex_unlock(&mx); return; }
    if (qn == QMAX) { free(q[QMAX - 1].badge); qn--; }  /* keep the newest */
    popup *p = &q[qn++]; memset(p, 0, sizeof *p);
    snprintf(p->l1, sizeof p->l1, "%s", l1 ? l1 : ""); snprintf(p->l2, sizeof p->l2, "%s", l2 ? l2 : "");
    p->accent = accent & 0xffffff; p->ms = ms;
    p->badge = load_png(badge_png, &p->bw, &p->bh);
    dirty = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mx);
}

/* show (text != NULL) or hide the progress pill. Thread-safe. */
void ui_progress(const char *text, const char *badge_png) {
    pthread_mutex_lock(&mx);
    if (!ui_start()) { pthread_mutex_unlock(&mx); return; }
    if (prog_badge) { stbi_image_free(prog_badge); prog_badge = 0; }
    prog_on = text && *text;
    if (prog_on) { snprintf(prog_text, sizeof prog_text, "%s", text); prog_badge = load_png(badge_png, &prog_bw, &prog_bh); }
    dirty = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mx);
}
