// render.c: the top screen. The painted world, then everything standing sorted by its feet, then the night: an
// ambient light per season, street lamps and lit windows once the power is on, the torch on the player's gun,
// muzzle flashes and explosions. Glowing things (zombie eyes, tracers, the box's beam) go on after the light.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

static uint16_t *lr, *lg, *lb;            /* light per pixel, 256 = full */
static int lw, lh;
static const char *ZPAL[8];
static const char *WOLFPAL, *BRUTEPAL, *BLOATPAL;

void render_init(void) {
    /* zombie variants: Swedish clothes on the dead (palette characters: c/C top, d detail, p/P trousers, h/H hair) */
    static const char *v[8][2] = {
        { "zv0", "c5a5f6eC444858dd8d8d0p3b3f4fP2a2d39h5b4636H3e3026" },          /* the commuter, grey suit */
        { "zv1", "cf08a24Cc06a14de8e8d0p2a4a7aP1e3458he0c020Hb09010" },          /* construction worker, hi-vis, hard hat */
        { "zv2", "c2848a8C1c3478df0f0f0p2848a8P1c3478hc8b080Ha08a60" },          /* tracksuit */
        { "zv3", "cb8a888C8a7a5ad6a5a4ap4a4a50P38383ehd8d8d8Hb0b0b0" },          /* the retiree, beige coat, white hair */
        { "zv4", "c2a2a30C1a1a20dc02a2ap3a4a6aP2a3450hc02a2aH8a1a1a" },          /* teen, black hoodie, red beanie */
        { "zv5", "ce8e8f0Cb8c0d0d7aa0d0p6a90b8P4a7090h8a6a4aH6a4a3a" },          /* nurse, scrubs */
        { "zv6", "c2a3a5aC1a2a40dd8e040p2a3a5aP1a2a40h1a1a1aH101010" },          /* police, with the yellow-green vest stripe */
        { "zv7", "c8c2b1eC6a1e14de8e4dap3a3a40P2a2a30hd8b060Hb09040" },          /* Falu red anorak, blond */
    };
    for (int i = 0; i < 8; i++) ZPAL[i] = art_palette_add(v[i][0], "zombie", v[i][1]);
    BRUTEPAL = art_palette_add("zbrute", "zombie", "c3a3a3aC262626d5a5a5ap2a2a2aP1a1a1as7a8a68S5a6a4ah2a2a2aH1a1a1a");
    BLOATPAL = art_palette_add("zbloat", "zombie", "c9a9a6aC7a7a4adb0b080p4a4a3aP3a3a2asa0b070S80904ah4a4a3aH3a3a2a");
    WOLFPAL = "wolf";
}

static void light_alloc(int w, int h) {
    if (lw == w && lh == h) return;
    free(lr); free(lg); free(lb);
    lw = w; lh = h;
    lr = malloc((size_t)w * h * 2); lg = malloc((size_t)w * h * 2); lb = malloc((size_t)w * h * 2);
}

static void ambient(int *r, int *g, int *b) {
    switch (G->season) {
    case SEASON_WINTER: *r = 70; *g = 80; *b = 112; break;     /* snow throws the little light back */
    case SEASON_SUMMER: *r = 150; *g = 140; *b = 170; break;   /* midsummer night: it never gets dark */
    default: *r = 52; *g = 58; *b = 82; break;
    }
    if (G->power_on) { *r += 10; *g += 10; *b += 8; }
    if (G->special == 1 && G->rstate == RS_ACTIVE) { *r = *r * 3 / 4; *g = *g * 3 / 4; *b = *b * 7 / 8; }   /* wolf night is darker */
}

static void add_light(float wx, float wy, float rad, uint32_t col, float k) {
    int cx = (int)(wx - G->camx), cy = (int)(wy - G->camy), r = (int)rad;
    if (cx + r < 0 || cy + r < 0 || cx - r >= lw || cy - r >= lh) return;
    int x0 = MAX(0, cx - r), x1 = MIN(lw - 1, cx + r), y0 = MAX(0, cy - r), y1 = MIN(lh - 1, cy + r);
    float inv = 1.0f / (rad * rad);
    int cr = (int)(CR(col) * k), cg = (int)(CG(col) * k), cb = (int)(CB(col) * k);
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        size_t row = (size_t)y * lw;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx;
            float f = 1.0f - (dx * dx + dy * dy * 1.3f) * inv;
            if (f <= 0) continue;
            f *= f;
            int a = (int)(f * 256);
            lr[row + x] = (uint16_t)MIN(1023, lr[row + x] + (cr * a >> 8));
            lg[row + x] = (uint16_t)MIN(1023, lg[row + x] + (cg * a >> 8));
            lb[row + x] = (uint16_t)MIN(1023, lb[row + x] + (cb * a >> 8));
        }
    }
}

/* the torch: a cone along the aim, soft at the edges, stopped by walls tile by tile */
static void add_torch(float wx, float wy, float ang, float range, float half) {
    int cx = (int)(wx - G->camx), cy = (int)(wy - G->camy), r = (int)range;
    int x0 = MAX(0, cx - r), x1 = MIN(lw - 1, cx + r), y0 = MAX(0, cy - r), y1 = MIN(lh - 1, cy + r);
    float ax = cosf(ang), ay = sinf(ang), cosh = cosf(half), cos_in = cosf(half * 0.5f);
    /* which tiles the torch reaches: a ray per tile centre */
    int tx0 = (int)((G->camx + x0) / TS), ty0 = (int)((G->camy + y0) / TS), tx1 = (int)((G->camx + x1) / TS), ty1 = (int)((G->camy + y1) / TS);
    static uint8_t vis[40][40];
    for (int ty = ty0; ty <= ty1 && ty - ty0 < 40; ty++)
        for (int tx = tx0; tx <= tx1 && tx - tx0 < 40; tx++) {
            Tile *t = tile_at(tx, ty);
            float px = tx * TS + 8, py = ty * TS + 8;
            int v = 0;
            if (t && (t->f & TF_BUILDING)) {                /* a wall is lit if the tile in front of it is */
                v = line_clear(wx, wy, px, py + 10) || line_clear(wx, wy, px, (ty + 1) * TS + 2);
            } else v = line_clear(wx, wy, px, py);
            vis[ty - ty0][tx - tx0] = (uint8_t)v;
        }
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        size_t row = (size_t)y * lw;
        int wyy = (int)(G->camy + y);
        int vty = wyy / TS - ty0;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx), d2 = dx * dx + dy * dy;
            if (d2 > range * range) continue;
            float d = sqrtf(d2) + 0.001f, c = (dx * ax + dy * ay) / d;
            float f;
            if (d < 18) f = 0.55f * (1 - d / 18);                  /* a little glow around you */
            else f = 0;
            if (c > cosh) {
                float edge = c > cos_in ? 1.0f : (c - cosh) / (cos_in - cosh);
                float fall = 1.0f - d / range;
                f += edge * fall * (fall + 0.25f) * 1.1f;
            }
            if (f <= 0.01f) continue;
            int vtx = (int)(G->camx + x) / TS - tx0;
            if (vty >= 0 && vty < 40 && vtx >= 0 && vtx < 40 && !vis[vty][vtx] && d > 14) continue;
            int a = (int)(f * 256);
            lr[row + x] = (uint16_t)MIN(1023, lr[row + x] + (250 * a >> 8));
            lg[row + x] = (uint16_t)MIN(1023, lg[row + x] + (236 * a >> 8));
            lb[row + x] = (uint16_t)MIN(1023, lb[row + x] + (200 * a >> 8));
        }
    }
}

static void apply_light(Surf *s) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    for (int y = 0; y < s->h; y++) {
        uint32_t *px = s->px + (size_t)y * s->pitch;
        size_t row = (size_t)y * lw;
        for (int x = 0; x < s->w; x++) {
            /* light in steps of 1/16 with ordered dither: a pixel-art night instead of smooth gradients */
            int d = bayer[y & 3][x & 3];
            int r = ((lr[row + x] + d) >> 4) << 4, g = ((lg[row + x] + d) >> 4) << 4, b = ((lb[row + x] + d) >> 4) << 4;
            uint32_t c = px[x];
            int cr = (int)CR(c) * r >> 8, cg = (int)CG(c) * g >> 8, cb = (int)CB(c) * b >> 8;
            px[x] = RGB(MIN(cr, 255), MIN(cg, 255), MIN(cb, 255));
        }
    }
}

/* ---------------------------------------------------------------- what stands */
enum { DR_PROP, DR_INTER, DR_ZOMBIE, DR_PLAYER, DR_ITEM, DR_GRENADE, DR_POWERUP, DR_SHOT };
typedef struct { int y, kind, idx; } Drawable;
static Drawable dl[2048]; static int ndl;
static void push(int y, int kind, int idx) { if (ndl < 2048) { dl[ndl].y = y; dl[ndl].kind = kind; dl[ndl].idx = idx; ndl++; } }
static int cmp_dl(const void *a, const void *b) { const Drawable *x = a, *y = b; return x->y != y->y ? x->y - y->y : x->kind - y->kind; }

static const char *DIRS[4] = { "down", "up", "side", "side" };

static const Img *zombie_img(Zombie *z, int *flip) {
    char name[32];
    int frame = ((int)z->anim) % 4; frame = frame == 1 ? 1 : frame == 3 ? 2 : 0;
    *flip = z->dir == 3 ? FLIP_X : 0;
    if (z->type == ZT_WOLF) {
        snprintf(name, sizeof name, "wolf_%d", ((int)z->anim) % 3);
        if (art_exists(name)) { *flip = (z->dir == 3 || (z->dir < 2 && z->vx < 0)) ? FLIP_X : 0; return art(name); }
    }
    if (z->type == ZT_MOOSE) {
        snprintf(name, sizeof name, "moose_%d", z->state == ZS_CHARGE ? ((int)(G->time * 12)) % 3 : ((int)z->anim) % 3);
        if (art_exists(name)) { *flip = z->dir == 3 || (z->dir < 2 && G->p.x < z->x) ? FLIP_X : 0; return art(name); }
    }
    if (z->type == ZT_BRUTE || z->type == ZT_BLOATER) {      /* two frames drawn: the second step is the first mirrored */
        const char *pre = z->type == ZT_BRUTE ? "brute" : "bloater";
        int f = frame == 2 ? 1 : frame;
        snprintf(name, sizeof name, "%s_%s_%d", pre, DIRS[z->dir], f);
        if (!art_exists(name)) snprintf(name, sizeof name, "%s_%s_0", pre, DIRS[z->dir]);
        if (!art_exists(name)) snprintf(name, sizeof name, "%s_down_%d", pre, f);
        if (art_exists(name)) {
            if (frame == 2 && z->dir < 2) *flip ^= FLIP_X;
            return art(name);
        }
    }
    if (z->state == ZS_ATTACK && z->dir >= 2) snprintf(name, sizeof name, "zombie_side_a");
    else snprintf(name, sizeof name, "zombie_%s_%d", DIRS[z->dir], frame);
    const char *pal = z->type == ZT_BRUTE ? BRUTEPAL : z->type == ZT_BLOATER ? BLOATPAL : ZPAL[z->variant & 7];
    return art_pal(name, pal);
}

static void draw_shadow(Surf *s, int x, int y, int rx) { ellipse_blend(s, x, y, rx, MAX(1, rx / 3), 0x000000, 90); }

static void draw_zombie(Surf *s, Zombie *z) {
    int sx = (int)(z->x - G->camx), sy = (int)(z->y - G->camy);
    int flip; const Img *im = zombie_img(z, &flip);
    if (!im) return;
    int ox = sx - im->w / 2, oy = sy - im->h + 1;
    z->rimg = 0;
    if (z->state == ZS_DEAD) {                              /* lying where it fell, fading */
        const Img *d = art_pal(z->type == ZT_WOLF && art_exists("wolf_dead") ? "wolf_dead" : z->type == ZT_MOOSE && art_exists("moose_dead") ? "moose_dead" : "zombie_dead",
                               z->type == ZT_WALKER ? ZPAL[z->variant & 7] : z->type == ZT_BRUTE ? BRUTEPAL : z->type == ZT_BLOATER ? BLOATPAL : 0);
        if (d) blit_ex(s, d, sx - d->w / 2, sy - d->h + 2, z->dir == 3 ? FLIP_X : 0, 0, 0, z->t > 4 ? (int)(255 * (6 - z->t) / 2) : 255);
        return;
    }
    if (z->state == ZS_WINDOW) {                            /* inside the window, behind the boards */
        Surf c = *s; surf_clip(&c, ox, oy, im->w, im->h - 4);
        blit(&c, im, ox, oy + 2, 0);
        z->rimg = im; z->rx = (int16_t)ox; z->ry = (int16_t)(oy + 2); z->rflip = 0; z->rclip = (int16_t)(oy + im->h - 4);
        return;
    }
    if (z->state == ZS_RISE) {                              /* climbing up out of the ground */
        float k = z->type == ZT_WOLF ? 1 - z->t / 0.5f : 1 - z->t / 1.3f;
        int show = (int)(im->h * CLAMP(k, 0, 1));
        Surf c = *s; surf_clip(&c, ox - 2, sy - show, im->w + 4, show);
        blit(&c, im, ox, sy - show, flip);
        ellipse_blend(s, sx, sy, 7, 2, 0x2a1e14, 160);
        return;
    }
    draw_shadow(s, sx, sy, z->type == ZT_MOOSE ? 16 : z->type == ZT_WOLF ? 7 : 6);
    int lunge = z->state == ZS_ATTACK ? 2 : 0;
    int lx = z->dir == 2 ? lunge : z->dir == 3 ? -lunge : 0, ly = z->dir == 0 ? lunge : z->dir == 1 ? -lunge : 0;
    uint32_t tint = 0; int amt = 0;
    if (z->flash > 0) { tint = 0xffffff; amt = 200; }
    else if (z->slow > 0) { tint = 0x9ad8ff; amt = 120; }
    else if (z->burn > 0) { tint = 0xff8030; amt = 60; }
    else if (z->state == ZS_WINDUP) { tint = 0xff4040; amt = (int)(G->time * 20) & 1 ? 120 : 0; }
    int bob = (z->type == ZT_WALKER && ((int)z->anim & 1)) ? 1 : 0;
    blit_ex(s, im, ox + lx, oy + ly + bob, flip, tint, amt, 255);
    z->rimg = im; z->rx = (int16_t)(ox + lx); z->ry = (int16_t)(oy + ly + bob); z->rflip = (uint8_t)flip; z->rclip = 0x7fff;
    if (z->state == ZS_ATTACK && z->t < 0.15f) {           /* the claws */
        int ax = sx + (z->dir == 2 ? 9 : z->dir == 3 ? -9 : 0), ay = sy - 9 + (z->dir == 0 ? 6 : z->dir == 1 ? -8 : 0);
        for (int k = -1; k <= 1; k++) line(s, ax - 3, ay - 3 + k * 3, ax + 3, ay + 3 + k * 3, 0xe8e8e8);
    }
    if (z->type == ZT_MOOSE || z->type == ZT_BRUTE) {      /* a health bar over the big ones */
        int bw = z->type == ZT_MOOSE ? 30 : 16;
        rectf(s, sx - bw / 2, oy - 4, bw, 2, 0x200000);
        rectf(s, sx - bw / 2, oy - 4, (int)(bw * CLAMP(z->hp / z->maxhp, 0, 1)), 2, 0xd02020);
    }
}

/* the glowing eyes, after the lighting: the sprite's eye pixels drawn again, unlit, with a little halo */
static void zombie_eyes(Surf *s, Zombie *z) {
    if (z->state == ZS_DEAD || z->state == ZS_RISE || !z->rimg) return;
    const Img *im = z->rimg;
    uint32_t c = z->type == ZT_WOLF ? 0x9ad8ff : z->type == ZT_MOOSE ? 0xff4020 : z->speed > 55 ? 0xff7a20 : 0xffd23c;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            uint32_t p = im->px[y * im->w + x] & 0xFFFFFF;
            if (p != 0xffd23c && p != 0x9ad8ff && p != 0xff4020) continue;
            int px = z->rx + ((z->rflip & FLIP_X) ? im->w - 1 - x : x), py = z->ry + y;
            if (py >= z->rclip) continue;
            pset(s, px, py, c);
            padd(s, px - 1, py, col_scale(c, 70)); padd(s, px + 1, py, col_scale(c, 70)); padd(s, px, py - 1, col_scale(c, 50));
        }
}

static void draw_player(Surf *s) {
    Player *p = &G->p;
    int sx = (int)(p->x - G->camx), sy = (int)(p->y - G->camy);
    char name[32];
    int frame = ((int)p->anim) % 4; frame = frame == 1 ? 1 : frame == 3 ? 2 : 0;
    snprintf(name, sizeof name, "player_%s_%d", DIRS[p->dir], frame);
    const Img *im = art(name);
    if (!im) return;
    draw_shadow(s, sx, sy, 6);
    if (p->downed) {                                         /* on the ground, the Kanelbulle working */
        const Img *d = art_pal("zombie_dead", "survivor");
        if (d) blit(s, d, sx - d->w / 2, sy - d->h + 2, 0);
        return;
    }
    int flip = p->dir == 3 ? FLIP_X : 0;
    float ca = cosf(p->aim), sa = sinf(p->aim);
    int hx = sx + (p->dir == 2 ? 2 : p->dir == 3 ? -2 : 0), hy = sy - 9;
    int gx = hx + (int)(ca * 9), gy = hy + (int)(sa * 7);
    int behind = sa < -0.35f;                                /* aiming up: the gun is behind you */
    uint32_t gun = 0x2a2c32, gun2 = 0x5a5e66;
    Weapon *w = &p->w[p->cur];
    if (w->def >= 0 && w->pap) { gun = 0x5a2a7a; gun2 = 0xb07ae0; }
    if (behind) { line(s, hx, hy, gx, gy, gun); line(s, hx, hy - 1, gx, gy - 1, gun2); }
    int alpha = p->invuln > 0 && ((int)(G->time * 12) & 1) ? 120 : 255;
    blit_ex(s, im, sx - im->w / 2, sy - im->h + 1 + ((frame && 1) ? 0 : 0), flip, p->hurt_t > 0 ? 0xff2020 : 0, p->hurt_t > 0 ? 140 : 0, alpha);
    if (!behind && p->melee_t <= 0) { line(s, hx, hy, gx, gy, gun); line(s, hx, hy - 1, gx, gy - 1, gun2); }
    if (p->melee_t > 0) {                                    /* the knife's arc */
        float a0 = p->melee_ang - 0.9f + (0.22f - p->melee_t) / 0.22f * 1.8f;
        int kx = sx + (int)(cosf(a0) * 13), ky = sy - 8 + (int)(sinf(a0) * 10);
        line(s, hx, hy, kx, ky, p->has_axe ? 0x8a5a3a : 0xd8d8e0);
        if (p->has_axe) rectf(s, kx - 2, ky - 2, 4, 4, 0x9aa0a8);
    }
}

/* items on the ground: their icon, bobbing, in their rarity's glow */
static void draw_item(Surf *s, Item *it) {
    int sx = (int)(it->x - G->camx), sy = (int)(it->y - G->camy);
    if (it->life - it->t < 5 && ((int)(it->t * 8) & 1)) return;
    int bob = (int)(sinf(it->t * 3) * 1.5f);
    const char *icon = 0;
    switch (it->kind) {
    case IK_WEAPON: icon = WEAPONS[it->w.def].icon; break;
    case IK_ARMOR: icon = ARMORS[it->ar.def].icon; break;
    case IK_CONS: icon = CONS[it->id].icon; break;
    case IK_AMMO: icon = "i_ammo"; break;
    case IK_CASH: icon = "i_cash"; break;
    }
    ellipse_blend(s, sx, sy, 7, 2, 0x000000, 80);
    const Img *im = icon ? art(icon) : 0;
    if (!im) { rectf(s, sx - 3, sy - 8 + bob, 6, 6, RARITY_COL[it->rar]); return; }
    blit(s, im, sx - im->w / 2, sy - im->h - 2 + bob, 0);
}

static void draw_powerup(Surf *s, PowerUp *pu) {
    if (pu->t > 15 && ((int)(pu->t * 6) & 1)) return;
    int sx = (int)(pu->x - G->camx), sy = (int)(pu->y - G->camy);
    static const char *icons[PU_COUNT] = { "pu_maxammo", "pu_insta", "pu_double", "pu_kaboom", "pu_carpenter", "pu_firesale" };
    int bob = (int)(sinf(pu->t * 4) * 2);
    ellipse_blend(s, sx, sy, 7, 2, 0x000000, 90);
    const Img *im = art(icons[pu->kind]);
    if (im) blit(s, im, sx - im->w / 2, sy - im->h - 6 + bob, 0);
}

/* ---------------------------------------------------------------- the screen */
static void draw_weather(Surf *s) {
    Rng r; rng_seed(&r, 99, 5);
    int n = G->season == SEASON_SUMMER ? 18 : 90;
    for (int i = 0; i < n; i++) {
        float bx = rng_float(&r) * 512, by = rng_float(&r) * 512, sp = 0.6f + rng_float(&r) * 0.8f;
        if (G->season == SEASON_WINTER) {                  /* snow drifting */
            float x = fmodf(bx + G->time * 12 * sp + sinf(G->time * 1.5f + i) * 6 - G->camx * 0.9f, 512), y = fmodf(by + G->time * 26 * sp - G->camy * 0.9f, 512);
            x = x < 0 ? x + 512 : x; y = y < 0 ? y + 512 : y;
            int px = (int)x * s->w / 512, py = (int)y * s->h / 512;
            pblend(s, px, py, 0xf4f8ff, 200);
            if (sp > 1.1f) pblend(s, px + 1, py, 0xf4f8ff, 120);
        } else if (G->season == SEASON_AUTUMN) {           /* rain streaks */
            float x = fmodf(bx - G->time * 40 * sp - G->camx, 512), y = fmodf(by + G->time * 300 * sp - G->camy, 512);
            x = x < 0 ? x + 512 : x; y = y < 0 ? y + 512 : y;
            int px = (int)x * s->w / 512, py = (int)y * s->h / 512;
            line_blend(s, px, py, px - 1, py + 4, 0x9ab0c8, 90);
        } else {                                           /* midsummer: pollen and moths in the light */
            float x = fmodf(bx + sinf(G->time * 0.7f + i) * 20 - G->camx * 0.95f, 512), y = fmodf(by + cosf(G->time * 0.5f + i * 2) * 14 - G->camy * 0.95f, 512);
            x = x < 0 ? x + 512 : x; y = y < 0 ? y + 512 : y;
            pblend(s, (int)x * s->w / 512, (int)y * s->h / 512, 0xfff0c0, 150);
        }
    }
}

static void tally(Surf *s, int x, int y, int n, uint32_t c) {   /* round marks in chalk, Black Ops style */
    if (n > 10) { char b[16]; snprintf(b, sizeof b, "%d", n); text_big(s, x, y - 2, 2, c, 0x200000, b); return; }
    for (int g = 0; g < (n + 4) / 5; g++) {
        int m = MIN(5, n - g * 5), gx = x + g * 22;
        for (int k = 0; k < MIN(m, 4); k++) { line(s, gx + k * 4, y, gx + k * 4 + 1, y + 13, c); line(s, gx + k * 4 + 1, y, gx + k * 4 + 2, y + 13, c); }
        if (m == 5) { line(s, gx - 2, y + 10, gx + 15, y + 3, c); line(s, gx - 2, y + 11, gx + 15, y + 4, c); }
    }
}

static void overlays(Surf *s) {
    Player *p = &G->p;
    /* hurt: red at the edges, stronger the lower the health */
    float hk = p->maxhp > 0 ? 1 - p->hp / p->maxhp : 0;
    float red = MAX(G->hurt_flash * 0.8f, hk > 0.45f ? (hk - 0.45f) * 1.6f * (0.75f + 0.25f * sinf(G->time * 6)) : 0);
    if (red > 0.02f) {
        for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
            float ex = fabsf(x - s->w / 2.0f) / (s->w / 2.0f), ey = fabsf(y - s->h / 2.0f) / (s->h / 2.0f);
            float e = MAX(ex, ey); e = e * e * e;
            int a = (int)(e * red * 220);
            if (a > 4) pblend(s, x, y, 0x8a0000, MIN(a, 200));
        }
    }
    if (G->flash_t > 0) rect_blend(s, 0, 0, s->w, s->h, 0xfff8e0, (int)(G->flash_t * 600));
    /* the round in the corner */
    uint32_t rc = G->rstate == RS_BREAK ? ((int)(G->time * 3) & 1 ? 0xf0f0f0 : 0xc81818) : 0xb81414;
    tally(s, 6, s->h - 20, G->round, rc);
    /* power-up timers along the bottom */
    int tx = s->w / 2 - 40;
    static const char *icons[3] = { "pu_insta", "pu_double", "pu_firesale" };
    float tt[3] = { G->insta_t, G->double_t, G->firesale_t };
    for (int k = 0; k < 3; k++) if (tt[k] > 0) {
        const Img *im = art(icons[k]);
        if (im && (tt[k] > 5 || ((int)(G->time * 6) & 1))) blit(s, im, tx, s->h - 20, 0);
        tx += 22;
    }
    /* banner: round changes, power-ups, perks */
    if (G->banner_t > 0) {
        float a = MIN(1.0f, G->banner_t / 0.5f);
        int k = 2;
        int w = text_w(FONT_NORMAL, G->banner) * k;
        int y = s->h / 3 - 10;
        if (a > 0.1f) {
            text_big(s, s->w / 2 - w / 2, y, k, G->banner_col, 0x100000, G->banner);
            if (G->banner2[0]) text_center(s, FONT_NORMAL, s->w / 2, y + 22, 0xe0e0e0, 0x000000, G->banner2);
        }
    }
    /* the prompt */
    if (G->prompt[0]) {
        int w = text_w(FONT_NORMAL, G->prompt) + 10, x = s->w / 2 - w / 2, y = s->h - 46;
        rect_blend(s, x, y, w, 14, 0x000000, 150);
        text(s, FONT_NORMAL, x + 5, y + 3, G->prompt_cost_ok ? 0xffffff : 0xff8070, G->prompt);
    }
    /* messages, newest at the bottom */
    for (int i = 0; i < 4; i++) {
        if (G->msg_t[i] <= 0) continue;
        int y = s->h - 62 - i * 11;
        uint32_t c = G->msg_t[i] < 1 ? col_scale(G->msg_col[i], (int)(G->msg_t[i] * 256)) : G->msg_col[i];
        text_sh(s, FONT_NORMAL, 6, y, c, 0x000000, G->msg[i]);
    }
    /* reloading bar over the player */
    if (p->reloading && p->reload_len > 0) {
        int sx = (int)(p->x - G->camx), sy = (int)(p->y - G->camy) - 26;
        rectf(s, sx - 10, sy, 20, 3, 0x101010);
        rectf(s, sx - 10, sy, (int)(20 * (1 - p->reload_t / p->reload_len)), 3, 0xf0f0f0);
    }
    if (p->downed) {
        char b[32]; snprintf(b, sizeof b, "%s %.0f", PERKS[PK_KANELBULLE].name, ceilf(p->down_t));
        text_center(s, FONT_NORMAL, s->w / 2, s->h / 2 - 30, 0xffd070, 0x000000, b);
    }
}

void render_game(Surf *s) {
    Player *p = &G->p;
    G->view_w = s->w; G->view_h = s->h;
    float shx = G->shake > 0 ? rng_rangef(&G->fx, -G->shake, G->shake) : 0, shy = G->shake > 0 ? rng_rangef(&G->fx, -G->shake, G->shake) : 0;
    float camx = G->camx, camy = G->camy;
    G->camx = floorf(camx + shx); G->camy = floorf(camy + shy);
    int cx = (int)G->camx, cy = (int)G->camy;
    fill(s, 0x101010);
    copy_rect(s, G->world, G->ww, G->ww, G->wh, cx, cy, s->w, s->h, 0, 0);
    /* on the walls: chalk outlines, boards, the box spots */
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type != IT_WALLBUY && it->type != IT_WINDOW) continue;
        int sx = it->tx * TS - cx, sy = it->ty * TS - cy;
        if (sx < -32 || sy < -32 || sx > s->w || sy > s->h) continue;
        if (it->type == IT_WINDOW) continue;               /* drawn sorted, over the zombie inside */
        inter_draw(s, it, sx, sy);
    }
    /* clouds of gas and fire on the ground */
    for (int i = 0; i < MAX_CLOUDS; i++) {
        Cloud *c = &G->clouds[i];
        if (!c->alive) continue;
        float k = c->t < c->dur - 1 ? 1 : c->dur - c->t;
        ellipse_blend(s, (int)(c->x - cx), (int)(c->y - cy), (int)c->r, (int)(c->r * 0.6f), c->kind ? 0x8aa040 : 0xf07020, (int)(70 * k));
    }
    /* the sorted pass */
    ndl = 0;
    for (int i = 0; i < G->nprops; i++) {
        Prop *pr = &G->props[i];
        if (!prop_is_tall(pr->kind)) continue;
        if (pr->x - cx < -40 || pr->x - cx > s->w + 40 || pr->y - cy < -8 || pr->y - cy > s->h + 56) continue;
        push(pr->y, DR_PROP, i);
    }
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type == IT_WALLBUY || it->type == IT_LOOT) continue;
        int bx = it->tx * TS - cx, by = it->ty * TS - cy;
        if (bx < -48 || bx > s->w + 16 || by < -16 || by > s->h + 48) continue;
        push((it->ty + it->th) * TS + (it->type == IT_WINDOW ? 1 : 0), DR_INTER, i);
    }
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive) continue;
        if (z->x - cx < -40 || z->x - cx > s->w + 40 || z->y - cy < -8 || z->y - cy > s->h + 48) continue;
        push((int)z->y - (z->state == ZS_DEAD ? 12 : 0), DR_ZOMBIE, i);
    }
    push((int)p->y, DR_PLAYER, 0);
    for (int i = 0; i < MAX_ITEMS; i++) if (G->items[i].alive) push((int)G->items[i].y - 4, DR_ITEM, i);
    for (int i = 0; i < MAX_POWERUPS; i++) if (G->pu[i].alive) push((int)G->pu[i].y, DR_POWERUP, i);
    for (int i = 0; i < MAX_GRENADES; i++) if (G->gr[i].alive) push((int)G->gr[i].y, DR_GRENADE, i);
    for (int i = 0; i < MAX_SHOTS; i++) if (G->shots[i].alive) push((int)G->shots[i].y + 8, DR_SHOT, i);
    qsort(dl, (size_t)ndl, sizeof dl[0], cmp_dl);
    for (int i = 0; i < ndl; i++) {
        Drawable *d = &dl[i];
        switch (d->kind) {
        case DR_PROP: prop_draw(s, &G->props[d->idx], G->props[d->idx].x - cx, G->props[d->idx].y - cy); break;
        case DR_INTER: inter_draw(s, &G->it[d->idx], G->it[d->idx].tx * TS - cx, G->it[d->idx].ty * TS - cy); break;
        case DR_ZOMBIE: draw_zombie(s, &G->z[d->idx]); break;
        case DR_PLAYER: draw_player(s); break;
        case DR_ITEM: draw_item(s, &G->items[d->idx]); break;
        case DR_POWERUP: draw_powerup(s, &G->pu[d->idx]); break;
        case DR_GRENADE: {
            Grenade *g = &G->gr[d->idx];
            int gx = (int)(g->x - cx), gy = (int)(g->y - cy);
            ellipse_blend(s, gx, gy, 3, 1, 0x000000, 90);
            circlef(s, gx, gy - (int)g->z - 2, 2, g->kind == C_SMALLARE ? 0xd02020 : g->kind == C_MOLOTOV ? 0x6a8a3a : 0x3a4a2a);
            break;
        }
        case DR_SHOT: {
            Shot *sh = &G->shots[d->idx];
            int x = (int)(sh->x - cx), y = (int)(sh->y - cy);
            if (sh->type == PR_ROCKET) { line(s, x, y, x - (int)(sh->vx * 0.03f), y - (int)(sh->vy * 0.03f), 0x5a5e54); }
            break;
        }
        }
    }
    /* particles */
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *pt = &G->parts[i];
        if (!pt->alive) continue;
        int x = (int)(pt->x - cx), y = (int)(pt->y - cy - pt->z);
        if (x < -4 || y < -4 || x > s->w + 4 || y > s->h + 4) continue;
        float k = pt->life / pt->max;
        switch (pt->type) {
        case PT_SMOKE: case PT_GAS: circle_blend(s, x, y, (int)(pt->size + (1 - k) * 3), pt->col, (int)(110 * k)); break;
        case PT_FIRE: case PT_SPARK: case PT_ELEC: case PT_GLOW: case PT_FROST: break;   /* glowing: after the light */
        case PT_CONFETTI: pset(s, x, y, (i % 3) == 0 ? 0x006aa7 : (i % 3) == 1 ? 0xfecc02 : 0xf0f0f0); break;
        case PT_WOOD: rectf(s, x, y, 2, 1, pt->col); break;
        default: pset(s, x, y, pt->col); break;
        }
    }
    /* ---- the night ---- */
    light_alloc(s->w, s->h);
    int ar, ag, ab; ambient(&ar, &ag, &ab);
    for (size_t i = 0; i < (size_t)lw * lh; i++) { lr[i] = (uint16_t)ar; lg[i] = (uint16_t)ag; lb[i] = (uint16_t)ab; }
    for (int i = 0; i < G->nlights; i++) add_light(G->lights[i].x, G->lights[i].y, G->lights[i].r, G->lights[i].col, G->lights[i].k);
    if (!p->downed) add_torch(p->x, p->y - 8, p->aim, 150, 0.62f);
    if (p->muzzle_t > 0) add_light(p->x + cosf(p->aim) * 12, p->y - 8 + sinf(p->aim) * 9, 70, 0xffe0a0, 1.2f);
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *pt = &G->parts[i];
        if (pt->alive && pt->type == PT_FIRE && (i & 3) == 0) add_light(pt->x, pt->y - pt->z, 30, 0xff9030, 0.5f * pt->life / pt->max);
    }
    for (int i = 0; i < MAX_CLOUDS; i++) if (G->clouds[i].alive && G->clouds[i].kind == 0) add_light(G->clouds[i].x, G->clouds[i].y, 50, 0xff8a30, 0.7f);
    for (int i = 0; i < MAX_SHOTS; i++) if (G->shots[i].alive) add_light(G->shots[i].x, G->shots[i].y, 40, G->shots[i].col, 0.9f);
    for (int i = 0; i < MAX_POWERUPS; i++) if (G->pu[i].alive) add_light(G->pu[i].x, G->pu[i].y - 8, 36, 0x80ff80, 0.8f);
    for (int i = 0; i < MAX_ITEMS; i++) if (G->items[i].alive && G->items[i].rar >= RAR_RARE) add_light(G->items[i].x, G->items[i].y - 6, 20, RARITY_COL[G->items[i].rar], 0.5f);
    for (int i = 0; i < G->nit; i++) {                   /* machines glow */
        Inter *it = &G->it[i];
        float x = it->tx * TS + it->tw * 8.0f, y = (it->ty + it->th) * TS;
        if (it->type == IT_PERK && (G->power_on || it->a == PK_KANELBULLE)) add_light(x, y - 4, 40, PERKS[it->a].color2, 0.8f);
        else if (it->type == IT_PAP && G->power_on) add_light(x, y - 6, 50, 0xc070ff, 0.8f);
        else if (it->type == IT_BOX && G->box_spots[G->box_at] == i && !G->box_moving) add_light(x, y - 6, 44, 0xffe8a0, 0.7f);
        else if (it->type == IT_POWER && !G->power_on) add_light(x, y - 6, 24, 0xff4040, 0.3f + 0.2f * sinf(G->time * 4));
    }
    if (G->flash_t > 0) for (size_t i = 0; i < (size_t)lw * lh; i++) { lr[i] = (uint16_t)MIN(1023, lr[i] + 200); lg[i] = (uint16_t)MIN(1023, lg[i] + 200); lb[i] = (uint16_t)MIN(1023, lb[i] + 220); }
    apply_light(s);
    /* ---- what glows ---- */
    for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive) zombie_eyes(s, &G->z[i]);
    for (int i = 0; i < 64; i++) {
        Tracer *t = &G->tr[i];
        if (!t->alive) continue;
        int x0 = (int)(t->x0 - cx), y0 = (int)(t->y0 - cy), x1 = (int)(t->x1 - cx), y1 = (int)(t->y1 - cy);
        if (t->kind == 2) {                                  /* lightning: jagged */
            int px = x0, py = y0;
            for (int k = 1; k <= 6; k++) {
                int nx = x0 + (x1 - x0) * k / 6 + (k < 6 ? rng_range(&G->fx, -4, 4) : 0), ny = y0 + (y1 - y0) * k / 6 + (k < 6 ? rng_range(&G->fx, -4, 4) : 0);
                line(s, px, py, nx, ny, 0xffffff); line_blend(s, px + 1, py, nx + 1, ny, t->col, 160);
                px = nx; py = ny;
            }
        } else {
            line_blend(s, x0, y0, x1, y1, t->col, 150);
            line(s, x1 - (x1 - x0) / 6, y1 - (y1 - y0) / 6, x1, y1, t->kind ? 0xe0a0ff : 0xffffe0);
        }
    }
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *pt = &G->parts[i];
        if (!pt->alive) continue;
        int x = (int)(pt->x - cx), y = (int)(pt->y - cy - pt->z);
        float k = pt->life / pt->max;
        if (pt->type == PT_FIRE) { pset(s, x, y, k > 0.6f ? 0xfff0a0 : k > 0.3f ? 0xffa030 : 0xc04010); if (k > 0.5f) pset(s, x, y - 1, 0xffd060); }
        else if (pt->type == PT_SPARK) pset(s, x, y, 0xfff0b0);
        else if (pt->type == PT_ELEC) { pset(s, x, y, 0xffffff); pblend(s, x + 1, y, pt->col, 160); }
        else if (pt->type == PT_GLOW) { pset(s, x, y, pt->col); }
        else if (pt->type == PT_FROST) { pblend(s, x, y, pt->col, 200); pblend(s, x + 1, y + 1, 0xffffff, 120); }
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *sh = &G->shots[i];
        if (!sh->alive) continue;
        int x = (int)(sh->x - cx), y = (int)(sh->y - cy);
        if (sh->type == PR_PLASMA) { circlef(s, x, y, 2, sh->col); pset(s, x, y, 0xffffff); }
        else { circlef(s, x, y, 1, 0xffd080); }
    }
    if (p->muzzle_t > 0 && !p->downed) {
        int mx = (int)(p->x - cx + cosf(p->aim) * 12), my = (int)(p->y - cy - 9 + sinf(p->aim) * 9);
        circlef(s, mx, my, 2, 0xffffe0);
        pset(s, mx + (int)(cosf(p->aim) * 4), my + (int)(sinf(p->aim) * 4), 0xffd060);
    }
    for (int i = 0; i < G->nprops; i++) {                  /* lamp heads */
        Prop *pr = &G->props[i];
        if (!G->power_on || (pr->kind != P_LAMP && pr->kind != P_LAMP_WALL)) continue;
        int x = pr->x - cx, y = pr->y - cy;
        if (x < -10 || x > s->w + 10 || y < 0 || y > s->h + 40) continue;
        if (pr->kind == P_LAMP) hline(s, x - 7, x - 3, y - 29, 0xfff4c8);
        else rectf(s, x - 1, y - 29, 3, 4, 0xffe0a0);
    }
    if (G->nbox_spots && !G->box_moving) {                 /* the box's beam of light */
        Inter *it = &G->it[G->box_spots[G->box_at]];
        int bx = it->tx * TS + 16 - cx, by = (it->ty + 1) * TS - cy - 10;
        for (int y = by; y > by - 120; y--) {
            int a = (int)(60 * (y - (by - 120)) / 120.0f);
            rect_blend(s, bx - 2, y, 5, 1, 0xd0e8ff, a);
        }
        if (it->state == 1 || it->state == 2) {           /* the weapons spin above it, then one stays */
            int show = it->state == 2 ? it->b : (int)(G->time * 10) % W_COUNT;
            const Img *im = show >= 0 ? art(WEAPONS[show].icon) : 0;
            if (im) blit(s, im, bx - im->w / 2, by - 14 - (int)(MIN(1.0f, it->t) * 6), 0);
        } else if (it->state == 3) {                        /* the Dalahäst rises and flies */
            const Img *im = art("dalahast");
            if (im) blit(s, im, bx - im->w / 2, by - 18 - (int)(it->t * 20), 0);
        }
    }
    for (int i = 0; i < G->nit; i++) {                    /* Smedjan's work, the power switch's light */
        Inter *it = &G->it[i];
        if (it->type == IT_PAP && it->state == 2) {
            const Img *im = art(WEAPONS[it->b].icon);
            if (im) blit_ex(s, im, it->tx * TS + 16 - cx - im->w / 2, it->ty * TS - cy - 8, 0, 0xc070ff, 120, 255);
        }
    }
    draw_weather(s);
    /* floating numbers */
    for (int i = 0; i < MAX_TEXTS; i++) {
        FloatText *f = &G->ft[i];
        if (!f->alive) continue;
        text_ol(s, FONT_SMALL, (int)(f->x - cx) - text_w(FONT_SMALL, f->s) / 2, (int)(f->y - cy), f->col, 0x000000, f->s);
    }
    G->camx = camx; G->camy = camy;
    overlays(s);
}
