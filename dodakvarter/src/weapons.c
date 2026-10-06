// weapons.c: guns, what they hit, explosions, grenades and the knife. Most guns are hitscan (a ray through the
// tiles and the zombies, drawn as a tracer); the Pansarskott and the Strålpistol fire projectiles with splash,
// the Åskvigg chains lightning between zombies, the Snöblåsare freezes a cone.
#include "game.h"
#include <stdio.h>

static const float RAR_DMG[RAR_COUNT] = { 1.0f, 1.15f, 1.35f, 1.6f, 1.9f };
static const float RAR_MAG[RAR_COUNT] = { 1.0f, 1.0f, 1.15f, 1.3f, 1.5f };
static const float RAR_RES[RAR_COUNT] = { 1.0f, 1.1f, 1.2f, 1.3f, 1.5f };
static const float RAR_RELOAD[RAR_COUNT] = { 1.0f, 0.95f, 0.9f, 0.85f, 0.75f };

Weapon weapon_make(int def, int rar) {
    Weapon w; memset(&w, 0, sizeof w);
    w.def = (int16_t)def; w.rar = (int8_t)rar; w.pap = 0;
    w.mag = (int16_t)weapon_mag(&w); w.reserve = (int16_t)weapon_reserve_max(&w);
    return w;
}
float weapon_dmg(const Weapon *w) {
    const WeaponDef *d = &WEAPONS[w->def];
    float k = RAR_DMG[w->rar] * (w->pap ? (d->cls == WC_WONDER ? 1.5f : 2.0f) : 1.0f);
    if (G->p.perks & (1u << PK_SALMIAK)) k *= 1.5f;      /* Double Tap 2.0: more damage per bullet */
    return d->dmg * k;
}
float weapon_rpm(const Weapon *w) {
    float r = WEAPONS[w->def].rpm * (w->pap ? 1.1f : 1.0f);
    if (G->p.perks & (1u << PK_SALMIAK)) r *= 1.33f;
    return r;
}
int weapon_mag(const Weapon *w) {
    const WeaponDef *d = &WEAPONS[w->def];
    if (d->mag <= 1) return d->mag + (w->pap ? 1 : 0);
    return (int)(d->mag * RAR_MAG[w->rar] * (w->pap ? 1.5f : 1.0f) + 0.5f);
}
int weapon_reserve_max(const Weapon *w) {
    const WeaponDef *d = &WEAPONS[w->def];
    return (int)(d->reserve * RAR_RES[w->rar] * (w->pap ? 1.5f : 1.0f) + 0.5f);
}
float weapon_reload(const Weapon *w) {
    float t = WEAPONS[w->def].reload * RAR_RELOAD[w->rar];
    if (G->p.perks & (1u << PK_SNABBKAFFE)) t *= 0.5f;   /* Speed Cola */
    return t;
}
const char *weapon_name(const Weapon *w) { return w->pap ? WEAPONS[w->def].pap_name : WEAPONS[w->def].name; }

static void tracer(float x0, float y0, float x1, float y1, uint32_t col, int kind, float life) {
    for (int i = 0; i < 64; i++) if (!G->tr[i].alive) {
        Tracer *t = &G->tr[i];
        t->alive = 1; t->x0 = x0; t->y0 = y0; t->x1 = x1; t->y1 = y1; t->col = col; t->kind = kind; t->life = life;
        return;
    }
}

/* ---------------------------------------------------------------- damage */
static void zombie_killed(Zombie *z, int crit, int melee) {
    Player *p = &G->p;
    z->state = ZS_DEAD; z->t = 0;
    p->kills++;
    G->stats_zombies_by_type[z->type]++;
    if (melee) p->knifes++;
    if (crit) p->crits++;
    /* Black Ops' kill money: 50, +10 for the torso, +50 for a headshot (our crit), +80 for the knife */
    int kr = melee ? 130 : crit ? 100 : 60;
    if (z->type == ZT_MOOSE) kr = 500;
    add_kr(kr, 1);
    char b[16]; snprintf(b, sizeof b, "+%d", G->double_t > 0 ? kr * 2 : kr);
    float_text(z->x, z->y - 20, crit || melee ? 0xffd040 : 0xf0f0f0, b);
    spawn_parts(PT_BLOOD, z->x, z->y - 8, z->type == ZT_MOOSE ? 40 : 14, 0x8a1010, 70);
    world_decal_blood(z->x, z->y - 2, z->type == ZT_MOOSE ? 40 : 10);
    sfx_at(SFX_SPLAT, z->x, z->y, 0.6f);
    if (z->type == ZT_BLOATER) {                          /* it bursts into a cloud that hurts */
        for (int i = 0; i < MAX_CLOUDS; i++) if (!G->clouds[i].alive) {
            Cloud *c = &G->clouds[i]; c->alive = 1; c->x = z->x; c->y = z->y - 4; c->r = 26; c->t = 0; c->dur = 6; c->kind = 1;
            break;
        }
        spawn_parts(PT_GAS, z->x, z->y - 6, 26, 0x9ab040, 40);
    }
    /* drops: the pending points drop, else 3% (never more than 4 a round) */
    if (z->type == ZT_WOLF && G->special == 1 && G->spawned >= G->to_spawn && zombies_alive() <= 1) {
        powerup_spawn(PU_MAXAMMO, z->x, z->y);             /* the last wolf brings Max Ammo */
    } else if (G->drop_pending || rng_chance(&G->rng, 0.03f)) {
        if (G->drop_pending && G->drops_round < 4) G->drop_pending = 0;
        powerup_try_drop(z->x, z->y);
    }
    if (z->type == ZT_MOOSE) {
        Weapon w = weapon_make(rng_chance(&G->rng, 0.5f) ? W_STRAL : W_STUDSARE, MAX(RAR_RARE, roll_rarity(2)));
        item_drop_weapon(w, z->x, z->y);
        item_drop_armor(armor_make(rng_chance(&G->rng, 0.5f) ? A_KRAVALL : A_KRAVALLHJALM, roll_rarity(2)), z->x + 12, z->y);
        shake(6);
    } else loot_zombie_drop(z->x, z->y);
}

void damage_zombie(Zombie *z, float dmg, int crit, int melee, float kx, float ky) {
    if (!z->alive || z->state == ZS_DEAD) return;
    if (G->insta_t > 0 && z->type != ZT_MOOSE) dmg = z->hp + 1;   /* Insta-Kill */
    if (crit) dmg *= 2;
    z->hp -= dmg; z->flash = 0.08f;
    if (z->type != ZT_MOOSE) { z->vx += kx; z->vy += ky; }
    spawn_parts(PT_BLOOD, z->x, z->y - 9, 3, 0x9a1414, 50);
    if (z->hp <= 0) zombie_killed(z, crit, melee);
    else {
        add_kr(10, 1);                                    /* every hit that doesn't kill: +10 */
        sfx_at(SFX_HIT, z->x, z->y, 0.35f);
    }
}

/* the closest zombie whose body the segment passes through, after `from` along it */
static Zombie *ray_hit(float x0, float y0, float dx, float dy, float maxd, float *hitd, Zombie **skip, int nskip) {
    Zombie *best = 0; float bd = maxd;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE) continue;
        int skipit = 0; for (int k = 0; k < nskip; k++) if (skip[k] == z) skipit = 1;
        if (skipit) continue;
        float r = z->type == ZT_MOOSE ? 16 : z->type == ZT_BRUTE ? 9 : z->type == ZT_WOLF ? 6 : 7;
        float cx = z->x, cy = z->y - (z->type == ZT_MOOSE ? 14 : z->type == ZT_WOLF ? 5 : 8);
        float px = cx - x0, py = cy - y0, t = px * dx + py * dy;
        if (t < 0 || t > bd) continue;
        float ex = px - dx * t, ey = py - dy * t;
        if (ex * ex + ey * ey <= r * r) { bd = t; best = z; }
    }
    *hitd = bd;
    return best;
}

/* how far a ray goes before a wall */
static float ray_wall(float x0, float y0, float dx, float dy, float maxd) {
    for (float t = 2; t < maxd; t += 3) {
        int tx = (int)floorf((x0 + dx * t) / TS), ty = (int)floorf((y0 + dy * t) / TS);
        if (opaque_at(tx, ty) && !window_tile(tx, ty)) return t;
    }
    return maxd;
}

static void fire_bullet(float x0, float y0, float ang, const Weapon *w, float dmg, int pierce, float range, int draw) {
    float dx = cosf(ang), dy = sinf(ang);
    float wall = ray_wall(x0, y0, dx, dy, range);
    Zombie *hit[8]; int nh = 0;
    float end = wall;
    for (;;) {
        float hd; Zombie *z = ray_hit(x0, y0, dx, dy, wall, &hd, hit, nh);
        if (!z) break;
        int crit = rng_chance(&G->rng, 0.12f) || (WEAPONS[w->def].cls == WC_SNIPER && rng_chance(&G->rng, 0.3f));
        damage_zombie(z, dmg, crit, 0, dx * 18, dy * 18);
        hit[nh++] = z;
        if (nh > pierce || nh >= 8) { end = hd; break; }
        dmg *= 0.75f;
    }
    if (end >= wall && wall < range) spawn_parts(PT_SPARK, x0 + dx * wall, y0 + dy * wall, 3, 0xffe0a0, 60);
    if (draw) tracer(x0, y0, x0 + dx * end, y0 + dy * end, WEAPONS[w->def].tracer, w->pap ? 1 : 0, 0.05f);
}

static void spawn_shot(int type, float x, float y, float ang, float speed, float dmg, float splash, const Weapon *w) {
    for (int i = 0; i < MAX_SHOTS; i++) if (!G->shots[i].alive) {
        Shot *s = &G->shots[i]; memset(s, 0, sizeof *s);
        s->alive = 1; s->type = type; s->x = s->x0 = x; s->y = s->y0 = y; s->vx = cosf(ang) * speed; s->vy = sinf(ang) * speed;
        s->life = 2.0f; s->dmg = dmg; s->splash = splash; s->w = *w; s->col = WEAPONS[w->def].tracer;
        if (w->pap && w->def == W_STRAL) s->col = 0x40ffd0;
        return;
    }
}

static void chain_lightning(float x, float y, float ang, const Weapon *w) {   /* Åskvigg: a bolt that jumps on */
    float dmg = weapon_dmg(w);
    int jumps = w->pap ? 14 : 8;
    float cx = x, cy = y;
    Zombie *hit[16]; int nh = 0;
    float range = 150;
    float dirx = cosf(ang), diry = sinf(ang);
    for (int j = 0; j < jumps; j++) {
        Zombie *best = 0; float bd = 1e9f;
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            Zombie *z = &G->z[i];
            if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE) continue;
            int done = 0; for (int k = 0; k < nh; k++) if (hit[k] == z) done = 1;
            if (done) continue;
            float dx = z->x - cx, dy = z->y - 8 - cy, d = sqrtf(dx * dx + dy * dy);
            if (d > (j == 0 ? range : 70)) continue;
            if (j == 0 && (dx * dirx + dy * diry) / (d + 0.01f) < 0.7f) continue;
            if (!shot_clear(cx, cy, z->x, z->y - 8)) continue;
            if (d < bd) { bd = d; best = z; }
        }
        if (!best) break;
        tracer(cx, cy, best->x, best->y - 8, w->pap ? 0xd0a0ff : 0xa0d0ff, 2, 0.15f);
        spawn_parts(PT_ELEC, best->x, best->y - 8, 6, 0xc0e0ff, 80);
        cx = best->x; cy = best->y - 8;
        hit[nh++] = best;
        damage_zombie(best, dmg, 0, 0, 0, 0);
    }
    if (!nh) tracer(x, y, x + dirx * 80, y + diry * 80, 0xa0d0ff, 2, 0.12f);
    G->flash_t = 0.06f;
}

static void frost_cone(float x, float y, float ang, const Weapon *w) {     /* Snöblåsare: freeze what's in front */
    float dmg = weapon_dmg(w) * (1 + G->round * 0.08f), range = WEAPONS[w->def].range * (w->pap ? 1.3f : 1.0f);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE) continue;
        float dx = z->x - x, dy = z->y - 8 - y, d = sqrtf(dx * dx + dy * dy);
        if (d > range) continue;
        if (fabsf(angdiff(atan2f(dy, dx), ang)) > 0.4f) continue;
        if (!shot_clear(x, y, z->x, z->y - 8)) continue;
        z->slow = MAX(z->slow, 2.5f);
        damage_zombie(z, dmg, 0, 0, dx / (d + 1) * 6, dy / (d + 1) * 6);
    }
    for (int k = 0; k < 3; k++) {
        float a = ang + rng_rangef(&G->fx, -0.35f, 0.35f);
        Part *p = 0;
        spawn_parts(PT_FROST, x + cosf(a) * 6, y + sinf(a) * 6, 1, 0xe0f4ff, 0);
        for (int i = MAX_PARTS - 1; i >= 0; i--) if (G->parts[i].alive && G->parts[i].type == PT_FROST && G->parts[i].life == G->parts[i].max) { p = &G->parts[i]; break; }
        if (p) { p->vx = cosf(a) * range * 1.6f; p->vy = sinf(a) * range * 1.6f; p->z = 8; p->vz = 0; p->life = p->max = 0.4f; }
    }
}

/* ---------------------------------------------------------------- firing */
void weapon_fire(void) {
    Player *p = &G->p;
    Weapon *w = &p->w[p->cur];
    if (w->def < 0 || p->reloading || p->fire_cd > 0) return;
    const WeaponDef *d = &WEAPONS[w->def];
    if (w->mag <= 0) {
        if (w->reserve > 0) { p->reloading = 1; p->reload_len = p->reload_t = weapon_reload(w); sfx(SFX_RELOAD, 0.6f, 0); }
        else if (!p->fired_this_press) { sfx(SFX_EMPTY, 0.6f, 0); msg(0xff8060, "%s", tr("No ammo")); }
        p->fired_this_press = 1;
        p->fire_cd = 0.25f;
        return;
    }
    if (d->fire == FM_SEMI && p->fired_this_press && d->cls != WC_PISTOL) {}  /* semi guns repeat when held */
    p->fire_cd = 60.0f / weapon_rpm(w);
    if (d->fire == FM_SEMI && p->fired_this_press) p->fire_cd *= 1.15f;       /* holding is a little slower than tapping */
    p->fired_this_press = 1;
    w->mag--;
    float ang = p->aim;
    float mx = p->x + cosf(ang) * 9, my = p->y - 8 + sinf(ang) * 7;
    float spread = d->spread * PI_F / 180.0f;
    float dmg = weapon_dmg(w);
    p->muzzle_t = 0.05f;
    switch (d->proj) {
    case PR_BULLET:
        for (int k = 0; k < d->pellets; k++) {
            float a = ang + rng_rangef(&G->rng, -spread, spread) * (d->pellets > 1 ? 1.0f : 0.5f + 0.5f * (p->vx != 0 || p->vy != 0));
            fire_bullet(mx, my, a, w, dmg, d->pierce + (w->pap ? 1 : 0), d->range, d->pellets < 4 || k % 2 == 0);
        }
        spawn_parts(PT_SHELL, p->x, p->y - 8, 1, 0xd8b040, 30);
        break;
    case PR_ROCKET: spawn_shot(PR_ROCKET, mx, my, ang, 230, dmg, d->splash * (w->pap ? 1.3f : 1.0f), w); break;
    case PR_PLASMA: spawn_shot(PR_PLASMA, mx, my, ang, 300, dmg, d->splash * (w->pap ? 1.4f : 1.0f), w); break;
    case PR_LIGHTNING: chain_lightning(mx, my, ang, w); break;
    case PR_FROST: frost_cone(mx, my, ang, w); break;
    }
    static const int snd[] = { SFX_PISTOL, SFX_SMG, SFX_RIFLE, SFX_SHOTGUN, SFX_SNIPER, SFX_LMG, SFX_ROCKET, SFX_RAY };
    int s = d->proj == PR_LIGHTNING ? SFX_ZAP : d->proj == PR_FROST ? SFX_FROST : d->cls == WC_WONDER ? SFX_RAY : snd[d->cls];
    sfx(s, d->proj == PR_FROST ? 0.35f : 0.7f, 0);
    if (d->cls == WC_SHOTGUN || d->cls == WC_SNIPER || d->cls == WC_LAUNCHER) shake(2.5f);
    else shake(0.8f);
    if (d->cls == WC_SHOTGUN) { p->vx -= cosf(ang) * 20; }
    if (w->mag == 0 && w->reserve > 0) { p->reloading = 1; p->reload_len = p->reload_t = weapon_reload(w); sfx(SFX_RELOAD, 0.6f, 0); }
}

void explode(float x, float y, float r, float dmg, int from_player) {
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        float dx = z->x - x, dy = z->y - 6 - y, d = sqrtf(dx * dx + dy * dy);
        if (d > r) continue;
        float k = 1.0f - 0.6f * d / r;
        damage_zombie(z, dmg * k, 0, 0, dx / (d + 1) * 60, dy / (d + 1) * 60);
    }
    Player *p = &G->p;
    float pd = sqrtf(dist2f(p->x, p->y - 6, x, y));
    if (pd < r * 0.8f && from_player) player_hurt(30 * (1 - pd / r), x, y);
    spawn_parts(PT_FIRE, x, y, 24, 0xffa030, 70);
    spawn_parts(PT_SMOKE, x, y, 14, 0x3a3a3a, 30);
    spawn_parts(PT_SPARK, x, y, 12, 0xffe080, 140);
    world_decal_scorch(x, y + 2, r * 0.45f);
    shake(7);
    G->flash_t = 0.1f;
    sfx_at(SFX_EXPLODE, x, y, 1.0f);
    /* light flash */
    if (G->nlights < MAX_LIGHTS) {}
}

void shots_update(float dt) {
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &G->shots[i];
        if (!s->alive) continue;
        s->life -= dt;
        float nx = s->x + s->vx * dt, ny = s->y + s->vy * dt;
        int boom = s->life <= 0 || opaque_at((int)floorf(nx / TS), (int)floorf(ny / TS));
        if (!boom) for (int k = 0; k < MAX_ZOMBIES; k++) {
            Zombie *z = &G->z[k];
            if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE) continue;
            float r = z->type == ZT_MOOSE ? 16 : 8;
            if (dist2f(nx, ny, z->x, z->y - 8) < r * r) { boom = 1; if (s->type == PR_PLASMA) damage_zombie(z, s->dmg, rng_chance(&G->rng, 0.12f), 0, s->vx * 0.05f, s->vy * 0.05f); break; }
        }
        s->x = nx; s->y = ny;
        if (s->type == PR_ROCKET) spawn_parts(PT_SMOKE, s->x, s->y, 1, 0x6a6a6a, 6);
        if (s->type == PR_PLASMA && rng_chance(&G->fx, 0.5f)) spawn_parts(PT_GLOW, s->x, s->y, 1, s->col, 8);
        if (boom) {
            s->alive = 0;
            if (s->type == PR_ROCKET) explode(s->x, s->y, s->splash, s->dmg, 1);
            else if (s->type == PR_PLASMA) {                 /* the ray gun's splash: weaker, and it hurts you close up */
                for (int k = 0; k < MAX_ZOMBIES; k++) {
                    Zombie *z = &G->z[k];
                    if (!z->alive || z->state == ZS_DEAD) continue;
                    float d = sqrtf(dist2f(z->x, z->y - 6, s->x, s->y));
                    if (d < s->splash) damage_zombie(z, s->dmg * 0.4f, 0, 0, 0, 0);
                }
                spawn_parts(PT_GLOW, s->x, s->y, 10, s->col, 60);
                if (dist2f(G->p.x, G->p.y - 6, s->x, s->y) < 18 * 18) player_hurt(20, s->x, s->y);
            }
        }
    }
    for (int i = 0; i < MAX_CLOUDS; i++) {                 /* gas from burst bloaters, fire from fire bombs */
        Cloud *c = &G->clouds[i];
        if (!c->alive) continue;
        c->t += dt;
        if (c->t > c->dur) { c->alive = 0; continue; }
        if (rng_chance(&G->fx, 0.5f)) spawn_parts(c->kind ? PT_GAS : PT_FIRE, c->x + rng_rangef(&G->fx, -c->r, c->r) * 0.7f, c->y + rng_rangef(&G->fx, -c->r, c->r) * 0.5f, 1, c->kind ? 0x9ab040 : 0xffa030, 10);
        if (dist2f(G->p.x, G->p.y, c->x, c->y) < c->r * c->r && c->kind == 1) player_hurt(12 * dt * 4, c->x, c->y), G->p.invuln = 0;
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            Zombie *z = &G->z[k];
            if (!z->alive || z->state == ZS_DEAD) continue;
            if (dist2f(z->x, z->y, c->x, c->y) < c->r * c->r) {
                if (c->kind == 0) { z->burn = 1.0f; damage_zombie(z, (200 + zombie_hp_for_round(G->round) * 0.3f) * dt, 0, 0, 0, 0); }
                else z->slow = MAX(z->slow, 0.5f);
            }
        }
    }
}

/* ---------------------------------------------------------------- grenades, the knife */
void throw_grenade(int kind) {
    Player *p = &G->p;
    if (kind == C_GRANAT) { if (p->grenades <= 0) return; p->grenades--; }
    for (int i = 0; i < MAX_GRENADES; i++) if (!G->gr[i].alive) {
        Grenade *g = &G->gr[i];
        g->alive = 1; g->kind = kind; g->x = p->x; g->y = p->y - 4;
        g->vx = cosf(p->aim) * 150 + p->vx * 0.3f; g->vy = sinf(p->aim) * 150 + p->vy * 0.3f;
        g->z = 10; g->vz = 70; g->t = kind == C_SMALLARE ? 5.0f : kind == C_MOLOTOV ? 3.0f : 2.0f;
        sfx(SFX_THROW, 0.5f, 0);
        return;
    }
}

void grenades_update(float dt) {
    for (int i = 0; i < MAX_GRENADES; i++) {
        Grenade *g = &G->gr[i];
        if (!g->alive) continue;
        g->t -= dt;
        float nx = g->x + g->vx * dt, ny = g->y + g->vy * dt;
        if (solid_at((int)floorf(nx / TS), (int)floorf(g->y / TS))) { g->vx = -g->vx * 0.5f; nx = g->x; }
        if (solid_at((int)floorf(g->x / TS), (int)floorf(ny / TS))) { g->vy = -g->vy * 0.5f; ny = g->y; }
        g->x = nx; g->y = ny;
        g->vz -= 260 * dt; g->z += g->vz * dt;
        if (g->z < 0) {
            g->z = 0; g->vz = -g->vz * 0.4f; g->vx *= 0.6f; g->vy *= 0.6f;
            if (g->kind == C_MOLOTOV) g->t = 0;               /* a fire bomb bursts where it lands */
        }
        if (g->kind == C_SMALLARE) {                          /* firecrackers: every zombie comes to listen */
            G->lure_on = 1; G->lure_x = g->x; G->lure_y = g->y;
            if (rng_chance(&G->fx, 0.3f)) { spawn_parts(PT_SPARK, g->x, g->y - 2, 2, 0xffe060, 60); sfx_at(SFX_BEEP, g->x, g->y, 0.25f); }
        }
        if (g->t <= 0) {
            g->alive = 0;
            if (g->kind == C_MOLOTOV) {
                for (int k = 0; k < MAX_CLOUDS; k++) if (!G->clouds[k].alive) {
                    Cloud *c = &G->clouds[k]; c->alive = 1; c->x = g->x; c->y = g->y; c->r = 30; c->t = 0; c->dur = 7; c->kind = 0;
                    break;
                }
                spawn_parts(PT_FIRE, g->x, g->y, 30, 0xffa030, 60);
                sfx_at(SFX_EXPLODE, g->x, g->y, 0.5f);
            } else {
                float dmg = 300 + 120 * G->round + zombie_hp_for_round(G->round) * (g->kind == C_SMALLARE ? 0.5f : 0.4f);
                explode(g->x, g->y, g->kind == C_SMALLARE ? 40 : 52, dmg, 1);
                if (g->kind == C_SMALLARE) G->lure_on = 0;
            }
        }
    }
}

void melee_attack(void) {
    Player *p = &G->p;
    p->melee_cd = p->has_axe ? 0.6f : 0.5f; p->melee_t = 0.22f; p->melee_ang = p->aim;
    sfx(SFX_KNIFE, 0.6f, 0);
    float dmg = p->has_axe ? 1000 : 150;
    if (p->has_axe && G->round > 12) dmg += zombie_hp_for_round(G->round) * 0.25f;
    int hits = 0, maxhits = p->has_axe ? 3 : 1;
    /* the closest ones first */
    for (int n = 0; n < maxhits; n++) {
        Zombie *best = 0; float bd = 1e9f;
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            Zombie *z = &G->z[i];
            if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE || z->flash > 0.07f) continue;
            float dx = z->x - p->x, dy = z->y - p->y, d = sqrtf(dx * dx + dy * dy);
            float reach = z->type == ZT_MOOSE ? 30 : 22;
            if (d > reach || fabsf(angdiff(atan2f(dy, dx), p->aim)) > 1.1f) continue;
            if (d < bd) { bd = d; best = z; }
        }
        if (!best) break;
        damage_zombie(best, dmg, 0, 1, cosf(p->aim) * 50, sinf(p->aim) * 50);
        best->flash = 0.1f;
        hits++;
    }
    if (hits) shake(1.5f);
}
