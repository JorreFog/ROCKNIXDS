// world.c: paints the town into one big bitmap once per map (and again where it changes): ground textures with
// their edges, road markings and other flat details, and the buildings' roofs and south facades. Each frame the
// renderer copies the visible part and draws the moving and standing-up things over it.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

static inline uint32_t *wpx(int x, int y) { return &G->world[(size_t)y * G->ww + x]; }
static inline void wset(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < G->ww && y < G->wh) *wpx(x, y) = c & 0xFFFFFF; }
static inline void wblend(int x, int y, uint32_t c, int a) {
    if (x < 0 || y < 0 || x >= G->ww || y >= G->wh) return;
    *wpx(x, y) = col_mix(*wpx(x, y), c, a);
}
static inline uint32_t vary(uint32_t c, int x, int y, int salt, int amp) {
    int v = (int)(hash3(x, y, salt) % (unsigned)(2 * amp + 1)) - amp;
    return RGB(CLAMP((int)CR(c) + v, 0, 255), CLAMP((int)CG(c) + v, 0, 255), CLAMP((int)CB(c) + v, 0, 255));
}
static inline uint32_t shade(uint32_t c, int k) { return col_scale(c, k); }   /* 256 = same */

/* a surface over the world bitmap, for the drawing helpers in gfx.c */
static Surf wsurf(void) { Surf s; s.w = G->ww; s.h = G->wh; s.pitch = G->ww; s.px = G->world; surf_noclip(&s); return s; }

/* ---------------------------------------------------------------- ground */
uint32_t ground_color(int g, int season) {
    int w = season == SEASON_WINTER, su = season == SEASON_SUMMER;
    switch (g) {
    case G_ASPHALT: return w ? 0x4a4c52 : su ? 0x4a4c50 : 0x36383e;
    case G_PARKING: return w ? 0x505258 : 0x404248;
    case G_SCHOOLYARD: return w ? 0x5e6066 : 0x55575c;
    case G_SIDEWALK: return w ? 0x9a9894 : 0x8a8a86;
    case G_COBBLE: return w ? 0x7a766f : 0x6e6a63;
    case G_GRASS: return w ? 0xe2e8ee : su ? 0x5e8c3a : 0x56693a;
    case G_GRAVEL: return w ? 0xd0d4d6 : 0x9a907e;
    case G_SAND: return w ? 0xe0e4e8 : 0xb8a682;
    case G_DIRT: return w ? 0xd8dde2 : 0x5a4636;
    case G_WATER: return w ? 0xa8c4d4 : su ? 0x2e4e66 : 0x23384a;
    case G_DECK: return w ? 0x8a7a6a : 0x6a5038;
    case G_TURF: return w ? 0xdce4e8 : 0x3f8a3a;
    case G_SOIL: return w ? 0xd4dade : 0x4a3628;
    case G_PLAZA: return w ? 0xb0aca4 : 0xa7a39a;
    case G_FLOOR: return 0x8a8a90;
    case G_RAIL: return w ? 0xb8bcc0 : 0x6a6458;
    case G_ROCK: return w ? 0xb0aaa8 : 0x8e8480;
    case G_FOREST: return w ? 0x2a3a30 : su ? 0x23402a : 0x1e3020;
    default: return 0xff00ff;
    }
}

static void paint_ground(int tx, int ty) {
    Tile *t = &G->t[ty][tx];
    int g = t->g, season = G->season, win = season == SEASON_WINTER;
    uint32_t base = ground_color(g, season);
    int ox = tx * TS, oy = ty * TS;
    for (int j = 0; j < TS; j++)
        for (int i = 0; i < TS; i++) {
            int x = ox + i, y = oy + j;
            uint32_t h = hash3(x, y, g * 31 + 5), c = base;
            switch (g) {
            case G_ASPHALT: case G_PARKING: case G_SCHOOLYARD:
                c = vary(base, x, y, 1, 4);
                if (h % 23 == 0) c = shade(base, 300);
                else if (h % 29 == 0) c = shade(base, 200);
                if (win && (h % 7 == 0)) c = 0x7d7468;                          /* grit */
                break;
            case G_SIDEWALK: {                                                  /* 8x8 concrete slabs */
                int sx = x / 8, sy = y / 8;
                c = vary(base, sx, sy, 3, 6);
                c = vary(c, x, y, 4, 3);
                if (x % 8 == 0 || y % 8 == 0) c = shade(base, 205);
                if (win && h % 9 == 0) c = 0x7d7468;
                break;
            }
            case G_PLAZA: {                                                     /* big slabs, offset rows */
                int row = y / 8, sx = (x + (row & 1) * 6) / 12;
                c = vary(base, sx, row, 5, 7);
                c = vary(c, x, y, 6, 2);
                if ((x + (row & 1) * 6) % 12 == 0 || y % 8 == 0) c = shade(base, 215);
                break;
            }
            case G_COBBLE: {                                                    /* rounded setts in rows */
                int row = y / 4, sx = (x + (row & 1) * 2) / 4;
                int lx = (x + (row & 1) * 2) % 4, ly = y % 4;
                c = vary(base, sx, row, 7, 14);
                if (ly == 0 || lx == 0) c = shade(base, 150);
                else if (ly == 1 && lx == 1) c = shade(c, 300);
                if (win && (lx == 0 || ly == 0) && h % 3 == 0) c = 0xe8eef4;
                break;
            }
            case G_GRASS:
                if (win) {
                    c = vary(base, x, y, 8, 3);
                    if (h % 41 == 0) c = 0xffffff;
                    else if (h % 13 == 0) c = 0xc8d4e0;
                } else {
                    c = vary(base, x, y, 9, 6);
                    if (h % 7 == 0) c = shade(base, 300);
                    else if (h % 11 == 0) c = shade(base, 190);
                    if (season == SEASON_AUTUMN && h % 37 == 0) c = (h >> 8) % 3 == 0 ? 0xc8642a : (h >> 8) % 3 == 1 ? 0xd8a83a : 0xa83a2a;
                    if (season == SEASON_SUMMER && h % 97 == 0) c = (h >> 9) % 3 == 0 ? 0xf0f0f0 : (h >> 9) % 3 == 1 ? 0xf0d040 : 0xa070d0;
                }
                break;
            case G_GRAVEL: c = vary(base, x, y, 10, 10); if (h % 5 == 0) c = shade(c, 180); if (h % 17 == 0) c = shade(c, 320); break;
            case G_SAND: c = vary(base, x, y, 11, 6); if (h % 9 == 0) c = shade(c, 220); break;
            case G_DIRT: case G_SOIL:
                c = vary(base, x, y, 12, 7);
                if (g == G_SOIL && !win && (y % 4 == 0)) c = shade(base, 170);
                if (g == G_SOIL && !win && (y % 4 == 2) && h % 3 == 0) c = 0x4a7a34;   /* rows of green */
                break;
            case G_WATER:
                if (win) {
                    c = vary(base, x, y, 13, 4);
                    if ((h % 61) == 0) c = 0xe8f4fa;
                    if (((x + y * 3) % 23) == 0 && h % 2) c = shade(base, 180);     /* cracks */
                } else {
                    c = base;
                    int wave = ((x + (y / 3) * 5 + (int)(hash3(y / 3, 0, 77) % 16)) % 16);
                    if (wave < 3 && y % 3 == 0) c = shade(base, 150);
                    if (h % 53 == 0) c = shade(base, 190);
                }
                break;
            case G_DECK: {                                                      /* planks across */
                c = vary(base, x / 16, y / 4, 14, 8);
                if (y % 4 == 3) c = shade(base, 140);
                if (x % 16 == (y / 4 * 7) % 16) c = shade(base, 160);
                break;
            }
            case G_TURF:
                c = vary(base, x, y, 15, 3);
                if (!win && ((x / 8) & 1)) c = shade(c, 235);                   /* mowing stripes */
                break;
            case G_FLOOR: c = (x % 8 == 0 || y % 8 == 0) ? 0x707078 : vary(base, x, y, 16, 3); break;
            case G_RAIL:
                c = vary(base, x, y, 17, 10);                                    /* ballast */
                if (h % 4 == 0) c = shade(c, 150);
                if ((y % 16) == 3 || (y % 16) == 12) c = 0x8a8a92;               /* rails */
                else if ((y % 16) == 4 || (y % 16) == 13) c = 0x5a5a62;
                if (x % 6 < 2 && (y % 16) > 1 && (y % 16) < 15 && (y % 16) != 3 && (y % 16) != 12) c = win ? 0x8a7a6a : 0x4a3a2a;   /* sleepers */
                break;
            case G_ROCK: {                                                      /* glacier-smoothed granite */
                c = vary(base, x, y, 18, 6);
                if (h % 9 == 0) c = 0xa08a84;
                if (h % 31 == 0) c = 0x5a6a4a;                                   /* lichen */
                if (win && j < 5 + (int)(hash3(x, ty, 3) % 3)) c = 0xeef2f5;
                break;
            }
            case G_FOREST: {                                                    /* dense spruce seen from above */
                int cx = (x / 8) * 8 + 4, cy = (y / 8) * 8 + 4;
                int d = abs(x - cx) + abs(y - cy) + (int)(hash3(x / 8, y / 8, 19) % 3);
                c = d < 3 ? shade(base, 330) : d < 5 ? shade(base, 260) : d < 7 ? base : shade(base, 150);
                if (win && d < 3 && h % 2) c = 0xdde6ee;
                break;
            }
            }
            *wpx(x, y) = c;
        }
}

/* where two grounds meet: curbs, lawn edges, shores */
static void paint_edges(int tx, int ty) {
    Tile *t = &G->t[ty][tx];
    int g = t->g, ox = tx * TS, oy = ty * TS;
    static const int d[4][2] = { {0,-1},{0,1},{-1,0},{1,0} };
    for (int k = 0; k < 4; k++) {
        Tile *n = tile_at(tx + d[k][0], ty + d[k][1]);
        if (!n || n->g == g) continue;
        int ng = n->g;
        for (int s = 0; s < TS; s++) {
            int x, y, x2, y2;
            if (k == 0) { x = ox + s; y = oy; x2 = x; y2 = y + 1; }
            else if (k == 1) { x = ox + s; y = oy + TS - 1; x2 = x; y2 = y - 1; }
            else if (k == 2) { x = ox; y = oy + s; x2 = x + 1; y2 = y; }
            else { x = ox + TS - 1; y = oy + s; x2 = x - 1; y2 = y; }
            if ((g == G_SIDEWALK || g == G_PLAZA) && (ng == G_ASPHALT || ng == G_PARKING)) {
                wset(x, y, 0xb8b8b4); wset(x2, y2, 0x9a9a96);                       /* the curb */
            } else if (g == G_GRASS && ng != G_FOREST && ng != G_WATER) {
                if (hash3(x, y, 21) % 3) wset(x, y, shade(*wpx(x, y), 170));
            } else if (g == G_WATER && ng != G_WATER) {
                wset(x, y, G->season == SEASON_WINTER ? 0xdde8ee : 0x5a7a8a);         /* the shore */
                if (hash3(x, y, 22) % 2) wset(x2, y2, G->season == SEASON_WINTER ? 0xc0d4e0 : 0x3a5a6e);
            } else if (g == G_TURF) {
                wset(x, y, 0xeeeeee);                                                  /* the pitch's border line */
            } else if (g == G_DECK && ng == G_WATER) {
                wset(x, y, 0x3a2a1a);
            } else if ((g == G_COBBLE || g == G_GRAVEL) && ng == G_GRASS) {
                if (hash3(x, y, 23) % 2) wset(x, y, shade(*wpx(x, y), 230));
            }
        }
    }
}

static void paint_deco(int tx, int ty) {
    Tile *t = &G->t[ty][tx];
    int ox = tx * TS, oy = ty * TS, win = G->season == SEASON_WINTER;
    Surf s = wsurf();
    uint32_t paint = win ? 0xd8dce0 : 0xe8e8e0;
    switch (t->deco) {
    case D_LINE_H: rectf(&s, ox + 2, oy + 7, 12, 2, paint); break;
    case D_LINE_V: rectf(&s, ox + 7, oy + 2, 2, 12, paint); break;
    case D_ZEBRA_H: for (int k = 0; k < 4; k++) rectf(&s, ox + k * 4, oy + 1, 2, 14, paint); break;
    case D_ZEBRA_V: for (int k = 0; k < 4; k++) rectf(&s, ox + 1, oy + k * 4, 14, 2, paint); break;
    case D_PARKING_V: rectf(&s, ox, oy, 1, 16, paint); break;
    case D_PARKING_H: rectf(&s, ox, oy, 16, 1, paint); break;
    case D_MANHOLE:
        circlef(&s, ox + 8, oy + 8, 6, 0x2a2a2e);
        circle(&s, ox + 8, oy + 8, 6, 0x4a4a50);
        for (int k = -4; k <= 4; k += 2) hline(&s, ox + 8 - 4, ox + 8 + 4, oy + 8 + k, 0x3c3c42);
        if (win) { pset(&s, ox + 4, oy + 5, 0xdde6ee); pset(&s, ox + 11, oy + 9, 0xdde6ee); }
        break;
    case D_DRAIN: rectf(&s, ox + 4, oy + 12, 8, 3, 0x2a2a2e); for (int k = 0; k < 4; k++) vline(&s, ox + 5 + k * 2, oy + 12, oy + 14, 0x5a5a60); break;
    case D_FLOWERS:
        for (int k = 0; k < 10; k++) {
            uint32_t h = hash3(tx, ty, k);
            int x = ox + 1 + h % 14, y = oy + 1 + (h >> 8) % 14;
            uint32_t c = win ? 0xe8eef4 : (h >> 16) % 4 == 0 ? 0xe84a6a : (h >> 16) % 4 == 1 ? 0xf0d040 : (h >> 16) % 4 == 2 ? 0xf0f0f0 : 0x9a6ad0;
            pset(&s, x, y, c); pset(&s, x, y + 1, win ? 0xc8d4e0 : 0x3a6a2a);
        }
        break;
    case D_HOPSCOTCH: {                                    /* "hage", chalked on the asphalt */
        uint32_t ch = 0xe8e0d0;
        rect_line(&s, ox + 4, oy - 12, 8, 7, ch); rect_line(&s, ox + 2, oy - 6, 6, 6, ch); rect_line(&s, ox + 8, oy - 6, 6, 6, ch);
        rect_line(&s, ox + 4, oy, 8, 7, ch); rect_line(&s, ox + 4, oy + 6, 8, 7, ch);
        break;
    }
    case D_PITCH_LINE_V: vline(&s, ox + 7, oy, oy + 15, 0xeeeeee); break;
    case D_PITCH_CIRCLE: circle(&s, ox + 7, oy + 8, 9, 0xeeeeee); vline(&s, ox + 7, oy, oy + 15, 0xeeeeee); break;
    case D_GRAVE_PLOT: rect_blend(&s, ox + 3, oy + 2, 10, 13, win ? 0xf0f4f8 : 0x3a2a1a, 120); break;
    case D_LEAVES:
        for (int k = 0; k < 6; k++) { uint32_t h = hash3(tx, ty, k + 40); pset(&s, ox + h % 16, oy + (h >> 8) % 16, (h >> 16) & 1 ? 0xc8642a : 0xd8a83a); }
        break;
    case D_PUDDLE: ellipse_blend(&s, ox + 8, oy + 9, 6, 3, 0x1a2a3a, 140); break;
    }
}

/* ---------------------------------------------------------------- buildings */
typedef struct { Surf s; int x0, y0, w, h, fy, fh; Building *b; } Bp;   /* pixels: footprint, facade top, height */

static void window_px(Bp *p, int x, int y, int w, int h, int lit, int cross, uint32_t frame) {
    Surf *s = &p->s;
    uint32_t glass = lit ? 0xffd27a : 0x1c2834, glass2 = lit ? 0xf0a840 : 0x2c3e4e;
    rectf(s, x - 1, y - 1, w + 2, h + 2, frame);
    rectf(s, x, y, w, h, glass);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (i + j < 3) pset(s, x + i, y + j, glass2);   /* a reflection */
    if (!lit && w > 2) pset(s, x + w - 1, y + h - 1, 0x3a4a5a);
    if (cross) { vline(s, x + w / 2, y, y + h - 1, frame); hline(s, x, x + w - 1, y + h / 2 - (h > 5), frame); }
}

static int lit_window(Building *b, int i, int j) {
    if (!b->lit || !G->power_on) return 0;
    return hash3((int)b->seed, i, j) % 100 < 38;
}

static void roof_flat(Bp *p, uint32_t roof) {
    Surf *s = &p->s; int win = G->season == SEASON_WINTER;
    int rh = p->fy - p->y0;
    uint32_t base = win ? 0xe6ecf2 : roof;
    for (int y = p->y0; y < p->fy; y++)
        for (int x = p->x0; x < p->x0 + p->w; x++) {
            uint32_t c = vary(base, x, y, 31, win ? 3 : 6);
            if (hash3(x, y, 32) % 11 == 0) c = shade(c, win ? 235 : 160);
            pset(s, x, y, c);
        }
    /* parapet */
    rect_line(s, p->x0, p->y0, p->w, rh, win ? 0xc8d2dc : shade(roof, 330));
    hline(s, p->x0 + 1, p->x0 + p->w - 2, p->y0 + 1, win ? 0xf6f8fa : shade(roof, 380));
    hline(s, p->x0, p->x0 + p->w - 1, p->fy - 1, shade(roof, 140));
    /* vents and stairwell boxes */
    for (int k = 0; k < p->w / 40 + 1; k++) {
        uint32_t h = hash3((int)p->b->seed, k, 33);
        int vx = p->x0 + 6 + (int)(h % (unsigned)MAX(1, p->w - 18)), vy = p->y0 + 4 + (int)((h >> 8) % (unsigned)MAX(1, rh - 12));
        if (rh < 14) break;
        bevel(s, vx, vy, 8, 6, win ? 0xdfe6ec : 0x7a7e86, win ? 0xffffff : 0x9a9ea6, 0x4a4e56);
        rect_blend(s, vx + 1, vy + 6, 8, 2, 0x000000, 70);
    }
}

static void roof_pitched(Bp *p, uint32_t roof, int ridge_vertical) {
    Surf *s = &p->s; int win = G->season == SEASON_WINTER;
    int rh = p->fy - p->y0;
    (void)ridge_vertical;
    for (int y = p->y0; y < p->fy; y++)
        for (int x = p->x0; x < p->x0 + p->w; x++) {
            int ly = y - p->y0;
            int upper = ly < rh / 2;
            uint32_t c = upper ? shade(roof, 300) : roof;                        /* the far slope catches the light */
            if ((ly % 4) == 3) c = shade(c, 170);                                /* rows of tiles */
            if (((x + (ly / 4) * 3) % 6) == 0) c = shade(c, 200);
            if (win) c = upper ? ((ly % 4 == 3) ? 0xc8d4e0 : 0xeef2f6) : ((ly % 4 == 3) ? 0xb0c0d0 : 0xdde6ee);
            pset(s, x, y, c);
        }
    hline(s, p->x0, p->x0 + p->w - 1, p->y0 + rh / 2, win ? 0xffffff : shade(roof, 380));   /* the ridge */
    rect_line(s, p->x0, p->y0, p->w, rh, shade(roof, 120));
    hline(s, p->x0, p->x0 + p->w - 1, p->fy - 1, shade(roof, 90));                          /* the eaves' shadow */
    /* a chimney */
    if (p->b->style == BS_VILLA || p->b->style == BS_OLDTOWN || p->b->style == BS_SAUNA) {
        uint32_t h = hash3((int)p->b->seed, 1, 34);
        int cx = p->x0 + 4 + (int)(h % (unsigned)MAX(1, p->w - 12));
        int cy = p->y0 + 1;
        bevel(s, cx, cy, 6, 8, 0x8a4b38, 0xa86a50, 0x5a2a20);
        rectf(s, cx + 1, cy + 1, 4, 2, 0x2a1a14);
        if (win) hline(s, cx, cx + 5, cy, 0xffffff);
    }
}

static void sign_board(Bp *p, int cx, int y, const char *txt, uint32_t bg, uint32_t fg) {
    Surf *s = &p->s;
    int tw = text_w(FONT_SMALL, txt), w = tw + 6, x = cx - w / 2;
    if (w > p->w - 2) { w = p->w - 2; x = p->x0 + 1; }
    bevel(s, x, y, w, 9, bg, shade(bg, 320), shade(bg, 150));
    surf_clip(s, x + 1, y + 1, w - 2, 7);
    text(s, FONT_SMALL, cx - tw / 2, y + 2, fg, txt);
    surf_noclip(s);
}

static void door_px(Bp *p, int x, int y, int w, int h, uint32_t col, uint32_t frame) {
    Surf *s = &p->s;
    rectf(s, x - 1, y - 1, w + 2, h + 1, frame);
    rectf(s, x, y, w, h, col);
    vline(s, x + w / 2, y, y + h - 1, shade(col, 150));
    pset(s, x + w / 2 - 2, y + h / 2, 0xd8c070); pset(s, x + w / 2 + 1, y + h / 2, 0xd8c070);
    hline(s, x - 2, x + w + 1, y + h, 0x6a6a6a);   /* the step */
}

static void facade(Bp *p) {
    Surf *s = &p->s; Building *b = p->b; int win = G->season == SEASON_WINTER;
    int x0 = p->x0, y0 = p->fy, w = p->w, h = p->fh;
    uint32_t wall = b->wall, trim = b->trim ? b->trim : 0xe8e4da;
    int floors = MAX(1, h / TS);
    /* the wall's material */
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            uint32_t c = wall; int lx = x - x0, ly = y - y0;
            switch (b->style) {
            case BS_LAMELL: c = vary(wall, x, y, 41, 3); if (lx % 32 == 0 || ly % 16 == 15) c = shade(wall, 200); break;   /* concrete panels */
            case BS_LAMELL_BRICK: case BS_SCHOOL: case BS_TVATT: case BS_BELLTOWER:
                if (b->style == BS_BELLTOWER) { c = vary(wall, x / 2, 0, 42, 6); if (lx % 3 == 0) c = shade(wall, 170); break; }
                c = vary(wall, (x + (ly / 3 & 1) * 3) / 6, ly / 3, 43, 10);
                if (ly % 3 == 2 || (x + (ly / 3 & 1) * 3) % 6 == 0) c = shade(wall, 180);        /* brick and mortar */
                break;
            case BS_VILLA: case BS_COTTAGE: case BS_SHED: case BS_SAUNA:
                c = (lx % 4 == 0) ? shade(wall, 180) : vary(wall, x, y / 4, 44, 4);              /* vertical boards */
                break;
            case BS_OLDTOWN: case BS_CHURCH: case BS_TOWER: c = vary(wall, x, y, 45, 5); if (hash3(x, y, 46) % 23 == 0) c = shade(wall, 225); break;
            case BS_WAREHOUSE: c = (lx % 3 == 0) ? shade(wall, 170) : (lx % 3 == 1) ? shade(wall, 290) : wall; break;   /* corrugated */
            case BS_GREENHOUSE: c = (lx % 8 == 0 || ly % 8 == 0) ? 0xd8dcd8 : vary(0x8ab0b8, x, y, 47, 6); break;
            case BS_MALL: c = vary(wall, x, y, 48, 3); if (ly < 4) c = b->wall2; break;
            default: c = vary(wall, x, y, 49, 4); break;
            }
            pset(s, x, y, c);
        }
    /* the plinth and the shadow under the eaves */
    hline(s, x0, x0 + w - 1, y0, shade(wall, 120));
    hline(s, x0, x0 + w - 1, y0 + 1, shade(wall, 190));
    rectf(s, x0, y0 + h - 2, w, 2, shade(wall, 140));
    if (win) for (int x = x0; x < x0 + w; x++) if (hash3(x, y0, 50) % 3) pset(s, x, y0 + h - 1, 0xeef2f5);
    /* windows, doors, balconies by style */
    switch (b->style) {
    case BS_LAMELL: case BS_LAMELL_BRICK: {
        int cols = w / 16;
        for (int f = 0; f < floors; f++)
            for (int c = 0; c < cols; c++) {
                int wx = x0 + c * 16 + 5, wy = y0 + f * 16 + 4;
                if (f == floors - 1 && (c % 4) == 1) {                     /* the stairwell door on the ground floor */
                    door_px(p, wx - 1, wy + 1, 7, 10, 0x5a4a3a, trim);
                    rectf(s, wx - 3, wy - 2, 11, 2, 0x6a6e74);              /* a little canopy */
                    continue;
                }
                window_px(p, wx, wy, 6, 7, lit_window(b, c, f), 0, trim);
                if (f < floors - 1 && (c % 2) == 0) {                      /* balconies with coloured fronts */
                    rectf(s, wx - 3, wy + 7, 12, 5, b->wall2);
                    hline(s, wx - 3, wx + 8, wy + 7, shade(b->wall2, 300));
                    for (int k = 0; k < 12; k += 2) vline(s, wx - 3 + k, wy + 8, wy + 11, shade(b->wall2, 190));
                    rect_blend(s, wx - 3, wy + 12, 12, 2, 0x000000, 90);
                }
            }
        break;
    }
    case BS_VILLA: case BS_COTTAGE: case BS_SAUNA: case BS_SHED: {
        /* white corner boards, windows with white frames and the cross of their bars */
        rectf(s, x0, y0, 2, h, trim); rectf(s, x0 + w - 2, y0, 2, h, trim);
        hline(s, x0, x0 + w - 1, y0 + 1, trim);
        int cols = w / 16;
        int door_c = (int)(hash3((int)b->seed, 2, 51) % (unsigned)MAX(1, cols));
        for (int f = 0; f < floors; f++)
            for (int c = 0; c < cols; c++) {
                int wx = x0 + c * 16 + 4, wy = y0 + f * 16 + 3;
                if (b->style == BS_SHED) { if (c == 0 && f == floors - 1) door_px(p, wx + 1, wy + 2, 7, h - 6, shade(wall, 150), trim); continue; }
                if (f == floors - 1 && c == door_c) { door_px(p, wx + 1, wy + 2, 7, 11, b->style == BS_SAUNA ? 0x5a3a2a : 0x3a5a7a, trim); continue; }
                window_px(p, wx, wy, 8, 8, lit_window(b, c, f), 1, trim);
            }
        break;
    }
    case BS_OLDTOWN: {
        int cols = w / 16;
        for (int f = 0; f < floors; f++)
            for (int c = 0; c < cols; c++) {
                int wx = x0 + c * 16 + 5, wy = y0 + f * 16 + 3;
                if (f == floors - 1 && c == 0) {                           /* an arched portal */
                    door_px(p, wx - 1, wy + 2, 7, 11, 0x4a2a1a, shade(wall, 160));
                    hline(s, wx, wx + 4, wy + 1, shade(wall, 160));
                    continue;
                }
                window_px(p, wx, wy, 6, 9, lit_window(b, c, f), 1, trim);
            }
        hline(s, x0, x0 + w - 1, y0 + 2, shade(wall, 300));            /* the cornice */
        break;
    }
    case BS_SHOP: case BS_KIOSK: {
        /* upper floor windows, a shop window and a door on the ground, a sign, an awning */
        if (floors > 1)
            for (int c = 0; c < w / 16; c++) window_px(p, x0 + c * 16 + 5, y0 + 4, 6, 7, lit_window(b, c, 0), 0, trim);
        int gy = y0 + (floors - 1) * 16;
        rectf(s, x0 + 2, gy + 4, w - 4, 10, 0x1c2834);
        int lit = G->power_on && b->lit;
        for (int x = x0 + 2; x < x0 + w - 2; x++) for (int y = gy + 4; y < gy + 14; y++) {
            uint32_t c = lit ? 0xe8d8a0 : 0x1c2834;
            if ((x - y) % 9 == 0) c = lit ? 0xfff0c0 : 0x2c3e4e;
            pset(s, x, y, c);
        }
        for (int x = x0 + 2 + 14; x < x0 + w - 4; x += 14) vline(s, x, gy + 4, gy + 13, 0x3a3e44);
        rect_line(s, x0 + 2, gy + 4, w - 4, 10, 0x3a3e44);
        door_px(p, x0 + w / 2 - 3, gy + 5, 6, 9, 0x2a3440, 0x3a3e44);
        /* striped awning */
        for (int x = x0 + 1; x < x0 + w - 1; x++) {
            uint32_t c = ((x - x0) / 3) & 1 ? b->trim : 0xf0ece0;
            vline(s, x, gy + 1, gy + 3, c);
        }
        hline(s, x0 + 1, x0 + w - 2, gy + 4, 0x000000);
        if (b->sign[0]) sign_board(p, x0 + w / 2, (floors > 1 ? y0 + 12 : gy - 8), b->sign, 0x1a1a22, lit ? 0xffe070 : 0xd8d0b0);
        if (b->style == BS_KIOSK) { rectf(s, x0 + 3, gy + 6, w - 6, 3, 0x6a5a4a); }  /* the counter */
        break;
    }
    case BS_CHURCH: case BS_TOWER: {
        int cols = w / 16;
        for (int c = 0; c < cols; c++) {
            int wx = x0 + c * 16 + 5, wy = y0 + 6;
            if (b->style == BS_CHURCH && c == cols / 2) { door_px(p, wx - 1, y0 + h - 16, 8, 14, 0x4a2a1a, 0x8a8070); continue; }
            /* tall arched windows */
            rectf(s, wx, wy + 2, 6, h - 16, G->power_on ? 0xd8a860 : 0x2a3444);
            hline(s, wx + 1, wx + 4, wy + 1, G->power_on ? 0xd8a860 : 0x2a3444);
            rect_line(s, wx - 1, wy + 1, 8, h - 14, 0x9a9488);
            vline(s, wx + 2, wy + 2, wy + h - 15, 0x9a9488);
        }
        if (b->style == BS_TOWER) {                                     /* the clock */
            circlef(s, x0 + w / 2, y0 + 8, 5, 0xf0e8d0); circle(s, x0 + w / 2, y0 + 8, 5, 0x3a3a3a);
            line(s, x0 + w / 2, y0 + 8, x0 + w / 2, y0 + 5, 0x1a1a1a); line(s, x0 + w / 2, y0 + 8, x0 + w / 2 + 2, y0 + 9, 0x1a1a1a);
        }
        break;
    }
    case BS_BELLTOWER: {
        rectf(s, x0 + 6, y0 + 4, w - 12, 10, 0x1a1414);                 /* the open bell chamber */
        circlef(s, x0 + w / 2, y0 + 11, 4, 0xb08a3a);
        rectf(s, x0 + w / 2 - 4, y0 + 11, 9, 4, 0x1a1414);
        rect_line(s, x0 + 5, y0 + 3, w - 10, 12, trim);
        break;
    }
    case BS_SCHOOL: case BS_TVATT: {
        int cols = w / 16;
        for (int f = 0; f < floors; f++)
            for (int c = 0; c < cols; c++) {
                int wx = x0 + c * 16 + 3, wy = y0 + f * 16 + 4;
                if (f == floors - 1 && c == cols / 2) { door_px(p, wx + 1, wy, 10, 12, 0x2a5a8a, trim); continue; }
                window_px(p, wx, wy, 10, 7, lit_window(b, c, f), 0, trim);
            }
        if (b->sign[0]) sign_board(p, x0 + w / 2, y0 + (floors > 1 ? 13 : 1), b->sign, b->style == BS_SCHOOL ? 0xe8e4da : 0x2a5a8a, b->style == BS_SCHOOL ? 0x2a3a5a : 0xffffff);
        break;
    }
    case BS_WAREHOUSE: {
        int dw = MIN(w - 8, 40);
        int dx = x0 + (w - dw) / 2;
        rectf(s, dx, y0 + h - 22, dw, 20, 0x5a5e66);                     /* the roller door */
        for (int y = y0 + h - 22; y < y0 + h - 2; y += 2) hline(s, dx, dx + dw - 1, y, 0x4a4e56);
        for (int x = x0; x < x0 + w; x += 6) { pset(s, x, y0 + 3, b->trim); pset(s, x + 1, y0 + 4, b->trim); }   /* hazard stripe */
        if (b->sign[0]) sign_board(p, x0 + w / 2, y0 + 1, b->sign, 0xe0a020, 0x1a1a1a);
        break;
    }
    case BS_STATION: {
        rectf(s, x0 + 3, y0 + 4, w - 6, h - 6, 0x1c2834);
        int lit = G->power_on;
        for (int x = x0 + 4; x < x0 + w - 4; x++) for (int y = y0 + 5; y < y0 + h - 2; y++) if (((x + y) % 7) == 0) pset(s, x, y, lit ? 0xd8e8f0 : 0x2c3e4e);
        if (lit) rect_blend(s, x0 + 3, y0 + 4, w - 6, h - 6, 0xc8d8e0, 90);
        door_px(p, x0 + w / 2 - 4, y0 + h - 13, 8, 11, 0x3a4450, 0x8a929a);
        if (!strcmp(b->sign, "T")) {                                     /* the tunnelbana's blue T */
            circlef(s, x0 + w / 2, y0 + 4, 5, 0xf2f2f2); circle(s, x0 + w / 2, y0 + 4, 5, 0x1f5fa8);
            hline(s, x0 + w / 2 - 3, x0 + w / 2 + 3, y0 + 2, 0x1f5fa8); vline(s, x0 + w / 2, y0 + 2, y0 + 7, 0x1f5fa8);
            vline(s, x0 + w / 2 + 1, y0 + 2, y0 + 7, 0x1f5fa8);
        } else if (b->sign[0]) sign_board(p, x0 + w / 2, y0 + 1, b->sign, 0x1f5fa8, 0xffffff);
        break;
    }
    case BS_MALL: {
        int ew = MIN(48, w - 16), ex = x0 + (w - ew) / 2;
        int lit = G->power_on;
        rectf(s, ex, y0 + h - 22, ew, 20, lit ? 0xd8e0e0 : 0x1c2834);    /* the glass entrance */
        for (int x = ex; x < ex + ew; x += 8) vline(s, x, y0 + h - 22, y0 + h - 3, 0x5a5e66);
        hline(s, ex, ex + ew - 1, y0 + h - 22, 0x5a5e66);
        if (b->sign[0]) sign_board(p, x0 + w / 2, y0 + 7, b->sign, b->wall2, 0x1a2a4a);
        for (int c = 0; c < w / 16; c++) { int wx = x0 + c * 16 + 4; if (wx > ex - 12 && wx < ex + ew + 4) continue; window_px(p, wx, y0 + 18, 8, 6, lit_window(b, c, 1), 0, 0xd0d0d0); }
        break;
    }
    case BS_GREENHOUSE: break;
    }
}

static void paint_building(int bi) {
    Building *b = &G->b[bi];
    Bp p; p.s = wsurf(); p.b = b;
    p.x0 = b->x * TS; p.y0 = b->y * TS; p.w = b->w * TS; p.h = b->h * TS;
    p.fh = b->fh * TS; p.fy = p.y0 + p.h - p.fh;
    switch (b->style) {
    case BS_VILLA: case BS_OLDTOWN: case BS_CHURCH: case BS_COTTAGE: case BS_SAUNA: case BS_SHED: case BS_BELLTOWER: case BS_TOWER:
        roof_pitched(&p, b->roof, 0); break;
    case BS_GREENHOUSE: roof_flat(&p, 0x9ac0c8); break;
    default: roof_flat(&p, b->roof); break;
    }
    facade(&p);
    /* the building's shadow on the ground to its east */
    for (int y = p.y0 + 4; y < p.y0 + p.h; y++)
        for (int x = p.x0 + p.w; x < p.x0 + p.w + 4; x++) {
            Tile *t = tile_at(x / TS, y / TS);
            if (t && !(t->f & TF_BUILDING)) wblend(x, y, 0x000000, 60 - (x - p.x0 - p.w) * 12);
        }
    if (b->style == BS_TOWER) {                                          /* a spire on the tower's roof */
        Surf s = wsurf(); int cx = p.x0 + p.w / 2;
        for (int k = 0; k < 14; k++) hline(&s, cx - k / 3, cx + k / 3, p.y0 + 2 + k, k < 2 ? 0xd8b040 : 0x6fa58c);
    }
}

/* ---------------------------------------------------------------- the whole picture */
void world_repaint_rect(int tx, int ty, int tw, int th) {
    int x0 = MAX(0, tx), y0 = MAX(0, ty), x1 = MIN(G->w, tx + tw), y1 = MIN(G->h, ty + th);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) paint_ground(x, y);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) paint_edges(x, y);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (G->t[y][x].deco) paint_deco(x, y);
    for (int i = 0; i < G->nb; i++) {
        Building *b = &G->b[i];
        if (b->x + b->w <= x0 || b->y + b->h <= y0 || b->x >= x1 || b->y >= y1) continue;
        paint_building(i);
    }
    props_paint_flat();
}

void world_paint(void) {
    free(G->world);
    G->ww = G->w * TS; G->wh = G->h * TS;
    G->world = malloc((size_t)G->ww * G->wh * 4);
    world_repaint_rect(0, 0, G->w, G->h);
}

/* ---------------------------------------------------------------- marks that stay */
void world_decal_blood(float x, float y, int amount) {
    Rng *r = &G->fx;
    int win = G->season == SEASON_WINTER;
    for (int k = 0; k < amount; k++) {
        float a = rng_float(r) * 2 * PI_F, d = rng_float(r) * (3 + amount * 0.6f);
        int px = (int)(x + cosf(a) * d), py = (int)(y + sinf(a) * d * 0.7f);
        Tile *t = tile_at(px / TS, py / TS);
        if (!t || (t->f & TF_BUILDING) || t->g == G_WATER) continue;
        uint32_t c = rng_chance(r, 0.5f) ? 0x5a0c0a : 0x7a1410;
        wblend(px, py, c, win ? 230 : 170);
        if (rng_chance(r, 0.3f)) wblend(px + 1, py, c, 140);
    }
}
void world_decal_scorch(float x, float y, float rad) {
    for (int j = -(int)rad; j <= (int)rad; j++)
        for (int i = -(int)rad; i <= (int)rad; i++) {
            float d = sqrtf((float)(i * i + j * j)) / rad;
            if (d > 1) continue;
            int px = (int)x + i, py = (int)y + j;
            Tile *t = tile_at(px / TS, py / TS);
            if (!t || (t->f & TF_BUILDING)) continue;
            if (hash3(px, py, 99) % 100 < (unsigned)(100 * (1 - d))) wblend(px, py, 0x141210, (int)(150 * (1 - d * d)));
        }
}
