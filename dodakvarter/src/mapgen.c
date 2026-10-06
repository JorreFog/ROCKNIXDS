// mapgen.c: a new town every run. The map is a grid of districts (zones), each filled by a generator for one kind
// of Swedish neighbourhood. Zones are walled off from each other and joined through gaps ("ports") that start
// blocked by a barrier the player pays to clear, as Call of Duty Zombies' doors and debris. Every zone gets
// spawn points (boarded windows, manholes, graves, bare ground), loot, and the map gets its Mystery Box spots, wall
// buys, perk machines, the power switch and the Pack-a-Punch.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

static Rng *R;                          /* the map's own generator: the same seed makes the same town */

/* ---------------------------------------------------------------- tiles */
static void set_ground(int x, int y, int g) { Tile *t = tile_at(x, y); if (t) t->g = (uint8_t)g; }
static void fill_ground(int x, int y, int w, int h, int g) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) set_ground(i, j, g);
}
static void set_flag(int x, int y, int f) { Tile *t = tile_at(x, y); if (t) t->f |= (uint8_t)f; }
static void clr_flag(int x, int y, int f) { Tile *t = tile_at(x, y); if (t) t->f &= (uint8_t)~f; }
static int has_flag(int x, int y, int f) { Tile *t = tile_at(x, y); return t ? (t->f & f) != 0 : 1; }
static void set_deco(int x, int y, int d) { Tile *t = tile_at(x, y); if (t) t->deco = (uint8_t)d; }
static int ground_at(int x, int y) { Tile *t = tile_at(x, y); return t ? t->g : -1; }
static int free_rect(int x, int y, int w, int h, int mask) {   /* no tile in the rect has any of mask's flags */
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (has_flag(i, j, mask)) return 0;
    return 1;
}
#define BLOCKERS (TF_SOLID | TF_RESERVED | TF_INTER)

/* ---------------------------------------------------------------- things */
static int add_prop(int kind, int px, int py, int solid_w, int solid_h) {
    if (G->nprops >= MAX_PROPS) return -1;
    Prop *p = &G->props[G->nprops];
    memset(p, 0, sizeof *p);
    p->x = (int16_t)px; p->y = (int16_t)py; p->kind = (uint8_t)kind; p->var = (uint8_t)rng_int(R, 256);
    if (solid_w > 0) {                  /* the tiles under its feet block walking */
        int tx = px / TS - (solid_w - 1) / 2, ty = (py - 1) / TS - (solid_h - 1);
        for (int j = 0; j < solid_h; j++) for (int i = 0; i < solid_w; i++) set_flag(tx + i, ty + j, TF_SOLID);
        p->solid = 1;
    }
    return G->nprops++;
}
/* a prop standing on tile (tx, ty), feet at the tile's bottom centre */
static int prop_on(int kind, int tx, int ty, int solid) {
    return add_prop(kind, tx * TS + TS / 2, ty * TS + TS, solid, solid);
}
static int add_inter(int type, int tx, int ty, int tw, int th, float ux, float uy) {
    if (G->nit >= MAX_INTER) return -1;
    Inter *it = &G->it[G->nit];
    memset(it, 0, sizeof *it);
    it->type = type; it->tx = tx; it->ty = ty; it->tw = tw; it->th = th; it->x = ux; it->y = uy; it->prop = -1;
    for (int j = ty; j < ty + th; j++) for (int i = tx; i < tx + tw; i++) {
        Tile *t = tile_at(i, j);
        if (t) { t->inter = (uint16_t)(G->nit + 1); t->f |= TF_INTER; }
    }
    return G->nit++;
}
static void add_spawn(int type, int zone, float x, float y, int inter) {
    if (G->nspawns >= MAX_SPAWNS) return;
    Spawn *s = &G->spawns[G->nspawns++];
    s->type = type; s->zone = zone; s->x = x; s->y = y; s->inter = inter;
    /* nothing may be built where they come out */
    set_flag((int)x / TS, (int)y / TS + (type == SP_WINDOW ? 1 : 0), TF_RESERVED);
}
static int add_building(int x, int y, int w, int h, int fh, int style) {
    if (G->nb >= MAX_BUILDINGS) return -1;
    Building *b = &G->b[G->nb];
    memset(b, 0, sizeof *b);
    b->x = x; b->y = y; b->w = w; b->h = h; b->fh = fh; b->style = style; b->seed = rng_u32(R);
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) {
        Tile *t = tile_at(i, j); if (!t) continue;
        t->f |= TF_SOLID | TF_OPAQUE | TF_BUILDING; t->bld = (uint8_t)(G->nb + 1);
    }
    return G->nb++;
}

/* a loot container: the prop, and an interactable on its tile used from the tile below (or beside) */
static int add_loot_prop(int kind, int tx, int ty, int solid) {
    int pi = prop_on(kind, tx, ty, solid);
    if (pi < 0) return -1;
    int ii = add_inter(IT_LOOT, tx, ty, 1, 1, tx * TS + TS / 2, ty * TS + TS + 4);
    if (ii < 0) return pi;
    G->it[ii].prop = pi; G->props[pi].loot = 1; G->props[pi].inter = (uint16_t)(ii + 1);
    return pi;
}

/* ---------------------------------------------------------------- paths */
/* an L-shaped path of width wd from (x0,y0) to (x1,y1), reserved so nothing is built on it */
static void carve(int x0, int y0, int x1, int y1, int wd, int g, int horiz_first) {
    int cx = x0, cy = y0;
    for (int pass = 0; pass < 2; pass++) {
        int along_x = (pass == 0) == horiz_first;
        if (along_x) {
            int s = SGN(x1 - cx);
            for (;; cx += s) {
                for (int k = 0; k < wd; k++) { set_ground(cx, cy + k, g); set_flag(cx, cy + k, TF_RESERVED | TF_PATH); clr_flag(cx, cy + k, TF_SOLID | TF_OPAQUE); }
                if (cx == x1 || !s) break;
            }
        } else {
            int s = SGN(y1 - cy);
            for (;; cy += s) {
                for (int k = 0; k < wd; k++) { set_ground(cx + k, cy, g); set_flag(cx + k, cy, TF_RESERVED | TF_PATH); clr_flag(cx + k, cy, TF_SOLID | TF_OPAQUE); }
                if (cy == y1 || !s) break;
            }
        }
    }
}

/* ---------------------------------------------------------------- layout */
typedef struct { int a, b; int x, y, w, h; int vertical; } Port;   /* gap between zones a and b */
static Port ports[48]; static int nports;
static int cell_zone[6][6];
static int colx[7], rowy[7];

static int zone_rect_inner(int z, int *x, int *y, int *w, int *h) {
    Zone *zn = &G->zones[z];
    *x = zn->x + 1; *y = zn->y + 1; *w = zn->w - 2; *h = zn->h - 2;
    return 1;
}

/* the boundary ring of a zone: what keeps its district in */
static void zone_ring(int z) {
    Zone *zn = &G->zones[z];
    for (int j = zn->y; j < zn->y + zn->h; j++)
        for (int i = zn->x; i < zn->x + zn->w; i++) {
            if (i != zn->x && j != zn->y && i != zn->x + zn->w - 1 && j != zn->y + zn->h - 1) continue;
            Tile *t = tile_at(i, j);
            int edge = i == 0 || j == 0 || i == G->w - 1 || j == G->h - 1;
            t->f |= TF_SOLID;
            if (edge) { t->g = G_FOREST; t->f |= TF_OPAQUE; continue; }
            switch (zn->type) {
            case Z_PARK: case Z_CHURCH: t->g = (zn->type == Z_PARK) ? G_FOREST : G_GRASS; if (zn->type == Z_PARK) t->f |= TF_OPAQUE; break;
            case Z_OLDTOWN: t->g = G_COBBLE; break;
            case Z_HARBOR: case Z_STATION: case Z_MALL: t->g = G_ASPHALT; break;
            case Z_TORG: t->g = G_PLAZA; break;
            case Z_SCHOOL: t->g = G_SCHOOLYARD; break;
            default: t->g = G_GRASS; break;
            }
        }
}

/* boundary props along the ring (fences, hedges, walls, forest is a ground) */
static void ring_props(int z) {
    Zone *zn = &G->zones[z];
    int kind_h, kind_v;
    switch (zn->type) {
    case Z_VILLA: kind_h = P_HEDGE_H; kind_v = P_HEDGE_V; break;
    case Z_ALLOT: kind_h = P_PICKET_H; kind_v = P_PICKET_V; break;
    case Z_CHURCH: kind_h = P_WALL_H; kind_v = P_WALL_V; break;
    case Z_OLDTOWN: kind_h = P_WALL_H; kind_v = P_WALL_V; break;
    case Z_GARDEN: kind_h = P_HEDGE_H; kind_v = P_HEDGE_V; break;
    case Z_PARK: return;
    default: kind_h = P_FENCE_H; kind_v = P_FENCE_V; break;
    }
    for (int j = zn->y; j < zn->y + zn->h; j++)
        for (int i = zn->x; i < zn->x + zn->w; i++) {
            int top = j == zn->y, bot = j == zn->y + zn->h - 1, left = i == zn->x, right = i == zn->x + zn->w - 1;
            if (!(top || bot || left || right)) continue;
            Tile *t = tile_at(i, j);
            if (!(t->f & TF_SOLID) || t->g == G_FOREST || (t->f & TF_INTER)) continue;
            prop_on((top || bot) ? kind_h : kind_v, i, j, 0);
            if (kind_h == P_HEDGE_H || kind_h == P_WALL_H) t->f |= TF_OPAQUE;
        }
}

/* ---------------------------------------------------------------- shared district pieces */
static const char *SHOP_SIGNS[] = {
    "LIVS", "PIZZERIA", "APOTEK", "FRISÖR", "KONDITORI", "BIBLIOTEK", "VÅRDCENTRAL", "BANK", "BLOMMOR", "GYM",
    "SKOMAKARE", "TOBAK", "CAFÉ", "SUSHI", "OPTIKER", "FRITIDSGÅRD", "KEBAB", "BUTIK", "POST", "TANDLÄKARE"
};
static const char *pick_sign(void) { return SHOP_SIGNS[rng_int(R, ARRAY_LEN(SHOP_SIGNS))]; }

static uint32_t pick(const uint32_t *c, int n) { return c[rng_int(R, n)]; }
static const uint32_t CONCRETE[] = { 0x9c9a92, 0xb9ae94, 0xa8a39a, 0xc2b8a0, 0x8f8a80 };
static const uint32_t BRICK[] = { 0x8a4b38, 0x9a5a40, 0x7c4030, 0xc9a96a, 0xd8d4c8 };
static const uint32_t BALCONY[] = { 0xd9782d, 0xd4a62a, 0x3e6fa3, 0x4e8a5c, 0x8a5a3a, 0x6a8a9c };
static const uint32_t OLDTOWN[] = { 0xc99a3b, 0xa44a2c, 0xe3c26b, 0xb88a5a, 0x8a9a6a, 0xd8b88a, 0xc07040, 0xe0d0a8 };
static const uint32_t COTTAGE[] = { 0x8c2b1e, 0xe8e2d0, 0xd8b84a, 0x5a8a5a, 0x8c2b1e, 0x4a6a9a };
static const uint32_t ROOF_DARK[] = { 0x3a3a40, 0x45403c, 0x2e3238, 0x4a4a52 };
static const uint32_t ROOF_TILE[] = { 0xb5532e, 0x9a4428, 0xa85a38 };

/* an apron of walkable ground in front of a facade (the row below the building), reserved */
static void apron(int bi, int g) {
    Building *b = &G->b[bi];
    for (int i = b->x; i < b->x + b->w; i++) {
        Tile *t = tile_at(i, b->y + b->h);
        if (!t || (t->f & TF_SOLID)) continue;
        if (g >= 0) t->g = (uint8_t)g;
        t->f |= TF_RESERVED;
    }
}

/* a boarded window or door on a building's facade (its bottom row) that zombies climb through */
static int add_window(int bi, int col, int zone) {
    Building *b = &G->b[bi];
    int tx = b->x + col, ty = b->y + b->h - 1;
    Tile *below = tile_at(tx, ty + 1);
    if (!below || (below->f & TF_SOLID) || (tile_at(tx, ty)->f & TF_INTER)) return -1;
    int ii = add_inter(IT_WINDOW, tx, ty, 1, 1, tx * TS + TS / 2, (ty + 1) * TS + 6);
    if (ii < 0) return -1;
    G->it[ii].state = 6;                /* boards */
    G->it[ii].a = bi;
    add_spawn(SP_WINDOW, zone, tx * TS + TS / 2, ty * TS + TS - 2, ii);
    below->f |= TF_RESERVED;
    return ii;
}

/* a lamp post, a bench or a bin somewhere along a path */
static void path_furniture(int x, int y, int w, int h, int density) {
    for (int n = 0; n < density; n++) {
        int i = x + rng_int(R, w), j = y + rng_int(R, h);
        Tile *t = tile_at(i, j);
        if (!t || (t->f & (TF_SOLID | TF_INTER))) continue;
        if (!(t->f & TF_RESERVED)) continue;
        /* next to the path, not on it */
        static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        int k = rng_int(R, 4), ni = i + d[k][0], nj = j + d[k][1];
        Tile *nt = tile_at(ni, nj);
        if (!nt || (nt->f & (TF_SOLID | TF_RESERVED | TF_INTER | TF_BUILDING))) continue;
        int r = rng_int(R, 10);
        if (r < 4) prop_on(P_LAMP, ni, nj, 1);
        else if (r < 7) prop_on(P_BENCH, ni, nj, 1);
        else add_loot_prop(P_BIN, ni, nj, 1);
    }
}

static void scatter(int kind, int x, int y, int w, int h, int n, int solid, int avoid) {
    for (int k = 0; k < n * 4 && n > 0; k++) {
        int i = x + rng_int(R, w), j = y + rng_int(R, h);
        if (!free_rect(i, j, 1, 1, avoid)) continue;
        Tile *t = tile_at(i, j);
        if (t->g == G_WATER) continue;
        prop_on(kind, i, j, solid);
        n--;
    }
}
static void trees(int x, int y, int w, int h, int n) {
    for (int k = 0; k < n * 4 && n > 0; k++) {
        int i = x + rng_int(R, w), j = y + rng_int(R, h);
        if (!free_rect(i - 1, j - 1, 3, 2, BLOCKERS) || tile_at(i, j)->g == G_WATER) continue;
        int r = rng_int(R, 100);
        int kind = r < 40 ? P_BIRCH : r < 75 ? P_PINE : r < 88 ? P_SPRUCE_SMALL : P_BUSH;
        prop_on(kind, i, j, 1);
        n--;
    }
}

/* ---------------------------------------------------------------- districts */
typedef struct { int x, y, w, h, zone; int hubx, huby; } Area;

/* connect every port of the zone to its hub with a path */
static void connect_ports(Area *a, int g, int wd) {
    Zone *zn = &G->zones[a->zone];
    for (int i = 0; i < nports; i++) {
        Port *p = &ports[i];
        if (p->a != a->zone && p->b != a->zone) continue;
        if (p->vertical) {              /* the gap crosses a vertical border: enter sideways */
            int ex = p->x == zn->x + zn->w - 1 ? p->x - 1 : p->x + 2;
            carve(ex, p->y, a->hubx, a->huby, wd, g, 1);
        } else {                        /* a horizontal border: enter from the north or south */
            int ey = p->y == zn->y + zn->h - 1 ? p->y - 1 : p->y + 2;
            carve(p->x, ey, a->hubx, a->huby, wd, g, 0);
        }
    }
}

/* a row of buildings filling the free runs of a horizontal strip */
static int building_row(Area *a, int y, int h, int fh, int style, int minw, int maxw, int gap, int *out, int maxout) {
    int n = 0, x = a->x;
    while (x < a->x + a->w && n < maxout) {
        while (x < a->x + a->w && !free_rect(x, y, 1, h, BLOCKERS)) x++;
        int run = 0;
        while (x + run < a->x + a->w && free_rect(x + run, y, 1, h, BLOCKERS)) run++;
        if (run >= minw) {
            int w = MIN(run, rng_range(R, minw, maxw));
            if (run - w < minw) w = run;   /* no sliver left over */
            if (w > maxw + minw) w = maxw;
            int bi = add_building(x, y, w, h, fh, style);
            if (bi >= 0) out[n++] = bi;
            x += w + gap;
        } else x += run + 1;
    }
    return n;
}

static void style_lamell(int bi) {
    Building *b = &G->b[bi];
    int brick = rng_chance(R, 0.4f);
    b->style = brick ? BS_LAMELL_BRICK : BS_LAMELL;
    b->wall = brick ? pick(BRICK, ARRAY_LEN(BRICK)) : pick(CONCRETE, ARRAY_LEN(CONCRETE));
    b->wall2 = pick(BALCONY, ARRAY_LEN(BALCONY));
    b->roof = pick(ROOF_DARK, ARRAY_LEN(ROOF_DARK));
    b->trim = 0xe8e4da; b->lit = 1;
}

static void gen_garden(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_GRASS);
    a->hubx = a->x + a->w / 2 - 1; a->huby = a->y + a->h / 2;
    connect_ports(a, G_SIDEWALK, 2);
    /* the yard's own cross of paths */
    carve(a->x + 2, a->huby, a->x + a->w - 3, a->huby, 2, G_SIDEWALK, 1);
    /* lamellhus along the north and south edges, facades on the yard side (north) and outward (south) */
    int out[8];
    int n = building_row(a, a->y, 5, 3, BS_LAMELL, 6, 11, 2, out, 4);
    for (int i = 0; i < n; i++) {
        style_lamell(out[i]); apron(out[i], G_SIDEWALK);
        Building *b = &G->b[out[i]];
        add_window(out[i], 1 + rng_int(R, MAX(1, b->w - 2)), a->zone);
        if (b->w > 8) add_window(out[i], b->w - 2, a->zone);
    }
    n = building_row(a, a->y + a->h - 6, 5, 3, BS_LAMELL, 6, 10, 2, out, 4);
    for (int i = 0; i < n; i++) {
        style_lamell(out[i]); apron(out[i], G_SIDEWALK);
        add_window(out[i], 1 + rng_int(R, MAX(1, G->b[out[i]].w - 2)), a->zone);
    }
    /* the yard: playground, tvättstuga, miljöhus, bike racks, benches, birches */
    int yx = a->x + 2, yy = a->y + 6, yw = a->w - 4, yh = a->h - 13;
    if (yh >= 3) {
        int px = yx + rng_int(R, MAX(1, yw - 6)), py = yy + rng_int(R, MAX(1, yh - 3));
        if (free_rect(px, py, 5, 3, BLOCKERS)) {
            fill_ground(px, py, 5, 3, G_SAND);
            for (int j = py; j < py + 3; j++) for (int i = px; i < px + 5; i++) set_flag(i, j, TF_RESERVED);
            prop_on(P_SWINGS, px + 1, py + 1, 1);
            prop_on(P_CLIMBER, px + 3, py + 2, 1);
            prop_on(P_SANDBOX, px + 4, py, 0);
        }
        for (int k = 0; k < 6; k++) {
            int tx = yx + rng_int(R, MAX(1, yw - 4)), ty = yy + rng_int(R, MAX(1, yh - 2));
            if (free_rect(tx - 1, ty - 1, 6, 4, BLOCKERS)) {
                int bi = add_building(tx, ty, 4, 3, 2, BS_TVATT);
                if (bi >= 0) {
                    Building *b = &G->b[bi]; b->wall = pick(BRICK, 3); b->roof = 0x3a3a40; snprintf(b->sign, sizeof b->sign, "TVÄTTSTUGA");
                    apron(bi, G_SIDEWALK);
                }
                break;
            }
        }
        trees(yx, yy, yw, yh, 4 + rng_int(R, 4));
        scatter(P_BENCH, yx, yy, yw, yh, 2, 1, BLOCKERS);
    }
    for (int k = 0; k < 3; k++) {       /* recycling and bins by the entrances */
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 5 + rng_int(R, 2);
        if (free_rect(tx, ty, 2, 1, BLOCKERS)) { add_loot_prop(P_RECYCLE, tx, ty, 1); break; }
    }
    scatter(P_BIKES, a->x + 1, a->y + 5, a->w - 2, 2, 2, 1, BLOCKERS);
    path_furniture(a->x, a->y, a->w, a->h, 10);
}

static void gen_torg(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_PLAZA);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2;
    /* a road along the south with the bus stop */
    int ry = a->y + a->h - 4;
    for (int i = a->x; i < a->x + a->w; i++) {
        for (int k = 0; k < 3; k++) { set_ground(i, ry + k, G_ASPHALT); set_flag(i, ry + k, TF_ROAD); }
        set_ground(i, ry + 3, G_SIDEWALK);
        if (i % 3 != 0) set_deco(i, ry + 1, D_LINE_H);
    }
    int zx = a->x + 3 + rng_int(R, MAX(1, a->w - 6));
    for (int k = 0; k < 3; k++) set_deco(zx, ry + k, D_ZEBRA_V);
    connect_ports(a, G_PLAZA, 2);
    for (int i = a->x; i < a->x + a->w; i++) for (int k = 0; k < 4; k++) set_flag(i, ry + k, TF_RESERVED);
    /* shops along the north, facades on the square */
    int out[8];
    int n = building_row(a, a->y, 4, 2, BS_SHOP, 4, 7, 0, out, 6);
    for (int i = 0; i < n; i++) {
        Building *b = &G->b[out[i]];
        b->wall = rng_chance(R, 0.5f) ? pick(CONCRETE, 5) : pick(BRICK, 5); b->roof = pick(ROOF_DARK, 4);
        b->trim = pick(BALCONY, ARRAY_LEN(BALCONY)); b->wall2 = 0x2a3440; b->lit = 1;
        snprintf(b->sign, sizeof b->sign, "%s", pick_sign());
        apron(out[i], G_PLAZA);
        if (b->w >= 5) add_window(out[i], 1 + rng_int(R, b->w - 2), a->zone);
    }
    /* the tunnelbana entrance and a kiosk on the square */
    for (int k = 0; k < 20; k++) {
        int tx = a->x + 2 + rng_int(R, MAX(1, a->w - 8)), ty = a->y + 6 + rng_int(R, MAX(1, a->h - 14));
        if (free_rect(tx - 1, ty - 1, 6, 5, BLOCKERS)) {
            int bi = add_building(tx, ty, 4, 3, 2, BS_STATION);
            if (bi >= 0) { G->b[bi].wall = 0x50606e; G->b[bi].roof = 0x2e3a48; snprintf(G->b[bi].sign, sizeof G->b[bi].sign, "T"); G->b[bi].lit = 1; apron(bi, G_PLAZA); }
            prop_on(P_SIGN_T, tx + 4 < a->x + a->w ? tx + 4 : tx - 1, ty + 2, 1);
            break;
        }
    }
    for (int k = 0; k < 20; k++) {
        int tx = a->x + 2 + rng_int(R, MAX(1, a->w - 6)), ty = a->y + 5 + rng_int(R, MAX(1, a->h - 12));
        if (free_rect(tx - 1, ty - 1, 5, 5, BLOCKERS)) {
            int bi = add_building(tx, ty, 3, 3, 2, BS_KIOSK);
            if (bi >= 0) { G->b[bi].wall = 0xd8c8a0; G->b[bi].roof = 0x8a2a20; G->b[bi].trim = 0xc83030; snprintf(G->b[bi].sign, sizeof G->b[bi].sign, rng_chance(R, 0.5f) ? "KIOSK" : "GATUKÖK"); G->b[bi].lit = 1; apron(bi, G_PLAZA); }
            break;
        }
    }
    /* a fountain or a statue in the middle */
    for (int k = 0; k < 40; k++) {                      /* near the middle, off the paths */
        int fx = a->hubx - 4 + rng_int(R, 9), fy = a->huby - 3 + rng_int(R, 7);
        if (free_rect(fx - 1, fy - 1, 3, 3, TF_SOLID | TF_INTER | TF_PATH)) { prop_on(rng_chance(R, 0.6f) ? P_FOUNTAIN : P_STATUE, fx, fy, 1); break; }
    }
    scatter(P_PLANTER, a->x + 1, a->y + 5, a->w - 2, a->h - 10, 4, 1, BLOCKERS);
    scatter(P_BENCH, a->x + 1, a->y + 5, a->w - 2, a->h - 10, 4, 1, BLOCKERS);
    scatter(P_LAMP, a->x + 1, a->y + 5, a->w - 2, a->h - 10, 4, 1, BLOCKERS);
    for (int k = 0; k < 6; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 5 + rng_int(R, MAX(1, a->h - 10));
        if (free_rect(tx, ty, 1, 1, BLOCKERS)) add_loot_prop(rng_chance(R, 0.5f) ? P_BIN : P_CART, tx, ty, 1);
    }
    /* the bus stop and parked cars on the road's far side */
    int bx = a->x + 2 + rng_int(R, MAX(1, a->w - 6));
    clr_flag(bx, ry - 1, TF_RESERVED);
    if (free_rect(bx, ry - 1, 2, 1, TF_SOLID | TF_INTER | TF_PATH)) { prop_on(P_BUSSTOP, bx, ry - 1, 0); set_flag(bx, ry - 1, TF_SOLID); set_flag(bx + 1, ry - 1, TF_SOLID); }
    for (int k = 0; k < 3; k++) {
        int cx = a->x + 1 + rng_int(R, MAX(1, a->w - 3));
        if (abs(cx - zx) > 2 && free_rect(cx, ry + 2, 2, 1, TF_SOLID | TF_INTER | TF_PATH)) {
            int pi = add_loot_prop(P_CAR, cx, ry + 2, 0);
            if (pi >= 0) { set_flag(cx, ry + 2, TF_SOLID); set_flag(cx + 1, ry + 2, TF_SOLID); G->props[pi].x += TS / 2; }
        }
    }
    if (rng_chance(R, 0.5f)) add_spawn(SP_MANHOLE, a->zone, (a->x + 2 + rng_int(R, a->w - 4)) * TS + 8, (ry + 1) * TS + 8, -1);
}

static void gen_villa(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_GRASS);
    /* the street through the middle, east-west */
    int ry = a->y + a->h / 2 - 1;
    for (int i = a->x; i < a->x + a->w; i++) {
        set_ground(i, ry - 1, G_SIDEWALK); set_flag(i, ry - 1, TF_RESERVED);
        for (int k = 0; k < 2; k++) { set_ground(i, ry + k, G_ASPHALT); set_flag(i, ry + k, TF_RESERVED | TF_ROAD); }
        set_ground(i, ry + 2, G_SIDEWALK); set_flag(i, ry + 2, TF_RESERVED);
    }
    a->hubx = a->x + a->w / 2; a->huby = ry;
    connect_ports(a, G_GRAVEL, 2);
    add_spawn(SP_MANHOLE, a->zone, (a->x + 3 + rng_int(R, a->w - 6)) * TS + 8, ry * TS + 16, -1);
    /* plots on both sides: house, garden, fence, flagpole, trampoline, apple tree */
    for (int side = 0; side < 2; side++) {
        int py = side == 0 ? a->y : ry + 3, ph = side == 0 ? ry - 1 - a->y : a->y + a->h - (ry + 3);
        if (ph < 5) continue;
        int x = a->x;
        while (x + 6 <= a->x + a->w) {
            int pw = rng_range(R, 6, 8); if (x + pw > a->x + a->w) pw = a->x + a->w - x;
            if (pw < 6) break;
            int hw = rng_range(R, 4, MIN(5, pw - 2)), hh = 4;
            int hx = x + 1 + rng_int(R, MAX(1, pw - hw - 1)), hy = side == 0 ? py + MAX(0, ph - hh - 2) : py + 1;
            if (!free_rect(hx, hy, hw, hh, BLOCKERS)) { x += pw; continue; }
            int bi = add_building(hx, hy, hw, hh, 2, BS_VILLA);
            if (bi < 0) break;
            Building *b = &G->b[bi];
            int r = rng_int(R, 10);
            b->wall = r < 6 ? 0x8c2b1e : r < 8 ? 0xe3c26b : r < 9 ? 0xe8e2d0 : 0x6a8aa0;
            b->trim = 0xf2f0ea; b->roof = rng_chance(R, 0.6f) ? pick(ROOF_DARK, 4) : pick(ROOF_TILE, 3); b->lit = 1;
            apron(bi, G_GRAVEL);
            add_window(bi, 1 + rng_int(R, hw - 2), a->zone);
            /* the garden around it */
            int gx = x, gw = pw;
            if (rng_chance(R, 0.7f)) {
                int fy = side == 0 ? py : py + ph - 1;
                for (int i = gx; i < gx + gw; i++) if (free_rect(i, fy, 1, 1, BLOCKERS)) prop_on(rng_chance(R, 0.5f) ? P_PICKET_H : P_HEDGE_H, i, fy, 1);
            }
            int ax = gx + rng_int(R, gw), ay = side == 0 ? py + rng_int(R, MAX(1, ph - 3)) : py + ph - 2;
            if (free_rect(ax - 1, ay - 1, 3, 2, BLOCKERS)) prop_on(rng_chance(R, 0.5f) ? P_APPLE : P_BIRCH, ax, ay, 1);
            int fx = gx + rng_int(R, gw), fy = side == 0 ? py + 1 + rng_int(R, MAX(1, ph - 4)) : py + ph - 2 - rng_int(R, 2);
            if (rng_chance(R, 0.55f) && free_rect(fx, fy, 1, 1, BLOCKERS)) prop_on(P_FLAGPOLE, fx, fy, 1);
            int tx = gx + rng_int(R, MAX(1, gw - 2)), ty = side == 0 ? py + rng_int(R, MAX(1, ph - 4)) : py + 2 + rng_int(R, MAX(1, ph - 4));
            if (rng_chance(R, 0.4f) && free_rect(tx, ty, 2, 2, BLOCKERS)) {
                int pi = prop_on(P_TRAMPOLINE, tx, ty + 1, 0);
                if (pi >= 0) { G->props[pi].x += TS / 2; for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) set_flag(tx + i, ty + j, TF_SOLID); }
            }
            /* the car in the driveway */
            int cx = hx + hw, cy = side == 0 ? ry - 2 : ry + 3;
            if (rng_chance(R, 0.6f) && free_rect(cx, cy - 1, 1, 2, BLOCKERS) && cx < a->x + a->w) {
                int pi = add_loot_prop(P_CAR_V, cx, cy, 0);
                if (pi >= 0) { set_flag(cx, cy, TF_SOLID); set_flag(cx, cy - 1, TF_SOLID); }
            }
            if (rng_chance(R, 0.3f)) {
                int sx = gx + rng_int(R, gw - 1), sy = side == 0 ? py : py + ph - 3;
                if (free_rect(sx, sy, 2, 2, BLOCKERS)) {
                    int si = add_building(sx, sy, 2, 2, 1, BS_SHED);
                    if (si >= 0) { G->b[si].wall = pick(COTTAGE, 6); G->b[si].roof = 0x3a3a40; G->b[si].trim = 0xf2f0ea; }
                }
            }
            x += pw;
        }
    }
    /* the mailbox row and the moose sign by the road */
    for (int k = 0; k < 10; k++) {
        int mx = a->x + 1 + rng_int(R, a->w - 2);
        if (free_rect(mx, ry - 2, 1, 1, BLOCKERS)) { add_loot_prop(P_MAILBOXES, mx, ry - 2, 1); break; }
    }
    for (int k = 0; k < 10; k++) {
        int mx = a->x + 1 + rng_int(R, a->w - 2);
        if (free_rect(mx, ry + 3, 1, 1, BLOCKERS)) { prop_on(P_SIGN_MOOSE, mx, ry + 3, 1); break; }
    }
    for (int i = a->x + 2; i < a->x + a->w - 1; i += 7) {   /* street lamps */
        if (free_rect(i, ry - 2, 1, 1, BLOCKERS)) prop_on(P_LAMP, i, ry - 2, 1);
    }
}

static void gen_oldtown(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_COBBLE);
    a->hubx = a->x + a->w / 2 - 2 + rng_int(R, 4); a->huby = a->y + a->h / 2 - 1 + rng_int(R, 3);
    /* a little square with the well */
    int sw = 6, sh = 4, sx = a->hubx - sw / 2, sy = a->huby - sh / 2;
    for (int j = sy; j < sy + sh; j++) for (int i = sx; i < sx + sw; i++) { set_ground(i, j, G_COBBLE); set_flag(i, j, TF_RESERVED); }
    connect_ports(a, G_COBBLE, 2);
    /* gränder: a few extra alleys */
    for (int k = 0; k < 2; k++) {
        int x0 = a->x + rng_int(R, a->w), y0 = rng_chance(R, 0.5f) ? a->y : a->y + a->h - 1;
        carve(x0, y0, a->hubx, a->huby, 1, G_COBBLE, rng_chance(R, 0.5f));
    }
    /* fill the rest with tall narrow houses, south faces on the alleys */
    for (int y = a->y; y < a->y + a->h; y++)
        for (int x = a->x; x < a->x + a->w; x++) {
            if (!free_rect(x, y, 1, 1, BLOCKERS)) continue;
            int w = rng_range(R, 3, 5), h = rng_range(R, 4, 5);
            while (w > 2 && !free_rect(x, y, w, 1, BLOCKERS)) w--;
            while (h > 3 && !free_rect(x, y, w, h, BLOCKERS)) h--;
            if (w < 2 || h < 3 || !free_rect(x, y, w, h, BLOCKERS)) continue;
            /* keep the row under it walkable if it is an alley: a facade needs ground in front */
            int bi = add_building(x, y, w, h, MIN(3, h - 1), BS_OLDTOWN);
            if (bi < 0) break;
            Building *b = &G->b[bi];
            b->wall = pick(OLDTOWN, ARRAY_LEN(OLDTOWN)); b->roof = rng_chance(R, 0.7f) ? pick(ROOF_TILE, 3) : 0x3a3a40; b->trim = 0xf0e8d8; b->lit = 1;
            if (rng_chance(R, 0.15f)) snprintf(b->sign, sizeof b->sign, "%s", rng_chance(R, 0.5f) ? "KROG" : pick_sign());
            if (w >= 3 && rng_chance(R, 0.35f)) add_window(bi, 1, a->zone);
        }
    prop_on(P_WELL, a->hubx, a->huby, 1);
    for (int j = a->y; j < a->y + a->h; j++)       /* lamps on the walls */
        for (int i = a->x; i < a->x + a->w; i++) {
            Tile *t = tile_at(i, j);
            if ((t->f & TF_BUILDING) && !has_flag(i, j + 1, TF_SOLID) && rng_chance(R, 0.08f) && !(t->f & TF_INTER))
                prop_on(P_LAMP_WALL, i, j + 1, 0);
        }
    scatter(P_BARREL, sx, sy, sw, sh, 1, 1, TF_SOLID | TF_INTER);
    for (int k = 0; k < 4; k++) {
        int tx = a->x + rng_int(R, a->w), ty = a->y + rng_int(R, a->h);
        Tile *t = tile_at(tx, ty);
        if (t && !(t->f & (TF_SOLID | TF_INTER | TF_PATH)) && (t->f & TF_RESERVED)) {
            /* only in a wide spot: never block an alley */
            int open = !has_flag(tx - 1, ty, TF_SOLID) + !has_flag(tx + 1, ty, TF_SOLID) + !has_flag(tx, ty - 1, TF_SOLID) + !has_flag(tx, ty + 1, TF_SOLID);
            if (open == 4) add_loot_prop(P_BARREL, tx, ty, 1);
        }
    }
}

static void gen_allot(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_GRAVEL);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2;
    connect_ports(a, G_GRAVEL, 1);
    int pw = 6, ph = 5;
    for (int y = a->y; y + ph <= a->y + a->h; y += ph + 1)
        for (int x = a->x; x + pw <= a->x + a->w; x += pw + 1) {
            if (!free_rect(x, y, pw, ph, BLOCKERS)) continue;
            fill_ground(x, y, pw, ph, G_GRASS);
            /* fence around, gate on the south */
            int gate = x + 1 + rng_int(R, pw - 2);
            for (int i = x; i < x + pw; i++) {
                prop_on(P_PICKET_H, i, y, 1);
                if (i != gate) prop_on(P_PICKET_H, i, y + ph - 1, 1);
            }
            for (int j = y + 1; j < y + ph - 1; j++) { prop_on(P_PICKET_V, x, j, 1); prop_on(P_PICKET_V, x + pw - 1, j, 1); }
            set_ground(gate, y + ph - 1, G_GRAVEL);
            /* the cottage, beds and things */
            int cx = x + 1 + rng_int(R, 2);
            int bi = add_building(cx, y + 1, 3, 2, 1, BS_COTTAGE);
            if (bi >= 0) {
                Building *b = &G->b[bi]; b->wall = pick(COTTAGE, 6); b->roof = rng_chance(R, 0.5f) ? 0x3a3a40 : pick(ROOF_TILE, 3); b->trim = 0xf2f0ea; b->lit = 1;
                if (rng_chance(R, 0.5f)) add_window(bi, 1, a->zone);
            }
            for (int i = x + 1; i < x + pw - 1; i++) if (free_rect(i, y + 3, 1, 1, BLOCKERS) && rng_chance(R, 0.6f)) { set_ground(i, y + 3, G_SOIL); set_deco(i, y + 3, rng_chance(R, 0.4f) ? D_FLOWERS : D_NONE); }
            int r = rng_int(R, 4);
            int tx = x + pw - 2, ty = y + 2;
            if (free_rect(tx, ty, 1, 1, BLOCKERS)) {
                if (r == 0) add_loot_prop(P_COMPOST, tx, ty, 1);
                else if (r == 1) prop_on(P_APPLE, tx, ty, 1);
                else if (r == 2) add_loot_prop(P_BARREL, tx, ty, 1);
                else prop_on(P_WASHLINE, tx, ty, 0);
            }
            if (rng_chance(R, 0.4f)) add_spawn(SP_GROUND, a->zone, (x + 2) * TS + 8, (y + 3) * TS + 8, -1);
        }
    if (free_rect(a->x, a->y, 1, 1, TF_SOLID)) {}
    trees(a->x, a->y, a->w, a->h, 3);
}

static void gen_park(Area *a, int season) {
    fill_ground(a->x, a->y, a->w, a->h, G_GRASS);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2;
    /* a lake in one corner, with a jetty and a sauna */
    int lx = rng_chance(R, 0.5f) ? a->x + 1 : a->x + a->w - 9, ly = rng_chance(R, 0.5f) ? a->y + 1 : a->y + a->h - 7;
    int lw = 8, lh = 6;
    for (int j = 0; j < lh; j++) for (int i = 0; i < lw; i++) {
        float dx = (i - lw / 2.0f + 0.5f) / (lw / 2.0f), dy = (j - lh / 2.0f + 0.5f) / (lh / 2.0f);
        float r = dx * dx + dy * dy + (rng_float(R) - 0.5f) * 0.25f;
        if (r < 1.0f) {
            set_ground(lx + i, ly + j, G_WATER);
            if (season != SEASON_WINTER) set_flag(lx + i, ly + j, TF_SOLID | TF_WATER); else set_flag(lx + i, ly + j, TF_WATER);
        }
    }
    connect_ports(a, G_GRAVEL, 2);
    /* the jetty from the lake's middle toward the hub */
    int jx = lx + lw / 2, jy = ly + lh / 2;
    for (int k = 0; k < 4; k++) {
        int tx = jx + (a->hubx > jx ? k : -k), ty = jy;
        if (ground_at(tx, ty) == G_WATER) { set_ground(tx, ty, G_DECK); clr_flag(tx, ty, TF_SOLID | TF_WATER); set_flag(tx, ty, TF_RESERVED); }
    }
    for (int k = 0; k < 30; k++) {                    /* the sauna by the shore */
        int tx = lx - 3 + rng_int(R, lw + 6), ty = ly - 3 + rng_int(R, lh + 6);
        if (free_rect(tx - 1, ty - 1, 5, 4, BLOCKERS | TF_WATER) && tx > a->x && ty > a->y && tx + 3 < a->x + a->w && ty + 2 < a->y + a->h) {
            int bi = add_building(tx, ty, 3, 2, 1, BS_SAUNA);
            if (bi >= 0) { G->b[bi].wall = 0x8c2b1e; G->b[bi].roof = 0x3a3a40; G->b[bi].trim = 0xf2f0ea; snprintf(G->b[bi].sign, sizeof G->b[bi].sign, "BASTU"); apron(bi, -1); }
            break;
        }
    }
    /* bedrock and woods */
    for (int k = 0; k < 4; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 3), ty = a->y + 1 + rng_int(R, a->h - 3);
        int w = rng_range(R, 1, 3), h = rng_range(R, 1, 2);
        if (!free_rect(tx - 1, ty - 1, w + 2, h + 2, BLOCKERS | TF_WATER)) continue;
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { set_ground(tx + i, ty + j, G_ROCK); set_flag(tx + i, ty + j, TF_SOLID); }
    }
    trees(a->x, a->y, a->w, a->h, 14 + rng_int(R, 6));
    scatter(P_BENCH, a->x, a->y, a->w, a->h, 3, 1, BLOCKERS | TF_WATER);
    scatter(P_ROCK, a->x, a->y, a->w, a->h, 3, 1, BLOCKERS | TF_WATER);
    for (int k = 0; season == SEASON_SUMMER && k < 40; k++) {   /* the midsommarstång in a clearing */
        int mx = a->hubx - 5 + rng_int(R, 11), my = a->huby - 4 + rng_int(R, 9);
        if (free_rect(mx - 1, my - 1, 3, 3, TF_SOLID | TF_INTER | TF_WATER | TF_PATH)) { prop_on(P_MAYPOLE, mx, my, 1); break; }
    }
    if (season == SEASON_WINTER) scatter(P_SNOWMAN, a->x, a->y, a->w, a->h, 2, 1, BLOCKERS | TF_WATER);
    for (int k = 0; k < 5; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 1 + rng_int(R, a->h - 2);
        if (free_rect(tx, ty, 1, 1, BLOCKERS | TF_WATER)) add_spawn(SP_GROUND, a->zone, tx * TS + 8, ty * TS + 8, -1);
    }
    path_furniture(a->x, a->y, a->w, a->h, 6);
    for (int k = 0; k < 3; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 1 + rng_int(R, a->h - 2);
        if (free_rect(tx, ty, 1, 1, BLOCKERS | TF_WATER)) add_loot_prop(P_BIN, tx, ty, 1);
    }
}

static void gen_school(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_SCHOOLYARD);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2 + 1;
    connect_ports(a, G_SCHOOLYARD, 2);
    int out[4];
    int n = building_row(a, a->y, 5, 3, BS_SCHOOL, 8, a->w, 1, out, 2);
    static const char *names[] = { "BJÖRK", "EK", "SJÖ", "BERG", "LIND", "ÄNG", "TALL", "SOL" };
    for (int i = 0; i < n; i++) {
        Building *b = &G->b[out[i]];
        b->wall = pick(BRICK, 3); b->roof = 0x3a3a40; b->trim = 0xf0ece0; b->lit = 1;
        snprintf(b->sign, sizeof b->sign, "%sSKOLAN", names[rng_int(R, ARRAY_LEN(names))]);
        apron(out[i], G_SCHOOLYARD);
        add_window(out[i], 2, a->zone);
        if (b->w > 10) add_window(out[i], b->w - 3, a->zone);
    }
    /* the artificial turf pitch with its fence */
    int pw = MIN(12, a->w - 4), ph = MIN(7, a->h - 8);
    int px = a->x + 1 + rng_int(R, MAX(1, a->w - pw - 2)), py = a->y + a->h - ph - 1;
    if (pw >= 8 && ph >= 5 && free_rect(px, py, pw, ph, TF_SOLID | TF_INTER | TF_BUILDING)) {
        for (int j = py; j < py + ph; j++) for (int i = px; i < px + pw; i++) {
            set_ground(i, j, G_TURF); set_flag(i, j, TF_RESERVED);
        }
        for (int i = px; i < px + pw; i++) { set_deco(i, py + ph / 2, 0); }
        set_deco(px + pw / 2, py + ph / 2, D_PITCH_CIRCLE);
        for (int j = py; j < py + ph; j++) set_deco(px + pw / 2, j, j == py + ph / 2 ? D_PITCH_CIRCLE : D_PITCH_LINE_V);
        prop_on(P_GOAL, px + 1, py + ph / 2, 0);
        prop_on(P_GOAL, px + pw - 2, py + ph / 2, 0);
    }
    /* hopscotch, bike racks, a moped */
    for (int k = 0; k < 6; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 6 + rng_int(R, MAX(1, a->h - 8));
        Tile *t = tile_at(tx, ty);
        if (t && t->g == G_SCHOOLYARD && !(t->f & (TF_SOLID | TF_INTER))) { set_deco(tx, ty, D_HOPSCOTCH); break; }
    }
    scatter(P_BIKES, a->x + 1, a->y + 5, a->w - 2, 2, 2, 1, BLOCKERS);
    scatter(P_KICKBIKE, a->x + 1, a->y + 6, a->w - 2, a->h - 8, 1, 1, BLOCKERS);
    trees(a->x, a->y + 5, a->w, a->h - 5, 3);
    path_furniture(a->x, a->y, a->w, a->h, 6);
    for (int k = 0; k < 3; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 2), ty = a->y + 6 + rng_int(R, MAX(1, a->h - 8));
        if (free_rect(tx, ty, 1, 1, BLOCKERS)) add_loot_prop(P_BIN, tx, ty, 1);
    }
}

static void gen_church(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_GRASS);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2 + 2;
    connect_ports(a, G_GRAVEL, 2);
    /* the church on the north side, its tower, and a red bell tower */
    int cw = MIN(9, a->w - 6), cx = a->x + (a->w - cw) / 2;
    if (free_rect(cx, a->y, cw, 6, BLOCKERS)) {
        int bi = add_building(cx, a->y, cw, 6, 3, BS_CHURCH);
        if (bi >= 0) { G->b[bi].wall = 0xece6da; G->b[bi].roof = rng_chance(R, 0.5f) ? 0x6fa58c : 0x3a3a40; G->b[bi].trim = 0xa8a090; G->b[bi].lit = 1; apron(bi, G_GRAVEL); add_window(bi, 1, a->zone); }
        if (free_rect(cx + cw, a->y + 1, 2, 5, BLOCKERS)) {
            int ti = add_building(cx + cw, a->y + 1, 2, 5, 3, BS_TOWER);
            if (ti >= 0) { G->b[ti].wall = 0xece6da; G->b[ti].roof = 0x6fa58c; G->b[ti].trim = 0xa8a090; }
        }
    }
    for (int k = 0; k < 20; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 3), ty = a->y + 7 + rng_int(R, MAX(1, a->h - 9));
        if (free_rect(tx - 1, ty - 1, 4, 4, BLOCKERS)) {
            int bi = add_building(tx, ty, 2, 3, 2, BS_BELLTOWER);
            if (bi >= 0) { G->b[bi].wall = 0x8c2b1e; G->b[bi].roof = 0x3a3a40; G->b[bi].trim = 0xf2f0ea; }
            break;
        }
    }
    /* the graves in rows, zombies come up through them */
    for (int j = a->y + 7; j < a->y + a->h - 1; j += 2)
        for (int i = a->x + 1; i < a->x + a->w - 1; i += 2) {
            if (!free_rect(i, j, 1, 1, BLOCKERS) || rng_chance(R, 0.25f)) continue;
            prop_on(P_GRAVE, i, j, 1);
            set_deco(i, j, D_GRAVE_PLOT);
            if (free_rect(i, j + 1, 1, 1, TF_SOLID) && rng_chance(R, 0.25f)) add_spawn(SP_GRAVE, a->zone, i * TS + 8, (j + 1) * TS + 6, -1);
        }
    trees(a->x, a->y + 6, a->w, a->h - 6, 4);
    path_furniture(a->x, a->y, a->w, a->h, 4);
}

static void gen_harbor(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_ASPHALT);
    /* water along the south edge, a quay above it */
    int wy = a->y + a->h - 3;
    for (int j = wy; j < a->y + a->h; j++) for (int i = a->x; i < a->x + a->w; i++) { set_ground(i, j, G_WATER); set_flag(i, j, TF_SOLID | TF_WATER); }
    for (int i = a->x; i < a->x + a->w; i++) { set_ground(i, wy - 1, G_PLAZA); set_flag(i, wy - 1, TF_RESERVED); }
    a->hubx = a->x + a->w / 2; a->huby = wy - 2;
    connect_ports(a, G_ASPHALT, 2);
    int out[6];
    int n = building_row(a, a->y, 5, 2, BS_WAREHOUSE, 6, 9, 1, out, 3);
    for (int i = 0; i < n; i++) {
        Building *b = &G->b[out[i]];
        b->wall = rng_chance(R, 0.5f) ? 0x6a7a88 : 0x8a6a4a; b->roof = 0x5a5e66; b->trim = 0xe0a020;
        snprintf(b->sign, sizeof b->sign, "%s", rng_chance(R, 0.5f) ? "LAGER" : "HAMN");
        apron(out[i], G_ASPHALT);
        add_window(out[i], b->w / 2, a->zone);
    }
    for (int k = 0; k < 8; k++) {       /* containers */
        int tx = a->x + 1 + rng_int(R, a->w - 4), ty = a->y + 6 + rng_int(R, MAX(1, a->h - 11));
        if (free_rect(tx - 1, ty - 1, 5, 3, BLOCKERS)) {
            int pi = add_loot_prop(P_CONTAINER, tx, ty, 0);
            if (pi >= 0) { G->props[pi].x += TS; for (int i = 0; i < 3; i++) set_flag(tx + i, ty, TF_SOLID | TF_OPAQUE); }
        }
    }
    scatter(P_PALLETS, a->x, a->y + 6, a->w, a->h - 9, 3, 1, BLOCKERS);
    scatter(P_FORKLIFT, a->x, a->y + 6, a->w, a->h - 9, 1, 1, BLOCKERS);
    for (int i = a->x + 2; i < a->x + a->w - 2; i += 6) prop_on(P_BOAT, i, a->y + a->h - 1, 0);
    path_furniture(a->x, a->y, a->w, a->h, 6);
    add_spawn(SP_MANHOLE, a->zone, a->hubx * TS + 8, (a->y + 7) * TS + 8, -1);
}

static void gen_station(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_ASPHALT);
    /* the tracks across the north, the platform under them */
    int ty0 = a->y;
    for (int j = ty0; j < ty0 + 3; j++) for (int i = a->x; i < a->x + a->w; i++) { set_ground(i, j, G_RAIL); set_flag(i, j, TF_SOLID); }
    for (int i = a->x; i < a->x + a->w; i++) { set_ground(i, ty0 + 3, G_FLOOR); set_ground(i, ty0 + 4, G_FLOOR); set_flag(i, ty0 + 3, TF_RESERVED); }
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h / 2 + 1;
    connect_ports(a, G_SIDEWALK, 2);
    for (int i = a->x + 1; i < a->x + a->w - 1; i += 4) if (free_rect(i, ty0 + 4, 1, 1, BLOCKERS)) prop_on(P_LAMP, i, ty0 + 4, 1);
    int sx = a->x + 2 + rng_int(R, MAX(1, a->w - 10));
    if (free_rect(sx, ty0 + 5, 6, 4, BLOCKERS)) {
        int bi = add_building(sx, ty0 + 5, 6, 4, 2, BS_STATION);
        if (bi >= 0) { G->b[bi].wall = 0x9a9a92; G->b[bi].roof = 0x3a4048; snprintf(G->b[bi].sign, sizeof G->b[bi].sign, "PENDELTÅG"); G->b[bi].lit = 1; apron(bi, G_SIDEWALK); add_window(bi, 1, a->zone); }
    }
    /* the bus terminal: shelters along a sidewalk, buses' bays */
    int by = a->y + a->h - 5;
    for (int i = a->x; i < a->x + a->w; i++) { set_ground(i, by, G_SIDEWALK); set_flag(i, by, TF_RESERVED); }
    for (int i = a->x + 2; i < a->x + a->w - 3; i += 7) if (free_rect(i, by - 1, 2, 1, BLOCKERS)) { prop_on(P_BUSSTOP, i, by - 1, 0); set_flag(i, by - 1, TF_SOLID); set_flag(i + 1, by - 1, TF_SOLID); }
    for (int i = a->x; i < a->x + a->w; i++) if (i % 3) set_deco(i, by + 2, D_LINE_H);
    scatter(P_TICKET, a->x, ty0 + 5, a->w, 3, 1, 1, BLOCKERS);
    for (int k = 0; k < 4; k++) {
        int cx = a->x + 1 + rng_int(R, a->w - 3), cy = by + 3 + rng_int(R, 2);
        if (free_rect(cx, cy, 2, 1, BLOCKERS)) {
            int pi = add_loot_prop(P_CAR, cx, cy, 0);
            if (pi >= 0) { G->props[pi].x += TS / 2; set_flag(cx, cy, TF_SOLID); set_flag(cx + 1, cy, TF_SOLID); }
        }
    }
    path_furniture(a->x, a->y, a->w, a->h, 6);
    add_spawn(SP_MANHOLE, a->zone, a->hubx * TS + 8, (by + 2) * TS + 8, -1);
}

static void gen_mall(Area *a) {
    fill_ground(a->x, a->y, a->w, a->h, G_PARKING);
    a->hubx = a->x + a->w / 2; a->huby = a->y + a->h - 4;
    connect_ports(a, G_SIDEWALK, 2);
    int out[3];
    int n = building_row(a, a->y, 6, 3, BS_MALL, 10, a->w - 2, 2, out, 1);
    static const char *names[] = { "STORMARKNAD", "BYGGVARUHUS", "MÖBELVARUHUS", "LEKSAKER", "ELEKTRONIK" };
    for (int i = 0; i < n; i++) {
        Building *b = &G->b[out[i]];
        b->wall = rng_chance(R, 0.5f) ? 0x3e6fa3 : 0x9c9a92; b->wall2 = 0xd4a62a; b->roof = 0x6a6e74; b->trim = 0xf0f0f0; b->lit = 1;
        snprintf(b->sign, sizeof b->sign, "%s", names[rng_int(R, ARRAY_LEN(names))]);
        apron(out[i], G_SIDEWALK);
        add_window(out[i], 2, a->zone); add_window(out[i], b->w - 3, a->zone);
    }
    /* rows of parking bays with cars */
    for (int j = a->y + 8; j < a->y + a->h - 2; j += 3)
        for (int i = a->x + 1; i < a->x + a->w - 1; i++) {
            if (has_flag(i, j, TF_RESERVED | TF_SOLID)) continue;
            set_deco(i, j, D_PARKING_V);
            if (rng_chance(R, 0.3f) && free_rect(i, j - 1, 1, 2, BLOCKERS)) {
                int pi = add_loot_prop(P_CAR_V, i, j, 0);
                if (pi >= 0) { set_flag(i, j, TF_SOLID); set_flag(i, j - 1, TF_SOLID); }
            }
        }
    for (int k = 0; k < 10; k++) {
        int tx = a->x + 1 + rng_int(R, a->w - 3), ty = a->y + 7 + rng_int(R, 2);
        if (free_rect(tx, ty, 3, 1, BLOCKERS)) { for (int i = 0; i < 3; i++) add_loot_prop(P_RECYCLE, tx + i, ty, 1); break; }
    }
    scatter(P_CART, a->x, a->y + 7, a->w, a->h - 8, 3, 1, BLOCKERS);
    scatter(P_LAMP, a->x, a->y + 7, a->w, a->h - 8, 4, 1, BLOCKERS);
    add_spawn(SP_MANHOLE, a->zone, (a->x + 3) * TS + 8, (a->y + a->h - 3) * TS + 8, -1);
}

/* ---------------------------------------------------------------- placement of the CoD pieces */
/* a free walkable tile with free ground on its south side, in the zone (for a machine against nothing) */
static uint16_t bfs_seen[MAPH_MAX][MAPW_MAX];
static int reach_n;                     /* tiles reachable from the start (barriers open) */
static int bfs(int sx, int sy, int open_barriers);
static int seen_rect(int x, int y, int w, int h) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i < 0 || j < 0 || i >= G->w || j >= G->h || !bfs_seen[j][i]) return 0;
    return 1;
}
static int find_spot(int z, int w, int h, int need_wall, int *ox, int *oy) {
    Zone *zn = &G->zones[z];
    for (int tries = 0; tries < 400; tries++) {
        int tx = zn->x + 1 + rng_int(R, MAX(1, zn->w - 2 - w)), ty = zn->y + 1 + rng_int(R, MAX(1, zn->h - 2 - h));
        if (!free_rect(tx, ty, w, h, TF_SOLID | TF_INTER | TF_WATER | TF_RESERVED)) continue;   /* never on a path */
        if (!free_rect(tx, ty + h, w, 1, TF_SOLID | TF_INTER | TF_WATER)) continue;   /* room in front */
        if (!seen_rect(tx, ty, w, h + 1)) continue;                                      /* where you can get to */
        if (tries < 300 && need_wall && !has_flag(tx, ty - 1, TF_BUILDING) && !has_flag(tx, ty - 1, TF_SOLID)) continue;
        /* don't stand in a one-tile alley: keep it passable around */
        int side_open = !has_flag(tx - 1, ty, TF_SOLID) || !has_flag(tx + w, ty, TF_SOLID);
        if (!side_open && tries < 350) continue;
        *ox = tx; *oy = ty;
        return 1;
    }
    return 0;
}

/* a machine on a free spot of the zone that doesn't cut anything off: every other tile stays reachable */
static int place_machine(int type, int z, int a_arg, int w, int h) {
    Zone *sz = &G->zones[G->start_zone];
    for (int attempt = 0; attempt < 12; attempt++) {
        int tx, ty;
        if (!find_spot(z, w, h, 1, &tx, &ty)) return 0;
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) set_flag(tx + i, ty + j, TF_SOLID);
        int n = bfs(sz->cx, sz->cy, 1);
        if (n != reach_n - w * h) {                         /* it closed something off: try elsewhere */
            for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) clr_flag(tx + i, ty + j, TF_SOLID);
            bfs(sz->cx, sz->cy, 1);
            continue;
        }
        reach_n = n;
        int ii = add_inter(type, tx, ty, w, h, tx * TS + w * TS / 2.0f, (ty + h) * TS + 6);
        if (ii < 0) return 0;
        G->it[ii].a = a_arg;
        if (type == IT_BOX && G->nbox_spots < 8) G->box_spots[G->nbox_spots++] = ii;
        return 1;
    }
    return 0;
}
/* the preferred zone, else any other open-able zone (the nearest first) */
static int place_machine_somewhere(int type, int z, int a_arg, int w, int h) {
    if (place_machine(type, z, a_arg, w, h)) return 1;
    for (int d = 0; d < 20; d++)
        for (int k = 0; k < G->nzones; k++)
            if (k != z && G->zones[k].dist == d && place_machine(type, k, a_arg, w, h)) return 1;
    return 0;
}

static void place_wallbuys(void) {
    /* weapon, price: what Black Ops' walls sell, at its prices; ammo is half */
    static const int pool[][2] = { {W_PIST88, 500}, {W_KPIST, 1000}, {W_AK5, 1200}, {W_HAGEL, 1500}, {W_AK4, 1800}, {W_REVOLVER, 900} };
    for (int z = 0; z < G->nzones; z++) {
        int want = z == G->start_zone ? 2 : rng_chance(R, 0.6f) ? 1 : 0;
        for (int k = 0; k < want; k++) {
            int choice = z == G->start_zone ? (k == 0 ? 1 : 3) : 1 + rng_int(R, ARRAY_LEN(pool) - 1);
            /* on a building's facade: a tile whose south neighbour is free */
            for (int tries = 0; tries < 300; tries++) {
                Zone *zn = &G->zones[z];
                int tx = zn->x + 1 + rng_int(R, zn->w - 2), ty = zn->y + 1 + rng_int(R, zn->h - 2);
                Tile *t = tile_at(tx, ty), *b = tile_at(tx, ty + 1);
                if (!t || !b || !(t->f & TF_BUILDING) || (t->f & TF_INTER) || (b->f & (TF_SOLID | TF_INTER)) || !bfs_seen[ty + 1][tx]) continue;
                int ii = add_inter(IT_WALLBUY, tx, ty, 1, 1, tx * TS + 8, (ty + 1) * TS + 6);
                if (ii < 0) return;
                G->it[ii].a = pool[choice][0]; G->it[ii].cost = pool[choice][1];
                break;
            }
        }
    }
    /* the axe (a better knife) and grenades somewhere away from the start */
    int extras[2][2] = { {-1, 3000}, {-2, 250} };
    for (int k = 0; k < 2; k++) {
        for (int tries = 0; tries < 400; tries++) {
            int z = rng_int(R, G->nzones);
            if (G->nzones > 2 && z == G->start_zone && k == 0) continue;
            Zone *zn = &G->zones[z];
            int tx = zn->x + 1 + rng_int(R, zn->w - 2), ty = zn->y + 1 + rng_int(R, zn->h - 2);
            Tile *t = tile_at(tx, ty), *b = tile_at(tx, ty + 1);
            if (!t || !b || !(t->f & TF_BUILDING) || (t->f & TF_INTER) || (b->f & (TF_SOLID | TF_INTER)) || !bfs_seen[ty + 1][tx]) continue;
            int ii = add_inter(IT_WALLBUY, tx, ty, 1, 1, tx * TS + 8, (ty + 1) * TS + 6);
            if (ii >= 0) { G->it[ii].a = extras[k][0]; G->it[ii].cost = extras[k][1]; }
            break;
        }
    }
}

/* ---------------------------------------------------------------- connectivity */
static int bfs(int sx, int sy, int open_barriers) {
    static int qx[MAPW_MAX * MAPH_MAX], qy[MAPW_MAX * MAPH_MAX];
    memset(bfs_seen, 0, sizeof bfs_seen);
    int h = 0, t = 0, n = 0;
    qx[t] = sx; qy[t++] = sy; bfs_seen[sy][sx] = 1;
    while (h < t) {
        int x = qx[h], y = qy[h++]; n++;
        static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            Tile *tt = tile_at(nx, ny);
            if (!tt || bfs_seen[ny][nx]) continue;
            int passable = !(tt->f & TF_SOLID);
            if (!passable && open_barriers && tt->inter && G->it[tt->inter - 1].type == IT_BARRIER) passable = 1;
            if (!passable) continue;
            bfs_seen[ny][nx] = 1; qx[t] = nx; qy[t++] = ny;
        }
    }
    return n;
}

/* walkable pockets nobody can reach become thicket: no loot or spawns stranded in them */
static void seal_pockets(int sx, int sy) {
    bfs(sx, sy, 1);
    for (int y = 0; y < G->h; y++)
        for (int x = 0; x < G->w; x++) {
            Tile *t = &G->t[y][x];
            if (t->f & TF_SOLID || bfs_seen[y][x]) continue;
            t->f |= TF_SOLID;
            if (t->g != G_WATER && !(t->f & TF_INTER)) prop_on(P_BUSH, x, y, 0);
        }
    /* spawn points and items in sealed tiles are dropped */
    int n = 0;
    for (int i = 0; i < G->nspawns; i++) {
        Spawn *s = &G->spawns[i];
        int tx = (int)s->x / TS, ty = (int)s->y / TS;
        if (s->type == SP_WINDOW) ty += 1;
        if (tx >= 0 && ty >= 0 && tx < G->w && ty < G->h && bfs_seen[ty][tx]) G->spawns[n++] = *s;
    }
    G->nspawns = n;
}

/* ---------------------------------------------------------------- the whole map */
static const char *ZONE_NAMES[Z_COUNT] = {
    "Gården", "Torget", "Villorna", "Gamla stan", "Kolonilotterna", "Parken", "Skolan", "Kyrkan", "Hamnen",
    "Stationen", "Köpcentret"
};
static const char *TOWN_A[] = { "Björk", "Gran", "Tall", "Ek", "Sjö", "Berg", "Ström", "Lind", "Hög", "Ny", "Väster", "Öster", "Söder", "Norr", "Ängs", "Skogs", "Lönn", "Hassel", "Råg", "Kvarn" };
static const char *TOWN_B[] = { "hagen", "dal", "by", "berga", "sta", "holm", "vik", "torp", "backen", "ängen", "gården", "skogen", "lunda", "näs", "bro" };

/* the name map_generate gives the town of this seed (its first two draws), for the title */
void town_name(uint64_t seed, char *out, int n) {
    Rng r; rng_seed(&r, seed, 7);
    int a = rng_int(&r, ARRAY_LEN(TOWN_A)), b = rng_int(&r, ARRAY_LEN(TOWN_B));
    snprintf(out, (size_t)n, "%s%s", TOWN_A[a], TOWN_B[b]);
}

int zone_at(float x, float y) {
    Tile *t = tile_at((int)(x / TS), (int)(y / TS));
    return t ? t->zone : 0;
}

void map_generate(uint64_t seed, int season) {
    Rng r; rng_seed(&r, seed, 7); R = &r;
    memset(G->t, 0, sizeof G->t);
    G->nzones = G->nb = G->nprops = G->nit = G->nspawns = G->nbox_spots = 0;
    nports = 0;
    snprintf(G->town, sizeof G->town, "%s%s", TOWN_A[rng_int(R, ARRAY_LEN(TOWN_A))], TOWN_B[rng_int(R, ARRAY_LEN(TOWN_B))]);

    /* the grid of cells */
    int zc = 4, zr = 3;
    G->zcols = zc; G->zrows = zr;
    colx[0] = 0; rowy[0] = 0;
    for (int i = 0; i < zc; i++) colx[i + 1] = colx[i] + rng_range(R, 24, 30);
    for (int j = 0; j < zr; j++) rowy[j + 1] = rowy[j] + rng_range(R, 19, 23);
    G->w = colx[zc]; G->h = rowy[zr];
    /* a couple of cells merge into bigger districts */
    for (int j = 0; j < zr; j++) for (int i = 0; i < zc; i++) cell_zone[j][i] = -1;
    int merges = rng_range(R, 0, 2);
    for (int j = 0; j < zr; j++)
        for (int i = 0; i < zc; i++) {
            if (cell_zone[j][i] >= 0) continue;
            int z = G->nzones++;
            Zone *zn = &G->zones[z]; memset(zn, 0, sizeof *zn);
            int cw = 1, ch = 1;
            if (merges > 0 && rng_chance(R, 0.3f)) {
                if (rng_chance(R, 0.5f) && i + 1 < zc && cell_zone[j][i + 1] < 0) cw = 2;
                else if (j + 1 < zr && cell_zone[j + 1][i] < 0) ch = 2;
                if (cw * ch > 1) merges--;
            }
            for (int b = 0; b < ch; b++) for (int a = 0; a < cw; a++) cell_zone[j + b][i + a] = z;
            zn->x = colx[i]; zn->y = rowy[j]; zn->w = colx[i + cw] - colx[i]; zn->h = rowy[j + ch] - rowy[j];
        }
    /* adjacency through cell edges, and a spanning tree plus extra loops */
    int adj[MAX_ZONES][MAX_ZONES]; memset(adj, 0, sizeof adj);
    for (int j = 0; j < zr; j++) for (int i = 0; i < zc; i++) {
        int a = cell_zone[j][i];
        if (i + 1 < zc && cell_zone[j][i + 1] != a) { int b = cell_zone[j][i + 1]; adj[a][b] = adj[b][a] = 1; }
        if (j + 1 < zr && cell_zone[j + 1][i] != a) { int b = cell_zone[j + 1][i]; adj[a][b] = adj[b][a] = 1; }
    }
    /* types: the start is a home-like district; the rest from a shuffled bag */
    int bag[24], nb = 0;
    static const int weights[Z_COUNT] = { 3, 2, 2, 1, 1, 2, 1, 1, 1, 1, 1 };
    for (int t = 0; t < Z_COUNT; t++) for (int k = 0; k < weights[t]; k++) bag[nb++] = t;
    for (int i = nb - 1; i > 0; i--) { int k = rng_int(R, i + 1); int tmp = bag[i]; bag[i] = bag[k]; bag[k] = tmp; }
    int start_candidates[MAX_ZONES], nsc = 0;
    for (int z = 0; z < G->nzones; z++) {   /* not on a corner: more ways out */
        Zone *zn = &G->zones[z];
        int corner = (zn->x == 0 || zn->x + zn->w == G->w) && (zn->y == 0 || zn->y + zn->h == G->h);
        if (!corner) start_candidates[nsc++] = z;
    }
    G->start_zone = nsc ? start_candidates[rng_int(R, nsc)] : 0;
    static const int start_types[] = { Z_GARDEN, Z_TORG, Z_VILLA, Z_GARDEN };
    G->zones[G->start_zone].type = start_types[rng_int(R, ARRAY_LEN(start_types))];
    int used[Z_COUNT] = { 0 }; used[G->zones[G->start_zone].type]++;
    int bi = 0;
    for (int z = 0; z < G->nzones; z++) {
        if (z == G->start_zone) continue;
        Zone *zn = &G->zones[z];
        int edge_s = zn->y + zn->h == G->h, edge_n = zn->y == 0;
        int t = -1;
        for (int tries = 0; tries < nb; tries++) {
            int c = bag[(bi + tries) % nb];
            if (used[c] >= (c == Z_GARDEN ? 2 : 1)) continue;
            if (c == Z_HARBOR && !edge_s) continue;      /* the water is at the map's southern edge */
            if (c == Z_STATION && !edge_n) continue;     /* the tracks run along the north */
            t = c; bi = (bi + tries + 1) % nb; break;
        }
        if (t < 0) t = rng_chance(R, 0.5f) ? Z_PARK : Z_GARDEN;
        zn->type = t; used[t]++;
    }
    /* which borders get a gap: a spanning tree from the start, then extra loops for training zombies around */
    int in_tree[MAX_ZONES] = { 0 }, nin = 1; in_tree[G->start_zone] = 1;
    int edge_used[MAX_ZONES][MAX_ZONES]; memset(edge_used, 0, sizeof edge_used);
    while (nin < G->nzones) {
        int ca[64], cb[64], nc = 0;
        for (int a = 0; a < G->nzones; a++) if (in_tree[a]) for (int b = 0; b < G->nzones; b++) if (!in_tree[b] && adj[a][b] && nc < 64) { ca[nc] = a; cb[nc++] = b; }
        if (!nc) break;
        int k = rng_int(R, nc);
        edge_used[ca[k]][cb[k]] = edge_used[cb[k]][ca[k]] = 1; in_tree[cb[k]] = 1; nin++;
    }
    for (int a = 0; a < G->nzones; a++) for (int b = a + 1; b < G->nzones; b++)
        if (adj[a][b] && !edge_used[a][b] && rng_chance(R, 0.4f)) edge_used[a][b] = edge_used[b][a] = 1;
    /* zone distances from the start along open borders */
    for (int z = 0; z < G->nzones; z++) G->zones[z].dist = 99;
    G->zones[G->start_zone].dist = 0;
    for (int it = 0; it < G->nzones; it++)
        for (int a = 0; a < G->nzones; a++) for (int b = 0; b < G->nzones; b++)
            if (edge_used[a][b] && G->zones[a].dist + 1 < G->zones[b].dist) G->zones[b].dist = G->zones[a].dist + 1;
    /* tiles belong to zones; rings first */
    for (int z = 0; z < G->nzones; z++) {
        Zone *zn = &G->zones[z];
        snprintf(zn->name, sizeof zn->name, "%s", ZONE_NAMES[zn->type]);
        for (int j = zn->y; j < zn->y + zn->h; j++) for (int i = zn->x; i < zn->x + zn->w; i++) G->t[j][i].zone = (uint8_t)z;
        zone_ring(z);
    }
    /* the ports: one gap per used border, in the middle half of the shared edge */
    for (int a = 0; a < G->nzones; a++) for (int b = a + 1; b < G->nzones; b++) {
        if (!edge_used[a][b]) continue;
        Zone *A = &G->zones[a], *B = &G->zones[b];
        Port p; memset(&p, 0, sizeof p); p.a = a; p.b = b;
        if (A->x + A->w == B->x || B->x + B->w == A->x) {   /* side by side: a vertical border */
            int bx = A->x + A->w == B->x ? B->x - 1 : A->x - 1;
            int y0 = MAX(A->y, B->y), y1 = MIN(A->y + A->h, B->y + B->h);
            int span = y1 - y0;
            p.vertical = 1; p.x = bx; p.w = 2; p.h = 2; p.y = y0 + span / 4 + rng_int(R, MAX(1, span / 2 - 2));
        } else {                                                /* stacked: a horizontal border */
            int by = A->y + A->h == B->y ? B->y - 1 : A->y - 1;
            int x0 = MAX(A->x, B->x), x1 = MIN(A->x + A->w, B->x + B->w);
            int span = x1 - x0;
            p.vertical = 0; p.y = by; p.w = 2; p.h = 2; p.x = x0 + span / 4 + rng_int(R, MAX(1, span / 2 - 2));
        }
        ports[nports++] = p;
        /* open the gap through both rings */
        for (int j = 0; j < p.h; j++) for (int i = 0; i < p.w; i++) {
            Tile *t = tile_at(p.x + i, p.y + j);
            if (!t) continue;
            t->f &= (uint8_t)~(TF_SOLID | TF_OPAQUE);
            t->f |= TF_RESERVED;
            if (t->g == G_FOREST || t->g == G_ROCK) t->g = G_GRAVEL;
        }
        /* the barrier across it */
        int ii = add_inter(IT_BARRIER, p.x, p.y, p.w, p.h, p.x * TS + p.w * TS / 2.0f, p.y * TS + p.h * TS / 2.0f);
        if (ii >= 0) {
            int d = MAX(G->zones[a].dist, G->zones[b].dist);
            G->it[ii].a = a; G->it[ii].b = b;
            G->it[ii].cost = 750 + 250 * MAX(0, d - 1) + 250 * rng_int(R, 2);
            if (G->it[ii].cost > 2000) G->it[ii].cost = 2000;
            G->it[ii].c = rng_int(R, 3);         /* looks: police barrier, construction fence, car wreck */
            for (int j = 0; j < p.h; j++) for (int i = 0; i < p.w; i++) set_flag(p.x + i, p.y + j, TF_SOLID);
        }
    }
    /* districts */
    for (int z = 0; z < G->nzones; z++) {
        Area a; memset(&a, 0, sizeof a); zone_rect_inner(z, &a.x, &a.y, &a.w, &a.h); a.zone = z;
        switch (G->zones[z].type) {
        case Z_GARDEN: gen_garden(&a); break;
        case Z_TORG: gen_torg(&a); break;
        case Z_VILLA: gen_villa(&a); break;
        case Z_OLDTOWN: gen_oldtown(&a); break;
        case Z_ALLOT: gen_allot(&a); break;
        case Z_PARK: gen_park(&a, season); break;
        case Z_SCHOOL: gen_school(&a); break;
        case Z_CHURCH: gen_church(&a); break;
        case Z_HARBOR: gen_harbor(&a); break;
        case Z_STATION: gen_station(&a); break;
        case Z_MALL: gen_mall(&a); break;
        }
        G->zones[z].cx = a.hubx; G->zones[z].cy = a.huby;
        ring_props(z);
    }
    /* zone centres must be walkable (a fountain may stand on the hub) */
    for (int z = 0; z < G->nzones; z++) {
        Zone *zn = &G->zones[z];
        if (!solid_at(zn->cx, zn->cy)) continue;
        for (int rr = 1; rr < 10; rr++) {
            int found = 0;
            for (int j = -rr; j <= rr && !found; j++) for (int i = -rr; i <= rr && !found; i++)
                if (!solid_at(zn->cx + i, zn->cy + j) && tile_at(zn->cx + i, zn->cy + j)->zone == z) { zn->cx += i; zn->cy += j; found = 1; }
            if (found) break;
        }
    }
    /* pockets nobody can reach are filled in, then everything else is placed where it can be reached and
     * without cutting anything off */
    seal_pockets(G->zones[G->start_zone].cx, G->zones[G->start_zone].cy);
    reach_n = bfs(G->zones[G->start_zone].cx, G->zones[G->start_zone].cy, 1);
    /* the CoD pieces */
    int order[MAX_ZONES]; for (int z = 0; z < G->nzones; z++) order[z] = z;
    for (int i = G->nzones - 1; i > 0; i--) { int k = rng_int(R, i + 1); int t = order[i]; order[i] = order[k]; order[k] = t; }
    /* the power switch far from the start, the Pack-a-Punch elsewhere far too */
    int far = G->start_zone, far2 = -1;
    for (int k = 0; k < G->nzones; k++) { int z = order[k]; if (G->zones[z].dist < 90 && G->zones[z].dist > G->zones[far].dist) far = z; }
    for (int k = 0; k < G->nzones; k++) { int z = order[k]; if (z != far && G->zones[z].dist >= 2 && G->zones[z].dist < 90) { far2 = z; break; } }
    if (far2 < 0) far2 = far;
    place_machine_somewhere(IT_POWER, far, 0, 1, 1);
    place_machine_somewhere(IT_PAP, far2, 0, 2, 1);
    /* perks: Kanelbulle near the start (it works without power), the others spread out */
    int perk_zone_used[MAX_ZONES] = { 0 };
    place_machine_somewhere(IT_PERK, G->start_zone, PK_KANELBULLE, 1, 1); perk_zone_used[G->start_zone]++;
    int k = 0;
    for (int pk = 0; pk < PK_COUNT; pk++) {
        if (pk == PK_KANELBULLE) continue;
        int z = -1;
        for (int tries = 0; tries < G->nzones * 2; tries++) {
            int c = order[(k + tries) % G->nzones];
            if (perk_zone_used[c] <= (tries >= G->nzones ? 1 : 0)) { z = c; k = (k + tries + 1) % G->nzones; break; }
        }
        if (z < 0) z = order[rng_int(R, G->nzones)];
        perk_zone_used[z]++;
        place_machine_somewhere(IT_PERK, z, pk, 1, 1);
    }
    /* the Mystery Box's spots: one in the start zone, the others in distinct zones */
    place_machine_somewhere(IT_BOX, G->start_zone, 0, 2, 1);
    for (int kk = 0, n = 0; kk < G->nzones && n < 4; kk++) {
        int z = order[kk];
        if (z == G->start_zone) continue;
        if (place_machine(IT_BOX, z, 0, 2, 1)) n++;
    }
    place_wallbuys();
    /* every zone gets at least three ways for zombies to come in */
    for (int z = 0; z < G->nzones; z++) {
        int n = 0;
        for (int i = 0; i < G->nspawns; i++) n += G->spawns[i].zone == z;
        Zone *zn = &G->zones[z];
        for (int tries = 0; tries < 600 && n < 3; tries++) {
            int tx = zn->x + 1 + rng_int(R, zn->w - 2), ty = zn->y + 1 + rng_int(R, zn->h - 2);
            if (!free_rect(tx, ty, 1, 1, TF_SOLID | TF_INTER | TF_WATER) || !bfs_seen[ty][tx]) continue;
            if (abs(tx - zn->cx) + abs(ty - zn->cy) < 4) continue;
            Tile *t = tile_at(tx, ty);
            int type = (t->f & TF_ROAD) || t->g == G_ASPHALT || t->g == G_PARKING ? SP_MANHOLE : SP_GROUND;
            add_spawn(type, z, tx * TS + 8, ty * TS + 8, -1); n++;
        }
    }
    /* manholes are painted where their spawns are */
    for (int i = 0; i < G->nspawns; i++)
        if (G->spawns[i].type == SP_MANHOLE) set_deco((int)G->spawns[i].x / TS, (int)G->spawns[i].y / TS, D_MANHOLE);
    for (int z = 0; z < G->nzones; z++) G->zones[z].open = z == G->start_zone;
    /* static lights: lamps light their surroundings once the power is on, some always */
    R = 0;
}

/* opening a barrier joins its zones */
void zone_open(int z) { if (z >= 0 && z < G->nzones) G->zones[z].open = 1; }
void refresh_walls(void) {}
