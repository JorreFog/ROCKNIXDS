// bot.c: a player made of code, for the tests (and the curious: --bot). It keeps its distance and shoots, buys
// its way into the town, flips the power on, buys perks and uses the box, all through the same Input a person
// gives. It is not good, but it plays every system the game has.
#include "game.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>

static int frame;
static int goal = -1, goal_item = -1; static float goal_t, stuck_t, lastx, lasty;
static int path_x, path_y; static float path_t;
static uint16_t dist[MAPH_MAX][MAPW_MAX];

/* steps from the goal tile, through walkable tiles (the goal itself may be solid) */
static void bfs_goal(int gx, int gy) {
    static int q[MAPW_MAX * MAPH_MAX];
    for (int y = 0; y < G->h; y++) for (int x = 0; x < G->w; x++) dist[y][x] = 0xFFFF;
    int h = 0, t = 0;
    dist[gy][gx] = 0; q[t++] = gy * MAPW_MAX + gx;
    static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    while (h < t) {
        int c = q[h++], x = c % MAPW_MAX, y = c / MAPW_MAX;
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (nx < 0 || ny < 0 || nx >= G->w || ny >= G->h || dist[ny][nx] != 0xFFFF) continue;
            if (G->t[ny][nx].f & TF_SOLID) continue;
            dist[ny][nx] = (uint16_t)(dist[y][x] + 1);
            q[t++] = ny * MAPW_MAX + nx;
        }
    }
}

static int reachable_goal(float x, float y) {
    int tx = (int)(x / TS), ty = (int)(y / TS);
    bfs_goal(tx, ty);
    int px = (int)(G->p.x / TS), py = (int)((G->p.y - 2) / TS);
    return dist[py][px] != 0xFFFF;
}

static void move_toward_goal(Input *in, float gx, float gy) {
    Player *p = &G->p;
    path_t -= 1.0f / 60;
    if (path_t <= 0 || (int)(gx / TS) != path_x || (int)(gy / TS) != path_y) {
        path_x = (int)(gx / TS); path_y = (int)(gy / TS); path_t = 0.5f;
        bfs_goal(path_x, path_y);
    }
    int px = (int)(p->x / TS), py = (int)((p->y - 2) / TS);
    int bx = px, by = py; uint16_t best = dist[py][px];
    static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int k = 0; k < 4; k++) {
        int nx = px + d[k][0], ny = py + d[k][1];
        if (nx < 0 || ny < 0 || nx >= G->w || ny >= G->h) continue;
        if (dist[ny][nx] < best) { best = dist[ny][nx]; bx = nx; by = ny; }
    }
    float tx = bx * TS + 8, ty = by * TS + 12;
    if (best == 0 || (bx == px && by == py)) { tx = gx; ty = gy; }
    float dx = tx - p->x, dy = ty - p->y;
    if (fabsf(dx) > 2) in->held |= BIT(dx > 0 ? B_RIGHT : B_LEFT);
    if (fabsf(dy) > 2) in->held |= BIT(dy > 0 ? B_DOWN : B_UP);
}

static int pick_goal(void) {
    Player *p = &G->p;
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        int want = 0;
        switch (it->type) {
        case IT_POWER: want = !G->power_on; break;
        case IT_BARRIER: want = !it->state && p->kr >= it->cost + 300 && (G->zones[it->a].open || G->zones[it->b].open); break;
        case IT_PERK: want = (G->power_on || it->a == PK_KANELBULLE) && !(p->perks & (1u << it->a)) && p->nperks < PERK_LIMIT && p->kr >= PERKS[it->a].cost + 200 && !(it->a == PK_KANELBULLE && p->bulle_used >= 3); break;
        case IT_BOX:                                         /* (not with an epic or legendary gun in hand: the box would take it) */
            want = G->box_spots[G->box_at] == i && !G->box_moving && p->w[p->cur].rar < RAR_EPIC && (it->state == 2 || (it->state == 0 && p->kr >= 1600)); break;
        case IT_PAP: want = G->power_on && ((it->state == 2) || (it->state == 0 && p->kr >= 5600 && p->w[p->cur].def >= 0 && !p->w[p->cur].pap)); break;
        case IT_LOOT: want = it->prop >= 0 && G->props[it->prop].loot == 1 && dist2f(p->x, p->y, it->x, it->y) < 200 * 200; break;
        case IT_WINDOW: want = it->state < 3 && dist2f(p->x, p->y, it->x, it->y) < 120 * 120; break;
        case IT_WALLBUY: {                                   /* a gun for the empty slot, or ammo for one we have */
            int k = -1, dry = 1;
            for (int j = 0; j < p->nslots; j++) {
                if (it->a >= 0 && p->w[j].def == it->a) k = j;
                if (p->w[j].def >= 0 && (p->w[j].mag > 0 || p->w[j].reserve > 0)) dry = 0;
            }
            if (k >= 0) want = p->w[k].reserve < weapon_reserve_max(&p->w[k]) / 3 && p->kr >= (p->w[k].pap ? 4500 : it->cost / 2) + 100;
            else want = it->a >= 0 && p->kr >= it->cost + 400 && (p->w[1].def < 0 || dry);
            break;
        }
        }
        if (!want) continue;
        float ux = it->x, uy = it->y;
        if (it->type == IT_BARRIER) {                        /* stand on the open side */
            Zone *za = &G->zones[it->a];
            int side_a = za->open;
            (void)side_a;
        }
        float d = sqrtf(dist2f(p->x, p->y, ux, uy));
        if (it->type == IT_POWER) d *= 0.3f;
        if (it->type == IT_PERK || it->type == IT_PAP) d *= 0.5f;
        if (d < bd && reachable_goal(ux, uy)) { bd = d; best = i; }
    }
    return best;
}

static int bot_fire(void) { return btn_fire() >= 0 ? btn_fire() : B_A; }   /* the twin layout: A fires to the right */

void bot_input(Input *in) {
    frame++;
    memset(in, 0, sizeof *in);
    int tap = frame & 1;
    switch (A.state) {
    case ST_SPLASH: if (A.t > 0.5f && tap) in->held |= BIT(btn_ok()); return;
    case ST_TITLE: if (A.t > 0.5f && tap) in->held |= BIT(btn_ok()); return;
    case ST_GAMEOVER: if (A.t > 2.5f && tap) in->held |= BIT(btn_ok()); return;
    case ST_NAME: if (tap) in->held |= BIT(btn_ok()); return;
    case ST_SCORES: if (A.t > 1.0f && tap) in->held |= BIT(btn_ok()); return;          /* back to the title, and again */
    case ST_PAUSE: if (tap) in->held |= BIT(B_START); return;
    default: break;
    }
    if (A.state != ST_PLAY) return;
    Player *p = &G->p;
    /* the nearest zombie in sight */
    Zombie *tgt = 0; float td = 1e9f; int crowd = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!zombie_hittable(z)) continue;
        float d = sqrtf(dist2f(z->x, z->y, p->x, p->y));
        if (d < 70) crowd++;
        if (d < td && d < 240 && shot_clear(p->x, p->y - 8, z->x, z->y - 8)) { td = d; tgt = z; }
    }
    if (G->boss.on) {                                       /* a boss in sight comes first */
        Zombie *b = &G->z[G->boss.zi];
        float d = sqrtf(dist2f(b->x, b->y, p->x, p->y));
        if (b->state != ZS_RISE && b->state != ZS_DEAD && !G->boss.hidden && d < 260 && shot_clear(p->x, p->y - 8, b->x, b->y - 8) && (!tgt || td > 40)) { tgt = b; td = d; }
    }
    if (tgt) {
        in->mouse = 1; in->mx = tgt->x - G->camx; in->my = tgt->y - 8 - G->camy;
        const Weapon *cw = &p->w[p->cur];
        if (!(cw->def >= 0 && cw->mag == 0 && cw->reserve == 0 && tap)) in->held |= BIT(bot_fire());   /* (a dry gun: pull again, and the game hands over a loaded one) */
        /* keep some distance: back away, sliding along walls */
        if (td < 80) {
            float ax = p->x - tgt->x, ay = p->y - tgt->y, l = sqrtf(ax * ax + ay * ay) + 0.01f;
            ax /= l; ay /= l;
            for (int k = 0; k < 8; k++) {
                float a = atan2f(ay, ax) + (k & 1 ? 1 : -1) * (k / 2) * 0.6f;
                float nx = p->x + cosf(a) * 14, ny = p->y + sinf(a) * 14;
                if (!solid_at((int)(nx / TS), (int)(ny / TS))) { ax = cosf(a); ay = sinf(a); break; }
            }
            if (ax > 0.3f) in->held |= BIT(B_RIGHT); else if (ax < -0.3f) in->held |= BIT(B_LEFT);
            if (ay > 0.3f) in->held |= BIT(B_DOWN); else if (ay < -0.3f) in->held |= BIT(B_UP);
            if (td < 30 && p->melee_cd <= 0 && (frame % 3) == 0 && bind_of(ACT_KNIFE) >= 0) in->held |= BIT(bind_of(ACT_KNIFE));
        }
        if (crowd >= 5 && p->grenades > 0 && (frame % 60) == 0) in->held |= BIT(bind_of(ACT_GRENADE));
    }
    /* health: use something from the bag */
    if (p->hp < p->maxhp * 0.4f && (frame % 30) == 0) {
        for (int i = 0; i < BAG_SLOTS; i++) if (p->bag[i].id == C_FORBAND || p->bag[i].id == C_PLASTER) { p->bag_sel = i; if (bind_of(ACT_ITEM) >= 0) in->held |= BIT(bind_of(ACT_ITEM)); break; }
    }
    int dry = 1;                                            /* out of ammo: go and get some even with them around */
    for (int j = 0; j < p->nslots; j++) if (p->w[j].def >= 0 && (p->w[j].mag > 0 || p->w[j].reserve > 0)) dry = 0;
    if (!tgt || td > 140 || dry) {
        /* a goal: power, doors, perks, the box, loot */
        goal_t -= 1.0f / 60;
        if (goal < 0 || goal_t <= 0) { goal = pick_goal(); goal_t = 3; }
        if (goal >= 0) {
            Inter *it = &G->it[goal];
            float ux = it->x, uy = it->y;
            if (it->type == IT_BARRIER) {                    /* the side of the barrier you're on */
                int z = zone_at(p->x, p->y);
                Zone *zn = &G->zones[z];
                float cx = it->tx * TS + it->tw * 8.0f, cy = it->ty * TS + it->th * 8.0f;
                if (cx < zn->x * TS) ux = cx + 24; else if (cx > (zn->x + zn->w) * TS - 32) ux = cx - 24; else ux = cx;
                if (cy < zn->y * TS) uy = cy + 26; else if (cy > (zn->y + zn->h) * TS - 32) uy = cy - 22; else uy = cy;
            }
            float d = sqrtf(dist2f(p->x, p->y, ux, uy));
            if (d > 10) move_toward_goal(in, ux, uy);
            if (inter_target() == goal) d = 0;              /* close enough to use it */
            if (d < 20) {
                if (it->type == IT_WINDOW || it->type == IT_LOOT) in->held |= BIT(btn_use());
                else if (tap) in->held |= BIT(btn_use());
                if (it->type != IT_WINDOW && it->type != IT_LOOT && it->type != IT_BOX && (frame % 20) == 0) goal = -1;
                if (it->type == IT_BOX && it->state == 2 && tap && it->c >= p->w[p->cur].rar) { in->held |= BIT(btn_use()); }   /* (only a gun as good) */
                if (it->type == IT_WINDOW && it->state >= 6) goal = -1;
                if (it->type == IT_LOOT && G->props[it->prop].loot != 1) goal = -1;
            }
            /* stuck: give up on it for a while */
            stuck_t += 1.0f / 60;
            if (stuck_t > 2) { if (dist2f(p->x, p->y, lastx, lasty) < 16 * 16 && d > 20) goal = -1; stuck_t = 0; lastx = p->x; lasty = p->y; }
        } else if (!tgt) {
            /* wander toward the zone's centre */
            Zone *zn = &G->zones[zone_at(p->x, p->y)];
            move_toward_goal(in, zn->cx * TS + 8, zn->cy * TS + 12);
        }
        /* pick up weapons and armour lying around */
        for (int i = 0; i < MAX_ITEMS; i++) {
            Item *it = &G->items[i];
            if (!it->alive || (it->kind != IK_WEAPON && it->kind != IK_ARMOR)) continue;
            if (dist2f(it->x, it->y, p->x, p->y) < 14 * 14 && tap && (it->kind == IK_ARMOR ? p->ar[ARMORS[it->ar.def].slot].def < 0 || it->ar.rar > p->ar[ARMORS[it->ar.def].slot].rar : it->w.rar >= RAR_RARE)) in->held |= BIT(btn_use());
        }
    }
    /* never the box with an epic or legendary gun in hand, whatever B was meant for: the box would take the gun */
    int t = inter_target();
    if ((in->held & BIT(btn_use())) && t >= 0 && G->it[t].type == IT_BOX && p->w[p->cur].rar >= RAR_EPIC) in->held &= ~BIT(btn_use());
    (void)goal_item;
}

/* --monkey: random buttons, sticks and touches on both screens, held for random stretches, through every screen
   (title, settings, the name, pause, play). It finds the crashes no sensible player would: the tests run it under
   the sanitizers. (QUIT on the title is ignored for it.) */
static uint64_t mk;
static uint32_t mk_next(void) { mk ^= mk << 13; mk ^= mk >> 7; mk ^= mk << 17; return (uint32_t)(mk >> 11); }
static int mk_int(int n) { return (int)(mk_next() % (uint32_t)n); }
void monkey_input(Input *in, uint64_t seed) {
    static int hold[B_COUNT], touch_t[2], stick_t, mouse_t;
    static Input m;
    if (!mk) mk = seed * 0x9E3779B97F4A7C15ull + 1;
    frame++;
    /* the menus and pause come up less often than shooting and walking */
    static const int every[B_COUNT] = { 40, 40, 40, 40, 25, 60, 90, 90, 120, 120, 200, 200, 600, 500, 900, 300 };   /* (the last: the stick's click) */
    for (int b = 0; b < B_COUNT; b++) {
        if (hold[b] > 0) { if (--hold[b] == 0) m.held &= ~BIT(b); continue; }
        if (mk_int(every[b]) == 0) { hold[b] = 1 + mk_int(mk_int(4) ? 6 : 90); m.held |= BIT(b); }
    }
    if (--stick_t <= 0) {
        stick_t = 1 + mk_int(60);
        m.has_sticks = mk_int(3) != 0;
        m.lx = m.has_sticks && mk_int(2) ? (mk_int(201) - 100) / 100.0f : 0;
        m.ly = m.has_sticks && mk_int(2) ? (mk_int(201) - 100) / 100.0f : 0;
        m.rx = m.has_sticks && mk_int(2) ? (mk_int(201) - 100) / 100.0f : 0;
        m.ry = m.has_sticks && mk_int(2) ? (mk_int(201) - 100) / 100.0f : 0;
    }
    for (int s = 0; s < 2; s++) {
        if (touch_t[s] > 0) {
            touch_t[s]--;
            if (m.touch[s] && mk_int(4) == 0) { m.tx[s] += mk_int(9) - 4; m.ty[s] += mk_int(9) - 4; }   /* a drag */
            if (!touch_t[s]) m.touch[s] = 0;
        } else if (mk_int(s ? 30 : 400) == 0) {
            int w = s ? A.bot_w : G->view_w, h = s ? A.bot_h : G->view_h;
            touch_t[s] = 1 + mk_int(mk_int(3) ? 8 : 120);
            m.touch[s] = 1; m.tx[s] = mk_int(w + 8) - 4; m.ty[s] = mk_int(h + 8) - 4;   /* (a little off the edges too) */
        }
    }
    if (--mouse_t <= 0) {
        mouse_t = 1 + mk_int(90);
        m.mouse = mk_int(3) == 0;
        m.mx = (float)(mk_int(G->view_w + 40) - 20); m.my = (float)(mk_int(G->view_h + 40) - 20);
        m.last_kbd = mk_int(2);
    }
    *in = m;
    in->quit = 0;
}
