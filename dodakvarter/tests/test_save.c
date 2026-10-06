// test_save: a run saved and loaded again goes on exactly as if it had never stopped. Two copies of a run play the
// same inputs after the save, one straight on and one after a different run and a load in between: their states must
// match byte for byte (but for the painted town and where zombies were last drawn, which are pointers). A file cut
// short or from another build is refused.
#include "../src/game.h"
#include "../src/menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the same made-up player for both: aim at the nearest zombie, fire, and walk a square */
static Input input_at(int f) {
    Input in; memset(&in, 0, sizeof in);
    in.held = BIT(B_A);
    float bd = 1e9f;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        float d = dist2f(z->x, z->y, G->p.x, G->p.y);
        if (d < bd) { bd = d; in.mouse = 1; in.mx = z->x - G->camx; in.my = z->y - 8 - G->camy; }
    }
    static const int dir[4] = { B_LEFT, B_UP, B_RIGHT, B_DOWN };
    in.held |= BIT(dir[(f / 120) % 4]);
    if (f % 600 < 30) in.held |= BIT(B_B);
    if (f % 900 == 450) in.held |= BIT(B_R2);
    return in;
}
static void play(int from, int n) {
    Input prev = input_at(from - 1);
    for (int f = from; f < from + n; f++) { Input in = input_at(f); game_update(&in, &prev, 1.0f / 60); prev = in; }
}
static void scrub(Game *g) { g->world = 0; for (int i = 0; i < MAX_ZOMBIES; i++) g->z[i].rimg = 0; }

int main(void) {
    char dir[] = "/tmp/dk-test-save-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("DK_DATA", dir, 1);
    G = calloc(1, sizeof *G); G->view_w = 320; G->view_h = 240;
    Game *straight = malloc(sizeof *G), *loaded = malloc(sizeof *G);

    for (int season = 0; season < SEASON_COUNT; season++) {
        game_new(4242 + season, season);
        G->god = 1;                                        /* (so that the run lasts) */
        G->p.kr = 20000; round_start(3 + season * 4);
        play(0, 2340);
        if (season == 1) { G->power_on = 1; world_power_wave(G->p.x, G->p.y); prop_lights(); }   /* saved mid-wave */
        if (season == 2) { bag_add(C_SMALLARE, 1); throw_grenade(C_SMALLARE); }                /* and with a lure out */
        play(2340, 60);
        CHECK(run_save() == 0, "season %d: the run wasn't saved", season);
        char town[32]; int round = 0;
        CHECK(run_peek(town, sizeof town, &round) == 0 && round == G->round && !strcmp(town, G->town), "season %d: peek says %s %d", season, town, round);
        play(2400, 1500);                                  /* straight on */
        memcpy(straight, G, sizeof *G); scrub(straight);
        game_new(99, (season + 1) % SEASON_COUNT);         /* something else meanwhile */
        play(0, 300);
        CHECK(run_load() == 0, "season %d: the run didn't load", season);
        CHECK(G->world != 0, "season %d: the town wasn't painted", season);
        play(2400, 1500);                                  /* the same, after the load */
        memcpy(loaded, G, sizeof *G); scrub(loaded);
        if (memcmp(straight, loaded, sizeof *G)) {
            size_t first = 0; const uint8_t *a = (const uint8_t *)straight, *b = (const uint8_t *)loaded;
            while (first < sizeof *G && a[first] == b[first]) first++;
            CHECK(0, "season %d: the loaded run went another way (first difference at byte %zu of %zu)", season, first, sizeof *G);
        }
        printf("season %d: round %d, %d kills, %d kr: the same after a save and a load\n", season, G->round, G->p.kills, G->p.kr);
    }
    /* a damaged file is refused (and left alone) */
    char p[600]; snprintf(p, sizeof p, "%s/run.sav", dir);
    FILE *f = fopen(p, "r+b");
    CHECK(f != 0, "no run.sav");
    if (f) { fseek(f, 5000, SEEK_SET); int c = fgetc(f); fseek(f, 5000, SEEK_SET); fputc(c ^ 0x5a, f); fclose(f); }
    CHECK(!run_saved() || run_load() != 0, "a damaged save was loaded");
    char town[32]; int round;
    CHECK(run_peek(town, sizeof town, &round) != 0, "a damaged save was offered");
    run_discard();
    CHECK(access(p, F_OK) != 0, "run.sav is still there after the run ended");
    rmdir(dir);
    printf("save: %d failures\n", fails);
    return fails ? 1 : 0;
}
