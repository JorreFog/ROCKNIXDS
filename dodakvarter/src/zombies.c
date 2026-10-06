// zombies.c: rounds and the dead. Round sizes, health, speeds and spawn rates are Black Ops' own formulas (see
// docs/research.md); zombies come through boarded windows, up out of manholes, graves and the ground, and find
// the player along a flow field (steps to the player from every tile, rebuilt as the player moves).
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- Black Ops' numbers */
float zombie_hp_for_round(int r) {
    if (r < 10) return 150.0f + 100.0f * (r - 1);
    float hp = 950;
    for (int i = 10; i <= r; i++) hp = floorf(hp * 1.1f);
    return hp;
}
int zombies_for_round(int r) {
    float m = MAX(1.0f, r / 5.0f);
    if (r >= 10) m *= 0.15f * r;
    float n = 24 + (int)(3 * m);                          /* solo: max + int(3m) */
    if (r == 1) n *= 0.25f; else if (r == 2) n *= 0.3f; else if (r == 3) n *= 0.5f; else if (r == 4) n *= 0.7f; else if (r == 5) n *= 0.9f;
    return (int)n;
}
static float spawn_delay(int r) { float d = 2.0f * powf(0.95f, (float)(r - 1)); return d < 0.08f ? 0.08f : d; }
#define MAX_ALIVE 24

int zombies_alive(void) {
    int n = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive && G->z[i].state != ZS_DEAD) n++;
    return n;
}

/* ---------------------------------------------------------------- the flow field */
static void bfs_from(uint16_t field[MAPH_MAX][MAPW_MAX], int sx, int sy) {
    static int q[MAPW_MAX * MAPH_MAX];
    for (int y = 0; y < G->h; y++) for (int x = 0; x < G->w; x++) field[y][x] = 0xFFFF;
    if (sx < 0 || sy < 0 || sx >= G->w || sy >= G->h) return;
    int h = 0, t = 0;
    field[sy][sx] = 0; q[t++] = sy * MAPW_MAX + sx;
    static const int d[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    while (h < t) {                                       /* 8-connected breadth first: each tile once */
        int c = q[h++], x = c % MAPW_MAX, y = c / MAPW_MAX;
        uint16_t v = field[y][x];
        for (int k = 0; k < 8; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (nx < 0 || ny < 0 || nx >= G->w || ny >= G->h) continue;
            if (field[ny][nx] != 0xFFFF || (G->t[ny][nx].f & TF_SOLID)) continue;
            if (k >= 4 && ((G->t[y][nx].f & TF_SOLID) || (G->t[ny][x].f & TF_SOLID))) continue;   /* no corner cutting */
            field[ny][nx] = (uint16_t)(v + 1);
            q[t++] = ny * MAPW_MAX + nx;
        }
    }
}

void flow_update(int force) {
    int tx = (int)(G->p.x / TS), ty = (int)((G->p.y - 2) / TS);
    G->flow_t -= 1.0f / 60;
    if (force || tx != G->flow_x || ty != G->flow_y || G->flow_t <= 0) {
        G->flow_x = tx; G->flow_y = ty; G->flow_t = 1.0f;
        bfs_from(G->flow, tx, ty);
    }
    if (G->lure_on && !G->lure_was) bfs_from(G->lureflow, (int)(G->lure_x / TS), (int)(G->lure_y / TS));
    G->lure_was = G->lure_on;
}

/* ---------------------------------------------------------------- spawning */
static int pick_spawn(void) {
    Player *p = &G->p;
    int pz = zone_at(p->x, p->y);
    float w[MAX_SPAWNS], total = 0, far_d = 0; int far = -1;
    for (int i = 0; i < G->nspawns; i++) {
        Spawn *s = &G->spawns[i]; w[i] = 0;
        if (!G->zones[s->zone].open) continue;
        float d = sqrtf(dist2f(s->x, s->y, p->x, p->y));
        if (s->type == SP_WINDOW) {                        /* a window already has someone in it */
            int busy = 0;
            for (int k = 0; k < MAX_ZOMBIES; k++) if (G->z[k].alive && G->z[k].state == ZS_WINDOW && G->z[k].window == s->inter) busy = 1;
            if (busy) continue;
        }
        if (d > far_d) { far_d = d; far = i; }
        if (d < 80) continue;                              /* never right next to you */
        float wt = s->zone == pz ? 4.0f : 1.0f;
        if (d < 260) wt *= 2.0f; else if (d > 520) wt *= 0.3f;
        if (s->type == SP_WINDOW) wt *= 1.5f;
        w[i] = wt; total += wt;
    }
    if (total <= 0) return far;                            /* standing among all the spawns: the farthest one */
    float r = rng_float(&G->rng) * total;
    int last = -1;
    for (int i = 0; i < G->nspawns; i++) if (w[i] > 0) { last = i; r -= w[i]; if (r <= 0) return i; }
    return last;                                           /* (rounding) */
}

static Zombie *new_zombie(void) {
    for (int i = 0; i < MAX_ZOMBIES; i++) if (!G->z[i].alive) { memset(&G->z[i], 0, sizeof G->z[i]); return &G->z[i]; }
    return 0;
}

void spawn_zombie(int type) {
    int si = pick_spawn();
    if (si < 0) return;
    Zombie *z = new_zombie();
    if (!z) return;
    Spawn *s = &G->spawns[si];
    int r = G->round;
    z->alive = 1; z->type = type; z->x = s->x; z->y = s->y; z->window = -1;
    z->variant = rng_int(&G->rng, 8);
    z->dir = 0;
    float hp = zombie_hp_for_round(r);
    /* speed: a roll from 8(R-1) to 8(R-1)+35: up to 35 walks, to 70 runs, over that sprints */
    int roll = 8 * (r - 1) + rng_int(&G->rng, 36);
    float sp = roll <= 35 ? 22 + rng_rangef(&G->rng, 0, 6) : roll <= 70 ? 44 + rng_rangef(&G->rng, 0, 6) : 60 + rng_rangef(&G->rng, 0, 8);
    switch (type) {
    case ZT_BRUTE: hp *= 3; sp = 26 + rng_rangef(&G->rng, 0, 4); break;
    case ZT_BLOATER: hp *= 1.5f; sp = 30; break;
    case ZT_WOLF: {
        static const float wolf_hp[] = { 400, 900, 1300, 1600 };
        hp = wolf_hp[MIN(G->wolf_rounds - 1, 3)]; sp = 82 + rng_rangef(&G->rng, 0, 8);
        break;
    }
    case ZT_MOOSE: hp = MIN(22500.0f, 5000.0f + 1000.0f * G->moose_count); sp = 36; z->variant = 0; break;
    }
    z->hp = z->maxhp = hp; z->speed = sp;
    z->lastx = z->x; z->lasty = z->y;
    if (s->type == SP_WINDOW && type != ZT_MOOSE && type != ZT_WOLF) {
        z->state = ZS_WINDOW; z->window = s->inter; z->t = 0;
    } else {
        z->state = ZS_RISE; z->t = type == ZT_WOLF ? 0.5f : 1.3f;
        if (s->type == SP_WINDOW) { z->y += TS; }          /* wolves and the moose don't climb */
        spawn_parts(PT_DUST, z->x, z->y, 8, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 30);
        if (type == ZT_WOLF) { G->flash_t = 0.12f; spawn_parts(PT_ELEC, z->x, z->y - 10, 10, 0xc0e0ff, 90); sfx_at(SFX_ZAP, z->x, z->y, 0.4f); }
    }
    G->spawned++;
}

/* ---------------------------------------------------------------- rounds */
void round_start(int n) {
    int open = 0; for (int z = 0; z < G->nzones; z++) open += G->zones[z].open;
    plat_log("round %d: %.0f s, %d kills, %d kr (%d earned), hp %.0f, %d perks, %s, %d zones open, power %s", n, G->time, G->p.kills,
             G->p.kr, G->p.kr_total, G->p.hp, G->p.nperks, G->p.w[G->p.cur].def >= 0 ? weapon_name(&G->p.w[G->p.cur]) : "-",
             open, G->power_on ? "on" : "off");
    G->round = n; G->rstate = RS_ACTIVE; G->rtime = 0; G->spawned = 0; G->special = 0; G->drops_round = 0;
    G->p.repair_kr = 0;
    G->spawn_cd = 2.0f;
    if (n == G->wolf_next) {                               /* Vargnatt: the hellhound round */
        G->special = 1; G->wolf_rounds++;
        G->to_spawn = G->wolf_rounds <= 2 ? 6 : 8;
        G->wolf_next = n + rng_range(&G->rng, 4, 5);
        banner(0x8ab0ff, tr("WOLF NIGHT"), 0);
        sfx(SFX_WOLF, 1, 0);
    } else {
        G->to_spawn = zombies_for_round(n);
        if (n == G->moose_next) { G->moose_pending = 1; G->moose_next = n + rng_range(&G->rng, 4, 5); }
    }
    char b[32]; snprintf(b, sizeof b, "%s %d", tr("ROUND"), n);
    if (!G->special) banner(0xc81818, b, n == 1 ? G->town : 0);
    G->round_flash = 2.5f;
    sfx(SFX_ROUND_START, 1, 0);
    if (n > 1) loot_restock(2 + MIN(4, n / 3));
    /* every new round gives the grenades back, as Black Ops does */
    if (n > 1) G->p.grenades = MIN(4, G->p.grenades + 2);
}

void round_update(float dt) {
    G->rtime += dt;
    static float zlog_t;
    if (getenv("DK_DEBUG_ZLOG") && (zlog_t += dt) > 10) {          /* where everyone is, every 10 s */
        zlog_t = 0;
        Weapon *w0 = &G->p.w[0], *w1 = &G->p.w[1];
        plat_log("t %.0f round %d state %d: to_spawn %d spawned %d alive %d, player %.0f,%.0f, guns %d:%d/%d %d:%d/%d, %d kr", G->time, G->round,
                 G->rstate, G->to_spawn, G->spawned, zombies_alive(), G->p.x, G->p.y, w0->def, w0->mag, w0->reserve, w1->def, w1->mag, w1->reserve, G->p.kr);
        for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive && G->z[i].state != ZS_DEAD) {
            Zombie *z = &G->z[i]; int tx = (int)(z->x / TS), ty = (int)((z->y - 2) / TS);
            int f = tx >= 0 && ty >= 0 && tx < G->w && ty < G->h ? G->flow[ty][tx] : -1;
            plat_log("  z%d type %d state %d at %.0f,%.0f (tile %d,%d flow %d) hp %.0f window %d", i, z->type, z->state, z->x, z->y, tx, ty, f, z->hp, z->window);
        }
    }
    if (G->round_flash > 0) G->round_flash -= dt;
    if (G->rstate == RS_BREAK) {
        if (G->rtime >= 10.0f) round_start(G->round + 1);   /* ten seconds between rounds */
        return;
    }
    /* spawning */
    G->spawn_cd -= dt;
    if (G->spawned < G->to_spawn && G->spawn_cd <= 0 && zombies_alive() < MAX_ALIVE) {
        int type = ZT_WALKER;
        if (G->special == 1) type = ZT_WOLF;
        else {
            if (G->round >= 6 && rng_chance(&G->rng, 0.06f + 0.004f * G->round)) type = ZT_BRUTE;
            else if (G->round >= 5 && rng_chance(&G->rng, 0.06f)) type = ZT_BLOATER;
        }
        spawn_zombie(type);
        G->spawn_cd = G->special == 1 ? 1.2f : spawn_delay(G->round);
    }
    /* the moose comes in the middle of its round */
    if (G->moose_pending && G->spawned >= G->to_spawn / 2) {
        G->moose_pending = 0;
        int before = G->spawned;
        spawn_zombie(ZT_MOOSE);
        if (G->spawned > before) { G->moose_count++; G->to_spawn++; banner(0xd8a040, tr("THE MOOSE IS HERE"), 0); sfx(SFX_MOOSE, 1, 0); shake(5); }
    }
    if (G->spawned >= G->to_spawn && zombies_alive() == 0) {
        G->rstate = RS_BREAK; G->rtime = 0;
        sfx(SFX_ROUND_END, 1, 0);
        G->round_flash = 2.5f;
    }
}

void kill_all_zombies(int give_kr) {
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        if (z->type == ZT_MOOSE) { damage_zombie(z, z->maxhp * 0.25f, 0, 0, 0, 0); continue; }
        z->state = ZS_DEAD; z->t = 0;
        G->p.kills++;
        spawn_parts(PT_BLOOD, z->x, z->y - 8, 8, 0x8a1010, 60);
        world_decal_blood(z->x, z->y, 6);
    }
    if (give_kr) add_kr(give_kr, 0);
}

/* ---------------------------------------------------------------- one zombie */
static void steer(Zombie *z, float *dx, float *dy) {
    Player *p = &G->p;
    float tx = p->x, ty = p->y;
    uint16_t (*field)[MAPW_MAX] = G->flow;
    if (G->lure_on && z->type != ZT_MOOSE) { tx = G->lure_x; ty = G->lure_y; field = G->lureflow; }
    float ddx = tx - z->x, ddy = ty - z->y, d = sqrtf(ddx * ddx + ddy * ddy);
    if (d < 90 && walk_clear(z->x, z->y, tx, ty, 4)) { *dx = ddx / (d + 0.01f); *dy = ddy / (d + 0.01f); return; }
    int cx = (int)(z->x / TS), cy = (int)((z->y - 2) / TS);
    if (cx < 0 || cy < 0 || cx >= G->w || cy >= G->h) { *dx = ddx / (d + 0.01f); *dy = ddy / (d + 0.01f); return; }
    uint16_t best = field[cy][cx]; int bx = cx, by = cy;
    static const int dd[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    for (int k = 0; k < 8; k++) {
        int nx = cx + dd[k][0], ny = cy + dd[k][1];
        if (nx < 0 || ny < 0 || nx >= G->w || ny >= G->h) continue;
        if (k >= 4 && (solid_at(nx, cy) || solid_at(cx, ny))) continue;
        if (field[ny][nx] < best) { best = field[ny][nx]; bx = nx; by = ny; }
    }
    if (best == 0xFFFF) { *dx = ddx / (d + 0.01f); *dy = ddy / (d + 0.01f); return; }
    float gx = bx * TS + TS / 2.0f - z->x, gy = by * TS + TS / 2.0f + 2 - z->y, gd = sqrtf(gx * gx + gy * gy);
    if (gd < 0.5f) { *dx = ddx / (d + 0.01f); *dy = ddy / (d + 0.01f); return; }
    *dx = gx / gd; *dy = gy / gd;
}

static void zombie_update(Zombie *z, float dt) {
    Player *p = &G->p;
    if (z->flash > 0) z->flash -= dt;
    if (z->slow > 0) z->slow -= dt;
    if (z->burn > 0) { z->burn -= dt; if (rng_chance(&G->fx, 0.3f)) spawn_parts(PT_FIRE, z->x, z->y - 10, 1, 0xffa030, 10); }
    switch (z->state) {
    case ZS_DEAD:
        z->t += dt;
        if (z->t > 6) z->alive = 0;                          /* the body fades */
        return;
    case ZS_RISE:
        z->t -= dt;
        if (rng_chance(&G->fx, 0.2f)) spawn_parts(PT_DUST, z->x, z->y, 1, G->season == SEASON_WINTER ? 0xe8eef4 : 0x5a4636, 20);
        if (z->t <= 0) z->state = ZS_CHASE;
        return;
    case ZS_WINDOW: {                                       /* pull the boards off, then climb out */
        Inter *it = &G->it[z->window];
        z->vx = z->vy = 0;                                  /* shots don't push it out of the frame */
        /* whoever stands right at the gap gets a swipe through it, as in Call of Duty */
        if (z->atk_cd > 0) z->atk_cd -= dt;
        if (z->swipe > 0) {
            z->swipe -= dt;
            if (z->swipe <= 0) {
                if (!p->downed && dist2f(p->x, p->y, it->x, it->y) < 20 * 20) player_hurt(50, z->x, z->y);
                z->atk_cd = 1.3f;
            }
            return;
        }
        if (it->state < 6 && z->atk_cd <= 0 && !p->downed && dist2f(p->x, p->y, it->x, it->y) < 15 * 15) {
            z->swipe = 0.4f; sfx_at(SFX_ZATTACK, z->x, z->y, 0.5f);
            return;
        }
        z->t += dt;
        if (it->state > 0) {
            if (z->t > 1.0f) { z->t = 0; it->state--; sfx_at(SFX_BOARD_BREAK, z->x, z->y, 0.6f); spawn_parts(PT_WOOD, z->x, z->y + 4, 4, 0x8a6a3a, 50); }
        } else if (z->t > 0.6f) {                           /* out, into the middle of the tile in front */
            z->state = ZS_CHASE; z->x = it->tx * TS + TS / 2.0f; z->y = (it->ty + 1) * TS + TS / 2.0f;
            unstick_actor(&z->x, &z->y, 5);
        }
        return;
    }
    default: break;
    }
    float slow = z->slow > 0 ? 0.35f : 1.0f;
    if (z->burn > 0) slow *= 0.8f;
    float pdx = p->x - z->x, pdy = p->y - z->y, pd = sqrtf(pdx * pdx + pdy * pdy);
    float reach = z->type == ZT_MOOSE ? 22 : z->type == ZT_BRUTE ? 16 : z->type == ZT_WOLF ? 13 : 14;
    if (z->atk_cd > 0) z->atk_cd -= dt;
    if (z->state == ZS_ATTACK) {
        z->t -= dt;
        if (z->t <= 0) {
            if (pd < reach + 4 && !p->downed) {
                float dmg = z->type == ZT_WOLF ? 40 : z->type == ZT_BRUTE ? 90 : z->type == ZT_MOOSE ? 120 : 60;
                player_hurt(dmg, z->x, z->y);
            }
            z->state = ZS_CHASE; z->atk_cd = z->type == ZT_WOLF ? 0.7f : 1.0f;
        }
        return;
    }
    if (z->type == ZT_MOOSE) {                               /* the moose lowers its head, then charges */
        if (z->state == ZS_WINDUP) {
            z->t -= dt;
            if (z->t <= 0) { z->state = ZS_CHARGE; z->t = 1.3f; sfx_at(SFX_MOOSE, z->x, z->y, 1); }
            return;
        }
        if (z->state == ZS_CHARGE) {
            z->t -= dt;
            int hit = move_actor(&z->x, &z->y, z->cx * 170 * dt, z->cy * 170 * dt, 9);
            if (rng_chance(&G->fx, 0.5f)) spawn_parts(PT_DUST, z->x, z->y, 1, G->season == SEASON_WINTER ? 0xe8eef4 : 0x6a5a4a, 30);
            if (pd < 22 && z->atk_cd <= 0) { player_hurt(150, z->x - z->cx * 10, z->y - z->cy * 10); z->atk_cd = 1.5f; }
            if (hit) { z->state = ZS_STUN; z->t = 1.2f; shake(5); sfx_at(SFX_EXPLODE, z->x, z->y, 0.5f); }
            else if (z->t <= 0) z->state = ZS_CHASE;
            return;
        }
        if (z->state == ZS_STUN) { z->t -= dt; if (z->t <= 0) z->state = ZS_CHASE; return; }
        if (pd < 180 && pd > 40 && z->atk_cd <= 0 && line_clear(z->x, z->y - 10, p->x, p->y - 8)) {
            z->state = ZS_WINDUP; z->t = 0.8f; z->cx = pdx / pd; z->cy = pdy / pd; z->atk_cd = 4.0f;
            return;
        }
    }
    if (pd < reach && z->atk_cd <= 0 && !p->downed && !G->lure_on) {
        z->state = ZS_ATTACK; z->t = z->type == ZT_WOLF ? 0.2f : 0.35f;
        sfx_at(z->type == ZT_WOLF ? SFX_WOLF : SFX_ZATTACK, z->x, z->y, 0.5f);
        return;
    }
    float dx, dy;
    steer(z, &dx, &dy);
    /* keep apart from the others */
    float sx = 0, sy = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *o = &G->z[i];
        if (o == z || !o->alive || o->state == ZS_DEAD || o->state == ZS_RISE || o->state == ZS_WINDOW) continue;
        float ox = z->x - o->x, oy = z->y - o->y, d2 = ox * ox + oy * oy;
        float md = (z->type == ZT_MOOSE || o->type == ZT_MOOSE) ? 22 : 10;
        if (d2 < md * md && d2 > 0.01f) { float d = sqrtf(d2); sx += ox / d * (md - d) / md; sy += oy / d * (md - d) / md; }
    }
    float vx = (dx + sx * 0.9f) * z->speed * slow, vy = (dy + sy * 0.9f) * z->speed * slow;
    /* knockback decays */
    vx += z->vx; vy += z->vy; z->vx *= 0.8f; z->vy *= 0.8f;
    float r = z->type == ZT_MOOSE ? 9 : z->type == ZT_BRUTE ? 6 : 5;
    unstick_actor(&z->x, &z->y, r);
    move_actor(&z->x, &z->y, vx * dt, vy * dt, r);
    z->anim += dt * (z->speed / 9.0f) * slow;
    if (fabsf(dx) > fabsf(dy)) z->dir = dx > 0 ? 2 : 3; else z->dir = dy > 0 ? 0 : 1;
    /* groans now and then */
    if (rng_chance(&G->fx, dt * 0.15f)) sfx_at(z->type == ZT_WOLF ? SFX_WOLF : SFX_GROAN1 + rng_int(&G->fx, 3), z->x, z->y, 0.35f);
    /* stuck far away for long: it comes back somewhere else (Black Ops does this too) */
    z->stuck_t += dt;
    if (z->stuck_t > 5) {
        if (dist2f(z->x, z->y, z->lastx, z->lasty) < 12 * 12 && (pd > 160 || (pd > 30 && !walk_clear(z->x, z->y, p->x, p->y, 4)))) {
            z->alive = 0; G->spawned--;                      /* back into the pool: it comes again from a spawn */
        }
        z->stuck_t = 0; z->lastx = z->x; z->lasty = z->y;
    }
    /* the last zombie of a round runs (from round 4) */
    if (G->round >= 4 && G->spawned >= G->to_spawn && zombies_alive() == 1 && z->type == ZT_WALKER && z->speed < 60) z->speed = 62;
}

void zombies_update(float dt) {
    for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive) zombie_update(&G->z[i], dt);
    if (G->lure_on) {
        int any = 0;
        for (int i = 0; i < MAX_GRENADES; i++) if (G->gr[i].alive && G->gr[i].kind == C_SMALLARE) any = 1;
        if (!any) G->lure_on = 0;
    }
}
