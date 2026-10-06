// test_rules: a few of the game's rules played out in a town, each on its own:
//   - the elstängsel needs the power and 1000 kr, kills what crosses its gap for 25 s without paying for it, shocks
//     you if you stand in it, then charges for a minute
//   - a zombie in a window swipes at whoever stands at the gap, and can be shot through the boards
//   - pulling the trigger on a gun that has run dry brings out one that hasn't
//   - a blast that doesn't kill may leave a walker crawling, slower, and a crawler never runs as the round's last
//   - finding the three trädgårdstomtar plays the song and leaves a present
//   - a perk machine plays its jingle now and then to whoever stands by it, once the power is on
//   - a boss every twentieth round, in turn: Insta-Kill and Kaboom don't kill it, the round waits for it, each one's
//     moves hurt you, and killed it leaves a legendary weapon, Max Ammo and money
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
    char dir[] = "/tmp/dk-test-rules-XXXXXX";                 /* (its log, not in the player's data) */
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("DK_DATA", dir, 1);
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
        /* not while its gap is still boarded up (it would guard nothing) */
        p->x = trap->x; p->y = trap->y; p->kr = 5000; G->power_on = 1;
        Input use0; memset(&use0, 0, sizeof use0); use0.held = BIT(btn_use());
        game_update(&use0, &none, 1.0f / 60);
        CHECK(trap->state == 0 && p->kr == 5000, "the elstängsel was bought before its gap was opened");
        G->power_on = 0;
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
        /* a boss in the gap burns, slowly, and pays nothing for it */
        {
            Zombie *bz = zombie_at(gx, gy, ZS_CHASE);
            bz->type = ZT_BOSS; bz->variant = BOSS_TROLL; bz->hp = bz->maxhp = 50000;
            memset(&G->boss, 0, sizeof G->boss); G->boss.on = 1; G->boss.kind = BOSS_TROLL; G->boss.zi = (int)(bz - G->z);
            int kr1 = p->kr; float hp1 = bz->hp;
            for (int t = 0; t < 30; t++) { bz->x = gx; bz->y = gy; tick(1, &none); }
            CHECK(bz->hp < hp1 && bz->state != ZS_DEAD && p->kr == kr1, "a boss in the elstängsel: %.0f of %.0f hp, %d kr (want %d)", bz->hp, hp1, p->kr, kr1);
            bz->alive = 0; memset(&G->boss, 0, sizeof G->boss); memset(G->hz, 0, sizeof G->hz);
        }
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
    /* the jingles: never from a machine without power (Kanelbulle aside), now and then with it */
    {
        Player *p = &G->p; calm(); G->over = 0; p->downed = 0; G->god = 1;
        Inter *m = 0;
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_PERK && G->it[i].a != PK_KANELBULLE) m = &G->it[i];
        CHECK(m != 0, "no perk machine");
        if (m) {
            p->x = m->x; p->y = m->y + 14; G->power_on = 0;
            int before = sfx_asked[SFX_JINGLE + m->a];
            for (int s = 0; s < 120; s++) { calm(); tick(60, &none); p->x = m->x; p->y = m->y + 14; }
            CHECK(sfx_asked[SFX_JINGLE + m->a] == before, "%s played its jingle without the power", PERKS[m->a].name);
            G->power_on = 1;
            for (int s = 0; s < 120; s++) { calm(); tick(60, &none); p->x = m->x; p->y = m->y + 14; }
            int n = sfx_asked[SFX_JINGLE + m->a] - before, notes = 0;
            CHECK(n >= 1 && n <= 8, "%s played its jingle %d times in two minutes", PERKS[m->a].name, n);
            for (int s = 0; s < 60 && !notes; s++) { calm(); tick(60, &none); p->x = m->x; p->y = m->y + 14;
                for (int i = 0; i < MAX_TEXTS; i++) notes += G->ft[i].alive && !strcmp(G->ft[i].s, "\xe2\x99\xaa"); }
            CHECK(notes, "no note rose from %s while its jingle played", PERKS[m->a].name);
        }
        G->god = 0;
    }
    /* the bosses */
    CHECK(boss_kind_for_round(19) < 0 && boss_kind_for_round(20) == BOSS_DRAUGEN && boss_kind_for_round(40) == BOSS_TROLL &&
          boss_kind_for_round(60) == BOSS_NACKEN && boss_kind_for_round(80) == BOSS_LINDORM && boss_kind_for_round(100) == BOSS_DRAUGEN &&
          boss_kind_for_round(30) < 0, "the bosses' rounds");
    for (int k = 0; k < BOSS_COUNT; k++) {
        game_new(300 + (uint64_t)k, k % SEASON_COUNT);
        Player *p = &G->p;
        G->power_on = 1;
        round_start(20 * (k + 1));
        for (int t = 0; t < 60 * 8 && !(G->boss.on && G->z[G->boss.zi].state == ZS_CHASE); t++) { G->spawn_cd = 1e9f; game_update(&none, &none, 1.0f / 60); }
        CHECK(G->boss.on, "%s didn't come up in round %d", boss_name(k), G->round);
        if (!G->boss.on) continue;
        Zombie *z = &G->z[G->boss.zi];
        CHECK(z->type == ZT_BOSS && z->variant == k && z->maxhp >= 30000, "boss %d: type %d variant %d, %.0f hp", k, z->type, z->variant, z->maxhp);
        /* up, it greets you: on the screen, the view leaning its way */
        float fx, fy; int focus = 0;
        for (int t = 0; t < 40; t++) { G->spawn_cd = 1e9f; game_update(&none, &none, 1.0f / 60); focus += boss_focus(&fx, &fy); }
        float vx = z->x - G->camx, vy = z->y - G->camy;
        CHECK(focus == 40 && vx > 0 && vx < G->view_w && vy > 20 && vy < G->view_h, "%s greeted you out of sight (%.0f, %.0f in the view)", boss_name(k), vx, vy);
        G->insta_t = 5; damage_zombie(z, 100, 0, 0, 0, 0); G->insta_t = 0;
        CHECK(z->state != ZS_DEAD && z->hp > z->maxhp * 0.9f, "%s died to Insta-Kill", boss_name(k));
        kill_all_zombies(0);
        CHECK(z->state != ZS_DEAD && z->hp > z->maxhp * 0.8f && z->hp < z->maxhp * 0.95f, "%s and Kaboom: %.0f of %.0f hp", boss_name(k), z->hp, z->maxhp);
        /* stand there for half a minute: it comes for you, and its moves hurt */
        p->hp = p->maxhp = 1e6f;
        float hx = p->x, hy = p->y; int moves = 0, last = -1;
        for (int t = 0; t < 60 * 30; t++) {
            G->spawn_cd = 1e9f;
            game_update(&none, &none, 1.0f / 60);
            if (G->boss.st != last) { moves++; last = G->boss.st; }
            if (p->downed || G->over) break;
            if (dist2f(p->x, p->y, hx, hy) > 40 * 40) { p->x = hx; p->y = hy; }   /* (pushed and pulled: back to the spot) */
        }
        CHECK(p->hp < 1e6f, "%s never hurt the player in half a minute", boss_name(k));
        CHECK(moves >= 4, "%s made only %d moves", boss_name(k), moves);
        CHECK(G->rstate == RS_ACTIVE, "the round ended with %s up", boss_name(k));
        /* killed: it falls (slowed down at first), its banner up through it; then the spoils and the fanfare */
        int kr = p->kr, kills = G->boss_kills, fanfare = sfx_asked[SFX_FANFARE];
        G->boss.hidden = 0; z->state = ZS_CHASE; z->hp = 1;
        damage_zombie(z, 10, 0, 0, 0, 0);
        CHECK(z->state == ZS_DEAD && !G->boss.on && G->boss_kills == kills + 1, "%s didn't die", boss_name(k));
        CHECK(boss_time_scale() < 1, "%s's fall wasn't slowed down", boss_name(k));
        banner(0x40ff40, "MAX AMMO", 0);
        CHECK(!strcmp(G->banner2, tr("slain")), "another banner went up over %s's", boss_name(k));
        p->x = z->x + 150; p->y = z->y;                    /* (away from where the spoils fall) */
        for (int t = 0; t < 60 * 3; t++) { G->spawn_cd = 1e9f; game_update(&none, &none, 1.0f / 60); }
        int legendary = 0, maxammo = 0;
        for (int i = 0; i < MAX_ITEMS; i++) legendary += G->items[i].alive && G->items[i].kind == IK_WEAPON && G->items[i].w.rar == RAR_LEGENDARY;
        for (int i = 0; i < MAX_POWERUPS; i++) maxammo += G->pu[i].alive && G->pu[i].kind == PU_MAXAMMO;
        CHECK(boss_time_scale() == 1 && z->alive && z->state == ZS_DEAD && z->t > 1.6f, "%s's fall: %.2f s in, time at %.2f", boss_name(k), z->t, boss_time_scale());
        CHECK(legendary >= 1 && maxammo >= 1 && p->kr >= kr + 2000 && sfx_asked[SFX_FANFARE] == fanfare + 1,
              "%s's spoils: %d legendary, %d max ammo, %d kr more, %d fanfares", boss_name(k), legendary, maxammo, p->kr - kr, sfx_asked[SFX_FANFARE] - fanfare);
        kill_all_zombies(0);
        for (int t = 0; t < 60 * 3 && G->rstate == RS_ACTIVE; t++) { G->spawn_cd = 1e9f; G->spawned = G->to_spawn; game_update(&none, &none, 1.0f / 60); }
        CHECK(G->rstate == RS_BREAK, "the round didn't end after %s fell", boss_name(k));
        for (int t = 0; t < 60 * 3; t++) game_update(&none, &none, 1.0f / 60);
        CHECK(G->boss.bar <= 0 && !G->boss.enraged, "%s's bar was still up in the break (%.2f)", boss_name(k), G->boss.bar);
    }
    /* where a boss comes up, in more towns: in sight as it greets you (the view leaning its way) */
    static const int towns[] = { 347, 494, 553, 615, 634, 641, 670 };
    for (int i = 0; i < (int)ARRAY_LEN(towns); i++) {
        game_new((uint64_t)towns[i], towns[i] % SEASON_COUNT);
        G->power_on = 1; round_start(20);
        for (int t = 0; t < 60 * 8 && !(G->boss.on && G->z[G->boss.zi].state == ZS_CHASE); t++) { G->spawn_cd = 1e9f; game_update(&none, &none, 1.0f / 60); }
        for (int t = 0; t < 40; t++) { G->spawn_cd = 1e9f; game_update(&none, &none, 1.0f / 60); }
        Zombie *z = &G->z[G->boss.zi];
        float vx = z->x - G->camx, vy = z->y - G->camy;
        CHECK(G->boss.on && vx > 0 && vx < G->view_w && vy > 20 && vy < G->view_h, "town %d: the boss greeted you out of sight (%.0f, %.0f)", towns[i], vx, vy);
    }
    char rm[640]; snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    if (system(rm)) printf("(couldn't remove %s)\n", dir);
    printf("rules: %d failures\n", fails);
    return fails ? 1 : 0;
}
