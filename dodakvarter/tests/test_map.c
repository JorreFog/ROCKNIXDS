// test_map: the town generator and the round formulas, checked over many seeds.
//   - every zone can be reached from the start once the barriers are open, and the start and zone centres are walkable
//   - every zone has spawn points that can be reached, the box has spots, the power switch and the Pack-a-Punch
//     exist and can be reached, perk machines and wall buys stand on reachable tiles
//   - zombie health, round sizes and spawn rates follow Black Ops' numbers (docs/research.md)
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
