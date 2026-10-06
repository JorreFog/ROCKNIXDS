// scene: set up a situation and write both screens to a PNG, for looking at the art in place.
//   scene <name> <out.png> [season] [seed]
//   names: horde wolves moose box loot pap power title gameover scores settings name howto pause
//   (power: the lights coming on, DK_SCENE_TICKS ticks after the switch)
#include "../src/game.h"
#include "../src/menu.h"
#include <stdio.h>
#include <stdlib.h>

static Surf top, bot;

static Zombie *put(int type, float dx, float dy, int variant) {
    for (int i = 0; i < MAX_ZOMBIES; i++) if (!G->z[i].alive) {
        Zombie *z = &G->z[i]; memset(z, 0, sizeof *z);
        z->alive = 1; z->type = type; z->state = ZS_CHASE; z->x = G->p.x + dx; z->y = G->p.y + dy; z->variant = variant;
        z->hp = z->maxhp = 1000; z->speed = type == ZT_WOLF ? 80 : 26; z->window = -1;
        float a = atan2f(-dy, -dx);
        z->dir = fabsf(cosf(a)) > fabsf(sinf(a)) ? (cosf(a) > 0 ? 2 : 3) : (sinf(a) > 0 ? 0 : 1);
        z->anim = (float)(i % 4);
        return z;
    }
    return 0;
}
static void ticks(int n, Input *in) { Input prev = *in; for (int i = 0; i < n; i++) { G->to_spawn = G->spawned = 999; game_update(in, &prev, 1.0f / 60); prev = *in; } }
static void shot(const char *path) {
    int gap = 6, w = top.w, h = top.h + gap + bot.h;
    uint32_t *px = calloc((size_t)w * h, 4);
    for (int y = 0; y < top.h; y++) memcpy(px + (size_t)y * w, top.px + (size_t)y * top.pitch, (size_t)top.w * 4);
    for (int y = 0; y < gap; y++) for (int x = 0; x < w; x++) px[(size_t)(top.h + y) * w + x] = 0x303030;
    for (int y = 0; y < bot.h; y++) memcpy(px + (size_t)(top.h + gap + y) * w, bot.px + (size_t)y * bot.pitch, (size_t)bot.w * 4);
    png_write(path, px, w, h, w);
    free(px);
}

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "horde", *out = argc > 2 ? argv[2] : "scene.png";
    int season = argc > 3 ? atoi(argv[3]) : 1;
    uint64_t seed = argc > 4 ? strtoull(argv[4], 0, 10) : 5;
    const char *sz = getenv("DK_HEADLESS_SIZE");
    int pw = 640, ph = 480; if (sz) sscanf(sz, "%dx%d", &pw, &ph);
    int sc = MAX(1, MIN(pw / 320, ph / 240));
    surf_alloc(&top, pw / sc, ph / sc); surf_alloc(&bot, pw / sc, ph / sc);
    G = calloc(1, sizeof *G); G->view_w = top.w; G->view_h = top.h;
    S.assist = 2; S.shake = 0; S.lang = getenv("DK_LANG_SV") ? LANG_SV : LANG_EN;
    if (getenv("DK_SCENE_FX")) S.effects = atoi(getenv("DK_SCENE_FX"));   /* 2: the light effects */
    render_init();
    A.bot_w = bot.w; A.bot_h = bot.h;
    Input in; memset(&in, 0, sizeof in);
    if (!strcmp(name, "splash")) {                         /* JorreFog productions, DK_SCENE_T seconds in */
        A.state = ST_SPLASH; A.t = getenv("DK_SCENE_T") ? (float)atof(getenv("DK_SCENE_T")) : 2.6f;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    if (!strcmp(name, "title") || !strcmp(name, "scores") || !strcmp(name, "settings") || !strcmp(name, "howto") || !strcmp(name, "name")) {
        A.state = !strcmp(name, "title") ? ST_TITLE : !strcmp(name, "scores") ? ST_SCORES : !strcmp(name, "settings") ? ST_SETTINGS : !strcmp(name, "howto") ? ST_HOWTO : ST_NAME;
        A.t = 3.3f; A.rank = -1; A.page = getenv("DK_SCENE_PAGE") ? atoi(getenv("DK_SCENE_PAGE")) : 1;
        if (getenv("DK_SCENE_SEL")) A.sel = atoi(getenv("DK_SCENE_SEL"));
        if (getenv("DK_SCENE_SAVED")) { A.has_save = A.save_checked = 1; snprintf(A.save_town, sizeof A.save_town, "Björkhagen"); A.save_round = 12; }
        if (getenv("DK_SCENE_CONFIRM")) A.confirm = atoi(getenv("DK_SCENE_CONFIRM"));   /* NEW RUN pressed once (T_NEW) */
        if (A.state == ST_NAME) { game_new(seed, season); G->round = 14; A.letters[0] = 9; A.letters[1] = 15; A.letters[2] = 26; A.name_pos = 2; }
        if (A.state == ST_SCORES) {
            static const char *n[] = { "JOR", "ÅSA", "ELL", "BOB", "KIM" };
            for (int i = 0; i < 5; i++) { Score s; memset(&s, 0, sizeof s); snprintf(s.name, sizeof s.name, "%s", n[i]); s.round = 23 - i * 4; s.kills = 512 - i * 90; s.kr = 98000 - i * 15000; snprintf(s.town, sizeof s.town, "%s", i & 1 ? "Björkhagen" : "Sjövik"); s.daily = i == 2 ? 20261006 : 0; score_insert(&s, score_rank(&s)); }
            A.rank = 1; A.page = getenv("DK_SCENE_PAGE") ? atoi(getenv("DK_SCENE_PAGE")) : 0;
            ST = (Stats){ 37, 6412, 288, 23, 41 * 3600 + 17 * 60, 1840250, 96, 71, 1204, 9, 2 };
        }
        app_render(&top, &bot); shot(out);
        return 0;
    }
    game_new(seed, season);
    Player *p = &G->p;
    if (!strcmp(name, "gnome")) {                          /* next to a trädgårdstomte (the first found, the next not) */
        Inter *g[3]; int n = 0;
        for (int i = 0; i < G->nit && n < 3; i++) if (G->it[i].type == IT_GNOME) g[n++] = &G->it[i];
        if (n) { p->x = g[0]->x + 18; p->y = g[0]->y + 10; G->camx = p->x - top.w / 2; G->camy = p->y - top.h / 2; }
        G->round = 4; G->rstate = RS_ACTIVE; G->banner_t = 0; p->aim = PI_F;
        ticks(2, &in); A.state = ST_PLAY;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    if (!strcmp(name, "boss")) {                           /* a boss up (DK_DEBUG_BOSS picks which), DK_SCENE_TICKS into the fight */
        G->power_on = 1; world_power_wave(p->x, p->y); prop_lights();
        G->god = 1;
        round_start(20);
        int n = getenv("DK_SCENE_TICKS") ? atoi(getenv("DK_SCENE_TICKS")) : 400;
        /* or: until a hazard of the kind DK_SCENE_HZ is about, or the boss is doing DK_SCENE_ST, and DK_SCENE_HZ_AFTER
           ticks more (its moves, pictured) */
        int hz = getenv("DK_SCENE_HZ") ? atoi(getenv("DK_SCENE_HZ")) : -1, after = getenv("DK_SCENE_HZ_AFTER") ? atoi(getenv("DK_SCENE_HZ_AFTER")) : 10;
        int st = getenv("DK_SCENE_ST") ? atoi(getenv("DK_SCENE_ST")) : -1;
        if (hz >= 0 || st >= 0) n = 60 * 120;
        for (int k = 0, seen = 0; k < n; k++) {
            G->spawn_cd = 1e9f;                             /* (only the boss) */
            Input none; memset(&none, 0, sizeof none); game_update(&none, &none, 1.0f / 60);
            if (st >= 0 && G->boss.on && G->z[G->boss.zi].state == ZS_CHASE && G->boss.st == st) seen = 1;
            if (hz >= 0 && !seen) for (int i = 0; i < MAX_HAZARDS; i++) seen |= G->hz[i].alive && G->hz[i].kind == hz;
            if (seen && after-- <= 0) break;
        }
        if (getenv("DK_SCENE_HP")) { Zombie *z = &G->z[G->boss.zi]; z->hp = z->maxhp * (float)atof(getenv("DK_SCENE_HP")); }
        if (getenv("DK_SCENE_KILL")) {                     /* then felled: DK_SCENE_KILL ticks into its fall */
            Zombie *z = &G->z[G->boss.zi]; G->boss.hidden = 0; z->state = ZS_CHASE; z->hp = 1;
            damage_zombie(z, 10, 0, 0, 0, 0);
            for (int k = atoi(getenv("DK_SCENE_KILL")); k > 0; k--) {
                G->spawn_cd = 1e9f;
                Input none; memset(&none, 0, sizeof none); game_update(&none, &none, 1.0f / 60);
            }
            G->camx = clampf(z->x - G->view_w / 2.0f, 0, (float)(G->ww - G->view_w));   /* (the picture: on it) */
            G->camy = clampf(z->y - 20 - G->view_h / 2.0f, 0, (float)(G->wh - G->view_h));
        }
        A.state = ST_PLAY;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    if (!strcmp(name, "jingle")) {                         /* by a perk machine (the power on) as its jingle plays */
        Inter *m = 0;
        for (int i = 0; i < G->nit && !m; i++) if (G->it[i].type == IT_PERK && G->it[i].a != PK_KANELBULLE) m = &G->it[i];
        if (m) { p->x = m->x + 16; p->y = m->y + 16; }
        G->power_on = 1; world_power_wave(p->x, p->y); prop_lights();
        G->round = 3; G->rstate = RS_ACTIVE; G->banner_t = 0; p->aim = PI_F;
        for (int k = 0; k < 7200; k++) {                   /* until a note rises, and a little more */
            for (int i = 0; i < MAX_ZOMBIES; i++) G->z[i].alive = 0;
            G->spawn_cd = 1e9f; p->x = m ? m->x + 16 : p->x; p->y = m ? m->y + 16 : p->y;
            ticks(1, &in);
            int note = 0; for (int i = 0; i < MAX_TEXTS; i++) note |= G->ft[i].alive && G->ft[i].t < 0.6f;
            if (note) break;
        }
        A.state = ST_PLAY;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    if (!strcmp(name, "trap")) {                           /* an elstängsel on, zombies walking into it */
        Inter *trap = 0;
        for (uint64_t sd = seed; !trap && sd < seed + 40; sd++) {
            if (sd != seed) game_new(sd, season);
            for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_TRAP) trap = &G->it[i];
        }
        if (trap) {
            Inter *gap = &G->it[trap->a];
            gap->state = 1;
            for (int j = 0; j < gap->th; j++) for (int i = 0; i < gap->tw; i++) { Tile *t = tile_at(gap->tx + i, gap->ty + j); t->f &= (uint8_t)~(TF_SOLID | TF_INTER); t->inter = 0; }
            G->power_on = 1; world_repaint_rect(0, 0, G->w, G->h); prop_lights();
            trap->state = 1; trap->t = TRAP_ON;
            p->x = trap->x + 10; p->y = trap->y + 6; G->camx = (gap->tx + 1) * TS - top.w / 2; G->camy = (gap->ty + 1) * TS - top.h / 2;
            float gx = (gap->tx + gap->tw / 2.0f) * TS, gy = (gap->ty + gap->th / 2.0f) * TS;
            for (int k = 0; k < 3; k++) put(ZT_WALKER, gx - p->x + (k - 1) * 30, gy - p->y - 40 + k * 20, k);
        }
        G->round = 9; G->rstate = RS_ACTIVE; G->banner_t = 0;
        p->aim = -PI_F / 2;
        ticks(getenv("DK_SCENE_TICKS") ? atoi(getenv("DK_SCENE_TICKS")) : 20, &in); A.state = ST_PLAY;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    if (!strcmp(name, "power")) {                          /* the switch thrown, the ring of light on its way */
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_POWER) {
            Inter *it = &G->it[i]; p->x = it->x; p->y = it->y + 4; G->camx = p->x - top.w / 2; G->camy = p->y - top.h / 2;
            G->power_on = 1; world_power_wave(it->x, it->y); prop_lights();
        }
        int n = getenv("DK_SCENE_TICKS") ? atoi(getenv("DK_SCENE_TICKS")) : 20;
        G->round = 3; G->rstate = RS_ACTIVE; p->aim = PI_F / 2;
        ticks(n, &in); A.state = ST_PLAY;
        app_render(&top, &bot); shot(out);
        return 0;
    }
    G->power_on = 1; world_repaint_rect(0, 0, G->w, G->h); prop_lights();
    p->kr = 12450; p->kills = 213;
    p->w[0] = weapon_make(W_AK5, RAR_EPIC); p->w[1] = weapon_make(W_HAGEL, RAR_RARE);
    p->ar[0] = armor_make(A_HOCKEYHJALM, RAR_RARE); p->ar[1] = armor_make(A_REFLEXVAST, RAR_UNCOMMON); p->ar[1].ap *= 0.6f;
    p->perks = (1u << PK_JULMUST) | (1u << PK_SNABBKAFFE) | (1u << PK_SALMIAK); p->nperks = 3; p->maxhp = p->hp = 250; p->hp = 160;
    bag_add(C_PLASTER, 3); bag_add(C_FORBAND, 1); bag_add(C_SMALLARE, 2); bag_add(C_TEJP, 1); bag_add(C_KAFFE, 1);
    p->grenades = 3;
    G->round = 12; G->rstate = RS_ACTIVE; G->banner_t = 0;
    msg(0xffffff, "Ak 5 [Epic]"); msg(0xa0ffa0, "Picked up Plåster");
    if (!strcmp(name, "horde") || !strcmp(name, "pause") || !strcmp(name, "gameover")) {
        for (int k = 0; k < 12; k++) {
            float a = k * 0.52f, d = 40 + (k % 3) * 22;
            put(ZT_WALKER, cosf(a) * d, sinf(a) * d * 0.8f, k);
        }
        put(ZT_BRUTE, 70, -30, 0); put(ZT_BLOATER, -70, 20, 0);
        p->aim = 0.3f; in.held = BIT(B_A);
        ticks(8, &in);
        if (!strcmp(name, "gameover")) { G->over = 1; A.state = ST_GAMEOVER; A.t = 3; A.rank = 2; G->round = 14; }
        else if (!strcmp(name, "pause")) { A.state = ST_PAUSE; A.sel = 0; }
        else A.state = ST_PLAY;
    } else if (!strcmp(name, "crawl")) {                    /* after a grenade: some of them crawling */
        for (int k = 0; k < 6; k++) { Zombie *z = put(ZT_WALKER, -70 + k * 26, (k & 1) ? 35 : 55, k); z->crawl = k % 2 == 0; z->dir = 1; }
        p->aim = PI_F / 2; ticks(3, &in); A.state = ST_PLAY;
    } else if (!strcmp(name, "wolves")) {
        G->special = 1;
        for (int k = 0; k < 5; k++) put(ZT_WOLF, -60 + k * 28, -40 + (k & 1) * 70, 0);
        p->aim = -1.2f; ticks(3, &in); A.state = ST_PLAY;
    } else if (!strcmp(name, "moose")) {
        Zombie *m = put(ZT_MOOSE, 90, -10, 0); m->state = ZS_WINDUP; m->t = 0.5f; m->dir = 3;
        put(ZT_WALKER, -50, 30, 3); put(ZT_WALKER, -40, -30, 5);
        p->aim = 0; ticks(2, &in); A.state = ST_PLAY;
    } else if (!strcmp(name, "box")) {
        Inter *it = &G->it[G->box_spots[G->box_at]];
        p->x = it->x; p->y = it->y + 4; G->camx = p->x - top.w / 2; G->camy = p->y - top.h / 2;
        it->state = 2; it->b = W_STRAL; it->c = RAR_LEGENDARY; it->t = 1;
        p->aim = -PI_F / 2; ticks(2, &in); A.state = ST_PLAY;
    } else if (!strcmp(name, "pap")) {
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_PAP) {
            Inter *it = &G->it[i]; p->x = it->x; p->y = it->y + 6; it->state = 2; it->b = W_KSP58; it->c = RAR_RARE;
        }
        p->w[0].pap = 1; p->aim = -PI_F / 2; G->camx = p->x - top.w / 2; G->camy = p->y - top.h / 2;
        ticks(2, &in); A.state = ST_PLAY;
    } else if (!strcmp(name, "loot")) {
        for (int r = 0; r < RAR_COUNT; r++) {
            item_drop_weapon(weapon_make(W_PIST88 + r * 2, r), p->x - 60 + r * 28, p->y + 30);
            item_drop_armor(armor_make(A_MOSSA + r, r), p->x - 60 + r * 28, p->y + 50);
        }
        for (int c = 0; c < C_COUNT; c++) item_drop(IK_CONS, c, 0, 1, p->x - 60 + c * 18, p->y - 30);
        item_drop(IK_AMMO, 0, 0, 1, p->x + 70, p->y - 30); item_drop(IK_CASH, 0, 0, 50, p->x + 90, p->y - 30);
        for (int k = 0; k < PU_COUNT; k++) { G->pu[k].alive = 1; G->pu[k].kind = k; G->pu[k].x = p->x - 70 + k * 26; G->pu[k].y = p->y - 55; }
        p->aim = PI_F / 2; ticks(1, &in); A.state = ST_PLAY;
    }
    if (getenv("DK_SCENE_LIGHTNING")) G->lightning_t = 0.28f;   /* the instant the lightning strikes */
    app_render(&top, &bot);
    shot(out);
    return 0;
}
