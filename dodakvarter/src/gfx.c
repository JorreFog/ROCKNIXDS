// gfx.c: surfaces, primitives and sprite blits. Plain C loops: at 320x240 everything here costs well under a
// millisecond a frame on the RG DS's Cortex-A55.
#include "gfx.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

void surf_alloc(Surf *s, int w, int h) {
    s->w = w; s->h = h; s->pitch = w;
    s->px = calloc((size_t)w * h, 4);
    surf_noclip(s);
}
void surf_free(Surf *s) { free(s->px); s->px = 0; }
void surf_noclip(Surf *s) { s->cx0 = 0; s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h; }
void surf_clip(Surf *s, int x, int y, int w, int h) {
    s->cx0 = MAX(0, x); s->cy0 = MAX(0, y); s->cx1 = MIN(s->w, x + w); s->cy1 = MIN(s->h, y + h);
    if (s->cx1 < s->cx0) s->cx1 = s->cx0;
    if (s->cy1 < s->cy0) s->cy1 = s->cy0;
}
void img_alloc(Img *im, int w, int h) { im->w = w; im->h = h; im->px = calloc((size_t)w * h, 4); }

uint32_t col_mix(uint32_t a, uint32_t b, int t) {
    int r = CR(a) + ((CR(b) - CR(a)) * t >> 8), g = CG(a) + ((CG(b) - CG(a)) * t >> 8), bl = CB(a) + ((CB(b) - CB(a)) * t >> 8);
    return RGB(r, g, bl);
}
uint32_t col_mul(uint32_t c, int r, int g, int b) {
    int rr = CR(c) * r >> 8, gg = CG(c) * g >> 8, bb = CB(c) * b >> 8;
    return RGB(MIN(rr, 255), MIN(gg, 255), MIN(bb, 255));
}
uint32_t col_scale(uint32_t c, int k) { return col_mul(c, k, k, k); }
uint32_t col_add(uint32_t a, uint32_t b) {
    return RGB(MIN(CR(a) + CR(b), 255), MIN(CG(a) + CG(b), 255), MIN(CB(a) + CB(b), 255));
}
static inline uint32_t blend(uint32_t d, uint32_t c, int a) {
    uint32_t rb = d & 0xFF00FF, g = d & 0x00FF00;
    rb += (((c & 0xFF00FF) - rb) * (uint32_t)a >> 8) & 0xFF00FF;
    g += (((c & 0x00FF00) - g) * (uint32_t)a >> 8) & 0x00FF00;
    return (rb | g) & 0xFFFFFF;
}

void fill(Surf *s, uint32_t c) {
    for (int y = s->cy0; y < s->cy1; y++) {
        uint32_t *p = s->px + (size_t)y * s->pitch;
        for (int x = s->cx0; x < s->cx1; x++) p[x] = c;
    }
}
void pset(Surf *s, int x, int y, uint32_t c) {
    if (x >= s->cx0 && y >= s->cy0 && x < s->cx1 && y < s->cy1) s->px[(size_t)y * s->pitch + x] = c & 0xFFFFFF;
}
void pblend(Surf *s, int x, int y, uint32_t c, int a) {
    if (x >= s->cx0 && y >= s->cy0 && x < s->cx1 && y < s->cy1) {
        uint32_t *p = &s->px[(size_t)y * s->pitch + x]; *p = blend(*p, c, a);
    }
}
void padd(Surf *s, int x, int y, uint32_t c) {
    if (x >= s->cx0 && y >= s->cy0 && x < s->cx1 && y < s->cy1) {
        uint32_t *p = &s->px[(size_t)y * s->pitch + x]; *p = col_add(*p, c);
    }
}
void rectf(Surf *s, int x, int y, int w, int h, uint32_t c) {
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    c &= 0xFFFFFF;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = s->px + (size_t)yy * s->pitch;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}
void rect_blend(Surf *s, int x, int y, int w, int h, uint32_t c, int a) {
    if (a <= 0) return;
    if (a >= 255) { rectf(s, x, y, w, h, c); return; }
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = s->px + (size_t)yy * s->pitch;
        for (int xx = x0; xx < x1; xx++) p[xx] = blend(p[xx], c, a);
    }
}
void rect_dither(Surf *s, int x, int y, int w, int h, uint32_t c, int level) {
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = s->px + (size_t)yy * s->pitch;
        for (int xx = x0; xx < x1; xx++) {
            int on = level >= 3 ? !((xx & 1) && (yy & 1)) : level == 2 ? ((xx ^ yy) & 1) == 0 : ((xx & 1) == 0 && (yy & 1) == 0);
            if (on) p[xx] = c & 0xFFFFFF;
        }
    }
}
void hline(Surf *s, int x0, int x1, int y, uint32_t c) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    rectf(s, x0, y, x1 - x0 + 1, 1, c);
}
void vline(Surf *s, int x, int y0, int y1, uint32_t c) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    rectf(s, x, y0, 1, y1 - y0 + 1, c);
}
void rect_line(Surf *s, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    hline(s, x, x + w - 1, y, c); hline(s, x, x + w - 1, y + h - 1, c);
    vline(s, x, y, y + h - 1, c); vline(s, x + w - 1, y, y + h - 1, c);
}
void bevel(Surf *s, int x, int y, int w, int h, uint32_t face, uint32_t hi, uint32_t lo) {
    rectf(s, x, y, w, h, face);
    hline(s, x, x + w - 2, y, hi); vline(s, x, y, y + h - 2, hi);
    hline(s, x + 1, x + w - 1, y + h - 1, lo); vline(s, x + w - 1, y + 1, y + h - 1, lo);
}
void line(Surf *s, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 4096; n++) {
        pset(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void line_blend(Surf *s, int x0, int y0, int x1, int y1, uint32_t c, int a) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 4096; n++) {
        pblend(s, x0, y0, c, a);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void circlef(Surf *s, int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++) {
        int w = (int)sqrtf((float)(r * r - y * y) + 0.3f);
        hline(s, cx - w, cx + w, cy + y, c);
    }
}
void circle_blend(Surf *s, int cx, int cy, int r, uint32_t c, int a) {
    for (int y = -r; y <= r; y++) {
        int w = (int)sqrtf((float)(r * r - y * y) + 0.3f);
        rect_blend(s, cx - w, cy + y, 2 * w + 1, 1, c, a);
    }
}
void ellipse_blend(Surf *s, int cx, int cy, int rx, int ry, uint32_t c, int a) {
    if (ry <= 0 || rx <= 0) return;
    for (int y = -ry; y <= ry; y++) {
        float f = 1.0f - (float)(y * y) / (float)(ry * ry);
        int w = (int)(rx * sqrtf(f > 0 ? f : 0) + 0.3f);
        rect_blend(s, cx - w, cy + y, 2 * w + 1, 1, c, a);
    }
}
void circle(Surf *s, int cx, int cy, int r, uint32_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pset(s, cx + x, cy + y, c); pset(s, cx + y, cy + x, c); pset(s, cx - y, cy + x, c); pset(s, cx - x, cy + y, c);
        pset(s, cx - x, cy - y, c); pset(s, cx - y, cy - x, c); pset(s, cx + y, cy - x, c); pset(s, cx + x, cy - y, c);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

/* the visible part of a blit: source start (sx, sy) and destination rectangle */
static int clip_blit(Surf *s, int w, int h, int *x, int *y, int *sx, int *sy, int *bw, int *bh) {
    *sx = 0; *sy = 0; *bw = w; *bh = h;
    if (*x < s->cx0) { *sx = s->cx0 - *x; *bw -= *sx; *x = s->cx0; }
    if (*y < s->cy0) { *sy = s->cy0 - *y; *bh -= *sy; *y = s->cy0; }
    if (*x + *bw > s->cx1) *bw = s->cx1 - *x;
    if (*y + *bh > s->cy1) *bh = s->cy1 - *y;
    return *bw > 0 && *bh > 0;
}

void blit(Surf *s, const Img *im, int x, int y, int flags) { blit_ex(s, im, x, y, flags, 0, 0, 255); }

void blit_ex(Surf *s, const Img *im, int x, int y, int flags, uint32_t tint, int amt, int alpha) {
    if (!im || !im->px) return;
    int sx, sy, bw, bh;
    int ox = x, oy = y;
    if (!clip_blit(s, im->w, im->h, &x, &y, &sx, &sy, &bw, &bh)) return;
    for (int j = 0; j < bh; j++) {
        int iy = sy + j; if (flags & FLIP_Y) iy = im->h - 1 - iy;
        const uint32_t *src = im->px + (size_t)iy * im->w;
        uint32_t *dst = s->px + (size_t)(y + j) * s->pitch + x;
        for (int i = 0; i < bw; i++) {
            int ix = sx + i; if (flags & FLIP_X) ix = im->w - 1 - ix;
            uint32_t c = src[ix]; int a = (int)CA(c);
            if (!a) continue;
            if (amt) c = col_mix(c, tint, amt);
            if (alpha < 255) a = a * alpha / 255;
            dst[i] = a >= 255 ? (c & 0xFFFFFF) : blend(dst[i], c, a);
        }
    }
    (void)ox; (void)oy;
}

void blit_region(Surf *s, const Img *im, int rx, int ry, int rw, int rh, int x, int y, int flags) {
    if (!im || !im->px) return;
    int sx, sy, bw, bh;
    if (!clip_blit(s, rw, rh, &x, &y, &sx, &sy, &bw, &bh)) return;
    for (int j = 0; j < bh; j++) {
        int iy = sy + j; if (flags & FLIP_Y) iy = rh - 1 - iy;
        const uint32_t *src = im->px + (size_t)(ry + iy) * im->w + rx;
        uint32_t *dst = s->px + (size_t)(y + j) * s->pitch + x;
        for (int i = 0; i < bw; i++) {
            int ix = sx + i; if (flags & FLIP_X) ix = rw - 1 - ix;
            uint32_t c = src[ix]; int a = (int)CA(c);
            if (!a) continue;
            dst[i] = a >= 255 ? (c & 0xFFFFFF) : blend(dst[i], c, a);
        }
    }
}

void blit_scaled(Surf *s, const Img *im, int x, int y, int k) {
    if (!im || !im->px) return;
    for (int j = 0; j < im->h; j++)
        for (int i = 0; i < im->w; i++) {
            uint32_t c = im->px[(size_t)j * im->w + i]; int a = (int)CA(c);
            if (!a) continue;
            if (a >= 255) rectf(s, x + i * k, y + j * k, k, k, c);
            else rect_blend(s, x + i * k, y + j * k, k, k, c, a);
        }
}

void blit_silhouette(Surf *s, const Img *im, int x, int y, int flags, uint32_t c, int alpha) {
    if (!im || !im->px) return;
    int sx, sy, bw, bh;
    if (!clip_blit(s, im->w, im->h, &x, &y, &sx, &sy, &bw, &bh)) return;
    for (int j = 0; j < bh; j++) {
        int iy = sy + j; if (flags & FLIP_Y) iy = im->h - 1 - iy;
        const uint32_t *src = im->px + (size_t)iy * im->w;
        uint32_t *dst = s->px + (size_t)(y + j) * s->pitch + x;
        for (int i = 0; i < bw; i++) {
            int ix = sx + i; if (flags & FLIP_X) ix = im->w - 1 - ix;
            int a = (int)CA(src[ix]);
            if (!a) continue;
            dst[i] = blend(dst[i], c, a * alpha / 255);
        }
    }
}

/* nearest-neighbour rotation: each destination pixel looks up its source (no holes) */
void blit_rot(Surf *s, const Img *im, float cx, float cy, float ang, int flags) {
    if (!im || !im->px) return;
    float ca = cosf(ang), sa = sinf(ang);
    float hw = im->w * 0.5f, hh = im->h * 0.5f;
    int r = (int)ceilf(sqrtf(hw * hw + hh * hh)) + 1;
    int x0 = MAX((int)cx - r, s->cx0), x1 = MIN((int)cx + r + 1, s->cx1);
    int y0 = MAX((int)cy - r, s->cy0), y1 = MIN((int)cy + r + 1, s->cy1);
    for (int y = y0; y < y1; y++) {
        uint32_t *dst = s->px + (size_t)y * s->pitch;
        float dy = y + 0.5f - cy;
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx;
            float u = dx * ca + dy * sa + hw, v = -dx * sa + dy * ca + hh;
            int iu = (int)floorf(u), iv = (int)floorf(v);
            if (iu < 0 || iv < 0 || iu >= im->w || iv >= im->h) continue;
            if (flags & FLIP_Y) iv = im->h - 1 - iv;
            uint32_t c = im->px[(size_t)iv * im->w + iu]; int a = (int)CA(c);
            if (!a) continue;
            dst[x] = a >= 255 ? (c & 0xFFFFFF) : blend(dst[x], c, a);
        }
    }
}

void copy_rect(Surf *dst, const uint32_t *src, int spitch, int sw, int sh, int sx, int sy, int w, int h, int dx, int dy) {
    /* clip against the source too: the camera can look past the map's edge */
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > sw) w = sw - sx;
    if (sy + h > sh) h = sh - sy;
    if (dx < dst->cx0) { sx += dst->cx0 - dx; w -= dst->cx0 - dx; dx = dst->cx0; }
    if (dy < dst->cy0) { sy += dst->cy0 - dy; h -= dst->cy0 - dy; dy = dst->cy0; }
    if (dx + w > dst->cx1) w = dst->cx1 - dx;
    if (dy + h > dst->cy1) h = dst->cy1 - dy;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memcpy(dst->px + (size_t)(dy + j) * dst->pitch + dx, src + (size_t)(sy + j) * spitch + sx, (size_t)w * 4);
}

int textf(Surf *s, int font, int x, int y, uint32_t c, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return text(s, font, x, y, c, buf);
}
