// game.c: a run from start to game over. The player, moving and colliding, the camera, the effects, and the
// order the other modules are updated in each tick (60 a second).
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

Game *G;

/* ---------------------------------------------------------------- messages and effects */
void msg(uint32_t col, const char *fmt, ...) {
    char b[64]; va_list ap0; va_start(ap0, fmt); vsnprintf(b, sizeof b, fmt, ap0); va_end(ap0);
    if (G->msg_t[0] > 0 && !strcmp(b, G->msg[0])) { G->msg_t[0] = 6; return; }   /* the same again: just keep it up */
    for (int i = 3; i > 0; i--) { memcpy(G->msg[i], G->msg[i - 1], sizeof G->msg[0]); G->msg_t[i] = G->msg_t[i - 1]; G->msg_col[i] = G->msg_col[i - 1]; }
    va_list ap; va_start(ap, fmt); vsnprintf(G->msg[0], sizeof G->msg[0], fmt, ap); va_end(ap);
    G->msg_t[0] = 6; G->msg_col[0] = col;
}
void banner(uint32_t col, const char *a, const char *b) {
    if (G->boss.pending && G->banner_t > 0.5f) return;      /* (a boss's name stays up until it comes) */
    if (boss_holds_banner()) return;                        /* (and when it's felled, for a moment) */
    snprintf(G->banner, sizeof G->banner, "%s", a ? a : "");
    snprintf(G->banner2, sizeof G->banner2, "%s", b ? b : "");
    G->banner_t = 3.0f; G->banner_col = col;
}
void float_text(float x, float y, uint32_t col, const char *s) {
    for (int i = 0; i < MAX_TEXTS; i++) if (!G->ft[i].alive) {
        FloatText *f = &G->ft[i];
        f->alive = 1; f->x = x; f->y = y; f->vy = -18; f->t = 0.9f; f->col = col; f->screen = 0;
        snprintf(f->s, sizeof f->s, "%s", s);
        return;
    }
}
void shake(float a) { if (S.shake) G->shake = MAX(G->shake, a); }

Part *spawn_parts(int type, float x, float y, int n, uint32_t col, float speed) {
    Rng *r = &G->fx;
    Part *last = 0;
    if (n > 1 && render_fx_light()) n = (n + 1) / 2;       /* the light effects: half the bits */
    for (int k = 0; k < n; k++) {
        Part *p = 0;
        for (int j = 0; j < MAX_PARTS; j++) {
            int i = (G->part_next + j) % MAX_PARTS;
            if (!G->parts[i].alive) { p = &G->parts[i]; G->part_next = (i + 1) % MAX_PARTS; break; }
        }
        if (!p) return last;
        last = p;
        memset(p, 0, sizeof *p);
        p->alive = 1; p->type = type; p->x = x; p->y = y; p->col = col;
        float a = rng_float(r) * 2 * PI_F, sp = speed * (0.4f + rng_float(r) * 0.8f);
        p->vx = cosf(a) * sp; p->vy = sinf(a) * sp * 0.7f;
        p->z = 4 + rng_float(r) * 6; p->vz = 20 + rng_float(r) * 60;
        p->max = p->life = 0.4f + rng_float(r) * 0.6f;
        p->size = 1;
        switch (type) {
        case PT_BLOOD: p->life = p->max = 0.5f + rng_float(r) * 0.4f; break;
        case PT_SMOKE: p->vz = 8 + rng_float(r) * 10; p->vx *= 0.3f; p->vy *= 0.3f; p->life = p->max = 0.8f + rng_float(r) * 0.8f; p->size = 2 + rng_float(r) * 2; break;
        case PT_SHELL: p->vz = 40 + rng_float(r) * 30; p->life = p->max = 0.6f; break;
        case PT_FIRE: p->vz = 18 + rng_float(r) * 20; p->vx *= 0.3f; p->vy *= 0.3f; p->life = p->max = 0.3f + rng_float(r) * 0.4f; break;
        case PT_SPARK: case PT_ELEC: p->vz = 30 + rng_float(r) * 40; p->life = p->max = 0.2f + rng_float(r) * 0.3f; break;
        case PT_DUST: p->vz = 10 + rng_float(r) * 20; p->life = p->max = 0.5f + rng_float(r) * 0.5f; break;
        case PT_GAS: p->vz = 4; p->vx *= 0.2f; p->vy *= 0.2f; p->life = p->max = 1.0f + rng_float(r); p->size = 2; break;
        case PT_GLOW: p->vz = 20; p->vx *= 0.2f; p->vy *= 0.2f; p->life = p->max = 0.8f; break;
        case PT_CONFETTI: p->vz = 60 + rng_float(r) * 60; p->life = p->max = 1.2f; break;
        }
    }
    return last;
}

static void parts_update(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &G->parts[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0) {
            if (p->type == PT_BLOOD && rng_chance(&G->fx, 0.5f)) world_decal_blood(p->x, p->y, 1);
            p->alive = 0; continue;
        }
        p->x += p->vx * dt; p->y += p->vy * dt;
        if (p->type == PT_SMOKE || p->type == PT_FIRE || p->type == PT_GAS || p->type == PT_GLOW) { p->z += p->vz * dt; continue; }
        p->vz -= 220 * dt; p->z += p->vz * dt;
        if (p->z < 0) {
            p->z = 0;
            if (p->type == PT_BLOOD) { world_decal_blood(p->x, p->y, 1); p->alive = 0; continue; }
            p->vz = -p->vz * 0.35f; p->vx *= 0.6f; p->vy *= 0.6f;
        }
    }
    for (int i = 0; i < MAX_TEXTS; i++) {
        FloatText *f = &G->ft[i];
        if (!f->alive) continue;
        f->t -= dt; f->y += f->vy * dt; f->vy *= 0.94f;
        if (f->t <= 0) f->alive = 0;
    }
    for (int i = 0; i < 64; i++) if (G->tr[i].alive && (G->tr[i].life -= dt) <= 0) G->tr[i].alive = 0;
}

/* ---------------------------------------------------------------- collision */
/* a box of half size r around (x, y) against solid tiles */
static int blocked(float x, float y, float r) {
    int x0 = (int)floorf((x - r) / TS), x1 = (int)floorf((x + r - 0.01f) / TS);
    int y0 = (int)floorf((y - r) / TS), y1 = (int)floorf((y + r - 0.01f) / TS);
    for (int ty = y0; ty <= y1; ty++) for (int tx = x0; tx <= x1; tx++) if (solid_at(tx, ty)) return 1;
    return 0;
}
/* moves with sliding; corners nudge the mover around them. Returns 1 if anything stopped it */
int move_actor(float *x, float *y, float dx, float dy, float r) {
    int hit = 0;
    float steps = ceilf(MAX(fabsf(dx), fabsf(dy)) / 4.0f); if (steps < 1) steps = 1;
    float sx = dx / steps, sy = dy / steps;
    for (int i = 0; i < (int)steps; i++) {
        if (sx != 0) {
            if (!blocked(*x + sx, *y, r)) *x += sx;
            else {
                hit = 1;
                /* slide past a corner: try a little up or down */
                if (fabsf(sy) < 0.01f) for (int k = 1; k <= 5; k++) {
                    if (!blocked(*x + sx, *y - k, r) && !blocked(*x, *y - k, r)) { *y -= MIN(1.0f, fabsf(sx)); break; }
                    if (!blocked(*x + sx, *y + k, r) && !blocked(*x, *y + k, r)) { *y += MIN(1.0f, fabsf(sx)); break; }
                }
            }
        }
        if (sy != 0) {
            if (!blocked(*x, *y + sy, r)) *y += sy;
            else {
                hit = 1;
                if (fabsf(sx) < 0.01f) for (int k = 1; k <= 5; k++) {
                    if (!blocked(*x - k, *y + sy, r) && !blocked(*x - k, *y, r)) { *x -= MIN(1.0f, fabsf(sy)); break; }
                    if (!blocked(*x + k, *y + sy, r) && !blocked(*x + k, *y, r)) { *x += MIN(1.0f, fabsf(sy)); break; }
                }
            }
        }
    }
    return hit;
}

/* something standing partly in a wall (climbing out of a window, pushed by a blast) is moved to the nearest place
   it fits, within a tile; 0 if there was none */
int unstick_actor(float *x, float *y, float r) {
    if (!blocked(*x, *y, r)) return 1;
    for (int d = 1; d <= 16; d++)
        for (int k = 0; k < 8; k++) {
            static const int o[8][2] = { {0,1},{0,-1},{1,0},{-1,0},{1,1},{-1,1},{1,-1},{-1,-1} };
            float nx = *x + o[k][0] * d, ny = *y + o[k][1] * d;
            if (!blocked(nx, ny, r)) { *x = nx; *y = ny; return 1; }
        }
    return 0;
}
/* a body r wide could walk the straight line between two points (nothing solid on the way) */
int walk_clear(float x0, float y0, float x1, float y1, float r) {
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 4) + 1;
    for (int i = 0; i <= n; i++) if (blocked(x0 + dx * i / n, y0 + dy * i / n, r)) return 0;
    return 1;
}
/* nothing opaque between two points (bullets, sight, light) */
int line_clear(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 6) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        if (opaque_at((int)floorf((x0 + dx * t) / TS), (int)floorf((y0 + dy * t) / TS))) return 0;
    }
    return 1;
}

/* a boarded window: shots go through the gaps (and hit whoever is pulling the boards off) */
int window_tile(int tx, int ty) { Tile *t = tile_at(tx, ty); return t && t->inter && G->it[t->inter - 1].type == IT_WINDOW; }
/* nothing a bullet stops at between two points */
int shot_clear(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 6) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        int tx = (int)floorf((x0 + dx * t) / TS), ty = (int)floorf((y0 + dy * t) / TS);
        if (opaque_at(tx, ty) && !window_tile(tx, ty)) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- the player */
float player_speed(void) {
    Player *p = &G->p;
    float s = 70;
    if (p->perks & (1u << PK_BLABAR)) s *= 1.12f;
    if (p->coffee_t > 0) s *= 1.15f;
    Weapon *w = &p->w[p->cur];
    if (w->def >= 0 && (WEAPONS[w->def].cls == WC_LMG || WEAPONS[w->def].cls == WC_LAUNCHER)) s *= 0.92f;
    return s;
}

static int held(const Input *in, int b) { return (in->held & BIT(b)) != 0; }
static int pressed(const Input *in, const Input *prev, int b) { return (in->held & BIT(b)) && !(prev->held & BIT(b)); }

/* the button that fires and the one that interacts (Settings: swap A/B) */
int btn_fire(void) { return S.swap_ab ? B_B : B_A; }
int btn_use(void) { return S.swap_ab ? B_A : B_B; }

static float aim_assist(float aim) {
    if (!S.assist) return aim;
    Player *p = &G->p;
    float cone = S.assist == 1 ? 0.35f : 0.65f, best = 1e9f, out = aim;
    float range = 230;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!zombie_hittable(z)) continue;
        float dx = z->x - p->x, dy = (z->y - 6) - (p->y - 6), d = sqrtf(dx * dx + dy * dy);
        if (d > range || d < 1) continue;
        float a = atan2f(dy, dx), da = fabsf(angdiff(a, aim));
        if (da > cone) continue;
        if (!shot_clear(p->x, p->y - 6, z->x, z->y - 6)) continue;
        float score = da / cone + d / range * 0.8f;
        if (score < best) { best = score; out = a; }
    }
    return out;
}

static void player_down(void) {
    Player *p = &G->p;
    p->downs++;
    int near = 0; for (int i = 0; i < MAX_ZOMBIES; i++) if (G->z[i].alive && G->z[i].state != ZS_DEAD && dist2f(G->z[i].x, G->z[i].y, p->x, p->y) < 40 * 40) near++;
    plat_log("%s in round %d at %.0f s: %d kills, %d zombies within 40 px, %.0f/%.0f armour", (p->perks & (1u << PK_KANELBULLE)) ? "down" : "game over",
             G->round, G->time, p->kills, near, p->ar[0].def >= 0 ? p->ar[0].ap : 0, p->ar[1].def >= 0 ? p->ar[1].ap : 0);
    if (p->perks & (1u << PK_KANELBULLE)) {               /* solo Quick Revive: you get back up, without your perks */
        if ((p->perks & (1u << PK_KAVIAR)) && p->nslots == 3) {   /* Mule Kick's third gun goes with it */
            if (p->w[2].def >= 0) item_drop_weapon(p->w[2], p->x + 6, p->y + 4);
            p->w[2].def = -1; p->nslots = 2;
            if (p->cur == 2) p->cur = p->w[0].def >= 0 ? 0 : 1;
        }
        p->downed = 1; p->down_t = 4.0f; p->perks = 0; p->nperks = 0;
        msg(0xffd070, "%s", tr("Downed!"));
        sfx(SFX_HURT, 1, 0);
        return;
    }
    G->over = 1; G->over_t = 0;
    p->hp = 0;
    sfx(SFX_GAMEOVER, 1, 0);
    music_play(MUS_GAMEOVER);
}

void player_hurt(float dmg, float fx, float fy) {
    Player *p = &G->p;
    if (p->invuln > 0 || p->downed || G->over || G->god) return;
    /* armour takes its share first: the vest more than the helmet */
    float share[2] = { 0.3f, 0.6f };
    for (int k = 0; k < 2; k++) {
        Armor *a = &p->ar[k];
        if (a->def < 0 || a->ap <= 0) continue;
        float take = MIN(dmg * share[k], a->ap);
        a->ap -= take; dmg -= take;
        if (a->ap <= 0.5f) { msg(0xff8060, "%s!", ARMORS[a->def].name); a->def = -1; a->ap = 0; }
    }
    p->hp -= dmg; p->regen_t = 0; p->hurt_t = 0.35f; p->last_hit_t = G->time;
    G->hurt_flash = 1.0f;
    shake(3);
    float dx = p->x - fx, dy = p->y - fy, d = sqrtf(dx * dx + dy * dy) + 0.01f;
    move_actor(&p->x, &p->y, dx / d * 5, dy / d * 5, 5);
    sfx(SFX_HURT, 0.8f, 0);
    spawn_parts(PT_BLOOD, p->x, p->y - 8, 6, 0xa01010, 50);
    if (p->hp <= 0) { p->hp = 0; player_down(); }
}

static void player_update(const Input *in, const Input *prev, float dt) {
    Player *p = &G->p;
    if (p->downed) {                                       /* getting back up with the Kanelbulle */
        p->down_t -= dt;
        if (p->down_t <= 0) {
            p->downed = 0; p->hp = p->maxhp = 100; p->invuln = 3.0f; p->revives++;
            msg(0x80ff80, "%s", tr("Revived"));
            for (int i = 0; i < MAX_ZOMBIES; i++) {        /* getting up throws the nearest ones back */
                Zombie *z = &G->z[i];
                if (!z->alive || z->state == ZS_DEAD) continue;
                float dx = z->x - p->x, dy = z->y - p->y, d = sqrtf(dx * dx + dy * dy);
                if (d > 90 || d < 0.1f) continue;
                z->vx += dx / d * 260; z->vy += dy / d * 260; z->slow = MAX(z->slow, 2.0f); z->atk_cd = 1.5f;
            }
            spawn_parts(PT_GLOW, p->x, p->y - 8, 24, 0xffd070, 90);
        }
        return;
    }
    if (G->over) return;
    p->maxhp = (p->perks & (1u << PK_JULMUST)) ? 250 : 100;
    /* health comes back 2.4 s after the last hit (5 s below 20%) */
    p->regen_t += dt;
    float delay = p->hp < p->maxhp * 0.2f ? 5.0f : 2.4f;
    if (p->regen_t > delay && p->hp < p->maxhp) p->hp = MIN(p->maxhp, p->hp + p->maxhp * dt * 0.8f);
    if (p->invuln > 0) p->invuln -= dt;
    if (p->hurt_t > 0) p->hurt_t -= dt;
    if (p->coffee_t > 0) p->coffee_t -= dt;
    if (p->muzzle_t > 0) p->muzzle_t -= dt;

    /* movement: d-pad or the left stick */
    float mx = 0, my = 0;
    if (held(in, B_LEFT)) mx -= 1;
    if (held(in, B_RIGHT)) mx += 1;
    if (held(in, B_UP)) my -= 1;
    if (held(in, B_DOWN)) my += 1;
    if (in->has_sticks && (fabsf(in->lx) > 0.01f || fabsf(in->ly) > 0.01f)) { mx = in->lx; my = in->ly; }
    float ml = sqrtf(mx * mx + my * my);
    if (ml > 1) { mx /= ml; my /= ml; ml = 1; }

    /* fire and aim */
    int twin = S.scheme == 1;
    int fire_held = 0;
    float aim = p->aim;
    int aim_locked = 0;
    if (twin) {                                            /* X up, B down, Y left, A right: fire that way */
        float ax = 0, ay = 0;
        if (held(in, B_X)) ay -= 1;
        if (held(in, B_B)) ay += 1;
        if (held(in, B_Y)) ax -= 1;
        if (held(in, B_A)) ax += 1;
        if (ax != 0 || ay != 0) { aim = atan2f(ay, ax); fire_held = 1; aim_locked = 1; }
    } else {
        fire_held = held(in, btn_fire());
    }
    if (in->has_sticks && (fabsf(in->rx) > 0.25f || fabsf(in->ry) > 0.25f)) {
        aim = atan2f(in->ry, in->rx); aim_locked = 1;
        if (sqrtf(in->rx * in->rx + in->ry * in->ry) > 0.6f) fire_held = 1;
    }
    if (in->touch[0] && S.touch_aim) {                    /* the top screen's own touch: aim there and fire */
        float tx = G->camx + in->tx[0], ty = G->camy + in->ty[0];
        aim = atan2f(ty - (p->y - 8), tx - p->x); aim_locked = 1; fire_held = 1;
    }
    if (in->mouse) {                                       /* desktop: the mouse aims */
        float tx = G->camx + in->mx, ty = G->camy + in->my;
        aim = atan2f(ty - (p->y - 8), tx - p->x); aim_locked = 1;
    }
    if (!aim_locked) {
        if (!fire_held && ml > 0.1f) aim = atan2f(my, mx);  /* facing follows walking, until you fire: then you strafe */
        else if (fire_held && ml > 0.1f && !(prev->held & BIT(btn_fire()))) aim = atan2f(my, mx);
        if (fire_held) aim = aim_assist(aim);
    } else if (!in->mouse && !in->touch[0]) aim = aim_assist(aim);
    p->aim = aim;

    /* sprint: L1 while moving, not while shooting */
    int want_sprint = held(in, twin ? B_L1 : B_L1) && ml > 0.2f && !fire_held && !p->reloading;
    float stmax = (p->perks & (1u << PK_BLABAR)) ? 8 : 4;
    if (want_sprint && p->stamina > 0) { p->sprinting = 1; p->stamina -= dt; }
    else { p->sprinting = 0; p->stamina = MIN(stmax, p->stamina + dt * 0.6f); }
    if (p->stamina <= 0) p->sprinting = 0;
    float sp = player_speed() * (p->sprinting ? 1.45f : 1.0f);
    if (fire_held && !p->sprinting) sp *= 0.9f;
    float ox = p->x, oy = p->y;
    move_actor(&p->x, &p->y, mx * sp * dt, my * sp * dt, 5);
    p->vx = (p->x - ox) / dt; p->vy = (p->y - oy) / dt;
    if (ml > 0.1f) {
        p->anim += dt * (p->sprinting ? 11 : 7);
        p->step_t -= dt;
        if (p->step_t <= 0) { p->step_t = p->sprinting ? 0.22f : 0.34f; sfx(SFX_STEP, 0.15f, 0); }
    } else p->anim = 0;
    /* zombies are in the way: push out of them */
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE || z->state == ZS_WINDOW) continue;
        float zr = z->type == ZT_MOOSE ? 14 : z->type == ZT_BOSS ? (G->boss.hidden || z->variant == BOSS_HAXAN ? 0 : 11) : z->type == ZT_BRUTE ? 8 : 6;   /* (the witch is up in the air) */
        if (zr <= 0) continue;
        float dx = p->x - z->x, dy = p->y - z->y, d = sqrtf(dx * dx + dy * dy), md = 5 + zr;
        if (d < md && d > 0.01f) {
            float push = (md - d) * 0.6f;
            move_actor(&p->x, &p->y, dx / d * push, dy / d * push, 5);
        }
    }
    /* which way the sprite faces */
    float ca = cosf(p->aim), sa = sinf(p->aim);
    if (fabsf(ca) > fabsf(sa)) p->dir = ca > 0 ? 2 : 3; else p->dir = sa > 0 ? 0 : 1;

    /* weapons */
    if (p->fire_cd > 0) p->fire_cd -= dt;
    if (p->swap_t > 0) p->swap_t -= dt;
    if (p->melee_cd > 0) p->melee_cd -= dt;
    if (p->melee_t > 0) p->melee_t -= dt;
    Weapon *w = &p->w[p->cur];
    if (p->reloading) {
        p->reload_t -= dt;
        if (p->reload_t <= 0) {
            p->reloading = 0;
            int need = weapon_mag(w) - w->mag, take = MIN(need, w->reserve);
            w->mag += take; w->reserve -= take;
        }
    }
    int reload_btn = twin ? B_SELECT : B_Y;
    if (pressed(in, prev, reload_btn) && !p->reloading && w->def >= 0 && w->mag < weapon_mag(w) && w->reserve > 0) {
        p->reloading = 1; p->reload_len = p->reload_t = weapon_reload(w);
        sfx(SFX_RELOAD, 0.6f, 0);
        if (p->perks & (1u << PK_LINGON)) {               /* Lingondricka: a shock around you, stronger the emptier */
            float k = 1.0f - (float)w->mag / MAX(1, weapon_mag(w));
            float r = 30 + 40 * k;
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                Zombie *z = &G->z[i];
                if (!z->alive || z->state == ZS_DEAD) continue;
                if (dist2f(z->x, z->y, p->x, p->y) < r * r) { damage_zombie(z, 300 + 600 * k + zombie_hp_for_round(G->round) * 0.15f * k, 0, 0, 0, 0); z->slow = MAX(z->slow, 1.0f); }
            }
            spawn_parts(PT_ELEC, p->x, p->y - 6, 18, 0xa0d0ff, 120);
            sfx(SFX_ZAP, 0.5f, 0);
        }
    }
    int swap_btn = twin ? B_R2 : B_X;
    if (pressed(in, prev, swap_btn) && p->swap_t <= 0) {
        int n = p->nslots;
        for (int k = 1; k < n; k++) {
            int c = (p->cur + k) % n;
            if (p->w[c].def >= 0) { p->cur = c; p->swap_t = 0.35f; p->reloading = 0; sfx(SFX_SWAP, 0.5f, 0); break; }
        }
        w = &p->w[p->cur];
    }
    if (!twin && pressed(in, prev, B_R1) && p->melee_cd <= 0) melee_attack();
    int nade_btn = twin ? B_L2 : B_R2;
    if (pressed(in, prev, nade_btn) && p->grenades > 0) throw_grenade(C_GRANAT);
    int item_btn = twin ? B_START : B_L2;
    if (!twin && pressed(in, prev, item_btn)) bag_use(p->bag_sel);
    if (!twin && pressed(in, prev, B_SELECT)) {              /* next item in the bag */
        for (int k = 1; k <= BAG_SLOTS; k++) { int c = (p->bag_sel + k) % BAG_SLOTS; if (p->bag[c].id >= 0) { p->bag_sel = c; break; } }
    }
    if (fire_held && !p->sprinting && p->swap_t <= 0 && p->melee_t <= 0) {
        if (twin) {                                          /* point blank in the twin scheme: the knife */
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                Zombie *z = &G->z[i];
                if (zombie_hittable(z) && z->state == ZS_CHASE && dist2f(z->x, z->y, p->x + cosf(aim) * 10, p->y + sinf(aim) * 10) < 14 * 14 && p->melee_cd <= 0) { melee_attack(); break; }
            }
        }
        if (p->melee_t <= 0) weapon_fire();
    }
    if (!fire_held) p->fired_this_press = 0;
}

/* ---------------------------------------------------------------- camera */
static void camera_update(float dt) {
    Player *p = &G->p;
    float lead = 26;
    float tx = p->x + cosf(p->aim) * lead - G->view_w / 2.0f, ty = p->y - 8 + sinf(p->aim) * lead * 0.8f - G->view_h / 2.0f;
    float bx, by;
    if (boss_focus(&bx, &by)) {                             /* a boss coming up, greeting you, falling: the view leans its way (you stay in it) */
        float mx = G->view_w / 2.0f - 30, my = G->view_h / 2.0f - 30;
        tx += clampf((bx - p->x) * 0.5f, -mx, mx); ty += clampf((by - p->y) * 0.5f, -my, my);
    }
    float k = 1 - expf(-dt * 6);
    G->camx += (tx - G->camx) * k; G->camy += (ty - G->camy) * k;
    G->camx = clampf(G->camx, 0, (float)(G->ww - G->view_w));
    G->camy = clampf(G->camy, 0, (float)(G->wh - G->view_h));
    if (G->shake > 0) G->shake = MAX(0, G->shake - dt * 12);
    if (G->hurt_flash > 0) G->hurt_flash = MAX(0, G->hurt_flash - dt * 2);
    if (G->flash_t > 0) G->flash_t -= dt;
    if (G->season == SEASON_AUTUMN && !G->over) {           /* a storm: lightning now and then, the thunder after it */
        if ((G->storm_t -= dt) <= 0) {
            G->storm_t = 25 + rng_float(&G->fx) * 60; G->lightning_t = 0.3f; G->thunder_t = 0.6f + rng_float(&G->fx) * 1.6f;
        }
        if (G->thunder_t > 0 && (G->thunder_t -= dt) <= 0) sfx(SFX_THUNDER, 0.8f, rng_rangef(&G->fx, -0.5f, 0.5f));
    }
    if (G->lightning_t > 0) G->lightning_t -= dt;
}

/* ---------------------------------------------------------------- a run */
void game_free(void) { if (G) { free(G->world); G->world = 0; } }

void game_new(uint64_t seed, int season) {
    int vw = G->view_w, vh = G->view_h;
    game_free();
    memset(G, 0, sizeof *G);
    G->view_w = vw; G->view_h = vh;
    G->seed = seed; G->season = season;
    G->storm_t = 20;                                        /* (the first lightning comes a while in) */
    rng_seed(&G->rng, seed, 1); rng_seed(&G->fx, seed, 2);
    map_generate(seed, season);
    world_paint();
    prop_lights();
    Player *p = &G->p;
    Zone *sz = &G->zones[G->start_zone];
    p->x = sz->cx * TS + TS / 2; p->y = sz->cy * TS + TS - 2;
    p->hp = p->maxhp = 100; p->nslots = 2; p->stamina = 4;
    p->w[0] = weapon_make(W_PIST88, RAR_COMMON);
    p->w[0].reserve = 68;
    p->w[1].def = p->w[2].def = -1;
    p->ar[0].def = p->ar[1].def = -1;
    for (int i = 0; i < BAG_SLOTS; i++) p->bag[i].id = -1;
    p->grenades = 2;
    p->kr = 500;
    p->aim = PI_F / 2;
    G->camx = p->x - G->view_w / 2; G->camy = p->y - G->view_h / 2;
    G->box_at = 0;
    G->drop_inc = 2000; G->drop_target = 2500;   /* points-based drops: the first at 2000 + 500 per player */
    G->wolf_next = rng_range(&G->rng, 5, 7);
    G->moose_next = rng_range(&G->rng, 9, 11);
    banner(0xd02020, G->town, G->zones[G->start_zone].name);
    /* test hooks: DK_DEBUG_ROUND=N starts there, DK_DEBUG_KR=N with that much money, DK_DEBUG_POWER=1 powered,
     * DK_DEBUG_OPEN=1 every barrier gone, DK_DEBUG_GOD=1 nothing hurts (long runs deep in the rounds),
     * DK_DEBUG_GUN=N a legendary gun of that kind in the second slot (as a player that deep would have) */
    const char *e;
    G->god = getenv("DK_DEBUG_GOD") != 0;
    if ((e = getenv("DK_DEBUG_KR"))) p->kr = atoi(e);
    if ((e = getenv("DK_DEBUG_GUN")) && atoi(e) >= 0 && atoi(e) < W_COUNT) { p->w[1] = weapon_make(atoi(e), RAR_LEGENDARY); p->cur = 1; }
    if (getenv("DK_DEBUG_POWER")) { G->power_on = 1; world_power_wave(G->p.x, G->p.y); prop_lights(); }
    if (getenv("DK_DEBUG_OPEN"))
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_BARRIER) {
            Inter *it = &G->it[i]; it->state = 1;
            for (int j = 0; j < it->th; j++) for (int k = 0; k < it->tw; k++) { Tile *t = tile_at(it->tx + k, it->ty + j); t->f &= (uint8_t)~(TF_SOLID | TF_INTER); t->inter = 0; }
            zone_open(it->a); zone_open(it->b);
        }
    round_start((e = getenv("DK_DEBUG_ROUND")) ? MAX(1, atoi(e)) : 1);
}

void game_update(const Input *in, const Input *prev, float dt) {
    if (!G->over) dt *= boss_time_scale();                  /* (a boss's fall: slowed down a moment) */
    G->time += dt;
    if (G->over) { G->over_t += dt; parts_update(dt); camera_update(dt); return; }
    for (int i = 0; i < 4; i++) if (G->msg_t[i] > 0) G->msg_t[i] -= dt;
    if (G->banner_t > 0) G->banner_t -= dt;
    if (G->insta_t > 0) G->insta_t -= dt;
    if (G->double_t > 0) G->double_t -= dt;
    if (G->firesale_t > 0) G->firesale_t -= dt;
    world_update(dt);
    player_update(in, prev, dt);
    inter_update(in, prev, dt);
    flow_update(0);
    round_update(dt);
    zombies_update(dt);
    hazards_update(dt);
    shots_update(dt);
    grenades_update(dt);
    items_update(dt);
    powerups_update(dt);
    parts_update(dt);
    camera_update(dt);
}

void add_kr(int n, int scaled) {
    if (scaled && G->double_t > 0) n *= 2;
    G->p.kr += n;
    if (n > 0) {
        G->p.kr_total += n;
        if (G->p.kr_total >= G->drop_target && scaled) {   /* points-based power-up drop, at the next kill */
            G->drop_inc = (int)(G->drop_inc * 1.14f);
            G->drop_target = G->p.kr_total + G->drop_inc;
            G->drop_pending = 1;
        }
    }
}
