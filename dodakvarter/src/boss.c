// boss.c: the bosses. Every twentieth round one of the old stories comes up out of the ground of the suburb, a
// health bar over the town, in turn: Draugen, the dead Viking chieftain (round 20), Bergatrollet, the mountain troll
// (40), Näcken, the fiddler in the water (60), and Lindormen, the lindworm (80); then again, stronger. Each is a
// zombie to everything else (shot, burnt, frozen, counted), with its own moves:
//   Draugen     cleaves with his axe, leaps onto you (a shockwave where he lands), blows his horn and the dead rise
//   Bergatrollet  smashes with its club, pounds the ground (three rings of shock), throws boulders; enraged at half
//   Näcken      plays: spirals of notes; beckons: you are drawn to him; sinks and comes up again beside you
//   Lindormen   bites, spits venom that pools, burrows under the asphalt and bursts up beneath you
// Insta-Kill doesn't kill them, Kaboom only hurts them, they don't care for firecrackers, and the elstängsel only
// burns them. Killing one pays well and leaves a legendary weapon.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

enum { B_CHASE, B_WINDUP, B_STRIKE, B_RECOVER, B_LEAP, B_AIR, B_HORN, B_POUND, B_THROW, B_ROAR, B_PLAY, B_BECKON,
       B_DIVE, B_UNDER, B_EMERGE, B_SPIT };

static const struct {
    const char *name, *title;           /* the banner: its name, and who it is (translated) */
    float hp, speed, r, h;              /* health (times the round's boss health), speed, bullet radius, height */
    uint32_t eye, col;                  /* its eyes; its colour on the map and the bar */
} BD[BOSS_COUNT] = {
    { "Draugen", "the dead chieftain", 1.0f, 34, 12, 46, 0x9ae8ff, 0x7ab8d8 },
    { "Bergatrollet", "the mountain troll", 1.2f, 26, 15, 54, 0xffb030, 0xc0a060 },
    { "Näcken", "the fiddler in the water", 0.85f, 26, 11, 46, 0xd8ff60, 0x60c8a0 },
    { "Lindormen", "the lindworm", 1.1f, 46, 12, 18, 0xff4020, 0x70b040 },
};
const char *boss_name(int kind) { return kind >= 0 && kind < BOSS_COUNT ? BD[kind].name : "?"; }

int boss_kind_for_round(int r) { return r >= 20 && r % 20 == 0 ? (r / 20 - 1) % BOSS_COUNT : -1; }

static Zombie *boss_z(void) {
    Boss *b = &G->boss;
    if (!b->on || b->zi < 0 || b->zi >= MAX_ZOMBIES) return 0;
    Zombie *z = &G->z[b->zi];
    return z->alive && z->type == ZT_BOSS ? z : 0;
}

/* ---------------------------------------------------------------- coming up */
void boss_round_start(int r) {
    int k = boss_kind_for_round(r);
    if (k < 0) return;
    if (getenv("DK_DEBUG_BOSS")) k = atoi(getenv("DK_DEBUG_BOSS")) % BOSS_COUNT;   /* tests: this one */
    Boss *b = &G->boss;
    memset(b, 0, sizeof *b);
    b->kind = k; b->zi = -1;
    G->banner_t = 0;                                        /* (its name over whatever was up) */
    char up[48]; snprintf(up, sizeof up, "%s", BD[k].name);
    for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32; else if ((unsigned char)*c == 0xc3 && (unsigned char)c[1] >= 0xa0) c[1] -= 0x20;
    banner(0xd02020, up, tr(BD[k].title));
    G->banner_t = 4.0f; b->pending = 1;
    sfx(SFX_ROAR, 1, 0);
    shake(4);
}

/* where it comes up: an open spawn, not too close and not too far, that the player can be reached from */
#define FALL 1.6f                                       /* (its fall, before it lies still: seconds) */

/* in sight as it comes up: where the view will be, ahead of your aim and leaning its way (as camera_update has it,
   held at the town's edge), with all of it in the picture */
static int in_sight(float sx, float sy) {
    const Player *p = &G->p;
    float mx = G->view_w / 2.0f - 30, my = G->view_h / 2.0f - 30;
    float vx = clampf(p->x + cosf(p->aim) * 26 + clampf((sx - p->x) * 0.5f, -mx, mx) - G->view_w / 2.0f, 0, (float)(G->ww - G->view_w));
    float vy = clampf(p->y - 8 + sinf(p->aim) * 21 + clampf((sy - 20 - p->y) * 0.5f, -my, my) - G->view_h / 2.0f, 0, (float)(G->wh - G->view_h));
    return sx - vx > 28 && sx - vx < G->view_w - 28 && sy - vy > 66 && sy - vy < G->view_h - 24 &&
           p->x - vx > 12 && p->x - vx < G->view_w - 12 && p->y - vy > 24 && p->y - vy < G->view_h - 8;
}
static int open_ground(float x, float y) {                  /* room for it, and a way from there to you */
    int tx = (int)(x / TS), ty = (int)((y - 2) / TS);
    if (tx < 1 || ty < 1 || tx >= G->w - 1 || ty >= G->h - 1 || G->flow[ty][tx] == 0xFFFF) return 0;
    for (int j = -1; j <= 0; j++) for (int i = -1; i <= 1; i++) if (solid_at(tx + i, ty + j)) return 0;
    return 1;
}

/* where it comes up: a spawn about 150 px away that you'll see; failing that, open ground in front of you */
static int boss_spot(float *x, float *y) {
    Player *p = &G->p;
    int best = -1; float bscore = 1e9f;
    for (int i = 0; i < G->nspawns; i++) {
        Spawn *s = &G->spawns[i];
        if (!G->zones[s->zone].open) continue;
        float sx = s->x, sy = s->y + (s->type == SP_WINDOW ? TS : 0);
        int tx = (int)(sx / TS), ty = (int)((sy - 2) / TS);
        if (tx < 0 || ty < 0 || tx >= G->w || ty >= G->h || solid_at(tx, ty) || G->flow[ty][tx] == 0xFFFF) continue;
        float d = sqrtf(dist2f(sx, sy, p->x, p->y));
        float score = fabsf(d - 150) + (d < 90 ? 400 : 0) + (in_sight(sx, sy) ? 0 : 150);
        if (score < bscore) { bscore = score; best = i; *x = sx; *y = sy; }
    }
    if (best >= 0 && bscore < 100) return best;
    for (int r = 0; r < 4; r++)                             /* out of the ground: ahead of you first, then around */
        for (int a = 0; a < 24; a++) {
            float ang = p->aim + (a & 1 ? 1 : -1) * ((a + 1) / 2) * (2 * PI_F / 24), d = 140 - r * 15;
            float gx = p->x + cosf(ang) * d, gy = p->y + sinf(ang) * d * 0.75f;
            if (open_ground(gx, gy) && in_sight(gx, gy)) { *x = gx; *y = gy; return 0; }
        }
    return best;
}

static void boss_appear(void) {
    Boss *b = &G->boss;
    float x, y;
    if (boss_spot(&x, &y) < 0) { x = G->p.x + 60; y = G->p.y; unstick_actor(&x, &y, 6); }
    Zombie *z = zombie_at_spot(ZT_BOSS, x, y);
    if (!z) return;
    /* about half a minute of a good gun at round 20; it grows slower than the zombies' (they're ten times as hard
       by round 40, a boss three) */
    float hp = 40000.0f * powf(G->round / 20.0f, 1.6f) * BD[b->kind].hp;
    if (getenv("DK_DEBUG_BOSS_HP")) hp *= (float)atof(getenv("DK_DEBUG_BOSS_HP"));
    z->hp = z->maxhp = hp;
    z->speed = BD[b->kind].speed; z->variant = b->kind;
    z->t = 2.2f;                                            /* rising out of the ground */
    b->on = 1; b->pending = 0; b->zi = (int)(z - G->z); b->chip = z->hp; b->bar = 0;
    b->cd[0] = 1; b->cd[1] = 3; b->cd[2] = 6; b->cd[3] = 5;
    b->face = 0; b->trail_head = 0; b->trail_d = 0;
    for (int i = 0; i < BOSS_TRAIL; i++) { b->trail[i][0] = x; b->trail[i][1] = y; }
    shake(7);
    spawn_parts(PT_DUST, x, y, 30, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 60);
    sfx_at(SFX_SLAM, x, y, 1); sfx(SFX_ROAR, 0.8f, 0);
    plat_log("boss %s up in round %d with %.0f hp", BD[b->kind].name, G->round, z->hp);
}

void boss_update_round(float dt) {
    Boss *b = &G->boss;
    (void)dt;
    if (b->pending && G->rtime > 3.0f) boss_appear();
    Zombie *z = boss_z();
    if (b->on && z) {
        if (z->state != ZS_RISE && music_now() == MUS_NONE) music_play(MUS_BOSS);   /* (again after the box's tune or a found song) */
        if ((int)(G->rtime / 15) != (int)((G->rtime - dt) / 15))   /* (the log: how the fight goes) */
            plat_log("boss %s: %.0f of %.0f hp, doing %d, %.0f px away", BD[b->kind].name, z->hp, z->maxhp, b->st, sqrtf(dist2f(z->x, z->y, G->p.x, G->p.y)));
        if (b->bar < 1) b->bar = MIN(1.0f, b->bar + dt * 0.8f);
        if (b->chip > z->hp) b->chip = MAX(z->hp, b->chip - z->maxhp * dt * 0.25f);
    } else if (b->bar > 0 && !b->on) b->bar = MAX(0.0f, b->bar - dt);
}

/* ---------------------------------------------------------------- hazards */
static Hazard *hz_new(int kind, float x, float y) {
    for (int i = 0; i < MAX_HAZARDS; i++) if (!G->hz[i].alive) {
        Hazard *h = &G->hz[i]; memset(h, 0, sizeof *h);
        h->alive = 1; h->kind = kind; h->x = h->x0 = x; h->y = h->y0 = y;
        return h;
    }
    return 0;
}
static void ring(float x, float y, float r, float dur, float dmg, int water) {
    Hazard *h = hz_new(HZ_RING, x, y);
    if (h) { h->r = r; h->dur = dur; h->dmg = dmg; h->a = (float)water; }
}
/* something thrown: from (x0, y0) at height z0 to (x, y) in dur seconds */
static void lob(int kind, float x0, float y0, float x, float y, float dur, float r, float dmg) {
    Hazard *h = hz_new(kind, x, y);
    if (!h) return;
    h->x0 = x0; h->y0 = y0; h->dur = dur; h->r = r; h->dmg = dmg;
}

static void hurt_near(float x, float y, float r, float dmg) {
    Player *p = &G->p;
    if (!p->downed && dist2f(p->x, p->y, x, y) < r * r) player_hurt(dmg, x, y);
}

void hazards_update(float dt) {
    Player *p = &G->p;
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive) continue;
        h->t += dt;
        float k = h->dur > 0 ? h->t / h->dur : 1;
        switch (h->kind) {
        case HZ_RING: {                                     /* a shockwave: it hurts once, where its edge passes you */
            float rad = h->r * MIN(1.0f, k), d = sqrtf(dist2f(p->x, p->y, h->x, h->y));
            if (!h->hit && h->dmg > 0 && !p->downed && d > rad - 7 && d < rad + 3) { player_hurt(h->dmg, h->x, h->y); h->hit = 1; }
            if (k >= 1) h->alive = 0;
            break;
        }
        case HZ_ROCK: case HZ_VENOM:                        /* through the air, then down */
            if (k >= 1) {
                if (h->kind == HZ_ROCK) {
                    hurt_near(h->x, h->y, h->r, h->dmg);
                    spawn_parts(PT_DUST, h->x, h->y, 14, 0x8a8478, 70);
                    spawn_parts(PT_GIB, h->x, h->y - 2, 8, 0x6a665e, 80);
                    shake(4); sfx_at(SFX_SLAM, h->x, h->y, 0.8f);
                    h->alive = 0;
                } else {                                    /* the venom splashes into a pool */
                    hurt_near(h->x, h->y, 10, h->dmg);
                    h->kind = HZ_POOL; h->t = 0; h->dur = 6.5f; h->r = 16; h->a = 0;
                    spawn_parts(PT_GAS, h->x, h->y - 2, 6, 0x7ad040, 20);
                }
            }
            break;
        case HZ_NOTE: {                                     /* Näcken's notes fly straight; a wall stops them */
            h->x += h->vx * dt; h->y += h->vy * dt;
            if (solid_at((int)(h->x / TS), (int)(h->y / TS)) || k >= 1) { h->alive = 0; break; }
            if (!p->downed && dist2f(p->x, p->y, h->x, h->y) < 8 * 8) { player_hurt(h->dmg, h->x, h->y); h->alive = 0; }
            break;
        }
        case HZ_POOL:                                       /* standing in it burns: every half second */
            h->a -= dt;
            if (!p->downed && h->a <= 0 && dist2f(p->x, p->y, h->x, h->y) < h->r * h->r) { player_hurt(h->dmg, h->x, h->y); h->a = 0.5f; }
            if (rng_chance(&G->fx, dt * 3)) spawn_parts(PT_GAS, h->x + rng_rangef(&G->fx, -h->r, h->r), h->y, 1, 0x7ad040, 6);
            if (k >= 1) h->alive = 0;
            break;
        default:                                            /* (the axe's swing: only seen) */
            if (k >= 1) h->alive = 0;
            break;
        }
    }
}

/* ---------------------------------------------------------------- moving */
static void walk(Zombie *z, float speed, float dt) {
    Boss *b = &G->boss;
    float dx, dy;
    zombie_steer(z, &dx, &dy);
    float slow = z->slow > 0 ? 0.6f : 1.0f;
    if (b->enraged) speed *= 1.3f;
    /* keep the small ones out from under its feet */
    float sx = 0, sy = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *o = &G->z[i];
        if (o == z || !o->alive || o->state == ZS_DEAD || o->state == ZS_RISE) continue;
        float ox = z->x - o->x, oy = z->y - o->y, d2 = ox * ox + oy * oy;
        if (d2 < 18 * 18 && d2 > 0.01f) { float d = sqrtf(d2); sx += ox / d * (18 - d) / 18 * 0.3f; sy += oy / d * (18 - d) / 18 * 0.3f; }
    }
    unstick_actor(&z->x, &z->y, 6);
    move_actor(&z->x, &z->y, (dx + sx) * speed * slow * dt, (dy + sy) * speed * slow * dt, 6);
    int step = (int)z->anim;
    z->anim += dt * speed / 14.0f * slow;
    if (b->kind == BOSS_TROLL && (int)z->anim != step) {   /* the troll's steps: you feel them */
        float d2 = dist2f(z->x, z->y, G->p.x, G->p.y);
        if (d2 < 160 * 160) shake(d2 < 80 * 80 ? 1.5f : 0.8f);
        sfx_at(SFX_SLAM, z->x, z->y, 0.25f);
        spawn_parts(PT_DUST, z->x + (step & 1 ? -8 : 8), z->y, 3, G->season == SEASON_WINTER ? 0xe8eef4 : 0x6a5a4a, 15);
    }
    if (fabsf(dx) > 0.2f) b->face = dx < 0 ? PI_F : 0;
    /* lost: it won't reach you from where it is */
    z->stuck_t += dt;
    if (z->stuck_t > 6) {
        if (dist2f(z->x, z->y, z->lastx, z->lasty) < 14 * 14 && dist2f(z->x, z->y, G->p.x, G->p.y) > 60 * 60) boss_stuck(z);
        z->stuck_t = 0; z->lastx = z->x; z->lasty = z->y;
    }
}

void boss_stuck(Zombie *z) {
    Boss *b = &G->boss;
    float x, y;
    if (boss_spot(&x, &y) < 0) return;
    z->x = x; z->y = y; z->state = ZS_RISE; z->t = 1.2f; b->st = B_CHASE; b->hidden = 0; b->h = 0;
    for (int i = 0; i < BOSS_TRAIL; i++) { b->trail[i][0] = x; b->trail[i][1] = y; }
    spawn_parts(PT_DUST, x, y, 20, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 50);
    plat_log("boss %s lost its way: up again at %.0f,%.0f", BD[b->kind].name, x, y);
}

static void face_player(Zombie *z) { G->boss.face = G->p.x < z->x ? PI_F : 0; }
/* the player in front of it, within reach */
static int in_reach(Zombie *z, float reach) {
    Player *p = &G->p;
    if (p->downed) return 0;
    float dx = p->x - z->x, dy = p->y - z->y;
    if (dx * dx + dy * dy > reach * reach) return 0;
    float fx = cosf(G->boss.face);
    return dx * fx > -10;                                   /* (not behind its back) */
}
static void set(Boss *b, int st, float t) { b->st = st; b->t = t; b->n = 0; }

/* ---------------------------------------------------------------- Draugen */
static void draugen(Zombie *z, Boss *b, float pd, float dt) {
    Player *p = &G->p;
    switch (b->st) {
    case B_CHASE:
        if (pd < 30 && b->cd[0] <= 0) { face_player(z); set(b, B_WINDUP, b->enraged ? 0.4f : 0.55f); return; }
        if (pd > 70 && pd < 210 && b->cd[1] <= 0 && line_clear(z->x, z->y - 10, p->x, p->y - 8)) {
            face_player(z); set(b, B_LEAP, 0.5f); b->tx = p->x; b->ty = p->y; return;
        }
        if (b->cd[2] <= 0 && pd < 260) { face_player(z); set(b, B_HORN, 1.7f); sfx_at(SFX_HORN, z->x, z->y, 1); return; }
        walk(z, z->speed, dt);
        return;
    case B_WINDUP:
        if (b->t <= 0) {
            set(b, B_STRIKE, 0.4f);
            sfx_at(SFX_SWOOSH, z->x, z->y, 0.9f);
            Hazard *h = hz_new(HZ_CLEAVE, z->x + cosf(b->face) * 14, z->y - 14); if (h) { h->dur = 0.25f; h->a = b->face; }
            if (in_reach(z, 38)) player_hurt(85, z->x, z->y);
            b->cd[0] = 1.6f;
        }
        return;
    case B_LEAP:
        if (b->t <= 0) { set(b, B_AIR, 0.75f); b->sx = z->x; b->sy = z->y; b->tx = p->x; b->ty = p->y; sfx_at(SFX_SWOOSH, z->x, z->y, 0.7f); }
        return;
    case B_AIR: {
        float k = 1 - b->t / 0.75f;
        z->x = lerpf(b->sx, b->tx, MIN(1.0f, k)); z->y = lerpf(b->sy, b->ty, MIN(1.0f, k));
        b->h = sinf(PI_F * MIN(1.0f, k)) * 44;
        if (b->t <= 0) {                                    /* down: a ring of shock through the ground */
            b->h = 0; unstick_actor(&z->x, &z->y, 6);
            ring(z->x, z->y, 74, 0.5f, 45, 0);
            hurt_near(z->x, z->y, 24, 90);
            shake(8); sfx_at(SFX_SLAM, z->x, z->y, 1);
            spawn_parts(PT_DUST, z->x, z->y, 24, G->season == SEASON_WINTER ? 0xe8eef4 : 0x6a5a4a, 90);
            set(b, B_RECOVER, 0.7f); b->cd[1] = 7;
        }
        return;
    }
    case B_HORN:                                            /* the horn: the dead around him rise */
        if (b->t < 1.1f && b->n == 0) {
            b->n = 1;
            int want = b->enraged ? 5 : 3, alive = 0;
            for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive && G->z[i].state != ZS_DEAD && G->z[i].type != ZT_BOSS) alive++;
            for (int k = 0; k < want && alive < 14; k++) {
                float a = rng_rangef(&G->rng, 0, 2 * PI_F), d = rng_rangef(&G->rng, 22, 46);
                float x = z->x + cosf(a) * d, y = z->y + sinf(a) * d * 0.7f;
                if (solid_at((int)(x / TS), (int)((y - 2) / TS))) continue;
                Zombie *w = zombie_at_spot(ZT_WALKER, x, y);
                if (w) { w->hp = w->maxhp = w->maxhp * 0.35f; alive++; b->summoned++; }   /* (old bones: they break sooner) */
            }
            spawn_parts(PT_FROST, z->x, z->y - 30, 30, 0x9ae8ff, 60);
            G->flash_t = 0.1f;
        }
        if (b->t <= 0) { set(b, B_CHASE, 0); b->cd[2] = 22; }
        return;
    default:
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
}

/* ---------------------------------------------------------------- Bergatrollet */
static void troll(Zombie *z, Boss *b, float pd, float dt) {
    Player *p = &G->p;
    switch (b->st) {
    case B_CHASE:
        if (pd < 34 && b->cd[0] <= 0) { face_player(z); set(b, B_WINDUP, b->enraged ? 0.5f : 0.7f); return; }
        if (pd < 115 && b->cd[2] <= 0) { face_player(z); set(b, B_POUND, 0.9f); return; }
        if (pd > 70 && pd < 280 && b->cd[1] <= 0 && shot_clear(z->x, z->y - 30, p->x, p->y - 8)) { face_player(z); set(b, B_THROW, 0.8f); return; }
        walk(z, z->speed, dt);
        return;
    case B_WINDUP:
        if (b->t <= 0) {
            set(b, B_STRIKE, 0.5f);
            float hx = z->x + cosf(b->face) * 18, hy = z->y + 2;
            if (in_reach(z, 42)) player_hurt(100, z->x, z->y);
            ring(hx, hy, 44, 0.35f, 30, 0);
            shake(6); sfx_at(SFX_SLAM, z->x, z->y, 1);
            spawn_parts(PT_DUST, hx, hy, 16, 0x6a5a4a, 70);
            b->cd[0] = 2.0f;
        }
        return;
    case B_POUND:                                           /* three slams, three rings */
        if (b->t <= 0) {
            b->n++;
            float hx = z->x + cosf(b->face) * 16;
            ring(hx, z->y + 2, 100, 0.95f, 40, 0);
            shake(5); sfx_at(SFX_SLAM, z->x, z->y, 0.9f);
            spawn_parts(PT_DUST, hx, z->y, 14, 0x6a5a4a, 60);
            if (b->n < 3) b->t = 0.5f;
            else { int n = b->n; set(b, B_RECOVER, 0.7f); b->n = n; b->cd[2] = b->enraged ? 7 : 9; }
        }
        return;
    case B_THROW:                                           /* a boulder over its head, then at you */
        if (b->t <= 0) {
            int rocks = b->enraged ? 2 : 1;
            for (int k = 0; k < rocks; k++) {
                float tx = p->x + p->vx * 0.5f + (k ? rng_rangef(&G->rng, -40, 40) : 0), ty = p->y + p->vy * 0.5f + (k ? rng_rangef(&G->rng, -30, 30) : 0);
                float d = sqrtf(dist2f(z->x, z->y, tx, ty));
                lob(HZ_ROCK, z->x, z->y - 2, tx, ty, 0.75f + d / 420.0f + k * 0.15f, 22, 70);
            }
            sfx_at(SFX_SWOOSH, z->x, z->y, 0.9f);
            set(b, B_RECOVER, 0.45f); b->cd[1] = b->enraged ? 3.2f : 4.5f;
        }
        return;
    default:
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
}

/* ---------------------------------------------------------------- Näcken */
static void nacken(Zombie *z, Boss *b, float pd, float dt) {
    Player *p = &G->p;
    switch (b->st) {
    case B_CHASE: {
        int clear = walk_clear(z->x, z->y, p->x, p->y, 3);   /* (his notes go along the ground: nothing in the way) */
        if (pd < 24 && b->cd[0] <= 0) { face_player(z); set(b, B_WINDUP, 0.35f); return; }
        if (b->cd[3] <= 0 && (pd < 50 || pd > 230 || !clear)) { set(b, B_DIVE, 0.8f); sfx_at(SFX_SPLASH, z->x, z->y, 0.8f); b->cd[3] = 9; return; }
        if (clear && pd < 230 && b->cd[1] <= 0) {
            face_player(z); set(b, B_PLAY, 3.2f); b->ang = rng_rangef(&G->rng, 0, 2 * PI_F); b->t2 = 0;
            sfx_at(SFX_FIDDLE, z->x, z->y, 1); b->cd[1] = 6; return;
        }
        if (clear && pd > 70 && pd < 190 && b->cd[2] <= 0) {
            face_player(z); set(b, B_BECKON, 2.6f); sfx_at(SFX_FIDDLE, z->x, z->y, 0.7f); b->cd[2] = 12; return;
        }
        if (pd > 120 || !clear) walk(z, z->speed, dt);      /* he likes you at a little distance, in sight */
        else { z->anim += dt * 2; face_player(z); }
        return;
    }
    case B_WINDUP:                                          /* long cold fingers */
        if (b->t <= 0) { if (in_reach(z, 30)) player_hurt(45, z->x, z->y); set(b, B_RECOVER, 0.4f); b->cd[0] = 1.4f; }
        return;
    case B_PLAY: {                                          /* his song: fans of notes sweeping across you */
        b->t2 -= dt;
        z->anim += dt * 6;
        if (b->t2 <= 0) {
            b->t2 = 0.12f;
            float base = atan2f(p->y - z->y, p->x - z->x), sw = sinf(b->ang) * 0.8f;
            int arms = b->enraged ? 4 : 3;
            for (int k = 0; k < arms; k++) {
                float a = base + (k == 0 ? sw : k == 1 ? -sw : k == 2 ? sw * 0.35f : -sw * 0.35f);
                Hazard *h = hz_new(HZ_NOTE, z->x + cosf(a) * 8, z->y + sinf(a) * 6);   /* (on the ground; drawn floating) */
                if (h) { h->vx = cosf(a) * 78; h->vy = sinf(a) * 78; h->dur = 3.0f; h->dmg = 12; h->a = (float)(k & 1); }
            }
            b->ang += 0.42f; b->n++;
        }
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
    case B_BECKON: {                                        /* drawn to him; reach him and he takes you */
        float dx = z->x - p->x, dy = z->y - p->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
        if (!p->downed) move_actor(&p->x, &p->y, dx / d * 38 * dt, dy / d * 38 * dt, 5);
        if (d < 20 && b->n == 0) { b->n = 1; player_hurt(60, z->x, z->y); b->t = MIN(b->t, 0.3f); }
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
    case B_DIVE:                                            /* into the water... */
        if (b->t <= 0) {
            set(b, B_UNDER, 1.1f); b->hidden = 1;
            float bx = p->x, by = p->y;
            for (int k = 0; k < 8; k++) {                   /* ...and up again a little way from you */
                float a = rng_rangef(&G->rng, 0, 2 * PI_F), d = rng_rangef(&G->rng, 34, 60);
                float x = p->x + cosf(a) * d, y = p->y + sinf(a) * d * 0.8f;
                if (!solid_at((int)(x / TS), (int)((y - 2) / TS)) && walk_clear(p->x, p->y, x, y, 4)) { bx = x; by = y; break; }
            }
            b->tx = bx; b->ty = by;
        }
        return;
    case B_UNDER:
        if (b->t <= 0) {
            z->x = b->tx; z->y = b->ty; unstick_actor(&z->x, &z->y, 6);
            b->hidden = 0; face_player(z);
            ring(z->x, z->y, 48, 0.45f, 40, 1);
            spawn_parts(PT_WATER, z->x, z->y - 6, 26, 0x8ac8ff, 80);
            sfx_at(SFX_SPLASH, z->x, z->y, 1);
            set(b, B_EMERGE, 0.6f);
        }
        return;
    default:
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
}

/* ---------------------------------------------------------------- Lindormen */
static void lindorm(Zombie *z, Boss *b, float pd, float dt) {
    Player *p = &G->p;
    switch (b->st) {
    case B_CHASE: {
        float ax = p->x - z->x, ay = p->y - z->y;
        if (pd < 30 && b->cd[0] <= 0) { b->face = atan2f(ay, ax); set(b, B_WINDUP, 0.4f); sfx_at(SFX_HISS, z->x, z->y, 0.8f); return; }
        if (b->cd[2] <= 0 && pd > 50) { set(b, B_DIVE, 0.7f); sfx_at(SFX_HISS, z->x, z->y, 1); b->cd[2] = 11; return; }
        if (pd > 60 && pd < 230 && b->cd[1] <= 0 && shot_clear(z->x, z->y - 8, p->x, p->y - 8)) {
            b->face = atan2f(ay, ax); set(b, B_SPIT, 0.6f); sfx_at(SFX_HISS, z->x, z->y, 0.7f); return;
        }
        float ox = z->x, oy = z->y;
        walk(z, z->speed, dt);
        if (fabsf(z->x - ox) + fabsf(z->y - oy) > 0.01f) {  /* it looks where it goes, turning smoothly */
            float want = atan2f(z->y - oy, z->x - ox);
            b->face += angdiff(want, b->face) * MIN(1.0f, dt * 8);
        }
        return;
    }
    case B_WINDUP:
        if (b->t <= 0) {
            float hx = z->x + cosf(b->face) * 16, hy = z->y + sinf(b->face) * 12;
            hurt_near(hx, hy, 22, 70);
            set(b, B_RECOVER, 0.35f); b->cd[0] = 1.4f;
        }
        return;
    case B_SPIT:                                            /* three gobs of venom, spread */
        if (b->t <= 0.3f && b->n == 0) {
            b->n = 1;
            float base = atan2f(p->y - z->y, p->x - z->x), d = MIN(pd, 150.0f);
            for (int k = -1; k <= 1; k++) {
                float a = base + k * 0.3f;
                lob(HZ_VENOM, z->x + cosf(b->face) * 12, z->y + sinf(b->face) * 8, z->x + cosf(a) * d, z->y + sinf(a) * d, 0.55f + d / 500, 10, 25);
            }
        }
        if (b->t <= 0) { set(b, B_CHASE, 0); b->cd[1] = b->enraged ? 4 : 5.5f; }
        return;
    case B_DIVE:                                            /* down through the asphalt */
        if (b->t <= 0) { set(b, B_UNDER, 3.0f); b->hidden = 1; spawn_parts(PT_DUST, z->x, z->y, 20, 0x5a5048, 60); shake(4); }
        return;
    case B_UNDER: {                                         /* a mound running at you under the ground */
        if (b->n == 0) {
            float dx = p->x - z->x, dy = p->y - z->y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
            float step = MIN(d, 100 * dt);
            z->x += dx / d * step; z->y += dy / d * step;
            if (rng_chance(&G->fx, 0.6f)) spawn_parts(PT_DUST, z->x, z->y, 1, 0x5a5048, 25);
            if (d < 8 || b->t <= 0) { b->n = 1; b->t = 0.75f; b->tx = z->x; b->ty = z->y; sfx_at(SFX_HISS, z->x, z->y, 1); }
        } else if (b->t <= 0) {                             /* up through the ground, under you */
            unstick_actor(&z->x, &z->y, 6);
            b->hidden = 0;
            ring(z->x, z->y, 40, 0.4f, 0, 0);
            hurt_near(b->tx, b->ty, 26, 75);
            shake(8); sfx_at(SFX_SLAM, z->x, z->y, 1);
            spawn_parts(PT_GIB, z->x, z->y, 18, 0x4a4640, 110);
            spawn_parts(PT_DUST, z->x, z->y, 24, 0x6a6058, 80);
            for (int i = 0; i < BOSS_TRAIL; i++) { b->trail[i][0] = z->x; b->trail[i][1] = z->y; }
            b->face = PI_F / 2;
            set(b, B_EMERGE, 0.6f);
        }
        return;
    }
    default:
        if (b->t <= 0) set(b, B_CHASE, 0);
        return;
    }
}

/* ---------------------------------------------------------------- every tick */
/* its fall: frost, stone, water or venom coming off it... */
static void falling(Zombie *z, float dt) {
    float k = z->t / FALL, h = z->variant == BOSS_LINDORM ? 12 : BD[z->variant].h;
    float px = z->x + rng_rangef(&G->fx, -10, 10), py = z->y - rng_rangef(&G->fx, 2, h);
    switch (z->variant) {
    case BOSS_DRAUGEN: if (rng_chance(&G->fx, dt * 45)) spawn_parts(PT_FROST, px, py, 1, 0x9ae8ff, 20 + 40 * k); break;
    case BOSS_TROLL: if (rng_chance(&G->fx, dt * 12)) spawn_parts(k < 0.5f ? PT_DUST : PT_GIB, px, py, 1, 0x8a8478, 25); break;
    case BOSS_NACKEN: if (rng_chance(&G->fx, dt * 35)) spawn_parts(PT_WATER, z->x + rng_rangef(&G->fx, -14, 14), z->y, 1, 0x8ac8ff, 50); break;
    default: if (rng_chance(&G->fx, dt * 30)) spawn_parts(PT_GAS, px, z->y - 6, 1, 0x7ad040, 20); break;
    }
    if (rng_chance(&G->fx, dt * 6)) shake(2);
}
/* ...and then it goes, each its own way */
static void fallen(Zombie *z) {
    switch (z->variant) {
    case BOSS_DRAUGEN:                                      /* back to dust and frost */
        spawn_parts(PT_DUST, z->x, z->y - 16, 40, 0x9a9488, 90);
        spawn_parts(PT_FROST, z->x, z->y - 24, 50, 0x9ae8ff, 110);
        sfx_at(SFX_SWOOSH, z->x, z->y, 0.8f);
        break;
    case BOSS_TROLL:                                        /* stone, and it crumbles */
        spawn_parts(PT_GIB, z->x, z->y - 20, 50, 0x7a766e, 140);
        spawn_parts(PT_DUST, z->x, z->y - 10, 50, 0x8a8478, 100);
        sfx_at(SFX_SLAM, z->x, z->y, 1);
        break;
    case BOSS_NACKEN:                                       /* into the water */
        spawn_parts(PT_WATER, z->x, z->y - 16, 70, 0x8ac8ff, 130);
        sfx_at(SFX_SPLASH, z->x, z->y, 1);
        break;
    default:                                                /* venom and scales */
        spawn_parts(PT_GAS, z->x, z->y - 8, 36, 0x7ad040, 60);
        spawn_parts(PT_GIB, z->x, z->y - 8, 30, 0x3a6a2a, 130);
        sfx_at(SFX_HISS, z->x, z->y, 1);
        break;
    }
    if (z->variant != BOSS_DRAUGEN && z->variant != BOSS_NACKEN) world_decal_blood(z->x, z->y, 60);
    shake(8);
    sfx(SFX_FANFARE, 0.9f, 0);
}

/* the view leans to a boss as it comes up and greets you, and as it falls (1: lean to x, y) */
int boss_focus(float *x, float *y) {
    const Boss *b = &G->boss;
    if (b->zi < 0 || b->zi >= MAX_ZOMBIES) return 0;
    const Zombie *z = &G->z[b->zi];
    if (!z->alive || z->type != ZT_BOSS) return 0;
    if (!(z->state == ZS_RISE || (z->state == ZS_DEAD && z->t < FALL) || (b->on && b->st == B_ROAR && b->n == 1))) return 0;
    *x = z->x; *y = z->y - (z->variant == BOSS_LINDORM ? 8 : 20);
    return 1;
}

/* the first moment of its fall is seen slowed down */
float boss_time_scale(void) {
    const Boss *b = &G->boss;
    if (b->zi < 0 || b->zi >= MAX_ZOMBIES) return 1;
    const Zombie *z = &G->z[b->zi];
    return z->alive && z->type == ZT_BOSS && z->state == ZS_DEAD && z->t < 0.5f ? 0.35f : 1;
}

int boss_ai(Zombie *z, float dt) {
    if (z->type != ZT_BOSS) return 0;
    Boss *b = &G->boss;
    Player *p = &G->p;
    if (z->flash > 0) z->flash -= dt;
    if (z->slow > 0) z->slow -= dt;
    if (z->burn > 0) { z->burn -= dt; if (rng_chance(&G->fx, 0.4f)) spawn_parts(PT_FIRE, z->x + rng_rangef(&G->fx, -8, 8), z->y - 20, 1, 0xffa030, 10); }
    if (z->state == ZS_DEAD) {                              /* it falls, then lies there a while */
        float t0 = z->t; z->t += dt;
        if (z->t < FALL) falling(z, dt);
        else if (t0 < FALL) fallen(z);
        if (z->t > 14) z->alive = 0;
        return 1;
    }
    if (z->state == ZS_RISE) {
        z->t -= dt;
        if (rng_chance(&G->fx, 0.5f)) spawn_parts(PT_DUST, z->x + rng_rangef(&G->fx, -12, 12), z->y, 1, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 30);
        if (z->t <= 0) {                                    /* up: it greets you, and then it comes */
            z->state = ZS_CHASE; set(b, B_ROAR, 1.4f); b->n = 1;
            b->face = b->kind == BOSS_LINDORM ? atan2f(p->y - z->y, p->x - z->x) : p->x < z->x ? PI_F : 0;
            sfx_at(b->kind == BOSS_NACKEN ? SFX_FIDDLE : SFX_ROAR, z->x, z->y, 1); shake(7);
            ring(z->x, z->y, 80, 0.8f, 0, b->kind == BOSS_NACKEN);
            spawn_parts(PT_DUST, z->x, z->y, 26, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 80);
        }
        return 1;
    }
    float rate = b->enraged ? 1.45f : 1.0f;
    for (int k = 0; k < 4; k++) if (b->cd[k] > 0) b->cd[k] -= dt * rate;
    b->t -= dt;
    if (!b->enraged && z->hp < z->maxhp * 0.5f && b->st == B_CHASE) {   /* half its health gone: enraged */
        b->enraged = 1; set(b, B_ROAR, 1.1f);
        sfx_at(SFX_ROAR, z->x, z->y, 1); shake(6);
        ring(z->x, z->y, 64, 0.6f, 0, 2);                  /* (a red wave: only seen) */
        spawn_parts(PT_SPARK, z->x, z->y - BD[b->kind].h * 0.5f, 24, 0xff4020, 90);
        msg(0xff6040, "%s %s", BD[b->kind].name, tr("is enraged"));
        return 1;
    }
    float pdx = p->x - z->x, pdy = p->y - z->y, pd = sqrtf(pdx * pdx + pdy * pdy);
    switch (b->kind) {
    case BOSS_DRAUGEN: draugen(z, b, pd, dt); break;
    case BOSS_TROLL: troll(z, b, pd, dt); break;
    case BOSS_NACKEN: nacken(z, b, pd, dt); break;
    default: lindorm(z, b, pd, dt); break;
    }
    /* the lindworm's body follows the way its head went */
    if (b->kind == BOSS_LINDORM && !b->hidden) {
        int hi = b->trail_head;
        float lx = b->trail[hi][0], ly = b->trail[hi][1], d = sqrtf(dist2f(lx, ly, z->x, z->y));
        if (d >= 3) { b->trail_head = (hi + 1) % BOSS_TRAIL; b->trail[b->trail_head][0] = z->x; b->trail[b->trail_head][1] = z->y; }
    }
    if (rng_chance(&G->fx, dt * 0.12f)) sfx_at(b->kind == BOSS_LINDORM ? SFX_HISS : SFX_GROAN1 + rng_int(&G->fx, 3), z->x, z->y, 0.6f);
    return 1;
}

/* a hit: what comes off it, at the height of its body (frost, stone, water, green blood and scales) */
void boss_hit(Zombie *z, float dmg) {
    (void)dmg;
    float cy, r = boss_hit_radius(z, &cy);
    float x = z->x + rng_rangef(&G->fx, -r * 0.6f, r * 0.6f), y = z->y - cy + rng_rangef(&G->fx, -6, 6);
    switch (z->variant) {
    case BOSS_DRAUGEN: spawn_parts(PT_FROST, x, y, 2, 0x9ae8ff, 50); spawn_parts(PT_BLOOD, x, y, 1, 0x4a5a6a, 40); break;
    case BOSS_TROLL: spawn_parts(PT_GIB, x, y, 2, 0x7a766e, 60); spawn_parts(PT_BLOOD, x, y, 1, 0x5a3a1a, 40); break;
    case BOSS_NACKEN: spawn_parts(PT_WATER, x, y, 3, 0x8ac8ff, 50); break;
    default: spawn_parts(PT_BLOOD, x, y, 2, 0x5a8a20, 50); spawn_parts(PT_GIB, x, y, 1, 0x3a6a2a, 50); break;
    }
}

void boss_killed(Zombie *z) {
    Boss *b = &G->boss;
    Player *p = &G->p;
    int tier = MAX(1, G->round / 20);
    b->on = 0; b->hidden = 0; b->h = 0; b->st = B_CHASE; b->enraged = 0;
    G->boss_kills++;
    char up[48]; snprintf(up, sizeof up, "%s", BD[b->kind].name);
    banner(0xf0d040, up, tr("slain"));
    G->banner_t = 3.5f;
    shake(10); G->flash_t = 0.1f;                         /* (a short one: the fall is slowed down) */
    sfx(SFX_ROAR, 1, 0); sfx(SFX_KABOOM, 0.7f, 0);
    if (music_now() == MUS_BOSS) music_play(MUS_NONE);
    spawn_parts(PT_BLOOD, z->x, z->y - 16, 30, 0x8a1010, 110);   /* (then it falls: falling(), fallen()) */
    spawn_parts(PT_CONFETTI, z->x, z->y - 30, 30, 0xf0d040, 90);
    /* the spoils: a legendary weapon, Max Ammo, and money */
    static const int spoils[] = { W_AK5, W_AK4, W_KSP58, W_HAGEL, W_STUDSARE, W_STRAL, W_ASKA, W_SNO };
    item_drop_weapon(weapon_make(spoils[rng_int(&G->rng, (int)ARRAY_LEN(spoils))], RAR_LEGENDARY), z->x + 14, z->y + 6);
    powerup_spawn(PU_MAXAMMO, z->x - 14, z->y + 6);
    int kr = 2000 * tier;
    add_kr(kr, 0);
    char t[24]; snprintf(t, sizeof t, "+%d kr", kr); float_text(z->x, z->y - 50, 0xffe060, t);
    for (int i = 0; i < MAX_HAZARDS; i++) if (G->hz[i].kind == HZ_NOTE || G->hz[i].kind == HZ_ROCK || G->hz[i].kind == HZ_VENOM) G->hz[i].alive = 0;
    plat_log("boss %s slain in round %d at %.0f s (%d kills)", BD[b->kind].name, G->round, G->time, p->kills);
}

float boss_hit_radius(const Zombie *z, float *cy) {
    int k = z->variant;
    if (k == BOSS_LINDORM) { *cy = 8; return BD[k].r; }
    *cy = BD[k].h * 0.45f;
    return BD[k].r;
}

/* ---------------------------------------------------------------- drawing */
static const char *sprite_for(const Zombie *z) {
    Boss *b = &G->boss;
    static char name[48];
    int step = ((int)z->anim) & 1;
    if (z->state == ZS_DEAD) { snprintf(name, sizeof name, "boss_%s_dead", z->variant == BOSS_DRAUGEN ? "draugen" : z->variant == BOSS_TROLL ? "troll" : z->variant == BOSS_NACKEN ? "nacken" : "lindorm"); return name; }
    switch (z->variant) {
    case BOSS_DRAUGEN:
        if (z->state == ZS_RISE) return "boss_draugen_horn";
        switch (b->st) {
        case B_WINDUP: return "boss_draugen_windup";
        case B_STRIKE: return "boss_draugen_attack";
        case B_LEAP: case B_AIR: return "boss_draugen_leap";
        case B_RECOVER: return b->h > 0 ? "boss_draugen_leap" : "boss_draugen_attack";
        case B_HORN: case B_ROAR: return "boss_draugen_horn";
        }
        snprintf(name, sizeof name, "boss_draugen_walk_%d", step); return name;
    case BOSS_TROLL:
        if (z->state == ZS_RISE) return "boss_troll_roar";
        switch (b->st) {
        case B_WINDUP: return "boss_troll_windup";
        case B_STRIKE: return "boss_troll_attack";
        case B_POUND: return b->n > 0 && b->t > 0.3f ? "boss_troll_attack" : "boss_troll_windup";
        case B_THROW: return "boss_troll_throw";
        case B_RECOVER: return b->n >= 3 ? "boss_troll_attack" : "boss_troll_walk_0";
        case B_ROAR: return "boss_troll_roar";
        }
        snprintf(name, sizeof name, "boss_troll_walk_%d", step); return name;
    case BOSS_NACKEN:
        switch (b->st) {
        case B_PLAY: snprintf(name, sizeof name, "boss_nacken_play_%d", ((int)(G->time * 7)) & 1); return name;
        case B_BECKON: case B_WINDUP: return "boss_nacken_beckon";
        case B_ROAR: if (b->n == 1) { snprintf(name, sizeof name, "boss_nacken_play_%d", ((int)(G->time * 7)) & 1); return name; } return "boss_nacken_beckon";
        case B_DIVE: case B_UNDER: case B_EMERGE: return "boss_nacken_dive";
        }
        snprintf(name, sizeof name, "boss_nacken_idle_%d", ((int)(G->time * 2.5f)) & 1); return name;
    default:
        return (b->st == B_WINDUP || b->st == B_SPIT || b->st == B_EMERGE || b->st == B_ROAR || ((int)(G->time * 1.5f) % 5 == 0)) ? "boss_lindorm_head_1" : "boss_lindorm_head_0";
    }
}

static const Img *boss_img(const char *name) {
    if (art_exists(name)) return art(name);
    return art_exists("moose_0") ? art("moose_0") : 0;     /* (before its own art is drawn) */
}

/* an ellipse's outline on the ground, blended */
static void ground_ring(Surf *s, int cx, int cy, float r, uint32_t col, int a, int thick) {
    int n = (int)(r * 3) + 12;
    for (int i = 0; i < n; i++) {
        float t = i * 2 * PI_F / n, c = cosf(t), sn = sinf(t);
        for (int k = 0; k < thick; k++) pblend(s, cx + (int)(c * (r - k)), cy + (int)(sn * (r - k) * 0.55f), col, a);
    }
}

/* how far down the lindworm's head still is (coming up, or going down) */
static int lindorm_sink(const Zombie *z) {
    const Boss *b = &G->boss;
    return z->state == ZS_RISE ? (int)(CLAMP(z->t / 2.2f, 0, 1) * 10) : b->st == B_DIVE ? (int)((0.7f - MAX(b->t, 0)) / 0.7f * 10) : 0;
}

static void draw_lindorm_body(Surf *s, Zombie *z, int cx, int cy) {
    Boss *b = &G->boss;
    const Img *seg = art_exists("boss_lindorm_body") ? art("boss_lindorm_body") : 0, *tail = art_exists("boss_lindorm_tail") ? art("boss_lindorm_tail") : 0;
    /* eight segments and the tail along the path (points 3 px apart): the first 18 px behind the head, then every 12;
       drawn from the tail forward, so each covers the back of the one behind */
    int nseg = 9;
    for (int k = nseg; k >= 1; k--) {
        int back = 6 + (k - 1) * 4;
        int i = ((b->trail_head - back) % BOSS_TRAIL + BOSS_TRAIL) % BOSS_TRAIL, j = (i + 1) % BOSS_TRAIL;
        float x = b->trail[i][0], y = b->trail[i][1], ang = atan2f(b->trail[j][1] - y, b->trail[j][0] - x);
        if (dist2f(x, y, z->x, z->y) < 1 && k > 1) {         /* (coiled up, just out of the ground) */
            x = z->x - cosf(b->face) * 5 * k; y = z->y - sinf(b->face) * 4 * k;
        }
        int sx = (int)(x - cx), sy = (int)(y - cy);
        const Img *im = k == nseg ? tail : seg;
        ellipse_blend(s, sx, sy + 3, k == nseg ? 5 : 8, 3, 0x000000, 70);
        if (im) blit_rot(s, im, (float)sx, (float)(sy - 4), ang + (k == nseg ? PI_F / 2 : -PI_F / 2), 0);   /* (the tail's tip points back) */
        else circlef(s, sx, sy - 4, k == nseg ? 4 : 7, 0x3a6a2a);
    }
}

/* its fall: it shudders, flashing; Draugen goes to frost, the troll to stone, Näcken sinks into his pool, the
   lindworm's head thrashes and drops */
static void draw_fall(Surf *s, Zombie *z, int sx, int sy) {
    Boss *b = &G->boss;
    static const char *pose[BOSS_COUNT] = { "boss_draugen_horn", "boss_troll_roar", "boss_nacken_dive", "boss_lindorm_head_1" };
    const Img *im = boss_img(pose[z->variant]);
    if (!im) return;
    float k = CLAMP(z->t / FALL, 0, 1);
    int jit = (int)(sinf(z->t * 70) * 2.5f * (1 - k * 0.6f)), white = ((int)(z->t * 14)) & 1;
    if (z->variant == BOSS_LINDORM) {
        draw_lindorm_body(s, z, (int)G->camx, (int)G->camy);
        ellipse_blend(s, sx, sy + 2, 14, 5, 0x000000, 90);
        blit_rot(s, im, (float)(sx + jit), (float)(sy - 8) + k * 5, b->face - PI_F / 2 + sinf(z->t * 18) * 0.5f * (1 - k), 0);
        if (white) circle_blend(s, sx + jit, sy - 8 + (int)(k * 5), 11, 0xffffff, 80);
        return;
    }
    int flip = (b->face > PI_F / 2 && b->face < 3 * PI_F / 2) ? FLIP_X : 0;
    int ox = sx - im->w / 2 + jit, oy = sy - im->h + 1, shw = z->variant == BOSS_TROLL ? 20 : 15;
    ellipse_blend(s, sx, sy, shw, shw / 3, 0x000000, (int)(110 * (1 - k * 0.5f)));
    if (z->variant == BOSS_DRAUGEN)
        blit_ex(s, im, ox, oy, flip, 0x9ae8ff, (int)(60 + 190 * k), (int)(255 * (1 - k * k)));
    else if (z->variant == BOSS_TROLL)
        blit_ex(s, im, ox, oy, flip, k < 0.3f && white ? 0xffffff : 0x6e6a62, k < 0.3f && white ? 170 : (int)(200 * MIN(1.0f, k * 1.5f)), 255);
    else {
        ellipse_blend(s, sx, sy, 18, 7, 0x0a1a2a, 200);
        int clip_h = (int)(im->h * (1 - k));
        if (clip_h > 0) {
            Surf c = *s; surf_clip(&c, ox - 4, oy, im->w + 8, im->h);
            blit_ex(&c, im, ox, oy + im->h - clip_h, flip, white ? 0xffffff : 0, white ? 120 : 0, 255);
        }
    }
}

void boss_draw(Surf *s, Zombie *z) {
    Boss *b = &G->boss;
    int cx = (int)G->camx, cy = (int)G->camy;
    int sx = (int)(z->x - cx), sy = (int)(z->y - cy);
    z->rimg = 0;
    if (z->state == ZS_DEAD && z->t < FALL) { draw_fall(s, z, sx, sy); return; }
    if (z->state == ZS_DEAD) {
        const Img *d = art_exists(sprite_for(z)) ? art(sprite_for(z)) : 0;
        if (d) blit_ex(s, d, sx - d->w / 2, sy - d->h + 3, b->face > 1.5f && b->face < 4.7f ? FLIP_X : 0, 0, 0, z->t > 11 ? (int)(255 * (14 - z->t) / 3) : 255);   /* (after its fall) */
        return;
    }
    if (z->variant == BOSS_LINDORM) {
        if (b->hidden) {                                    /* a mound of asphalt running under the ground */
            const Img *m = art_exists("boss_lindorm_mound") ? art("boss_lindorm_mound") : 0;
            if (m) blit(s, m, sx - m->w / 2, sy - m->h + 2, ((int)(G->time * 8)) & 1 ? FLIP_X : 0);
            else ellipse_blend(s, sx, sy, 10, 4, 0x4a4038, 220);
            return;
        }
        if (z->state != ZS_RISE) draw_lindorm_body(s, z, cx, cy);
        const Img *im = boss_img(sprite_for(z));
        if (!im) return;
        int rise = lindorm_sink(z);
        ellipse_blend(s, sx, sy + 2, 14, 5, 0x000000, 90);
        uint32_t tint = z->flash > 0 ? 0xffffff : b->st == B_WINDUP ? 0xff4040 : 0; int amt = z->flash > 0 ? 200 : b->st == B_WINDUP && ((int)(G->time * 20) & 1) ? 120 : 0;
        (void)tint; (void)amt;
        blit_rot(s, im, (float)sx, (float)(sy - 8 + rise), b->face - PI_F / 2, 0);
        if (z->flash > 0) circle_blend(s, sx, sy - 8 + rise, 10, 0xffffff, 90);
        return;
    }
    const Img *im = boss_img(sprite_for(z));
    if (!im) return;
    int flip = (b->face > PI_F / 2 && b->face < 3 * PI_F / 2) ? FLIP_X : 0;
    int lift = (int)b->h;
    int ox = sx - im->w / 2, oy = sy - im->h + 1 - lift;
    /* its shadow, smaller the higher it is */
    int shw = z->variant == BOSS_TROLL ? 20 : 15;
    ellipse_blend(s, sx, sy, MAX(6, shw - lift / 4), MAX(2, shw / 3 - lift / 12), 0x000000, 110);
    if (z->variant == BOSS_NACKEN) {                        /* his pool of dark water goes with him */
        int pr = b->st == B_DIVE ? (int)(18 + (0.8f - MAX(b->t, 0)) * 10) : 18;
        ellipse_blend(s, sx, sy, pr, pr / 3 + 1, 0x0a1a2a, 200);
        ellipse_blend(s, sx, sy, pr - 4, pr / 3 - 1, 0x12304a, 160);
        for (int k = 0; k < 3; k++) {
            float rr = fmodf(G->time * 9 + k * 6, 18);
            ground_ring(s, sx, sy, rr + 2, 0x6aa8d8, (int)(90 * (1 - rr / 18)), 1);
        }
    }
    if (b->hidden) return;                                  /* under the water */
    int clip_h = im->h;
    if (z->state == ZS_RISE) clip_h = (int)(im->h * CLAMP(1 - z->t / 2.2f, 0, 1));
    else if (z->variant == BOSS_NACKEN && b->st == B_DIVE) clip_h = (int)(im->h * CLAMP(b->t / 0.8f, 0, 1));
    else if (z->variant == BOSS_NACKEN && b->st == B_EMERGE) clip_h = (int)(im->h * CLAMP(1 - b->t / 0.6f, 0.2f, 1));
    uint32_t tint = 0; int amt = 0;
    if (z->flash > 0) { tint = 0xffffff; amt = 170; }
    else if ((b->st == B_WINDUP || b->st == B_POUND || b->st == B_LEAP) && ((int)(G->time * 16) & 1)) { tint = 0xff3020; amt = 110; }
    else if (b->enraged && ((int)(G->time * 4) & 1)) { tint = 0xff2010; amt = 40; }
    else if (z->slow > 0) { tint = 0x9ad8ff; amt = 90; }
    Surf c = *s;
    if (clip_h < im->h) surf_clip(&c, ox - 4, oy + (im->h - clip_h) - (z->state == ZS_RISE ? 0 : 0), im->w + 8, clip_h);
    int dy = z->state == ZS_RISE || (z->variant == BOSS_NACKEN && (b->st == B_DIVE || b->st == B_EMERGE)) ? im->h - clip_h : 0;
    blit_ex(clip_h < im->h ? &c : s, im, ox, oy + dy, flip, tint, amt, 255);
    z->rimg = im; z->rx = (int16_t)ox; z->ry = (int16_t)(oy + dy); z->rflip = (uint8_t)flip; z->rclip = (int16_t)(oy + im->h);
}

/* after the light: what glows */
void boss_glow(Surf *s, Zombie *z) {
    Boss *b = &G->boss;
    if (z->state == ZS_DEAD || b->hidden) return;
    uint32_t eye = BD[z->variant].eye;
    if (z->variant == BOSS_LINDORM) {                       /* its eyes, turned with its head */
        float a = G->boss.face - PI_F / 2, ca = cosf(a), sa = sinf(a);
        int hx = (int)(z->x - G->camx), hy = (int)(z->y - 8 - G->camy) + lindorm_sink(z);   /* (with the head, as it comes up) */
        static const int eyes[2][2] = { { -7, -1 }, { 6, -1 } };
        for (int e = 0; e < 2; e++) {
            int ex = hx + (int)lroundf(eyes[e][0] * ca - eyes[e][1] * sa), ey = hy + (int)lroundf(eyes[e][0] * sa + eyes[e][1] * ca);
            pset(s, ex, ey, eye); padd(s, ex - 1, ey, col_scale(eye, 70)); padd(s, ex + 1, ey, col_scale(eye, 70)); padd(s, ex, ey - 1, col_scale(eye, 50));
        }
        return;
    }
    if (z->rimg) {            /* its eyes: the sprite's eye pixels, unlit */
        const Img *im = z->rimg;
        for (int y = 0; y < im->h / 2; y++)
            for (int x = 0; x < im->w; x++) {
                uint32_t p = im->px[y * im->w + x] & 0xFFFFFF;
                if (p != eye) continue;
                int px = z->rx + ((z->rflip & FLIP_X) ? im->w - 1 - x : x), py = z->ry + y;
                if (py >= z->rclip) continue;
                pset(s, px, py, eye);
                padd(s, px - 1, py, col_scale(eye, 60)); padd(s, px + 1, py, col_scale(eye, 60));
            }
    }
}

void boss_lights(void) {
    Boss *b = &G->boss;
    Zombie *z = boss_z();
    if (z && z->state != ZS_DEAD && !b->hidden) {
        float hy = z->variant == BOSS_LINDORM ? z->y - 10 : z->y - BD[z->variant].h * 0.8f - b->h;
        render_add_light(z->x, hy, z->variant == BOSS_TROLL ? 40 : 34, BD[z->variant].eye, 0.55f);
        if (z->variant == BOSS_NACKEN) render_add_light(z->x, z->y, 50, 0x3a8ac8, 0.45f);
    }
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive) continue;
        if (h->kind == HZ_NOTE) render_add_light(h->x, h->y - 16, 22, 0x60e0ff, 0.5f);
        else if (h->kind == HZ_POOL) render_add_light(h->x, h->y, 30, 0x60d040, 0.35f);
        else if (h->kind == HZ_RING && h->a > 0.5f) render_add_light(h->x, h->y, h->r, h->a > 1.5f ? 0xff4020 : 0x6ab8ff, 0.3f);
    }
}

/* warnings and shockwaves: drawn after the night's light, so that they can be seen in the dark */
static void telegraphs(Surf *s) {
    int cx = (int)G->camx, cy = (int)G->camy;
    Boss *b = &G->boss;
    Zombie *z = boss_z();
    /* warnings: where something will come down or up */
    if (z && b->kind == BOSS_LINDORM && b->st == B_UNDER && b->n == 1) {
        int a = 120 + (int)(100 * sinf(G->time * 20));
        ground_ring(s, (int)(b->tx - cx), (int)(b->ty - cy), 26, 0xff3020, a, 2);
        ellipse_blend(s, (int)(b->tx - cx), (int)(b->ty - cy), 24, 13, 0x400000, 70);
    }
    if (z && b->kind == BOSS_NACKEN && b->st == B_UNDER) {
        float k = 1 - CLAMP(b->t / 1.1f, 0, 1);
        int r = (int)(6 + k * 14);
        ellipse_blend(s, (int)(b->tx - cx), (int)(b->ty - cy), r, r / 3 + 1, 0x0a1a2a, 200);
        ground_ring(s, (int)(b->tx - cx), (int)(b->ty - cy), 48 * k, 0x6ab8ff, 90, 1);
    }
    if (z && b->kind == BOSS_DRAUGEN && (b->st == B_AIR || b->st == B_LEAP)) {
        ground_ring(s, (int)(b->tx - cx), (int)(b->ty - cy), 24, 0xff3020, 140 + (int)(80 * sinf(G->time * 18)), 2);
    }
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive) continue;
        int x = (int)(h->x - cx), y = (int)(h->y - cy);
        float k = h->dur > 0 ? MIN(1.0f, h->t / h->dur) : 1;
        if (h->kind == HZ_RING) {                           /* the shockwave: a bright edge, a fainter one inside */
            float rad = h->r * k;
            int kind = (int)(h->a + 0.5f);                  /* (0 dust, 1 water, 2 a rage) */
            uint32_t c = kind == 2 ? 0xff5030 : kind == 1 ? 0x9ad8ff : 0xf0e0c0;
            ground_ring(s, x, y, rad, c, (int)(240 * (1 - k * 0.5f)), 3);
            ground_ring(s, x, y, rad * 0.82f, kind == 2 ? 0xa01808 : kind == 1 ? 0x4a88d8 : 0xa08060, (int)(150 * (1 - k)), 2);
        } else if (h->kind == HZ_ROCK || h->kind == HZ_VENOM)   /* where it will land */
            ground_ring(s, x, y, h->r, h->kind == HZ_ROCK ? 0xff4020 : 0x80e040, 120 + (int)(120 * k), 2);
    }
}

void hazards_draw_ground(Surf *s) {
    int cx = (int)G->camx, cy = (int)G->camy;
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive) continue;
        int x = (int)(h->x - cx), y = (int)(h->y - cy);
        float k = h->dur > 0 ? MIN(1.0f, h->t / h->dur) : 1;
        switch (h->kind) {
        case HZ_ROCK: case HZ_VENOM:                       /* its shadow on the way */
            ellipse_blend(s, (int)(lerpf(h->x0, h->x, k) - cx), (int)(lerpf(h->y0, h->y, k) - cy), h->kind == HZ_ROCK ? 7 : 3, 2, 0x000000, 110);
            break;
        case HZ_POOL: {
            float a = h->t < 0.3f ? h->t / 0.3f : h->t > h->dur - 1 ? h->dur - h->t : 1;
            ellipse_blend(s, x, y, (int)h->r, (int)(h->r * 0.55f), 0x3a8a1a, (int)(170 * a));
            ellipse_blend(s, x, y, (int)h->r - 4, (int)(h->r * 0.55f) - 2, 0x6ad028, (int)(120 * a));
            for (int k2 = 0; k2 < 3; k2++) {
                int bx = x + (int)(sinf(h->t * 3 + k2 * 2.1f) * h->r * 0.5f), by = y + (int)(cosf(h->t * 2 + k2 * 1.7f) * h->r * 0.25f);
                pset(s, bx, by, 0xb8ff60);
            }
            break;
        }
        default: break;
        }
    }
}

void hazards_draw_air(Surf *s) {
    int cx = (int)G->camx, cy = (int)G->camy;
    const Img *rock = art_exists("boss_troll_rock") ? art("boss_troll_rock") : 0;
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive || (h->kind != HZ_ROCK && h->kind != HZ_VENOM)) continue;
        float k = MIN(1.0f, h->t / h->dur);
        float x = lerpf(h->x0, h->x, k), y = lerpf(h->y0, h->y, k);
        float z = h->kind == HZ_ROCK ? lerpf(56, 0, k) + sinf(PI_F * k) * 60 : lerpf(14, 0, k) + sinf(PI_F * k) * 30;
        int sx = (int)(x - cx), sy = (int)(y - cy - z);
        if (h->kind == HZ_ROCK) { if (rock) blit_rot(s, rock, (float)sx, (float)sy, h->t * 6, 0); else circlef(s, sx, sy, 6, 0x7a766e); }
        else { circlef(s, sx, sy, 3, 0x5ac020); pset(s, sx - 1, sy - 1, 0xc8ff80); }
    }
}

/* the notes and the axe's swing glow; drawn after the light */
static void note_glyph(Surf *s, int x, int y, uint32_t c) {
    /* a quaver: head, stem and flag */
    pset(s, x, y + 3, c); pset(s, x + 1, y + 3, c); pset(s, x, y + 2, c); pset(s, x + 1, y + 2, c);
    vline(s, x + 2, y - 2, y + 3, c); pset(s, x + 3, y - 1, c); pset(s, x + 4, y, c);
}
void boss_air_glow(Surf *s) {
    int cx = (int)G->camx, cy = (int)G->camy;
    Boss *b = &G->boss;
    Zombie *z = boss_z();
    telegraphs(s);
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G->hz[i];
        if (!h->alive) continue;
        int x = (int)(h->x - cx), y = (int)(h->y - cy);
        if (h->kind == HZ_NOTE) {
            uint32_t c = h->a > 0.5f ? 0x9af0ff : 0xd8ff90;
            y -= 16 + (int)(sinf(h->t * 9 + i) * 2);        /* floating above its spot, bobbing */
            pblend(s, x, y + 17, 0x000000, 60);
            circle_blend(s, x + 1, y + 1, 4, c, 50);
            note_glyph(s, x - 1, y - 2, c);
        } else if (h->kind == HZ_CLEAVE) {                  /* the axe's swing: an arc of light */
            float k = h->t / h->dur;
            for (int j = 0; j < 14; j++) {
                float a = h->a - 1.3f + j * 0.2f;
                int ax = x + (int)(cosf(a) * 24), ay = y + (int)(sinf(a) * 16);
                pblend(s, ax, ay, 0xe8f4ff, (int)(230 * (1 - k)));
                pblend(s, ax, ay + 1, 0x9ad8ff, (int)(150 * (1 - k)));
            }
        }
    }
    /* Näcken's song pulls you: a shimmering thread */
    if (z && b->kind == BOSS_NACKEN && b->st == B_BECKON) {
        Player *p = &G->p;
        int x0 = (int)(z->x - cx), y0 = (int)(z->y - 26 - cy), x1 = (int)(p->x - cx), y1 = (int)(p->y - 10 - cy);
        for (int j = 0; j <= 24; j++) {
            float t = j / 24.0f;
            int x = x0 + (int)((x1 - x0) * t), y = y0 + (int)((y1 - y0) * t) + (int)(sinf(t * 12 + G->time * 10) * 3);
            pblend(s, x, y, 0x9af0ff, 160);
        }
        if (((int)(G->time * 6)) & 1) note_glyph(s, (x0 + x1) / 2, (y0 + y1) / 2 - 6, 0xd8ff90);
    }
}

void boss_overlay(Surf *s) {
    Boss *b = &G->boss;
    if (b->bar <= 0 || G->over) return;
    Zombie *z = b->on ? boss_z() : 0;
    int w = MIN(s->w - 60, 220), x0 = (s->w - w) / 2, y0 = 15;
    float hp = z ? CLAMP(z->hp / z->maxhp, 0, 1) : 0, chip = z ? CLAMP(b->chip / z->maxhp, 0, 1) : 0;
    int fillw = (int)(w * b->bar);                          /* (it fills in as it comes up) */
    rect_blend(s, x0 - 8, 1, w + 16, 23, 0x000000, 110);
    char name[48]; snprintf(name, sizeof name, "%s", BD[b->kind].name);
    text_ol(s, FONT_NORMAL, s->w / 2 - text_w(FONT_NORMAL, name) / 2, 3, 0xf0e0c0, 0x000000, name);
    pset(s, s->w / 2 - text_w(FONT_NORMAL, name) / 2 - 6, 7, BD[b->kind].eye); pset(s, s->w / 2 + text_w(FONT_NORMAL, name) / 2 + 5, 7, BD[b->kind].eye);
    if (b->enraged) { const char *e = tr("enraged"); text_ol(s, FONT_SMALL, x0 + w - text_w(FONT_SMALL, e), 5, ((int)(G->time * 4) & 1) ? 0xff6040 : 0xc03020, 0x000000, e); }
    rectf(s, x0 - 2, y0 - 2, w + 4, 10, 0x000000);
    rectf(s, x0 - 1, y0 - 1, w + 2, 8, 0x4a3a30);
    rectf(s, x0, y0, w, 6, 0x2a0808);
    rectf(s, x0, y0, MIN(fillw, (int)(w * chip)), 6, 0xe8e0d0);   /* what it just lost, fading */
    uint32_t c = b->enraged ? (((int)(G->time * 6)) & 1 ? 0xe03818 : 0xc02010) : 0xc81818;
    int hw = MIN(fillw, (int)(w * hp));
    rectf(s, x0, y0, hw, 6, c);
    hline(s, x0, x0 + hw - 1, y0, col_scale(c, 380));
    hline(s, x0, x0 + hw - 1, y0 + 5, col_scale(c, 160));
    vline(s, x0 + w / 2, y0, y0 + 5, 0x000000);              /* half: where it goes into a rage */
    /* off the screen: an arrow on the edge pointing to it */
    if (z && z->state != ZS_DEAD) {
        float sx = z->x - G->camx, sy = z->y - 20 - G->camy;
        if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) {
            float ax = s->w / 2.0f, ay = s->h / 2.0f, dx = sx - ax, dy = sy - ay, d = sqrtf(dx * dx + dy * dy) + 0.01f;
            dx /= d; dy /= d;
            float k = MIN((s->w / 2.0f - 12) / (fabsf(dx) + 0.001f), (s->h / 2.0f - 12) / (fabsf(dy) + 0.001f));
            int px = (int)(ax + dx * k), py = (int)(ay + dy * k);
            uint32_t col = ((int)(G->time * 4) & 1) ? 0xff3020 : 0xffd040;
            for (int pass = 0; pass < 2; pass++)            /* a dark edge, then the arrowhead */
                for (int j = -4; j < 5; j++) {
                    float w = (4 - j) * 0.7f;               /* wide at the back, a point at the front */
                    for (float u = -w; u <= w; u += 0.5f) {
                        int qx = px + (int)lroundf(dx * j - dy * u), qy = py + (int)lroundf(dy * j + dx * u);
                        if (pass == 0) { pset(s, qx - 1, qy, 0x000000); pset(s, qx + 1, qy, 0x000000); pset(s, qx, qy - 1, 0x000000); pset(s, qx, qy + 1, 0x000000); }
                        else pset(s, qx, qy, col);
                    }
                }
        }
    }
}
