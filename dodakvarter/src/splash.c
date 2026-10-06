// splash.c: the studio's splash when the game starts: JorreFog productions. Fog drifts in over the night, the emblem
// comes up out of it on a deep note, JORREFOG falls into place a letter at a time, a light runs over the letters and
// PRODUCTIONS opens out under them; on the lower screen all of it again in dark water, with the fog over that too.
// About four and a half seconds; any button (or a touch) cuts it short. Then the title.
#include "game.h"
#include "menu.h"
#include <stdlib.h>

#define FADE_AT 3.9f                    /* (when it starts to go to black, and how long that takes) */
#define FADE_LEN 0.6f

/* ---------------------------------------------------------------- the fog: soft value noise, drifting */
/* value noise that tiles: 16 x 8 lattice cells, 16 samples to a cell, worked out once */
#define NW 256
#define NH 128
static uint8_t noise_tex[NH][NW];
static float lattice(int x, int y) { return (hash3(x & 15, y & 7, 31) & 0xffff) / 65535.0f; }
static void noise_init(void) {
    static int done;
    if (done) return;
    done = 1;
    for (int v = 0; v < NH; v++)
        for (int u = 0; u < NW; u++) {
            int xi = u / 16, yi = v / 16;
            float fx = (u & 15) / 16.0f, fy = (v & 15) / 16.0f;
            fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
            float a = lattice(xi, yi), b = lattice(xi + 1, yi), c = lattice(xi, yi + 1), d = lattice(xi + 1, yi + 1);
            noise_tex[v][u] = (uint8_t)(255 * (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy));
        }
}
/* at (x, y) in lattice cells */
static inline int nz(float x, float y) { return noise_tex[(int)floorf(y * 16) & (NH - 1)][(int)floorf(x * 16) & (NW - 1)]; }
/* two layers, the near one faster; lying low (thin at the top of the screen, thickest at its foot); amount 0..1 */
static void fog(Surf *s, float t, float amount, int seed) {
    if (amount <= 0) return;
    noise_init();
    const int fr = 0x8a, fg = 0x98, fb = 0xb4;
    float so = seed * 3.7f;
    for (int y = 0; y + 1 < s->h; y += 2) {
        float yy = (float)y / s->h, low = (0.2f + 0.8f * yy * sqrtf(yy)) * 190 * amount / 255.0f;
        float y1 = y / 26.0f + t * 0.05f + so, y2 = (y + t * 3) / 13.0f + so * 0.5f;
        uint32_t *row0 = s->px + (size_t)y * s->pitch, *row1 = row0 + s->pitch;
        for (int x = 0; x + 1 < s->w; x += 2) {
            int n = (nz((x + t * 10) / 60.0f + so, y1) * 166 + nz((x - t * 7) / 26.0f, y2) * 90) >> 8;   /* (0.65 and 0.35) */
            int a = (int)((n - 107) * low);                  /* (0.42 of 255) */
            if (a <= 2) continue;
            if (a > 80) a = 80;
            uint32_t *p[4] = { row0 + x, row0 + x + 1, row1 + x, row1 + x + 1 };
            for (int k = 0; k < 4; k++) {
                uint32_t c = *p[k];
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                *p[k] = (uint32_t)((r + (((fr - r) * a) >> 8)) << 16 | (g + (((fg - g) * a) >> 8)) << 8 | (b + (((fb - b) * a) >> 8)));
            }
        }
    }
}

/* ---------------------------------------------------------------- the letters */
/* the letters of a word drawn as one sprite: found by the empty columns between them */
static int letter_spans(const Img *im, int (*span)[2], int max) {
    int n = 0, in = 0;
    for (int x = 0; x <= im->w; x++) {
        int ink = 0;
        if (x < im->w) for (int y = 0; y < im->h && !ink; y++) ink = (im->px[y * im->w + x] >> 24) != 0;
        if (ink && !in) { if (n < max) span[n][0] = x; in = 1; }
        else if (!ink && in) { if (n < max) span[n++][1] = x; in = 0; }
    }
    return n;
}
/* a light running over a sprite's ink: a slanted band at x - y / 2 = band */
static void shine(Surf *s, const Img *im, int ox, int oy, float band, int a) {
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            if (!(im->px[y * im->w + x] >> 24)) continue;
            float d = fabsf(x - y * 0.5f - band);
            if (d < 5) padd(s, ox + x, oy + y, col_scale(0xffffff, (int)(a * (1 - d / 5))));
        }
}
static float ease_out(float k) { k = CLAMP(k, 0, 1); return 1 - (1 - k) * (1 - k) * (1 - k); }

/* ---------------------------------------------------------------- the two screens */
static void splash_top(Surf *s, float t) {
    int w = s->w, h = s->h;
    for (int y = 0; y < h; y++) hline(s, 0, w - 1, y, col_mix(0x02030a, 0x0e1428, y * 256 / h));   /* the night */
    const Img *em = art_exists("logo_emblem") ? art("logo_emblem") : 0, *wm = art_exists("logo_wordmark") ? art("logo_wordmark") : 0;
    const Img *pr = art_exists("logo_productions") ? art("logo_productions") : 0, *sp = art_exists("logo_spark") ? art("logo_spark") : 0;
    int ew = em ? em->w : 64, eh = em ? em->h : 64, wh = wm ? wm->h : 20, ph = pr ? pr->h : 8;
    int total = eh + 10 + wh + 8 + ph, top = (h - total) / 2 - 4;
    int ex = w / 2 - ew / 2, ey = top, wy = ey + eh + 10, py = wy + wh + 8;
    /* the moon's glow behind the emblem, swelling as it comes */
    float rise = ease_out((t - 0.45f) / 0.9f);
    if (rise > 0) for (int r = 3; r >= 1; r--) circle_blend(s, w / 2, ey + eh / 2, ew / 2 + r * 12, 0x6a88c8, (int)(14 * rise));
    fog(s, t, MIN(1.0f, t / 0.8f) * 0.8f, 3);
    /* the emblem, up out of the fog */
    if (rise > 0) {
        int a = (int)(255 * CLAMP((t - 0.45f) / 0.7f, 0, 1));
        if (em) blit_ex(s, em, ex, ey + (int)((1 - rise) * 10), 0, 0, 0, a);
        else circle_blend(s, w / 2, ey + eh / 2 + (int)((1 - rise) * 10), ew / 2, 0xc8d0e0, a);
        if (em && t > 1.9f && t < 2.8f) shine(s, em, ex, ey, (t - 1.9f) / 0.9f * (em->w + 40) - 20, 150);
    }
    /* JORREFOG: the letters fall into place one after another */
    if (wm) {
        int span[16][2], n = letter_spans(wm, span, 16), wx = w / 2 - wm->w / 2;
        for (int i = 0; i < n; i++) {
            float k = (t - 1.3f - i * 0.075f) / 0.28f;
            if (k <= 0) continue;
            float drop = (1 - ease_out(k)) * -14 + (k > 0.7f && k < 1.1f ? sinf((k - 0.7f) / 0.4f * PI_F) * 1.5f : 0);
            Surf c = *s; surf_clip(&c, wx + span[i][0], wy - 20, span[i][1] - span[i][0], wm->h + 40);
            blit_ex(&c, wm, wx, wy + (int)drop, 0, 0, 0, (int)(255 * CLAMP(k * 1.6f, 0, 1)));
        }
        if (t > 2.05f && t < 2.75f) shine(s, wm, wx, wy, (t - 2.05f) / 0.7f * (wm->w + 30) - 15, 190);
    } else if (t > 1.3f) text_center(s, FONT_NORMAL, w / 2, wy + 4, 0xc8d4ea, 0x000000, "JORREFOG");
    /* PRODUCTIONS: opening out from the middle as it comes in */
    float pk = ease_out((t - 2.15f) / 0.8f);
    if (pk > 0) {
        if (pr) {
            int span[16][2], n = letter_spans(pr, span, 16), px0 = w / 2 - pr->w / 2;
            for (int i = 0; i < n; i++) {
                int dx = (int)lroundf((i - (n - 1) / 2.0f) * 2.0f * pk);   /* (two more pixels between each, at the end) */
                Surf c = *s; surf_clip(&c, px0 + span[i][0] + dx, py - 2, span[i][1] - span[i][0], pr->h + 4);
                blit_ex(&c, pr, px0 + dx, py, 0, 0, 0, (int)(255 * pk));
            }
        } else text_center(s, FONT_SMALL, w / 2, py, 0xe0a040, 0x000000, "PRODUCTIONS");
    }
    /* twinkles on the emblem's rim and on the letters, now and then */
    if (sp && t > 2.3f) {
        for (int k = 0; k < 3; k++) {
            int slot = (int)(t * 2.2f) * 3 + k;             /* (a new place every so often) */
            float life = fmodf(t * 2.2f, 1.0f);
            uint32_t hh = hash3(slot, k, 77);
            int x, y;
            if (k == 0) { float a = (hh % 628) / 100.0f; x = w / 2 + (int)(cosf(a) * ew * 0.47f); y = ey + eh / 2 + (int)(sinf(a) * eh * 0.47f); }
            else { x = w / 2 - (wm ? wm->w / 2 : 60) + (int)(hh % (unsigned)(wm ? wm->w : 120)); y = wy + (int)((hh >> 12) % (unsigned)wh); }
            int a = (int)(255 * sinf(PI_F * life));
            if (a > 20) blit_ex(s, sp, x - sp->w / 2, y - sp->h / 2, 0, 0, 0, a);
        }
    }
    /* the deep note's flash */
    if (t > 1.15f && t < 1.6f) rect_blend(s, 0, 0, w, h, 0xd8e4ff, (int)(110 * (1 - (t - 1.15f) / 0.45f)));
    if (t > FADE_AT) rect_blend(s, 0, 0, w, h, 0x000000, (int)(255 * MIN(1.0f, (t - FADE_AT) / FADE_LEN)));
}

/* the lower screen: the upper one in dark water below it (the two screens meet at the water's edge), fog over that */
static void splash_bottom(Surf *b, const Surf *top, float t) {
    int w = b->w, h = b->h;
    for (int y = 0; y < h; y++) {
        uint32_t water = col_mix(0x0a1020, 0x020308, y * 256 / h);
        int wr = (water >> 16) & 255, wg = (water >> 8) & 255, wb = water & 255;
        float depth = (float)y / h;
        int sy = top->h - 1 - (int)(y * 0.92f);             /* (a little squashed, as a reflection lies) */
        int dx = (int)(sinf(y * 0.21f + t * 3.2f) * (1 + depth * 3) + sinf(y * 0.07f - t * 1.7f) * depth * 2);
        int k = (int)(150 * (1 - depth * 0.7f));            /* (how much of it shows: less the deeper) */
        uint32_t *out = b->px + (size_t)y * b->pitch;
        if (sy < 0) { for (int x = 0; x < w; x++) out[x] = water; continue; }
        const uint32_t *src = top->px + (size_t)sy * top->pitch;
        for (int x = 0; x < w; x++) {
            uint32_t c = src[CLAMP(x + dx, 0, top->w - 1)];
            int r = (((c >> 16) & 255) * 196 + 0x2a * 60) >> 8, g = (((c >> 8) & 255) * 196 + 0x4a * 60) >> 8, bl = ((c & 255) * 196 + 0x7a * 60) >> 8;   /* (bluer) */
            out[x] = (uint32_t)((wr + (((r - wr) * k) >> 8)) << 16 | (wg + (((g - wg) * k) >> 8)) << 8 | (wb + (((bl - wb) * k) >> 8)));
        }
    }
    for (int k = 0; k < 6; k++) {                           /* glints on the water, drifting */
        uint32_t hh = hash3(k, 3, 9);
        int y = 6 + (int)(hh % (unsigned)(h / 2)), len = 6 + (int)((hh >> 8) % 14);
        int x = (int)fmodf((hh >> 4) % (unsigned)w + t * (6 + k), (float)(w + 30)) - 15;
        int a = (int)(60 + 50 * sinf(t * 2 + k));
        for (int i = 0; i < len; i++) pblend(b, x + i, y, 0xa0b8e0, a * (len - abs(2 * i - len)) / len);
    }
    fog(b, t + 7, MIN(1.0f, t / 0.8f) * 0.6f, 9);
    if (t > 1.15f && t < 1.6f) rect_blend(b, 0, 0, w, h, 0xd8e4ff, (int)(60 * (1 - (t - 1.15f) / 0.45f)));
    if (t > FADE_AT) rect_blend(b, 0, 0, w, h, 0x000000, (int)(255 * MIN(1.0f, (t - FADE_AT) / FADE_LEN)));
}

void splash_render(Surf *top, Surf *bot) {
    splash_top(top, A.t);
    splash_bottom(bot, top, A.t);
}

/* 1 when it's over (the title next). A button or a touch after the first moment: on to the fade */
int splash_update(const Input *in, const Input *prev) {
    static int sung;                                        /* (the sound is all of it: swell, the deep note at 0.7 s, the letters, the light) */
    if (!sung && A.t >= 0.5f) { sung = 1; sfx(SFX_STING, 1, 0); }
    int any = (in->held & ~prev->held & (BIT(B_A) | BIT(B_B) | BIT(B_START) | BIT(B_X) | BIT(B_Y))) || (in->touch[1] && !prev->touch[1]);
    if (any && A.t > 0.25f && A.t < FADE_AT) A.t = FADE_AT + FADE_LEN * 0.4f;   /* (cut short: straight into the fade) */
    return A.t >= FADE_AT + FADE_LEN;
}
