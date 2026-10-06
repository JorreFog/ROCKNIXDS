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

    for (int c = 0; c <= SEASON_COUNT; c++) {             /* each season, then a boss fight (the troll, round 40) */
        int season = c % SEASON_COUNT;
        game_new(4242 + c, season);
        G->god = 1;                                        /* (so that the run lasts) */
        G->p.kr = 20000; round_start(c == SEASON_COUNT ? 40 : 3 + season * 4);
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
        if (c == SEASON_COUNT) {
            int hz = 0; for (int i = 0; i < MAX_HAZARDS; i++) hz += G->hz[i].alive;
            CHECK(G->boss.on, "no boss in the boss fight");
            printf("boss fight: %s at %.0f of %.0f hp, %d hazards: the same after a save and a load\n", boss_name(G->boss.kind),
                   G->boss.on ? G->z[G->boss.zi].hp : 0, G->boss.on ? G->z[G->boss.zi].maxhp : 0, hz);
        } else printf("season %d: round %d, %d kills, %d kr: the same after a save and a load\n", season, G->round, G->p.kills, G->p.kr);
    }
    /* dying and quitting at once (the exit hotkey before the game over screen): the run neither comes back on the
       title nor goes uncounted, and a high score not yet named is kept */
    {
        Input none, start; memset(&none, 0, sizeof none); memset(&start, 0, sizeof start); start.held = BIT(B_START);
        A.seed_override = 4343; app_new_run();
        play(0, 600);
        CHECK(run_save() == 0 && run_saved(), "the autosave wasn't written");   /* (as between rounds) */
        int runs = ST.runs, n = nscores;
        G->over = 1; G->over_t = 0;                        /* dead: the game over screen comes in 1.5 s */
        app_update(&none, &none, 1.0f / 60);
        app_update(&start, &none, 1.0f / 60);              /* START: no pause menu for the dead */
        CHECK(A.state == ST_PLAY, "paused while dead (state %d)", A.state);
        CHECK(!run_saved(), "the dead run is still saved");
        CHECK(ST.runs == runs + 1, "the run wasn't counted in the stats");
        app_quit();
        CHECK(!run_saved(), "quitting saved the dead run");
        CHECK(nscores == n + 1 || A.rank < 0, "the score was lost on quitting (rank %d)", A.rank);
        app_update(&none, &none, 1.0f / 60);
        CHECK(ST.runs == runs + 1, "the run was counted twice");
    }
    /* the title: NEW RUN asks once more before it gives up a saved run; then that run is counted (its game over, a
       high score if it earned one) and the new one starts */
    {
        Input none, down, ok; memset(&none, 0, sizeof none); memset(&down, 0, sizeof down); memset(&ok, 0, sizeof ok);
        down.held = BIT(B_DOWN); ok.held = BIT(btn_fire());
        A.seed_override = 4444; app_new_run(); play(0, 300);
        CHECK(run_save() == 0, "the run wasn't saved");
        A.state = ST_TITLE; A.sel = 0; A.save_checked = 0; A.confirm = 0;
        app_update(&none, &none, 1.0f / 60);
        CHECK(A.has_save, "no Continue on the title");
        app_update(&down, &none, 1.0f / 60);                /* NEW RUN */
        app_update(&ok, &none, 1.0f / 60);
        CHECK(A.state == ST_TITLE && run_saved(), "NEW RUN gave up the saved run at the first press");
        int runs = ST.runs;
        app_update(&none, &none, 1.0f / 60);
        app_update(&ok, &none, 1.0f / 60);
        CHECK(A.state == ST_GAMEOVER && !run_saved() && ST.runs == runs + 1, "NEW RUN's second press didn't give up the saved run (state %d, %d runs, was %d)", A.state, ST.runs, runs);
        for (int k = 0; k < 900 && A.state != ST_PLAY; k++) app_update(k & 1 ? &ok : &none, &none, 1.0f / 60);   /* the game over, the scores */
        CHECK(A.state == ST_PLAY && G->round == 1 && !G->over && !A.unnamed, "no new run after the given-up one (state %d, round %d)", A.state, G->round);
        CHECK(ST.runs == runs + 1, "the given-up run was counted %d times", ST.runs - runs);
        G->over = 1; app_update(&none, &none, 1.0f / 60); A.unnamed = 0;   /* (that one ends here) */
    }
    run_save();
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
    char rm[640]; snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    if (system(rm)) printf("(couldn't remove %s)\n", dir);
    printf("save: %d failures\n", fails);
    return fails ? 1 : 0;
}
