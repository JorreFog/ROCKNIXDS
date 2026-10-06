// test_rules: a few of the game's rules played out in a town, each on its own:
//   - the elstängsel needs the power and 1000 kr, kills what crosses its gap for 25 s without paying for it, shocks
//     you if you stand in it, then charges for a minute
//   - a zombie in a window swipes at whoever stands at the gap, and can be shot through the boards
//   - pulling the trigger on a gun that has run dry brings out one that hasn't
//   - a blast that doesn't kill may leave a walker crawling, slower, and a crawler never runs as the round's last
//   - finding the three trädgårdstomtar plays the song and leaves a present
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Input none, fire;
static void tick(int n, const Input *in) { for (int i = 0; i < n; i++) game_update(in, &none, 1.0f / 60); }
static void calm(void) { for (int i = 0; i < MAX_ZOMBIES; i++) G->z[i].alive = 0; G->to_spawn = G->spawned = 999; G->spawn_cd = 1e9f; }
static Zombie *zombie_at(float x, float y, int state) {
    for (int i = 0; i < MAX_ZOMBIES; i++) if (!G->z[i].alive) {
        Zombie *z = &G->z[i]; memset(z, 0, sizeof *z);
        z->alive = 1; z->type = ZT_WALKER; z->state = state; z->x = x; z->y = y; z->hp = z->maxhp = 500; z->speed = 20; z->window = -1;
        return z;
    }
    return 0;
}

int main(void) {
    G = calloc(1, sizeof *G); G->view_w = 320; G->view_h = 240;
    fire.held = BIT(B_A);
    int seed = 1;
    Inter *trap = 0;
    for (; seed < 50 && !trap; seed++) {
        game_new((uint64_t)seed, seed % SEASON_COUNT);
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_TRAP) trap = &G->it[i];
    }
    CHECK(trap != 0, "no town with an elstängsel in 50");
    if (trap) {
        Player *p = &G->p; Inter *gap = &G->it[trap->a];
        G->round = 5; G->rstate = RS_ACTIVE; calm();
        /* open its gap as if bought */
        gap->state = 1;
        for (int j = 0; j < gap->th; j++) for (int i = 0; i < gap->tw; i++) { Tile *t = tile_at(gap->tx + i, gap->ty + j); t->f &= (uint8_t)~(TF_SOLID | TF_INTER); t->inter = 0; }
        p->x = trap->x; p->y = trap->y; p->kr = 5000;
        Input use; memset(&use, 0, sizeof use); use.held = BIT(btn_use());
        game_update(&use, &none, 1.0f / 60);
        CHECK(trap->state == 0 && p->kr == 5000, "the elstängsel went on without the power");
        G->power_on = 1;
        game_update(&use, &none, 1.0f / 60);
        CHECK(trap->state == 1 && p->kr == 5000 - TRAP_COST, "the elstängsel didn't go on for %d kr (state %d, %d kr)", TRAP_COST, trap->state, p->kr);
        float gx = (gap->tx + gap->tw / 2.0f) * TS, gy = (gap->ty + gap->th / 2.0f) * TS + 4;
        Zombie *z = zombie_at(gx, gy, ZS_CHASE);
        int kills = p->kills, kr = p->kr;
        tick(2, &none);
        CHECK(z->state == ZS_DEAD, "a zombie in the gap lived");
        CHECK(p->kills == kills + 1 && p->kr == kr, "the trap's kill: %d kills (want %d), %d kr (want %d)", p->kills, kills + 1, p->kr, kr);
        /* you, in the gap */
        float hp = p->hp; p->x = gx; p->y = gy; p->invuln = 0;
        tick(2, &none);
        CHECK(p->hp < hp || p->downed, "standing in the elstängsel didn't hurt");
        p->x = trap->x; p->y = trap->y; p->hp = p->maxhp; p->downed = 0; G->over = 0;
        tick((int)(TRAP_ON * 60) + 10, &none);
        CHECK(trap->state == 2, "the elstängsel didn't go off after %.0f s", TRAP_ON);
        z = zombie_at(gx, gy, ZS_CHASE);
        tick(2, &none);
        CHECK(z->state != ZS_DEAD, "a zombie died in the gap while it charged");
        z->alive = 0;
        tick((int)(TRAP_WAIT * 60) + 10, &none);
        CHECK(trap->state == 0, "the elstängsel didn't charge in %.0f s", TRAP_WAIT);
    }
    /* a zombie in a window */
    Inter *win = 0;
    for (int i = 0; i < G->nit && !win; i++) if (G->it[i].type == IT_WINDOW) win = &G->it[i];
    CHECK(win != 0, "no window");
    if (win) {
        Player *p = &G->p; calm(); G->god = 0; G->over = 0; p->downed = 0; p->hp = p->maxhp = 100; p->invuln = 0;
        Zombie *z = zombie_at(win->tx * TS + TS / 2, win->ty * TS + TS - 2, ZS_WINDOW); z->window = (int)(win - G->it);
        win->state = 3;                                    /* three boards gone */
        p->x = win->x; p->y = win->y; p->aim = -PI_F / 2;
        tick(60, &none);
        CHECK(p->hp < 100, "standing at a broken window, the zombie in it didn't swipe");
        float hp0 = z->hp;
        p->w[0] = weapon_make(W_AK5, RAR_COMMON); p->cur = 0; p->reloading = 0; p->fire_cd = 0; p->swap_t = 0;
        for (int k = 0; k < 20 && z->alive && z->state != ZS_DEAD; k++) { p->aim = atan2f(z->y - 8 - (p->y - 8), z->x - p->x); weapon_fire(); p->fire_cd = 0; }
        CHECK(z->state == ZS_DEAD || z->hp < hp0, "shots through the boards didn't hit the zombie in the window");
    }
    /* an empty gun */
    {
        Player *p = &G->p; calm();
        p->w[0] = weapon_make(W_AK5, RAR_COMMON); p->w[0].mag = 0; p->w[0].reserve = 0;
        p->w[1] = weapon_make(W_PIST88, RAR_COMMON); p->cur = 0; p->fired_this_press = 0; p->reloading = 0; p->fire_cd = 0;
        weapon_fire();
        CHECK(p->cur == 1, "a dry gun didn't hand over to the one with ammo");
    }
    /* a blast that doesn't kill may take a walker's legs: it crawls on, slower, and doesn't run as the round's last */
    {
        Player *p = &G->p; calm(); G->over = 0; p->downed = 0; G->god = 1;
        Zombie *zs[10];
        for (int k = 0; k < 10; k++) { zs[k] = zombie_at(p->x + 60 + (k % 5) * 3, p->y + 40 + (k / 5) * 3, ZS_CHASE); zs[k]->hp = zs[k]->maxhp = 1e6f; zs[k]->speed = 30; }
        explode(p->x + 66, p->y + 40, 60, 500, 0);
        int crawlers = 0, slow = 1;
        for (int k = 0; k < 10; k++) if (zs[k]->crawl) { crawlers++; if (zs[k]->speed > 30 * 0.36f && zs[k]->speed > 9.0f) slow = 0; }
        CHECK(crawlers >= 1 && crawlers <= 9 && slow, "a blast made %d crawlers of 10 (slow: %d)", crawlers, slow);
        G->round = 6; G->spawned = G->to_spawn = 10;
        for (int k = 0; k < 10; k++) if (!zs[k]->crawl || k != 0) zs[k]->alive = 0;
        for (int k = 0; k < 10; k++) if (zs[k]->crawl) { zs[k]->alive = 1; for (int j = 0; j < 10; j++) if (j != k) zs[j]->alive = 0; tick(30, &none); CHECK(zs[k]->speed < 20, "the last crawler of a round ran (speed %.0f)", zs[k]->speed); break; }
        calm(); G->god = 0;
    }
    /* the three trädgårdstomtar: the song, and a present */
    {
        Player *p = &G->p; calm(); G->over = 0; p->downed = 0;
        int n = 0, kr = p->kr; Inter *gn[3];
        for (int i = 0; i < G->nit && n < 3; i++) if (G->it[i].type == IT_GNOME) gn[n++] = &G->it[i];
        CHECK(n == 3, "%d tomtar in the town, not three", n);
        Input use; memset(&use, 0, sizeof use); use.held = BIT(btn_use());
        for (int k = 0; k < n; k++) {
            CHECK(!G->song, "the song before the %d. tomte", k + 1);
            p->x = gn[k]->x; p->y = gn[k]->y + 6;
            game_update(&use, &none, 1.0f / 60); game_update(&none, &none, 1.0f / 60);
            CHECK(gn[k]->state == 1, "tomte %d wasn't found standing on it", k + 1);
        }
        int weapons = 0; for (int i = 0; i < MAX_ITEMS; i++) weapons += G->items[i].alive && G->items[i].kind == IK_WEAPON && G->items[i].w.rar == RAR_EPIC;
        CHECK(G->song && weapons >= 1 && p->kr >= kr + 1000, "three tomtar: song %d, %d epic weapons dropped, %d kr (from %d)", G->song, weapons, p->kr, kr);
    }
    printf("rules: %d failures\n", fails);
    return fails ? 1 : 0;
}
