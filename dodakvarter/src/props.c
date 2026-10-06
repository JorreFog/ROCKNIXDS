// props.c: everything standing in the town, drawn with code (trees, lamps, cars, the Mystery Box, perk machines).
// Low props that fit inside their tile are painted into the world bitmap once (props_paint_flat); the tall ones
// are drawn every frame, sorted with the actors by their feet.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define WIN (G->season == SEASON_WINTER)
#define AUT (G->season == SEASON_AUTUMN)
#define SUM (G->season == SEASON_SUMMER)
static uint32_t sh(uint32_t c, int k) { return col_scale(c, k); }
static uint32_t hv(uint32_t h, int a, int b) { return (uint32_t)a + h % (uint32_t)(b - a + 1); }
uint32_t vary_c(uint32_t c, int x, int y);
void sign_board_small(Surf *s, int cx, int y, const char *txt, uint32_t bg, uint32_t fg);

int prop_is_tall(int k) {
    switch (k) {
    case P_FENCE_H: case P_FENCE_V: case P_HEDGE_H: case P_HEDGE_V: case P_WALL_H: case P_WALL_V: case P_PICKET_H:
    case P_PICKET_V: case P_RAILING_H: case P_SANDBOX: case P_BENCH: case P_BIN: case P_RECYCLE: case P_COMPOST:
    case P_PLANTER: case P_BARREL: case P_PALLETS: case P_GRAVE: case P_GOAL: case P_KICKBIKE: case P_TABLE:
    case P_HYDRANT: case P_ROCK: case P_BOAT: case P_BUSH: case P_TRAMPOLINE: case P_CAR: case P_CAR_V:
    case P_BIKES: case P_CART: case P_BOLLARD: case P_WHEELBARROW: case P_CRATES:
        return 0;
    }
    return 1;
}

static const uint32_t CAR_COLORS[] = { 0x8c2b1e, 0x3e6fa3, 0xd8d4c8, 0x2a2a2e, 0x4e8a5c, 0xb8a040, 0x6a6e74, 0x7a3a5a };

/* a blob of foliage: a filled circle with a lit side and a shadow side, ragged edges */
static void foliage(Surf *s, int cx, int cy, int r, uint32_t c, uint32_t seed) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d2 = i * i + j * j;
            uint32_t h = hash3(cx + i + (int)seed, cy + j, 61);
            if (d2 > r * r + (int)(h % (unsigned)(r + 1)) - r / 2) continue;
            uint32_t col = c;
            if (i + j < -r / 2) col = sh(c, 330);
            else if (i + j > r / 2) col = sh(c, 170);
            if (h % 5 == 0) col = sh(col, 240);
            if (h % 7 == 0) col = sh(col, 190);
            pset(s, cx + i, cy + j, col);
        }
}

static void snow_cap(Surf *s, int x0, int x1, int y) { if (!WIN) return; for (int x = x0; x <= x1; x++) { pset(s, x, y, 0xf2f6fa); if (hash3(x, y, 62) % 2) pset(s, x, y - 1, 0xffffff); } }

static void tree_birch(Surf *s, int x, int y, uint32_t v) {
    int hgt = 26 + (int)(v % 6);
    for (int k = 0; k < hgt; k++) {                                      /* white bark with black marks */
        uint32_t c = (hash3(x, y - k, 63) % 5 == 0) ? 0x2a2a2a : (k % 7 == 3 ? 0xbab6ae : 0xeeeae2);
        pset(s, x, y - k, c); pset(s, x + 1, y - k, sh(c, 210));
    }
    if (WIN) {                                                           /* bare branches with snow */
        for (int b = 0; b < 5; b++) {
            int by = y - hgt + 4 + b * 4, dir = b & 1 ? 1 : -1;
            line(s, x, by, x + dir * (5 + b % 3), by - 4, 0x6a625a);
            pset(s, x + dir * (5 + b % 3), by - 5, 0xf4f8fa);
        }
        return;
    }
    uint32_t leaf = AUT ? ((v >> 4) & 1 ? 0xd8a83a : 0xc8842a) : SUM ? 0x6aa040 : 0x6a9a3a;
    foliage(s, x, y - hgt + 3, 7, leaf, v);
    foliage(s, x - 4, y - hgt + 9, 5, leaf, v + 1);
    foliage(s, x + 5, y - hgt + 8, 5, leaf, v + 2);
    for (int k = 0; k < 6; k++) pset(s, x + (int)hv(hash3(x, k, 64), 0, 8) - 4, y - hgt + (int)hv(hash3(k, y, 65), 0, 12), 0xeeeae2);
}

static void tree_pine(Surf *s, int x, int y, uint32_t v) {           /* tall, red-brown trunk, a flat dark crown */
    int hgt = 30 + (int)(v % 6);
    for (int k = 0; k < hgt - 6; k++) { pset(s, x, y - k, k > hgt / 2 ? 0xa8603a : 0x5a3a2a); pset(s, x + 1, y - k, 0x4a2a1a); }
    uint32_t c = 0x2e4a2a;
    foliage(s, x, y - hgt + 4, 6, c, v);
    foliage(s, x - 5, y - hgt + 7, 4, c, v + 3);
    foliage(s, x + 5, y - hgt + 6, 4, c, v + 4);
    if (WIN) { snow_cap(s, x - 5, x + 4, y - hgt - 1); snow_cap(s, x - 8, x - 3, y - hgt + 3); snow_cap(s, x + 3, x + 8, y - hgt + 2); }
}

static void tree_spruce(Surf *s, int x, int y, uint32_t v, int small) {
    int hgt = small ? 18 : 28 + (int)(v % 5);
    vline(s, x, y - 3, y, 0x4a2a1a);
    uint32_t c = 0x1f3a26;
    for (int k = 0; k < hgt - 2; k++) {
        int yy = y - 3 - k, w = (hgt - k) * (small ? 5 : 7) / hgt / 1;
        if (w < 1) w = 1;
        int band = (k % 5);
        w += band < 2 ? 1 : 0;
        for (int i = -w; i <= w; i++) {
            uint32_t col = i < 0 ? sh(c, 330) : i > w / 2 ? sh(c, 160) : c;
            if (band == 4) col = sh(col, 150);
            if (WIN && band == 0 && hash3(x + i, yy, 66) % 3) col = 0xf2f6fa;
            pset(s, x + i, yy, col);
        }
    }
}

static void tree_apple(Surf *s, int x, int y, uint32_t v) {
    vline(s, x, y - 10, y, 0x5a3a2a); vline(s, x + 1, y - 9, y, 0x3a2418);
    line(s, x, y - 9, x - 3, y - 13, 0x5a3a2a); line(s, x + 1, y - 9, x + 4, y - 12, 0x5a3a2a);
    if (WIN) { snow_cap(s, x - 3, x + 4, y - 14); return; }
    uint32_t leaf = AUT ? 0x7a8a3a : 0x4e7a34;
    foliage(s, x, y - 16, 8, leaf, v);
    for (int k = 0; k < 7; k++) {
        uint32_t h = hash3(x, y, k + 70);
        int ax = x - 6 + (int)(h % 13), ay = y - 22 + (int)((h >> 8) % 12);
        pset(s, ax, ay, AUT ? 0xd02a20 : SUM ? 0x9acd4a : 0xc83020);
    }
}

static void lamp(Surf *s, int x, int y, int lit) {
    vline(s, x, y - 30, y, 0x3a3e44); vline(s, x + 1, y - 30, y, 0x2a2e34);
    rectf(s, x - 1, y - 2, 4, 2, 0x2a2e34);
    hline(s, x - 5, x + 1, y - 30, 0x3a3e44);                             /* the arm */
    rectf(s, x - 8, y - 30, 6, 3, 0x2a2e34);                              /* the head */
    hline(s, x - 7, x - 3, y - 27, lit ? 0xfff0b0 : 0x5a5a50);
    snow_cap(s, x - 8, x - 3, y - 31);
}

static void flagpole(Surf *s, int x, int y, uint32_t v) {
    vline(s, x, y - 40, y, 0xf0f0f0); vline(s, x + 1, y - 40, y, 0xb8b8b8);
    pset(s, x, y - 41, 0xd8c040);
    float t = G->time * 4 + (v & 15);
    if (v & 1) {                                                          /* the long pennant (vimpel) */
        for (int i = 0; i < 18; i++) {
            int dy = (int)(sinf(t + i * 0.5f) * 1.5f);
            int hh = 3 - i / 7;
            for (int j = 0; j < hh; j++) pset(s, x + 2 + i, y - 39 + j + dy, j == hh / 2 && hh > 1 ? 0xfecc02 : 0x006aa7);
        }
    } else {                                                              /* the flag */
        for (int i = 0; i < 14; i++) {
            int dy = (int)(sinf(t + i * 0.45f) * 1.2f);
            for (int j = 0; j < 9; j++) {
                int cross = (i == 4 || i == 5) || (j == 4);
                pset(s, x + 2 + i, y - 39 + j + dy, cross ? 0xfecc02 : 0x006aa7);
            }
        }
    }
}

static void car_h(Surf *s, int x, int y, uint32_t col, int open) {   /* a boxy station wagon, side on: 30 x 14 */
    int x0 = x - 15, y0 = y - 15;
    rect_blend(s, x0 + 1, y - 3, 30, 3, 0x000000, 80);
    rectf(s, x0, y0 + 6, 30, 7, col);                                     /* body */
    hline(s, x0, x0 + 29, y0 + 6, sh(col, 320));
    rectf(s, x0 + 4, y0 + 1, 22, 6, sh(col, 270));                        /* the long roof of a kombi */
    rectf(s, x0 + 5, y0 + 2, 6, 4, 0x2a3a4a); rectf(s, x0 + 12, y0 + 2, 6, 4, 0x2a3a4a); rectf(s, x0 + 19, y0 + 2, 6, 4, 0x2a3a4a);
    pset(s, x0 + 5, y0 + 2, 0x5a7a8a); pset(s, x0 + 12, y0 + 2, 0x5a7a8a); pset(s, x0 + 19, y0 + 2, 0x5a7a8a);
    rectf(s, x0 + 3, y0 + 11, 6, 4, 0x1a1a1a); rectf(s, x0 + 21, y0 + 11, 6, 4, 0x1a1a1a);   /* wheels */
    pset(s, x0 + 5, y0 + 12, 0x8a8a8a); pset(s, x0 + 23, y0 + 12, 0x8a8a8a);
    hline(s, x0 + 1, x0 + 28, y0 + 9, sh(col, 160));
    pset(s, x0, y0 + 8, 0xf0e8a0); pset(s, x0 + 29, y0 + 8, 0xc02020);
    if (open) { rectf(s, x0 + 25, y0 - 2, 5, 8, sh(col, 200)); rectf(s, x0 + 26, y0 + 4, 4, 2, 0x1a1a1a); }
    if (WIN) { hline(s, x0 + 4, x0 + 25, y0, 0xf4f8fa); hline(s, x0 + 5, x0 + 24, y0 + 1, 0xe6ecf2); }
}

static void car_v(Surf *s, int x, int y, uint32_t col, int open) {   /* seen from behind/above: 14 x 30 */
    int x0 = x - 7, y0 = y - 30;
    rect_blend(s, x0 + 1, y - 2, 14, 3, 0x000000, 80);
    rectf(s, x0, y0 + 2, 14, 27, col);
    rectf(s, x0 + 2, y0 + 6, 10, 15, sh(col, 270));                       /* roof */
    rectf(s, x0 + 2, y0 + 3, 10, 3, 0x2a3a4a); rectf(s, x0 + 2, y0 + 21, 10, 4, 0x2a3a4a);   /* windscreens */
    pset(s, x0 + 2, y0 + 21, 0x5a7a8a);
    rectf(s, x0 - 1, y0 + 5, 1, 5, 0x1a1a1a); rectf(s, x0 + 14, y0 + 5, 1, 5, 0x1a1a1a);
    rectf(s, x0 - 1, y0 + 20, 1, 5, 0x1a1a1a); rectf(s, x0 + 14, y0 + 20, 1, 5, 0x1a1a1a);
    pset(s, x0 + 1, y0 + 28, 0xc02020); pset(s, x0 + 12, y0 + 28, 0xc02020);
    rectf(s, x0 + 4, y0 + 27, 6, 2, 0xe8e0c0);                            /* the number plate */
    if (open) rectf(s, x0 + 1, y0 + 25, 12, 4, 0x1a1a1a);
    if (WIN) rectf(s, x0 + 2, y0 + 7, 10, 13, 0xeef2f6);
}

static void bus_stop(Surf *s, int x, int y) {                          /* glass shelter, bench, the green city-bus sign */
    int x0 = x - 16, y0 = y - 22;
    rect_blend(s, x0, y0 + 4, 30, 17, 0x9ab8c8, 90);                      /* glass */
    rect_line(s, x0, y0 + 4, 30, 17, 0x5a6066);
    rectf(s, x0 - 1, y0 + 2, 32, 3, 0x3a3e44);                            /* roof */
    snow_cap(s, x0 - 1, x0 + 30, y0 + 1);
    rectf(s, x0 + 3, y0 + 15, 24, 2, 0x6a5038);                           /* bench */
    vline(s, x0 + 33, y0 - 6, y, 0x9a9a9a);
    rectf(s, x0 + 30, y0 - 8, 8, 5, 0x2a8a3a); rect_line(s, x0 + 30, y0 - 8, 8, 5, 0xf0f0f0);
    pset(s, x0 + 33, y0 - 6, 0xf0f0f0); pset(s, x0 + 34, y0 - 6, 0xf0f0f0);
}

static void swings(Surf *s, int x, int y) {
    uint32_t c = 0xd04a2a;
    line(s, x - 12, y, x - 9, y - 20, c); line(s, x - 6, y, x - 9, y - 20, c);
    line(s, x + 12, y, x + 9, y - 20, c); line(s, x + 6, y, x + 9, y - 20, c);
    hline(s, x - 9, x + 9, y - 20, 0x3a3a3a);
    float t = sinf(G->time * 1.3f) * 2;
    for (int k = 0; k < 2; k++) {
        int sx = x - 4 + k * 8 + (int)(k ? -t : t);
        line(s, x - 4 + k * 8, y - 20, sx, y - 7, 0x8a8a8a);
        rectf(s, sx - 2, y - 7, 5, 2, 0x2a2a2a);
    }
    snow_cap(s, x - 9, x + 9, y - 21);
}

static void climber(Surf *s, int x, int y) {                          /* klätterställning */
    uint32_t c = 0x3e6fa3, d = 0xd8a020;
    rect_line(s, x - 8, y - 16, 16, 16, c);
    for (int k = 1; k < 4; k++) { hline(s, x - 8, x + 7, y - 16 + k * 4, d); vline(s, x - 8 + k * 4, y - 16, y - 1, c); }
    line(s, x + 8, y - 16, x + 14, y, 0xc04030);                          /* the slide */
    line(s, x + 9, y - 16, x + 15, y, 0xe06050);
    snow_cap(s, x - 8, x + 8, y - 17);
}

static void fountain(Surf *s, int x, int y) {
    ellipse_blend(s, x, y - 5, 14, 6, 0x6a6a68, 255);
    ellipse_blend(s, x, y - 6, 12, 4, WIN ? 0xc8dce8 : 0x3a5a7a, 255);
    rectf(s, x - 2, y - 16, 4, 10, 0x8a8a86);
    if (!WIN) {
        float t = G->time * 6;
        for (int k = 0; k < 8; k++) {
            float a = k * 0.8f + t * 0.2f;
            int px = x + (int)(cosf(a) * (4 + (int)(t + k) % 5)), py = y - 16 + (int)((int)(t * 2 + k * 3) % 9);
            pset(s, px, py, 0xc8e8f8);
        }
        rectf(s, x - 1, y - 20, 2, 4, 0xd8f0ff);
    }
}

static void well(Surf *s, int x, int y) {                             /* Stortorget's brunn: stone, a little roof */
    ellipse_blend(s, x, y - 4, 9, 4, 0x7a746a, 255);
    ellipse_blend(s, x, y - 5, 6, 2, 0x1a1a1a, 255);
    vline(s, x - 7, y - 20, y - 4, 0x5a3a2a); vline(s, x + 7, y - 20, y - 4, 0x5a3a2a);
    for (int k = 0; k < 5; k++) hline(s, x - 9 + k, x + 9 - k, y - 20 - k, k == 4 ? 0x3a3a3a : 0x6a3a2a);
    snow_cap(s, x - 9, x + 9, y - 21);
}

static void container(Surf *s, int x, int y, uint32_t v, int open) {   /* shipping container, 46 x 20 */
    static const uint32_t cc[] = { 0xb03a2a, 0x2a5a9a, 0x3a7a4a, 0xd87a20, 0x7a7a7a };
    uint32_t c = cc[v % 5];
    int x0 = x - 23, y0 = y - 21;
    rectf(s, x0, y0, 46, 20, c);
    for (int k = 0; k < 46; k += 3) vline(s, x0 + k, y0 + 2, y0 + 17, sh(c, 170));
    rect_line(s, x0, y0, 46, 20, sh(c, 120));
    hline(s, x0, x0 + 45, y0, sh(c, 300));
    if (open) rectf(s, x0 + 38, y0 + 3, 7, 15, 0x1a1a1a);
    snow_cap(s, x0, x0 + 45, y0);
}

static void maypole(Surf *s, int x, int y) {                          /* midsommarstång */
    vline(s, x, y - 44, y, 0x3a6a2a); vline(s, x + 1, y - 44, y, 0x2a5a1a);
    hline(s, x - 10, x + 11, y - 38, 0x3a6a2a);
    circle(s, x - 9, y - 32, 4, 0x3a6a2a); circle(s, x + 10, y - 32, 4, 0x3a6a2a);
    for (int k = 0; k < 10; k++) {
        uint32_t h = hash3(x, k, 71);
        pset(s, x + (int)(h % 3) - 1, y - 4 - (int)((h >> 4) % 38), (h >> 8) % 2 ? 0xf0f0f0 : 0xe84a6a);
    }
    pset(s, x - 9, y - 36, 0xf0d040); pset(s, x + 10, y - 36, 0xf0d040);
}

static void sign_moose(Surf *s, int x, int y) {                       /* the yellow triangle with the moose */
    vline(s, x, y - 18, y, 0x9a9a9a);
    for (int j = 0; j < 12; j++) hline(s, x - j / 2 - 1, x + j / 2 + 1, y - 30 + j, j < 1 || j > 10 ? 0xc81e1e : 0xf0c818);
    for (int j = 1; j < 11; j++) { pset(s, x - j / 2 - 1, y - 30 + j, 0xc81e1e); pset(s, x + j / 2 + 1, y - 30 + j, 0xc81e1e); }
    hline(s, x - 2, x + 2, y - 23, 0x1a1a1a); hline(s, x - 3, x + 3, y - 22, 0x1a1a1a);
    pset(s, x - 3, y - 21, 0x1a1a1a); pset(s, x + 2, y - 21, 0x1a1a1a); pset(s, x + 3, y - 24, 0x1a1a1a); pset(s, x + 4, y - 25, 0x1a1a1a);
}

static void sign_t(Surf *s, int x, int y) {                           /* the tunnelbana lantern */
    vline(s, x, y - 20, y, 0x5a5e66);
    rectf(s, x - 5, y - 32, 11, 12, 0xf2f2f2);
    rect_line(s, x - 5, y - 32, 11, 12, 0xb8b8b8);
    hline(s, x - 3, x + 3, y - 30, 0x1f5fa8); hline(s, x - 3, x + 3, y - 29, 0x1f5fa8);
    rectf(s, x - 1, y - 28, 2, 6, 0x1f5fa8);
}

static void grave(Surf *s, int x, int y, uint32_t v) {
    uint32_t c = (v & 1) ? 0x8a8a86 : 0x6a6a68;
    rectf(s, x - 4, y - 12, 9, 10, c);
    hline(s, x - 3, x + 3, y - 13, c); hline(s, x - 2, x + 2, y - 14, c);
    hline(s, x - 3, x + 3, y - 12, sh(c, 300));
    if (v & 2) { vline(s, x, y - 11, y - 6, 0x3a3a3a); hline(s, x - 2, x + 2, y - 9, 0x3a3a3a); }
    else { hline(s, x - 2, x + 2, y - 9, 0x4a4a4a); hline(s, x - 2, x + 1, y - 7, 0x4a4a4a); }
    hline(s, x - 5, x + 5, y - 2, sh(c, 160));
    snow_cap(s, x - 3, x + 3, y - 15);
}

static void mailboxes(Surf *s, int x, int y, int open) {             /* a row on a shared stand */
    vline(s, x - 6, y - 8, y, 0x5a5a5a); vline(s, x + 6, y - 8, y, 0x5a5a5a);
    static const uint32_t cc[] = { 0x2a5a9a, 0xd8d4c8, 0x3a7a4a, 0x8a2a20 };
    for (int k = 0; k < 4; k++) {
        rectf(s, x - 9 + k * 5, y - 16, 4, 7, cc[k]);
        hline(s, x - 9 + k * 5, x - 6 + k * 5, y - 16, sh(cc[k], 300));
        pset(s, x - 8 + k * 5, y - 12, 0x1a1a1a);
    }
    if (open) rectf(s, x - 9, y - 18, 4, 2, 0x2a5a9a);
    snow_cap(s, x - 9, x + 10, y - 17);
}

static void statue(Surf *s, int x, int y) {
    rectf(s, x - 6, y - 8, 12, 8, 0x8a8680); hline(s, x - 6, x + 5, y - 8, 0xa8a49e);
    uint32_t b = 0x4a7a6a;                                                /* a verdigris figure */
    rectf(s, x - 2, y - 22, 4, 14, b); circlef(s, x, y - 25, 3, b);
    line(s, x + 2, y - 20, x + 6, y - 26, b);
    snow_cap(s, x - 3, x + 3, y - 29);
}

static void phonebox(Surf *s, int x, int y) {
    rectf(s, x - 5, y - 24, 11, 24, 0x8a2a20);
    rect_blend(s, x - 3, y - 20, 7, 16, 0x9ab8c8, 120);
    rectf(s, x - 5, y - 26, 11, 3, 0x6a1a14);
    snow_cap(s, x - 5, x + 5, y - 27);
}

static void washline(Surf *s, int x, int y) {
    vline(s, x - 8, y - 16, y, 0x8a8a8a); vline(s, x + 8, y - 16, y, 0x8a8a8a);
    hline(s, x - 8, x + 8, y - 15, 0xd8d8d8);
    if (!WIN) { rectf(s, x - 5, y - 14, 4, 6, 0xd8d0f0); rectf(s, x + 1, y - 14, 5, 5, 0xe8a0a0); }
}

static void ticket(Surf *s, int x, int y) {
    rectf(s, x - 4, y - 20, 9, 20, 0x1f5fa8); rectf(s, x - 2, y - 17, 5, 5, G->power_on ? 0x8ad8f8 : 0x1a2a3a);
    rectf(s, x - 2, y - 9, 5, 2, 0x1a1a1a);
}

static void stall(Surf *s, int x, int y, uint32_t v) {   /* torghandel */
    static const uint32_t produce[] = { 0xc83a2a, 0xe08a20, 0x7ab83a, 0xe8c840, 0x7a3a8a, 0x3a8a3a };
    vline(s, x - 13, y - 22, y - 1, 0x5a4a3a); vline(s, x + 12, y - 22, y - 1, 0x5a4a3a);
    rectf(s, x - 14, y - 10, 28, 3, 0x9a7a52); rectf(s, x - 13, y - 7, 26, 4, 0x6a5038);    /* the table, its skirt */
    for (int k = 0; k < 4; k++) {                                          /* crates of fruit */
        int cx = x - 13 + k * 7;
        rectf(s, cx, y - 13, 6, 3, 0xb08a5a); hline(s, cx, cx + 5, y - 11, 0x7a5a38);
        uint32_t c = produce[(v + k * 3) % ARRAY_LEN(produce)];
        if (WIN) c = 0x8a5a3a;                                              /* (in winter: sacks of potatoes) */
        for (int i = 0; i < 6; i++) { pset(s, cx + i, y - 14, (i + k) & 1 ? c : col_scale(c, 200)); if (i % 2 == 0) pset(s, cx + i, y - 15, col_scale(c, 300)); }
    }
    uint32_t stripe = v & 1 ? 0xc83030 : 0x2a6a3a;
    for (int i = -15; i <= 15; i++) vline(s, x + i, y - 26, y - 22, ((i + 15) / 3) & 1 ? 0xf0ece0 : stripe);   /* the awning */
    for (int i = -15; i <= 15; i += 3) pset(s, x + i + 1, y - 21, ((i + 15) / 3) & 1 ? 0xf0ece0 : stripe);  /* its scallops */
    hline(s, x - 15, x + 15, y - 27, col_scale(stripe, 150));
    snow_cap(s, x - 15, x + 15, y - 28);
}
static void buoy(Surf *s, int x, int y) {                                   /* livboj: orange and white, on a red post */
    vline(s, x, y - 21, y - 1, 0xa83228); vline(s, x + 1, y - 21, y - 1, 0x7a1e18);
    for (int a = 0; a < 24; a++) {
        float t = a * PI_F / 12;
        uint32_t c = (a / 3) & 1 ? 0xf2f0ea : 0xf07020;
        pset(s, x + (int)lrintf(cosf(t) * 5), y - 13 + (int)lrintf(sinf(t) * 4), c);
        pset(s, x + (int)lrintf(cosf(t) * 4), y - 13 + (int)lrintf(sinf(t) * 3), col_scale(c, 210));
    }
    snow_cap(s, x - 1, x + 2, y - 22);
}
static void notice(Surf *s, int x, int y) {                                 /* anslagstavla */
    vline(s, x - 7, y - 18, y - 1, 0x5a4a3a); vline(s, x + 7, y - 18, y - 1, 0x5a4a3a);
    rectf(s, x - 9, y - 21, 19, 12, 0x7a5a3a); rect_line(s, x - 9, y - 21, 19, 12, 0x4a3828);
    hline(s, x - 10, x + 10, y - 22, 0x3a3a40);
    rectf(s, x - 7, y - 19, 4, 6, 0xf0f0e8); rectf(s, x - 2, y - 18, 5, 4, 0xf0e070); rectf(s, x + 4, y - 19, 4, 7, 0xe8f0f8);
    pset(s, x - 6, y - 17, 0x4a4a5a); pset(s, x - 5, y - 15, 0x4a4a5a); pset(s, x + 5, y - 16, 0xc83030); pset(s, x, y - 16, 0x4a4a5a);
    snow_cap(s, x - 10, x + 10, y - 23);
}

static void forklift(Surf *s, int x, int y) {
    rectf(s, x - 8, y - 12, 14, 9, 0xe0a020); rectf(s, x - 6, y - 20, 8, 8, 0x2a2a2a);
    rect_line(s, x - 6, y - 20, 8, 8, 0x5a5a5a);
    vline(s, x + 7, y - 18, y, 0x4a4a4a); hline(s, x + 7, x + 12, y - 1, 0x6a6a6a);
    circlef(s, x - 5, y - 2, 2, 0x1a1a1a); circlef(s, x + 3, y - 2, 2, 0x1a1a1a);
}

static void bonfire(Surf *s, int x, int y) {
    for (int k = 0; k < 6; k++) line(s, x - 6 + k * 2, y, x, y - 8, 0x5a3a2a);
    float t = G->time * 10;
    for (int k = 0; k < 14; k++) {
        uint32_t h = hash3(k, (int)t, 72);
        pset(s, x - 4 + (int)(h % 9), y - 6 - (int)((h >> 4) % 10), (h >> 8) % 3 ? 0xf0a020 : 0xf0e040);
    }
}

static void snowman(Surf *s, int x, int y) {
    circlef(s, x, y - 5, 5, 0xf4f8fa); circlef(s, x, y - 13, 4, 0xf4f8fa); circlef(s, x, y - 19, 3, 0xf4f8fa);
    pset(s, x - 1, y - 20, 0x1a1a1a); pset(s, x + 1, y - 20, 0x1a1a1a); pset(s, x, y - 19, 0xe08020);
    hline(s, x - 3, x + 3, y - 22, 0x1a1a1a); rectf(s, x - 2, y - 25, 5, 3, 0x1a1a1a);
}

static void bush(Surf *s, int x, int y, uint32_t v) {
    uint32_t c = WIN ? 0x5a6a5a : AUT ? 0x6a6a2a : 0x3a6a2a;
    foliage(s, x, y - 6, 6, c, v);
    if (WIN) snow_cap(s, x - 5, x + 5, y - 11);
    else if (SUM && (v & 1)) for (int k = 0; k < 4; k++) pset(s, x - 3 + (int)(hash3(k, x, 73) % 7), y - 9 + (int)(hash3(y, k, 74) % 6), 0xf0f0f0);
}

/* ---------------------------------------------------------------- flat props (baked) */
static void fence_h(Surf *s, int x0, int y0, int kind, uint32_t v) {     /* x0,y0: tile's top-left */
    switch (kind) {
    case P_FENCE_H:                                                       /* chain-link with posts */
        for (int i = 0; i < 16; i++) for (int j = 3; j < 14; j++) if (((i + j) % 4 == 0) || ((i - j + 64) % 4 == 0)) pset(s, x0 + i, y0 + j, 0x8a9096);
        vline(s, x0 + 1, y0 + 2, y0 + 15, 0x5a6066); hline(s, x0, x0 + 15, y0 + 2, 0x6a7076);
        rect_blend(s, x0, y0 + 14, 16, 2, 0x000000, 70);
        break;
    case P_HEDGE_H:
        for (int i = 0; i < 16; i++) for (int j = 2; j < 15; j++) {
            uint32_t h = hash3(x0 + i, y0 + j, 75), c = WIN ? 0x4a5a4a : AUT ? 0x4a5a2a : 0x2e5a2a;
            if (j < 5) c = WIN ? 0xf0f4f8 : sh(c, 330);
            else if (h % 4 == 0) c = sh(c, 160);
            else if (h % 5 == 0) c = sh(c, 270);
            pset(s, x0 + i, y0 + j, c);
        }
        rect_blend(s, x0, y0 + 14, 16, 2, 0x000000, 90);
        break;
    case P_WALL_H:                                                        /* a stone wall */
        for (int i = 0; i < 16; i++) for (int j = 4; j < 16; j++) {
            int row = (j - 4) / 4, sx = (i + (row & 1) * 3) / 6;
            uint32_t c = vary_c(0x8a8478, sx + x0, row + y0);
            if ((j - 4) % 4 == 0 || (i + (row & 1) * 3) % 6 == 0) c = 0x5a564e;
            pset(s, x0 + i, y0 + j, c);
        }
        hline(s, x0, x0 + 15, y0 + 4, WIN ? 0xf4f8fa : 0xa8a49a);
        break;
    case P_PICKET_H: case P_RAILING_H: {                                  /* white or Falu red picket fence */
        uint32_t c = (v & 3) == 0 ? 0x8c2b1e : 0xf0ece4;
        if (kind == P_RAILING_H) c = 0x4a4e56;
        hline(s, x0, x0 + 15, y0 + 7, sh(c, 200)); hline(s, x0, x0 + 15, y0 + 11, sh(c, 200));
        for (int i = 1; i < 16; i += 4) { vline(s, x0 + i, y0 + 4, y0 + 14, c); vline(s, x0 + i + 1, y0 + 5, y0 + 14, sh(c, 220)); pset(s, x0 + i, y0 + 3, c); if (WIN) pset(s, x0 + i, y0 + 3, 0xffffff); }
        break;
    }
    }
}
static void fence_v(Surf *s, int x0, int y0, int kind, uint32_t v) {
    switch (kind) {
    case P_FENCE_V:
        for (int j = 0; j < 16; j++) { pset(s, x0 + 7, y0 + j, 0x8a9096); pset(s, x0 + 8, y0 + j, 0x6a7076); }
        rectf(s, x0 + 6, y0 + 1, 4, 3, 0x5a6066);
        break;
    case P_HEDGE_V:
        for (int j = 0; j < 16; j++) for (int i = 3; i < 13; i++) {
            uint32_t h = hash3(x0 + i, y0 + j, 76), c = WIN ? 0x4a5a4a : AUT ? 0x4a5a2a : 0x2e5a2a;
            if (i < 6) c = WIN ? 0xf0f4f8 : sh(c, 320);
            else if (h % 4 == 0) c = sh(c, 160);
            pset(s, x0 + i, y0 + j, c);
        }
        break;
    case P_WALL_V:
        for (int j = 0; j < 16; j++) for (int i = 4; i < 12; i++) {
            uint32_t c = vary_c(0x8a8478, x0 + i / 4, y0 + j / 4);
            if (j % 4 == 0 || i == 4) c = 0x5a564e;
            pset(s, x0 + i, y0 + j, c);
        }
        if (WIN) vline(s, x0 + 5, y0, y0 + 15, 0xf4f8fa);
        break;
    case P_PICKET_V: {
        uint32_t c = (v & 3) == 0 ? 0x8c2b1e : 0xf0ece4;
        vline(s, x0 + 7, y0, y0 + 15, c); vline(s, x0 + 8, y0, y0 + 15, sh(c, 200));
        for (int j = 2; j < 16; j += 5) rectf(s, x0 + 6, y0 + j, 4, 2, c);
        break;
    }
    }
}
uint32_t vary_c(uint32_t c, int x, int y) {
    int v = (int)(hash3(x, y, 77) % 17) - 8;
    return RGB(CLAMP((int)CR(c) + v, 0, 255), CLAMP((int)CG(c) + v, 0, 255), CLAMP((int)CB(c) + v, 0, 255));
}

static void flat_prop(Surf *s, const Prop *p) {
    int x = p->x, y = p->y, x0 = x - TS / 2, y0 = y - TS;
    uint32_t v = p->var;
    int searched = p->loot == 2;
    switch (p->kind) {
    case P_FENCE_H: case P_HEDGE_H: case P_WALL_H: case P_PICKET_H: case P_RAILING_H: fence_h(s, x0, y0, p->kind, v); break;
    case P_FENCE_V: case P_HEDGE_V: case P_WALL_V: case P_PICKET_V: fence_v(s, x0, y0, p->kind, v); break;
    case P_SANDBOX:
        rect_line(s, x0 + 1, y0 + 4, 14, 11, 0x8a5a3a); rect_line(s, x0 + 2, y0 + 5, 12, 9, 0x6a4a2a);
        pset(s, x0 + 6, y0 + 9, 0xd03030); pset(s, x0 + 7, y0 + 9, 0xd03030); pset(s, x0 + 9, y0 + 10, 0x3060d0);
        break;
    case P_BENCH: {
        uint32_t c = (v & 1) ? 0x2a5a3a : 0x7a5038;
        rect_blend(s, x0 + 1, y0 + 13, 14, 2, 0x000000, 70);
        rectf(s, x0 + 1, y0 + 4, 14, 2, c); rectf(s, x0 + 1, y0 + 7, 14, 2, sh(c, 220)); rectf(s, x0 + 1, y0 + 10, 14, 2, c);
        vline(s, x0 + 2, y0 + 10, y0 + 14, 0x2a2a2a); vline(s, x0 + 13, y0 + 10, y0 + 14, 0x2a2a2a);
        snow_cap(s, x0 + 1, x0 + 14, y0 + 4);
        break;
    }
    case P_BIN:                                                            /* the green park bin on a post */
        vline(s, x, y0 + 8, y0 + 15, 0x3a3a3a);
        rectf(s, x - 4, y0 + 2, 8, 9, 0x2a6a3a);
        hline(s, x - 4, x + 3, y0 + 2, searched ? 0x1a1a1a : 0x4a8a5a);
        vline(s, x - 4, y0 + 3, y0 + 10, 0x3a7a4a);
        if (searched) { pset(s, x - 1, y0 + 1, 0xd8d0b0); pset(s, x + 1, y0 + 2, 0xc84a2a); }
        snow_cap(s, x - 4, x + 3, y0 + 1);
        break;
    case P_RECYCLE: {                                                      /* återvinning: the colour code */
        static const uint32_t rc[] = { 0x2a5aa8, 0xb8986a, 0x7a3a9a, 0x8a8a8a, 0xf0f0f0, 0x2a8a3a };
        uint32_t c = rc[v % 6];
        rectf(s, x0 + 1, y0 + 3, 14, 12, c);
        hline(s, x0 + 1, x0 + 14, y0 + 3, sh(c, 300)); vline(s, x0 + 14, y0 + 4, y0 + 14, sh(c, 160));
        rectf(s, x0 + 5, y0 + 6, 6, 2, searched ? 0x000000 : 0x1a1a1a);
        snow_cap(s, x0 + 1, x0 + 14, y0 + 2);
        break;
    }
    case P_COMPOST: rectf(s, x0 + 2, y0 + 5, 12, 10, 0x5a4030); for (int j = y0 + 6; j < y0 + 15; j += 3) hline(s, x0 + 2, x0 + 13, j, 0x3a2a1a); snow_cap(s, x0 + 2, x0 + 13, y0 + 4); break;
    case P_PLANTER:
        rectf(s, x0 + 2, y0 + 8, 12, 7, 0x8a8680); hline(s, x0 + 2, x0 + 13, y0 + 8, 0xa8a49e);
        if (!WIN) foliage(s, x, y0 + 6, 4, AUT ? 0x8a6a2a : 0x3a7a2a, v);
        else hline(s, x0 + 3, x0 + 12, y0 + 7, 0xf4f8fa);
        break;
    case P_BARREL: {
        uint32_t c = (v & 1) ? 0x3a5a8a : 0x6a4a2a;
        rectf(s, x - 5, y0 + 3, 10, 12, c); hline(s, x - 5, x + 4, y0 + 6, sh(c, 160)); hline(s, x - 5, x + 4, y0 + 11, sh(c, 160));
        ellipse_blend(s, x, y0 + 3, 5, 1, searched ? 0x1a1a1a : sh(c, 280), 255);
        break;
    }
    case P_PALLETS: for (int k = 0; k < 3; k++) { rectf(s, x0 + 1, y0 + 12 - k * 4, 14, 3, 0xb08a5a); for (int i = 0; i < 14; i += 4) pset(s, x0 + 1 + i, y0 + 14 - k * 4, 0x5a4020); } break;
    case P_GRAVE: grave(s, x, y, v); break;
    case P_GOAL:
        rect_line(s, x - 3, y0 - 6, 7, 20, 0xf0f0f0);
        for (int j = y0 - 5; j < y0 + 13; j += 2) hline(s, x - 2, x + 2, j, 0xb8b8b8);
        break;
    case P_KICKBIKE:                                                      /* a sparkstötting in winter, a kick scooter otherwise */
        if (WIN) { line(s, x - 6, y - 2, x + 6, y - 2, 0x8a8a8a); vline(s, x + 4, y - 12, y - 2, 0xa02a20); hline(s, x + 2, x + 6, y - 12, 0xa02a20); rectf(s, x - 3, y - 6, 5, 4, 0xa02a20); }
        else { line(s, x - 5, y - 2, x + 4, y - 2, 0x2a6aa8); vline(s, x + 4, y - 12, y - 2, 0x8a8a8a); circlef(s, x - 5, y - 2, 1, 0x1a1a1a); circlef(s, x + 4, y - 1, 1, 0x1a1a1a); }
        break;
    case P_BOLLARD:                                                        /* pollare: black iron, a rounded head */
        ellipse_blend(s, x, y0 + 15, 4, 1, 0x000000, 80);
        rectf(s, x - 2, y0 + 9, 5, 6, 0x26262a); rectf(s, x - 3, y0 + 7, 7, 3, 0x34343a); pset(s, x - 1, y0 + 8, 0x6a6a72);
        if (WIN) hline(s, x - 3, x + 3, y0 + 6, 0xeef2f5);
        break;
    case P_WHEELBARROW:                                                    /* skottkärra */
        rectf(s, x0 + 3, y0 + 7, 9, 5, 0x3a7a3a); hline(s, x0 + 2, x0 + 12, y0 + 6, 0x5a9a5a); hline(s, x0 + 4, x0 + 10, y0 + 12, 0x2a5a2a);
        circlef(s, x0 + 13, y0 + 12, 2, 0x1a1a1a); pset(s, x0 + 13, y0 + 12, 0x8a8a8a);
        line(s, x0 + 3, y0 + 11, x0 + 0, y0 + 14, 0x7a5a38); line(s, x0 + 5, y0 + 12, x0 + 3, y0 + 15, 0x7a5a38);
        if (WIN) hline(s, x0 + 3, x0 + 11, y0 + 7, 0xeef2f5); else { pset(s, x0 + 6, y0 + 8, 0x6a4a2a); pset(s, x0 + 8, y0 + 8, 0x5a3a20); }
        break;
    case P_CRATES:                                                         /* fish crates, stacked */
        for (int k = 0; k < 3; k++) {
            uint32_t c = (v + k) % 3 == 0 ? 0x2a5aa8 : (v + k) % 3 == 1 ? 0xd8d8d0 : 0x3a8a6a;
            int yy = y0 + 11 - k * 4, xx = x0 + 2 + (k == 1 ? 1 : 0);
            rectf(s, xx, yy, 12, 4, c); hline(s, xx, xx + 11, yy, col_scale(c, 300)); rectf(s, xx + 4, yy + 1, 4, 1, col_scale(c, 140));
        }
        snow_cap(s, x0 + 2, x0 + 13, y0 + 2);
        break;
    case P_TABLE: rectf(s, x0 + 2, y0 + 6, 12, 3, 0x7a5038); vline(s, x0 + 3, y0 + 9, y0 + 14, 0x5a3a2a); vline(s, x0 + 12, y0 + 9, y0 + 14, 0x5a3a2a); break;
    case P_HYDRANT: rectf(s, x - 2, y0 + 6, 5, 9, 0xd8b020); rectf(s, x - 3, y0 + 5, 7, 2, 0xc8a010); break;
    case P_ROCK: {
        uint32_t c = 0x8e8480;
        for (int j = -6; j <= 0; j++) for (int i = -7; i <= 7; i++) {
            if (i * i * 3 + j * j * 8 > 150 + (int)(hash3(i, j, (int)v) % 20)) continue;
            uint32_t col = j < -4 ? sh(c, 300) : i > 3 ? sh(c, 180) : c;
            if (hash3(x + i, y + j, 78) % 9 == 0) col = 0x5a6a4a;
            if (WIN && j < -3) col = 0xf0f4f8;
            pset(s, x + i, y - 2 + j, col);
        }
        break;
    }
    case P_BOAT: {
        uint32_t c = (v & 1) ? 0xe8e4da : 0x8c2b1e;
        for (int k = 0; k < 5; k++) hline(s, x - 10 + k, x + 10 - k, y - 8 + k, k == 0 ? sh(c, 300) : c);
        rectf(s, x - 6, y - 10, 9, 2, 0x6a5038);
        break;
    }
    case P_BUSH: bush(s, x, y, v); break;
    case P_TRAMPOLINE:
        ellipse_blend(s, x, y - 9, 14, 8, 0x2a5a9a, 255);
        ellipse_blend(s, x, y - 9, 11, 6, WIN ? 0xeef2f6 : 0x1a1a1e, 255);
        for (int k = -12; k <= 12; k += 6) vline(s, x + k, y - 6, y, 0x5a5a5a);
        break;
    case P_CAR: car_h(s, x, y, CAR_COLORS[v % ARRAY_LEN(CAR_COLORS)], searched); break;
    case P_CAR_V: car_v(s, x, y, CAR_COLORS[v % ARRAY_LEN(CAR_COLORS)], searched); break;
    case P_BIKES:
        for (int k = 0; k < 3; k++) {
            int bx = x0 + 2 + k * 5;
            circle(s, bx, y0 + 4, 2, 0x2a2a2a); circle(s, bx, y0 + 12, 2, 0x2a2a2a);
            vline(s, bx, y0 + 4, y0 + 12, (k == 1) ? 0x2a6aa8 : (k == 2) ? 0xa02a20 : 0x3a3a3a);
        }
        hline(s, x0, x0 + 15, y0 + 9, 0x8a8a8a);
        break;
    case P_CART:                                                           /* kundvagn */
        rect_line(s, x - 6, y0 + 4, 12, 8, 0x9aa0a6);
        for (int i = x - 4; i < x + 6; i += 3) vline(s, i, y0 + 4, y0 + 11, 0x7a8086);
        vline(s, x + 6, y0 + 1, y0 + 4, 0x9aa0a6); hline(s, x + 4, x + 7, y0 + 1, 0x2a6aa8);
        pset(s, x - 5, y0 + 14, 0x1a1a1a); pset(s, x + 4, y0 + 14, 0x1a1a1a);
        if (searched) rect_blend(s, x - 5, y0 + 5, 10, 6, 0x000000, 60);
        break;
    }
}

void props_paint_flat(int x0, int y0, int x1, int y1) {
    Surf s; s.w = G->ww; s.h = G->wh; s.pitch = G->ww; s.px = G->world; surf_clip(&s, x0, y0, x1 - x0, y1 - y0);
    for (int i = 0; i < G->nprops; i++) {
        const Prop *p = &G->props[i];
        if (prop_is_tall(p->kind) || p->x + 40 < x0 || p->x - 40 >= x1 || p->y + 24 < y0 || p->y - 40 >= y1) continue;
        flat_prop(&s, p);
    }
}

void prop_bounds(const Prop *p, int *w, int *h) {
    switch (p->kind) {
    case P_BIRCH: case P_PINE: *w = 24; *h = 40; break;
    case P_FLAGPOLE: *w = 40; *h = 46; break;
    case P_MAYPOLE: *w = 28; *h = 48; break;
    case P_CONTAINER: case P_CONTAINER_V: *w = 48; *h = 26; break;
    case P_BUSSTOP: *w = 48; *h = 36; break;
    case P_STALL: *w = 40; *h = 36; break;
    default: *w = 32; *h = 36; break;
    }
}

/* tall props, every frame (sx, sy: the prop's feet on screen) */
void prop_draw(Surf *s, const Prop *p, int sx, int sy) {
    uint32_t v = p->var;
    int lit = powered_at(p->x, p->y);
    switch (p->kind) {
    case P_BIRCH: tree_birch(s, sx, sy - 1, v); break;
    case P_PINE: tree_pine(s, sx, sy - 1, v); break;
    case P_SPRUCE_SMALL: tree_spruce(s, sx, sy - 1, v, 1); break;
    case P_APPLE: tree_apple(s, sx, sy - 1, v); break;
    case P_LAMP: lamp(s, sx, sy - 2, lit); break;
    case P_LAMP_WALL:                                                      /* a lantern on the wall above */
        rectf(s, sx - 2, sy - 30, 5, 6, 0x2a2a2a);
        rectf(s, sx - 1, sy - 29, 3, 4, lit ? 0xffe0a0 : 0x5a5040);
        break;
    case P_FLAGPOLE: flagpole(s, sx, sy - 1, v); break;
    case P_BUSSTOP: bus_stop(s, sx + 8, sy - 1); break;
    case P_SWINGS: swings(s, sx, sy - 1); break;
    case P_SLIDE: case P_CLIMBER: climber(s, sx, sy - 1); break;
    case P_MAILBOXES: mailboxes(s, sx, sy - 1, p->loot == 2); break;
    case P_FOUNTAIN: fountain(s, sx, sy); break;
    case P_STATUE: statue(s, sx, sy); break;
    case P_WELL: well(s, sx, sy - 1); break;
    case P_CONTAINER: case P_CONTAINER_V: container(s, sx, sy - 1, v, p->loot == 2); break;
    case P_MAYPOLE: maypole(s, sx, sy - 1); break;
    case P_SIGN_MOOSE: sign_moose(s, sx, sy - 1); break;
    case P_SIGN_T: sign_t(s, sx, sy - 1); break;
    case P_PHONEBOX: phonebox(s, sx, sy - 1); break;
    case P_WASHLINE: washline(s, sx, sy - 1); break;
    case P_TICKET: ticket(s, sx, sy - 1); break;
    case P_FORKLIFT: forklift(s, sx, sy - 1); break;
    case P_BONFIRE: bonfire(s, sx, sy - 1); break;
    case P_SNOWMAN: snowman(s, sx, sy - 1); break;
    case P_STALL: stall(s, sx, sy - 1, v); break;
    case P_BUOY: buoy(s, sx, sy - 1); break;
    case P_NOTICE: notice(s, sx, sy - 1); break;
    default: break;
    }
}

/* lights that belong to props: street lamps once the power is on, the T lantern, wall lanterns */
void prop_lights(void) {
    G->nlights = 0;
    for (int i = 0; i < G->nprops && G->nlights < MAX_LIGHTS - 64; i++) {
        Prop *p = &G->props[i];
        if (!G->power_on) {
            if (p->kind == P_BONFIRE) G->lights[G->nlights++] = (Light){ p->x, p->y - 6, 60, 0xf0a040, 1.0f };
            continue;
        }
        if (p->kind == P_LAMP) G->lights[G->nlights++] = (Light){ p->x - 5, p->y - 4, 64, 0xf2c890, 1.0f, 1 };
        else if (p->kind == P_LAMP_WALL) G->lights[G->nlights++] = (Light){ p->x, p->y - 4, 44, 0xf2b65a, 0.9f, 1 };
        else if (p->kind == P_SIGN_T) G->lights[G->nlights++] = (Light){ p->x, p->y - 10, 36, 0xd8e8ff, 0.8f, 1 };
        else if (p->kind == P_TICKET) G->lights[G->nlights++] = (Light){ p->x, p->y - 8, 24, 0x8ad8f8, 0.7f, 1 };
        else if (p->kind == P_BONFIRE) G->lights[G->nlights++] = (Light){ p->x, p->y - 6, 60, 0xf0a040, 1.0f };
    }
    /* lit shop windows and stations glow onto the pavement */
    for (int i = 0; i < G->nb && G->power_on && G->nlights < MAX_LIGHTS - 16; i++) {
        Building *b = &G->b[i];
        int st = b->style;
        if (st == BS_SHOP || st == BS_KIOSK || st == BS_STATION || st == BS_MALL)
            G->lights[G->nlights++] = (Light){ (b->x + b->w / 2.0f) * TS, (b->y + b->h) * TS + 4, 26 + b->w * 6, 0xf0e0b0, 0.8f, 1 };
        else if (b->lit && (st == BS_LAMELL || st == BS_LAMELL_BRICK || st == BS_OLDTOWN || st == BS_VILLA || st == BS_SCHOOL || st == BS_CHURCH))
            G->lights[G->nlights++] = (Light){ (b->x + b->w / 2.0f) * TS, (b->y + b->h) * TS - 4, 18 + b->w * 4, 0xf2c070, 0.45f, 1 };
    }
}

/* ---------------------------------------------------------------- the CoD pieces */
static void barrier_draw(Surf *s, Inter *it, int x0, int y0) {          /* 32 x 32 at the port */
    int w = it->tw * TS, h = it->th * TS;
    switch (it->c) {
    case 0:                                                                /* police barriers, red and white */
        for (int row = 0; row < 2; row++) {
            int by = y0 + 6 + row * 14;
            for (int x = 0; x < w; x++) for (int j = 0; j < 4; j++) pset(s, x0 + x, by + j, ((x + j) / 4) & 1 ? 0xd02020 : 0xf0f0f0);
            vline(s, x0 + 3, by + 4, by + 9, 0x3a3a3a); vline(s, x0 + w - 4, by + 4, by + 9, 0x3a3a3a);
        }
        rectf(s, x0 + w / 2 - 12, y0 + 11, 24, 7, 0xf0d020);
        text(s, FONT_SMALL, x0 + w / 2 - 11, y0 + 12, 0x1a1a1a, "STOPP");
        break;
    case 1:                                                                /* byggstängsel: mesh panels on concrete feet */
        for (int row = 0; row < 2; row++) {
            int by = y0 + row * 16;
            for (int x = 0; x < w; x++) for (int j = 2; j < 13; j++) if (((x + j) % 3 == 0) || ((x - j + 99) % 3 == 0)) pset(s, x0 + x, by + j, 0xa8acb0);
            rect_line(s, x0, by + 1, w, 13, 0x7a7e84);
            rectf(s, x0 + 2, by + 13, 6, 3, 0x8a8a86); rectf(s, x0 + w - 8, by + 13, 6, 3, 0x8a8a86);
            rectf(s, x0 + w / 2 - 6, by + 4, 12, 5, 0xe06a1a);
        }
        break;
    default:                                                               /* a wrecked car and debris */
        car_h(s, x0 + w / 2, y0 + 18, 0x5a5e54, 0);
        for (int k = 0; k < 12; k++) {
            uint32_t hh = hash3(it->tx, it->ty, k);
            rectf(s, x0 + (int)(hh % (unsigned)w) - 2, y0 + 18 + (int)((hh >> 8) % 12), 4, 2, (hh >> 16) & 1 ? 0x6a5038 : 0x7a7a7a);
        }
        line(s, x0 + 2, y0 + 28, x0 + w - 3, y0 + 22, 0x6a5038);
        break;
    }
    (void)h;
}

static void window_draw(Surf *s, Inter *it, int x0, int y0) {           /* boards over the window opening */
    rectf(s, x0 + 3, y0 + 2, 10, 12, 0x0e0c0c);
    for (int k = 0; k < it->state; k++) {
        int by = y0 + 2 + k * 2;
        int tilt = (k & 1) ? 1 : -1;
        line(s, x0 + 1, by + (tilt > 0 ? 0 : 1), x0 + 14, by + (tilt > 0 ? 1 : 0), 0x8a6a3a);
        line(s, x0 + 1, by + 1 + (tilt > 0 ? 0 : 1), x0 + 14, by + 1 + (tilt > 0 ? 1 : 0), 0x6a4a2a);
        pset(s, x0 + 2, by + 1, 0x3a3a3a); pset(s, x0 + 13, by + 1, 0x3a3a3a);
    }
}

static void perk_draw(Surf *s, Inter *it, int x, int y) {               /* a vending machine, 16 x 28, feet at y */
    const PerkDef *pd = &PERKS[it->a];
    int on = powered_at(it->x, it->y) || it->a == PK_KANELBULLE;
    if (it->a == PK_KANELBULLE && G->p.bulle_used >= 3) return;          /* gone after three, as solo Quick Revive */
    uint32_t c = on ? pd->color : sh(pd->color, 140);
    int x0 = x - 8, y0 = y - 28;
    rect_blend(s, x0 + 1, y - 2, 16, 3, 0x000000, 90);
    rectf(s, x0, y0, 16, 27, c);
    rectf(s, x0, y0, 16, 6, pd->color2);
    rect_line(s, x0, y0, 16, 27, sh(c, 120));
    rectf(s, x0 + 2, y0 + 8, 12, 12, on ? 0x1a2a3a : 0x0a0e12);
    const Img *icon = art(pd->icon);
    if (icon) blit_ex(s, icon, x0 + 2, y0 + 8, 0, 0, 0, on ? 255 : 110);
    rectf(s, x0 + 3, y0 + 22, 10, 3, 0x1a1a1a);
    if (on) { pset(s, x0 + 13, y0 + 2, (int)(G->time * 4) & 1 ? 0xffffff : 0xffe080); hline(s, x0 + 1, x0 + 14, y0 + 7, sh(pd->color2, 330)); }
    snow_cap(s, x0, x0 + 15, y0 - 1);
}

static void pap_draw(Surf *s, Inter *it, int x, int y) {                /* Smedjan: a forge and an anvil */
    int on = G->power_on;
    int x0 = x - 16, y0 = y - 28;
    rect_blend(s, x0 + 2, y - 3, 32, 4, 0x000000, 90);
    rectf(s, x0 + 1, y0 + 6, 14, 20, 0x4a3a34);                            /* the furnace */
    rect_line(s, x0 + 1, y0 + 6, 14, 20, 0x2a1a14);
    rectf(s, x0 + 4, y0 + 12, 8, 8, on ? 0xf08a2a : 0x2a1a14);
    if (on) for (int k = 0; k < 6; k++) pset(s, x0 + 5 + (int)(hash3(k, (int)(G->time * 12), 79) % 6), y0 + 13 + (int)(hash3((int)(G->time * 12), k, 80) % 6), 0xffe060);
    rectf(s, x0 + 5, y0, 6, 6, 0x3a2a24);                                  /* chimney */
    /* the anvil */
    rectf(s, x0 + 18, y0 + 14, 13, 4, 0x3a3e44); rectf(s, x0 + 16, y0 + 14, 4, 2, 0x3a3e44);
    rectf(s, x0 + 21, y0 + 18, 7, 4, 0x2a2e34); rectf(s, x0 + 19, y0 + 22, 11, 4, 0x2a2e34);
    hline(s, x0 + 18, x0 + 30, y0 + 14, 0x6a6e74);
    if (it->state == 1) {                                                  /* working: the weapon glows on the anvil */
        rectf(s, x0 + 20, y0 + 11, 10, 3, 0xffb040);
    }
    sign_board_small(s, x0 + 16, y0 - 6, "SMEDJAN", on ? 0x7a2a8a : 0x3a2a3a, 0xffe0ff);
}

void sign_board_small(Surf *s, int cx, int y, const char *txt, uint32_t bg, uint32_t fg) {
    int tw = text_w(FONT_SMALL, txt), w = tw + 4;
    bevel(s, cx - w / 2, y, w, 8, bg, sh(bg, 320), sh(bg, 150));
    text(s, FONT_SMALL, cx - tw / 2, y + 2, fg, txt);
}

static void trap_draw(Surf *s, Inter *it, int x, int y) {               /* the elstängsel's cabinet: a lever, a lamp */
    int x0 = x - 6, y0 = y - 20;
    rect_blend(s, x0 + 1, y - 2, 12, 3, 0x000000, 90);
    rectf(s, x0, y0, 12, 19, 0x5a6a5a); rect_line(s, x0, y0, 12, 19, 0x2e3a2e);
    hline(s, x0 + 1, x0 + 10, y0 + 1, 0x8a9a8a);
    for (int j = 0; j < 4; j++) hline(s, x0 + 3 - j / 2, x0 + 3 + j / 2, y0 + 3 + j, 0xf0c818);    /* the flash sign */
    pset(s, x0 + 3, y0 + 5, 0x1a1a1a);
    int on = it->state == 1;
    rectf(s, x0 + 7, y0 + 5, 3, 9, 0x2a2e34);
    rectf(s, x0 + 8, on ? y0 + 4 : y0 + 10, 1, 5, 0xc02020); rectf(s, x0 + 7, on ? y0 + 3 : y0 + 14, 3, 2, 0x1a1a1a);
    uint32_t lamp = !G->power_on ? 0x302020 : it->state == 0 ? 0x40ff60 : it->state == 1 ? ((int)(G->time * 10) & 1 ? 0xffffa0 : 0x8080ff) : 0xff4040;
    rectf(s, x0 + 2, y0 + 12, 3, 3, lamp);
    snow_cap(s, x0, x0 + 11, y0 - 1);
}
static void power_draw(Surf *s, Inter *it, int x, int y) {              /* the switchgear with its lever */
    (void)it;
    int x0 = x - 8, y0 = y - 26;
    rect_blend(s, x0 + 1, y - 2, 16, 3, 0x000000, 90);
    rectf(s, x0, y0, 16, 25, 0x6a7078); rect_line(s, x0, y0, 16, 25, 0x3a3e44);
    hline(s, x0 + 1, x0 + 14, y0 + 1, 0x9aa0a8);
    for (int j = 0; j < 5; j++) hline(s, x0 + 4 - j / 2, x0 + 4 + j / 2, y0 + 4 + j, 0xf0c818);   /* the warning sign */
    pset(s, x0 + 4, y0 + 6, 0x1a1a1a);
    rectf(s, x0 + 9, y0 + 8, 4, 12, 0x2a2e34);
    int up = G->power_on;
    rectf(s, x0 + 10, up ? y0 + 6 : y0 + 15, 2, 6, 0xc02020);              /* the lever */
    rectf(s, x0 + 9, up ? y0 + 5 : y0 + 19, 4, 2, 0x1a1a1a);
    pset(s, x0 + 3, y0 + 20, up ? 0x40ff60 : 0x305030);
    pset(s, x0 + 5, y0 + 20, up ? 0x305030 : (int)(G->time * 2) & 1 ? 0xff4040 : 0x502020);
    snow_cap(s, x0, x0 + 15, y0 - 1);
}

static void box_draw(Surf *s, Inter *it, int x, int y) {                /* Lådan: the Mystery Box, 30 x 14 */
    int x0 = x - 15, y0 = y - 16;
    int active = G->box_spots[G->box_at] == (int)(it - G->it) && !G->box_moving;
    if (!active) {                                                          /* an empty spot: a faint chalk mark */
        rect_line(s, x0 + 2, y0 + 8, 26, 6, 0x5a5a5a);
        return;
    }
    rect_blend(s, x0 + 1, y - 3, 30, 4, 0x000000, 100);
    int open = it->state >= 1;
    rectf(s, x0, y0 + 4, 30, 11, 0x6a4a2a);
    for (int k = 0; k < 30; k += 6) vline(s, x0 + k, y0 + 4, y0 + 14, 0x4a3018);
    rect_line(s, x0, y0 + 4, 30, 11, 0x3a2410);
    hline(s, x0, x0 + 29, y0 + 9, 0x3a2410);
    if (open) {
        rectf(s, x0, y0 - 3, 30, 4, 0x5a3a20);                              /* the lid up */
        rectf(s, x0 + 2, y0 + 5, 26, 4, 0xffe8a0);
    } else {
        rectf(s, x0 - 1, y0 + 2, 32, 3, 0x7a5a32);
        text(s, FONT_SMALL, x0 + 4, y0 + 7, 0xe0c060, "?");
        text(s, FONT_SMALL, x0 + 24, y0 + 7, 0xe0c060, "?");
    }
    snow_cap(s, x0 - 1, x0 + 30, y0 + 1);
}

void inter_draw(Surf *s, Inter *it, int sx, int sy) {
    switch (it->type) {
    case IT_BARRIER: if (!it->state) barrier_draw(s, it, sx, sy); break;
    case IT_WINDOW: window_draw(s, it, sx, sy); break;
    case IT_PERK: perk_draw(s, it, sx + it->tw * TS / 2, sy + it->th * TS); break;
    case IT_PAP: pap_draw(s, it, sx + it->tw * TS / 2, sy + it->th * TS); break;
    case IT_POWER: power_draw(s, it, sx + it->tw * TS / 2, sy + it->th * TS); break;
    case IT_TRAP: trap_draw(s, it, sx + it->tw * TS / 2, sy + it->th * TS); break;
    case IT_BOX: box_draw(s, it, sx + it->tw * TS / 2, sy + it->th * TS); break;
    case IT_WALLBUY: {                                                       /* a chalk outline of the weapon */
        if (it->a >= 0) {
            const Img *im = art(WEAPONS[it->a].icon);
            if (im) blit_silhouette(s, im, sx + 8 - im->w / 2, sy + 3, 0, 0xe8e4d8, 170);
        } else if (it->a == -1) {                                            /* the axe */
            line(s, sx + 3, sy + 13, sx + 12, sy + 3, 0xe8e4d8); rectf(s, sx + 10, sy + 2, 4, 5, 0xe8e4d8);
        } else {                                                             /* grenades */
            circle(s, sx + 6, sy + 9, 3, 0xe8e4d8); circle(s, sx + 11, sy + 9, 3, 0xe8e4d8);
        }
        break;
    }
    default: break;
    }
}
