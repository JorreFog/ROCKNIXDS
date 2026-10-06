// test_map: the town generator and the round formulas, checked over many seeds.
//   - every zone can be reached from the start once the barriers are open, and the start and zone centres are walkable
//   - every zone has spawn points that can be reached, the box has spots, the power switch and the Pack-a-Punch
//     exist and can be reached, perk machines and wall buys stand on reachable tiles
//   - zombie health, round sizes and spawn rates follow Black Ops' numbers (docs/research.md)
//   - repainting part of the town changes nothing that was there; the power wave ends as a whole repaint does
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t seen[MAPH_MAX][MAPW_MAX];
static void flood(int sx, int sy) {
    static int q[MAPW_MAX * MAPH_MAX];
    memset(seen, 0, sizeof seen);
    int h = 0, t = 0; q[t++] = sy * MAPW_MAX + sx; seen[sy][sx] = 1;
    while (h < t) {
        int c = q[h++], x = c % MAPW_MAX, y = c / MAPW_MAX;
        static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            Tile *tt = tile_at(nx, ny);
            if (!tt || seen[ny][nx]) continue;
            int pass = !(tt->f & TF_SOLID) || (tt->inter && G->it[tt->inter - 1].type == IT_BARRIER);
            if (!pass) continue;
            seen[ny][nx] = 1; q[t++] = ny * MAPW_MAX + nx;
        }
    }
}
/* an interactable is usable if the tile its user stands on is reachable */
static int usable(const Inter *it) {
    int tx = (int)(it->x / TS), ty = (int)((it->y - 1) / TS);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int x = tx + dx, y = ty + dy;
        if (x >= 0 && y >= 0 && x < G->w && y < G->h && seen[y][x] && !(G->t[y][x].f & TF_SOLID)) return 1;
    }
    return 0;
}

/* the painted town: a repaint anywhere gives the same pixels (nothing drawn or blended twice), and the power
   coming on building by building ends where a whole repaint with the power on does */
static void paint_checks(int seed) {
    world_paint();
    size_t n = (size_t)G->ww * G->wh;
    uint32_t *ref = malloc(n * 4); memcpy(ref, G->world, n * 4);
    uint32_t r = (uint32_t)seed * 2654435761u;
    for (int k = 0; k < 40; k++) {
        r = r * 1664525u + 1013904223u; int x = (int)(r >> 8) % G->w;
        r = r * 1664525u + 1013904223u; int y = (int)(r >> 8) % G->h;
        r = r * 1664525u + 1013904223u; int w = 1 + (int)(r >> 8) % 9, h = 1 + (int)(r >> 20) % 7;
        world_repaint_rect(x - 2, y - 2, w, h);
        if (memcmp(ref, G->world, n * 4)) {
            CHECK(0, "seed %d: repainting %d,%d %dx%d changed what was there", seed, x - 2, y - 2, w, h);
            if (getenv("DK_TEST_VERBOSE")) {
                int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -1, by1 = -1, cnt = 0;
                for (size_t i = 0; i < n; i++) if (ref[i] != G->world[i]) { int px = (int)(i % G->ww), py = (int)(i / G->ww); cnt++; bx0 = MIN(bx0, px); by0 = MIN(by0, py); bx1 = MAX(bx1, px); by1 = MAX(by1, py); }
                printf("   %d px differ in %d,%d-%d,%d (tiles %d,%d-%d,%d)\n", cnt, bx0, by0, bx1, by1, bx0 / TS, by0 / TS, bx1 / TS, by1 / TS);
                for (int ty = by0 / TS; ty <= by1 / TS; ty++) for (int tx = bx0 / TS; tx <= bx1 / TS; tx++) { Tile *t = tile_at(tx, ty); printf("   tile %d,%d g %d f %d deco %d bld %d\n", tx, ty, t->g, t->f, t->deco, t->bld); }
                for (int i = 0; i < G->nprops; i++) { Prop *pp = &G->props[i]; if (pp->x >= bx0 - 40 && pp->x <= bx1 + 40 && pp->y >= by0 - 40 && pp->y <= by1 + 40) printf("   prop %d kind %d at %d,%d\n", i, pp->kind, pp->x, pp->y); }
            }
            memcpy(G->world, ref, n * 4);
        }
    }
    Inter *sw = 0; for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_POWER) sw = &G->it[i];
    if (sw) {
        G->power_on = 1; world_power_wave(sw->x, sw->y);
        for (int k = 0; k < 60 * 30 && G->wave_on; k++) world_update(1.0f / 60);
        CHECK(!G->wave_on, "seed %d: the power wave never ends", seed);
        memcpy(ref, G->world, n * 4);
        world_repaint_rect(0, 0, G->w, G->h);
        CHECK(!memcmp(ref, G->world, n * 4), "seed %d: the power wave left other pixels than a whole repaint", seed);
    }
    free(ref); free(G->world); G->world = 0;
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 300;
    G = calloc(1, sizeof *G);
    for (int seed = 1; seed <= n; seed++) {
        int season = seed % SEASON_COUNT;
        memset(G, 0, sizeof *G);
        G->season = season;
        map_generate((uint64_t)seed, season);
        Zone *sz = &G->zones[G->start_zone];
        CHECK(!solid_at(sz->cx, sz->cy), "seed %d: the start (%d,%d) is solid", seed, sz->cx, sz->cy);
        flood(sz->cx, sz->cy);
        for (int z = 0; z < G->nzones; z++) {
            Zone *zn = &G->zones[z];
            CHECK(seen[zn->cy][zn->cx], "seed %d: zone %d (%s) can't be reached", seed, z, zn->name);
            int sp = 0;
            for (int i = 0; i < G->nspawns; i++) if (G->spawns[i].zone == z) {
                Spawn *s = &G->spawns[i];
                int tx = (int)s->x / TS, ty = (int)s->y / TS + (s->type == SP_WINDOW ? 1 : 0);
                CHECK(seen[ty][tx], "seed %d: spawn %d (type %d) in zone %d unreachable", seed, i, s->type, z);
                sp++;
            }
            CHECK(sp >= 2, "seed %d: zone %d (%s) has %d spawns", seed, z, zn->name, sp);
        }
        int power = 0, pap = 0, perks = 0, walls = 0;
        for (int i = 0; i < G->nit; i++) {
            Inter *it = &G->it[i];
            switch (it->type) {
            case IT_POWER: power++; CHECK(usable(it), "seed %d: power switch unreachable", seed); break;
            case IT_PAP: pap++; CHECK(usable(it), "seed %d: Pack-a-Punch unreachable", seed); break;
            case IT_PERK: perks++; CHECK(usable(it), "seed %d: perk %d unreachable", seed, it->a); break;
            case IT_WALLBUY: walls++; CHECK(usable(it), "seed %d: wall buy %d unreachable", seed, it->a); break;
            case IT_BOX: CHECK(usable(it), "seed %d: a box spot is unreachable", seed); break;
            case IT_BARRIER: CHECK(it->cost >= 750 && it->cost <= 2000, "seed %d: barrier cost %d", seed, it->cost); break;
            }
        }
        CHECK(power == 1, "seed %d: %d power switches", seed, power);
        CHECK(pap == 1, "seed %d: %d Pack-a-Punch machines", seed, pap);
        CHECK(perks == PK_COUNT, "seed %d: %d perk machines", seed, perks);
        CHECK(G->nbox_spots >= 2, "seed %d: %d box spots", seed, G->nbox_spots);
        CHECK(walls >= 2, "seed %d: %d wall buys", seed, walls);
        CHECK(G->w <= MAPW_MAX && G->h <= MAPH_MAX, "seed %d: map %dx%d too big", seed, G->w, G->h);
        if (seed <= 60) paint_checks(seed);
    }
    /* Black Ops' rounds (docs/research.md, section A1) */
    static const int counts[] = { 6, 8, 13, 18, 24, 27, 28, 28, 29, 33 };
    for (int r = 1; r <= 10; r++) CHECK(zombies_for_round(r) == counts[r - 1], "round %d: %d zombies, want %d", r, zombies_for_round(r), counts[r - 1]);
    CHECK(zombies_for_round(20) == 60, "round 20: %d zombies, want 60", zombies_for_round(20));
    CHECK(zombies_for_round(30) == 105, "round 30: %d zombies, want 105", zombies_for_round(30));
    CHECK(zombie_hp_for_round(1) == 150 && zombie_hp_for_round(9) == 950, "health rounds 1/9: %.0f %.0f", zombie_hp_for_round(1), zombie_hp_for_round(9));
    CHECK(zombie_hp_for_round(10) == 1045, "health round 10: %.0f", zombie_hp_for_round(10));
    CHECK(fabsf(zombie_hp_for_round(20) - 2701) < 2, "health round 20: %.0f, want 2701", zombie_hp_for_round(20));
    printf("%d maps checked, %d failures\n", n, fails);
    return fails ? 1 : 0;
}
