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
//   - the difficulty: what a hit does, the dead's health and number, by Easy, Medium and Hard
//   - lock-on (R2): held, the nearest in sight, and the aim follows it; let go, free; let go and held again at once,
//     the next nearest, round again; the one locked on falls and the nearest is next
#include "../src/game.h"
#include "../src/menu.h"
#include "../src/update.h"
#include <unistd.h>
#include <sys/stat.h>
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
    S.diff = DIFF_HARD;                                     /* the rules below at the game's own numbers */
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
    /* the bosses: every twentieth round, all eight in an order drawn for the town, then the same again */
    {
        int seen = 0, again = 1, differs = 0, first[BOSS_COUNT];
        game_new(300, 0);
        for (int i = 0; i < BOSS_COUNT; i++) { first[i] = boss_kind_for_round(20 * (i + 1)); if (first[i] >= 0) seen |= 1 << first[i]; }
        for (int i = 0; i < BOSS_COUNT; i++) again &= boss_kind_for_round(20 * (i + 1 + BOSS_COUNT)) == first[i];
        game_new(301, 0);
        for (int i = 0; i < BOSS_COUNT; i++) differs |= boss_kind_for_round(20 * (i + 1)) != first[i];
        CHECK(boss_kind_for_round(19) < 0 && boss_kind_for_round(30) < 0 && seen == (1 << BOSS_COUNT) - 1 && again && differs,
              "the bosses' rounds: %x seen, again %d, another town's order differs %d", seen, again, differs);
    }
    for (int k = 0; k < BOSS_COUNT; k++) {
        game_new(300 + (uint64_t)k, k % SEASON_COUNT);
        Player *p = &G->p;
        G->power_on = 1;
        int r = 20; while (boss_kind_for_round(r) != k && r < 20 * BOSS_COUNT) r += 20;   /* (its round in this town) */
        round_start(r);
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
    /* the splash when the game starts: over by itself in five seconds, at once with a button; then the title */
    {
        Input pa; memset(&pa, 0, sizeof pa); pa.held = BIT(B_A);
        A.state = ST_SPLASH; A.t = 0;
        int t = 0;
        for (; t < 60 * 6 && A.state == ST_SPLASH; t++) app_update(&none, &none, 1.0f / 60);
        CHECK(A.state == ST_TITLE && t > 60 * 4 && t < 60 * 5, "the splash: state %d after %.1f s", A.state, t / 60.0f);
        A.state = ST_SPLASH; A.t = 0;
        for (t = 0; t < 60; t++) app_update(&none, &none, 1.0f / 60);
        app_update(&pa, &none, 1.0f / 60);
        for (t = 0; t < 60 * 2 && A.state == ST_SPLASH; t++) app_update(&none, &none, 1.0f / 60);
        CHECK(A.state == ST_TITLE && t < 30, "a button didn't cut the splash short (%d frames more)", t);
    }
    /* Settings > Buttons: a button given to an action is taken from the one that had it; the game follows the
       bindings; they survive the settings file; a file with nonsense falls back to the defaults */
    {
        binds_default(LAYOUT_CLASSIC); S.scheme = 0; S.swap_ab = 0;
        CHECK(bind_of(ACT_FIRE) == B_A && bind_of(ACT_RELOAD) == B_Y, "default bindings: fire %d reload %d", bind_of(ACT_FIRE), bind_of(ACT_RELOAD));
        CHECK(bind_set(LAYOUT_CLASSIC, ACT_FIRE, B_Y) && bind_of(ACT_FIRE) == B_Y && bind_of(ACT_RELOAD) == B_A, "fire to Y: fire %d, reload %d (should have A)", bind_of(ACT_FIRE), bind_of(ACT_RELOAD));
        CHECK(!bind_set(LAYOUT_CLASSIC, ACT_FIRE, B_START) && !bind_set(LAYOUT_TWIN, ACT_USE, B_A) && !bind_set(LAYOUT_TWIN, ACT_FIRE, B_R1), "a button the layout keeps for itself was bound");
        CHECK(binds_valid(LAYOUT_CLASSIC) && binds_valid(LAYOUT_TWIN), "the tables stopped being permutations");
        game_new(7, 0); G->round = 1; G->rstate = RS_ACTIVE; calm();
        Player *p = &G->p; p->w[p->cur].mag = 10; p->w[p->cur].reserve = 50;
        int mag0 = p->w[p->cur].mag;
        Input y; memset(&y, 0, sizeof y); y.held = BIT(B_Y);
        for (int t = 0; t < 10; t++) game_update(&y, t ? &y : &none, 1.0f / 60);
        CHECK(p->w[p->cur].mag < mag0, "Y bound to fire didn't fire (mag %d of %d)", p->w[p->cur].mag, mag0);
        p->w[p->cur].mag = 3; p->reloading = 0;
        Input a; memset(&a, 0, sizeof a); a.held = BIT(B_A);
        game_update(&a, &none, 1.0f / 60);
        CHECK(p->reloading, "A bound to reload didn't reload");
        settings_save();
        S.bind[LAYOUT_CLASSIC][ACT_FIRE] = -1;
        settings_load();
        CHECK(bind_of(ACT_FIRE) == B_Y && bind_of(ACT_RELOAD) == B_A, "the bindings didn't come back from settings.txt (fire %d reload %d)", bind_of(ACT_FIRE), bind_of(ACT_RELOAD));
        char sp[600]; snprintf(sp, sizeof sp, "%s/settings.txt", dir);
        FILE *sf = fopen(sp, "a"); if (sf) { fprintf(sf, "c_use %d\n", B_Y); fclose(sf); }   /* Y twice */
        settings_load();
        CHECK(bind_of(ACT_FIRE) == B_A && bind_of(ACT_USE) == B_B, "a broken table wasn't put back to the defaults (fire %d use %d)", bind_of(ACT_FIRE), bind_of(ACT_USE));
        binds_default(LAYOUT_CLASSIC); settings_save();
    }
    /* the difficulty */
    {
        float hurt[DIFF_COUNT], hp[DIFF_COUNT]; int count[DIFF_COUNT];
        for (int d = 0; d < DIFF_COUNT; d++) {
            S.diff = d; game_new(11, 0); calm(); G->rstate = RS_ACTIVE; G->round = 9;
            CHECK(G->diff == d, "the run didn't take the difficulty (%d, not %d)", G->diff, d);
            Player *p = &G->p; G->over = 0; p->invuln = 0; p->hp = 100; G->god = 0;
            player_hurt(40, p->x + 10, p->y); hurt[d] = 100 - p->hp;
            Zombie *z = zombie_at_spot(ZT_WALKER, p->x + 40, p->y); hp[d] = z ? z->maxhp : 0;
            G->round = 1; G->rstate = RS_BREAK; G->rtime = 0; G->to_spawn = G->spawned = 0;
            calm(); G->wolf_next = G->moose_next = 999; round_start(30); count[d] = G->to_spawn;
        }
        CHECK(hurt[DIFF_HARD] > 39 && hurt[DIFF_HARD] < 41, "a 40 hit on hard took %.1f", hurt[DIFF_HARD]);
        CHECK(hurt[DIFF_EASY] < hurt[DIFF_MEDIUM] && hurt[DIFF_MEDIUM] < hurt[DIFF_HARD], "hits by difficulty: %.1f %.1f %.1f", hurt[0], hurt[1], hurt[2]);
        CHECK(hp[DIFF_EASY] < hp[DIFF_MEDIUM] && hp[DIFF_MEDIUM] < hp[DIFF_HARD] && hp[DIFF_HARD] == zombie_hp_for_round(9), "health by difficulty: %.0f %.0f %.0f", hp[0], hp[1], hp[2]);
        CHECK(count[DIFF_EASY] < count[DIFF_MEDIUM] && count[DIFF_MEDIUM] < count[DIFF_HARD] && count[DIFF_HARD] == zombies_for_round(30), "round 30's count by difficulty: %d %d %d", count[0], count[1], count[2]);
        S.diff = DIFF_HARD;
    }
    /* lock-on */
    {
        binds_default(LAYOUT_CLASSIC); S.scheme = 0;
        CHECK(bind_of(ACT_LOCK) == B_R2 && bind_of(ACT_GRENADE) == B_L2 && bind_of(ACT_ITEM) < 0, "classic defaults: lock %d grenade %d item %d", bind_of(ACT_LOCK), bind_of(ACT_GRENADE), bind_of(ACT_ITEM));
        game_new(7, 0); calm(); G->rstate = RS_ACTIVE; G->round = 1; G->god = 1;
        Player *p = &G->p;
        G->camx = p->x - G->view_w / 2; G->camy = p->y - G->view_h / 2;
        /* three in the open round the player (the start zone's middle), at 30, 50 and 70 px */
        Zombie *a = 0, *b = 0, *c = 0;
        float ang[3] = { 0, 2.1f, 4.2f }, dist[3] = { 30, 50, 70 };
        Zombie **zs[3] = { &a, &b, &c };
        for (int k = 0; k < 3; k++) {
            float x = p->x + cosf(ang[k]) * dist[k], y = p->y + sinf(ang[k]) * dist[k];
            if (shot_clear(p->x, p->y - 6, x, y - 6)) *zs[k] = zombie_at(x, y, ZS_CHASE), (*zs[k])->speed = 0;
        }
        Input r2; memset(&r2, 0, sizeof r2); r2.held = BIT(B_R2);
        int ia = a ? (int)(a - G->z) + 1 : -1, ib = b ? (int)(b - G->z) + 1 : -1, ic = c ? (int)(c - G->z) + 1 : -1;
        CHECK(a && b && c, "the start zone has walls in the way of the test's zombies (%d %d %d)", ia, ib, ic);
        if (a && b && c) {
            for (int t = 0; t < 5; t++) game_update(&r2, t ? &r2 : &none, 1.0f / 60);
            CHECK(p->lock == ia, "held, locked on %d, not the nearest %d", p->lock, ia);
            float want = atan2f((a->y - 6) - (p->y - 6), a->x - p->x);
            CHECK(fabsf(angdiff(p->aim, want)) < 0.05f, "the aim doesn't follow the lock (%.2f, not %.2f)", p->aim, want);
            game_update(&none, &r2, 1.0f / 60);
            CHECK(p->lock == 0, "let go, still locked on %d", p->lock);
            for (int t = 0; t < 60; t++) game_update(&none, &none, 1.0f / 60);
            game_update(&r2, &none, 1.0f / 60);
            CHECK(p->lock == ia, "held again a second later: %d, not the nearest %d", p->lock, ia);
            game_update(&none, &r2, 1.0f / 60); game_update(&none, &none, 1.0f / 60);
            game_update(&r2, &none, 1.0f / 60);
            CHECK(p->lock == ib, "let go and held again at once: %d, not the next nearest %d", p->lock, ib);
            game_update(&none, &r2, 1.0f / 60); game_update(&r2, &none, 1.0f / 60);
            CHECK(p->lock == ic, "and again: %d, not %d", p->lock, ic);
            game_update(&none, &r2, 1.0f / 60); game_update(&r2, &none, 1.0f / 60);
            CHECK(p->lock == ia, "past the farthest: %d, not the nearest again %d", p->lock, ia);
            a->alive = 0;                                   /* it falls while held: the nearest of the rest */
            game_update(&r2, &r2, 1.0f / 60);
            CHECK(p->lock == ib, "after the locked one fell: %d, not %d", p->lock, ib);
            game_update(&none, &r2, 1.0f / 60);
            CHECK(p->lock == 0, "let go at the end: still %d", p->lock);
            /* SELECT: a tap is the next item, held it's used */
            p->bag[0].id = C_FORBAND; p->bag[0].n = 2; p->bag[1].id = C_PLASTER; p->bag[1].n = 1; p->bag_sel = 0;
            Input sel; memset(&sel, 0, sizeof sel); sel.held = BIT(B_SELECT);
            game_update(&sel, &none, 1.0f / 60); game_update(&none, &sel, 1.0f / 60);
            CHECK(p->bag_sel == 1 && p->bag[1].n == 1, "a tap on SELECT: slot %d, %d left", p->bag_sel, p->bag[1].n);
            p->hp = 30; G->god = 0;
            for (int t = 0; t < 30; t++) game_update(&sel, t ? &sel : &none, 1.0f / 60);
            game_update(&none, &sel, 1.0f / 60);
            CHECK(p->bag[1].id < 0 && p->hp > 55, "holding SELECT didn't use the item (slot 1: id %d n %d, hp %.0f)", p->bag[1].id, p->bag[1].n, p->hp);
        }
    }
    /* Settings > Game updates, with a stand-in for device/update.sh: the title looks once, says there's a newer one,
       the row (from the title) installs it, asking first over a saved run, and the game quits for the session to
       start the new one (status 75) */
    {
        char up[700]; snprintf(up, sizeof up, "%s/update.sh", dir);
        FILE *f = fopen(up, "w");
        if (f) { fprintf(f, "#!/bin/sh\necho \"$1\" >> '%s/calls'\ncase $1 in check) echo 'UPDATE 9.1.0' ;; install) echo 'STEP Downloading v9.1.0'; sleep 0.3; echo 'DONE 9.1.0' ;; esac\n", dir); fclose(f); }
        chmod(up, 0755);
        setenv("DK_UPDATER", up, 1);
        A.state = ST_TITLE; A.t = 0; A.quit = 0; A.has_save = 1; A.save_checked = 1;
        int t = 0;
        char v[24];
        for (; t < 60 * 5 && update_state(v, sizeof v, 0, 0) != UPD_AVAILABLE; t++) { app_update(&none, &none, 1.0f / 60); usleep(2000); }
        CHECK(update_state(v, sizeof v, 0, 0) == UPD_AVAILABLE && !strcmp(v, "9.1.0"), "the title didn't find the update (state %d, '%s')", update_state(0, 0, 0, 0), v);
        A.state = ST_SETTINGS; A.settings_from = ST_TITLE; A.sel = 12;
        Input ok; memset(&ok, 0, sizeof ok); ok.held = BIT(btn_ok());
        app_update(&ok, &none, 1.0f / 60); app_update(&none, &ok, 1.0f / 60);
        CHECK(A.upd_confirm && update_state(0, 0, 0, 0) == UPD_AVAILABLE, "over a saved run, the first press updated (confirm %d, state %d)", A.upd_confirm, update_state(0, 0, 0, 0));
        app_update(&ok, &none, 1.0f / 60); app_update(&none, &ok, 1.0f / 60);
        for (t = 0; t < 60 * 6 && !A.quit; t++) { app_update(&none, &none, 1.0f / 60); usleep(2000); }
        CHECK(A.quit == 2, "after the update the game didn't quit to restart (quit %d, state %d)", A.quit, update_state(0, 0, 0, 0));
        char cp[700]; snprintf(cp, sizeof cp, "%s/calls", dir);
        FILE *c = fopen(cp, "r"); char l1[16] = "", l2[16] = "";
        if (c) { if (fscanf(c, "%15s %15s", l1, l2) != 2) l2[0] = 0; fclose(c); }
        CHECK(!strcmp(l1, "check") && !strcmp(l2, "install"), "the updater was called '%s', '%s'", l1, l2);
        A.quit = 0; unsetenv("DK_UPDATER");
    }
    /* Kert Barlsson on the radio: every line fits his box in both languages; his introduction once, then a greeting;
       lines one at a time; the power on is something he talks about; Settings > Radio off keeps him quiet */
    {
        static uint32_t px[320 * 400]; Surf t = { 254, 400, 254, px, 0, 0, 254, 400 };
        for (int lang = 0; lang < 2; lang++) {
            S.lang = lang;
            for (int i = 0; i < radio_lines() + radio_tips(); i++) {
                int id = i < radio_lines() ? i : 1000 + i - radio_lines();
                int h = text_wrap(&t, FONT_NORMAL, 0, 0, 254 - 6, 0xffffff, radio_text(id));
                CHECK(h <= 38, "radio line %d (%s) is %d px tall, the box has 38: %.40s...", id, lang ? "sv" : "en", h, radio_text(id));
            }
        }
        S.lang = LANG_EN;
        S.radio = 1; S.radio_heard = 0; S.radio_tip = 0;
        game_new(21, 0); calm(); G->rstate = RS_ACTIVE; G->round = 1; G->god = 1;
        int on = -1;
        for (int k = 0; k < 60 * 4 && on < 0; k++) { game_update(&none, &none, 1.0f / 60); if (G->radio.cur >= 0) on = G->radio.cur; }
        CHECK(on == 0 && S.radio_heard, "the first run didn't start with his introduction (line %d, heard %d)", on, S.radio_heard);
        int seen = 0, last = -1;
        for (int k = 0; k < 60 * 90; k++) {
            game_update(&none, &none, 1.0f / 60);
            if (G->radio.cur >= 0 && G->radio.cur != last) { seen++; last = G->radio.cur; }
            if (G->radio.cur < 0) last = -1;
        }
        CHECK(seen >= 4 && G->radio.nq == 0 && G->radio.cur < 0, "the introduction: %d lines on air, %d waiting", seen, G->radio.nq);
        G->power_on = 1;
        int power = 0;
        for (int k = 0; k < 60 * 30 && !power; k++) { game_update(&none, &none, 1.0f / 60); power = G->radio.cur >= 0; }
        CHECK(power, "the power came on and Kert said nothing");
        game_new(22, 0); calm(); G->rstate = RS_ACTIVE; G->round = 1; G->god = 1;
        on = -1;
        for (int k = 0; k < 60 * 4 && on < 0; k++) { game_update(&none, &none, 1.0f / 60); if (G->radio.cur >= 0) on = G->radio.cur; }
        CHECK(on > 3 && on < 10, "the second run: line %d, not a greeting (the introduction once is enough)", on);
        S.radio = 0;
        game_new(23, 0); calm(); G->rstate = RS_ACTIVE; G->round = 1; G->god = 1; G->power_on = 1;
        int any = 0;
        for (int k = 0; k < 60 * 20; k++) { game_update(&none, &none, 1.0f / 60); any |= G->radio.cur >= 0; }
        CHECK(!any, "Settings > Radio off, and he talked anyway");
        S.radio = 1;
    }
    char rm[640]; snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    if (system(rm)) printf("(couldn't remove %s)\n", dir);
    printf("rules: %d failures\n", fails);
    return fails ? 1 : 0;
}
