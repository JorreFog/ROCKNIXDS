// loot.c: the roguelike part. Loot in bins, cars, mailboxes and sheds, dropped by the dead, restocked every
// round; its rarity climbs with the round. Power-ups follow Black Ops' rules: 3% a kill, a points-based drop
// that gets further away each time, four a round at most, dealt from a shuffled deck.
#include "game.h"
#include <stdio.h>

/* rarity odds move up with the round (and a bonus for better sources) */
int roll_rarity(int bonus) {
    float r = (float)G->round + bonus * 4;
    float w[RAR_COUNT] = { MAX(12.0f, 100 - 4.5f * r), 30 + 4 * r, 9 + 2.6f * r, 2.5f + 1.3f * r, 0.6f + 0.55f * r };
    float tot = 0; for (int i = 0; i < RAR_COUNT; i++) tot += w[i];
    float x = rng_float(&G->rng) * tot;
    for (int i = 0; i < RAR_COUNT; i++) { x -= w[i]; if (x <= 0) return i; }
    return RAR_COMMON;
}

Armor armor_make(int def, int rar) {
    static const float k[RAR_COUNT] = { 1.0f, 1.2f, 1.45f, 1.75f, 2.1f };
    Armor a; a.def = (int16_t)def; a.rar = (int8_t)rar; a.max = a.ap = ARMORS[def].ap * k[rar];
    return a;
}

static Item *new_item(float x, float y) {
    for (int i = 0; i < MAX_ITEMS; i++) if (!G->items[i].alive) {
        Item *it = &G->items[i]; memset(it, 0, sizeof *it);
        it->alive = 1; it->x = x; it->y = y; it->life = 90; it->w.def = -1; it->ar.def = -1;
        return it;
    }
    /* full: replace the oldest */
    Item *o = &G->items[0];
    for (int i = 1; i < MAX_ITEMS; i++) if (G->items[i].t > o->t) o = &G->items[i];
    memset(o, 0, sizeof *o); o->alive = 1; o->x = x; o->y = y; o->life = 90; o->w.def = -1; o->ar.def = -1;
    return o;
}
void item_drop(int kind, int id, int rar, int n, float x, float y) {
    Item *it = new_item(x, y);
    it->kind = kind; it->id = id; it->rar = rar; it->n = n;
}
void item_drop_weapon(Weapon w, float x, float y) {
    Item *it = new_item(x, y);
    it->kind = IK_WEAPON; it->w = w; it->rar = w.rar; it->life = 120;
}
void item_drop_armor(Armor a, float x, float y) {
    Item *it = new_item(x, y);
    it->kind = IK_ARMOR; it->ar = a; it->rar = a.rar; it->life = 120;
}

/* something random: weights per kind, rarity from the round */
static void random_item(float x, float y, int bonus, int allow_big) {
    Rng *r = &G->rng;
    float k = rng_float(r);
    if (allow_big && k < 0.12f) {                           /* a weapon */
        static const int pool[] = { W_PIST88, W_REVOLVER, W_KPIST, W_AK5, W_AK4, W_HAGEL, W_STUDSARE, W_KSP58, W_PSKOTT };
        int d = pool[rng_int(r, ARRAY_LEN(pool))];
        Weapon w = weapon_make(d, roll_rarity(bonus));
        w.reserve = (int16_t)(weapon_reserve_max(&w) / 2);
        item_drop_weapon(w, x, y);
    } else if (allow_big && k < 0.27f) {                    /* armour */
        int rar = roll_rarity(bonus);
        int d = rng_int(r, A_COUNT);
        if (G->round < 5 && (d == A_KRAVALL || d == A_KRAVALLHJALM)) d = rng_chance(r, 0.5f) ? A_REFLEXVAST : A_CYKELHJALM;
        item_drop_armor(armor_make(d, rar), x, y);
    } else if (k < 0.47f) {                                 /* ammo */
        item_drop(IK_AMMO, 0, RAR_COMMON, 1, x, y);
    } else if (k < 0.62f) {                                 /* cash: a wallet */
        int n = (25 + rng_int(r, 50)) * (1 + G->round / 4);
        item_drop(IK_CASH, 0, RAR_COMMON, n, x, y);
    } else {                                                /* a consumable */
        static const int cw[C_COUNT] = { 26, 8, 14, 10, 8, 10, 10 };
        int tot = 0; for (int i = 0; i < C_COUNT; i++) tot += cw[i];
        int x2 = rng_int(r, tot), id = 0;
        for (int i = 0; i < C_COUNT; i++) { x2 -= cw[i]; if (x2 < 0) { id = i; break; } }
        item_drop(IK_CONS, id, RAR_COMMON, id == C_PLASTER ? 2 : 1, x, y);
    }
}

void loot_container(Inter *it) {
    Prop *p = it->prop >= 0 ? &G->props[it->prop] : 0;
    if (!p || p->loot != 1) return;
    p->loot = 2;
    float x = it->x, y = it->y;
    int big = p->kind == P_CAR || p->kind == P_CAR_V || p->kind == P_CONTAINER || p->kind == P_RECYCLE;
    int n = rng_chance(&G->rng, 0.18f) ? 0 : 1 + (rng_chance(&G->rng, big ? 0.45f : 0.2f));
    if (!n) { msg(0xa0a0a0, "%s", tr("Empty")); }
    for (int k = 0; k < n; k++) random_item(x + (k ? 8 : 0), y + (k ? 3 : 0), big, 1);
    if (p->kind == P_MAILBOXES && n) item_drop(IK_CASH, 0, RAR_COMMON, (40 + rng_int(&G->rng, 60)) * (1 + G->round / 4), x - 8, y);
    world_repaint_rect(p->x / TS - 2, p->y / TS - 2, 5, 4);
    sfx(SFX_PICKUP, 0.4f, 0);
}

/* a new round: searched containers in open zones fill up again, and a few things appear on the ground */
void loot_restock(int count) {
    for (int tries = 0; tries < count * 20 && count > 0; tries++) {
        Prop *p = &G->props[rng_int(&G->rng, MAX(1, G->nprops))];
        if (p->loot != 2 || !G->zones[zone_at(p->x, p->y - 4)].open) continue;
        p->loot = 1; count--;
        world_repaint_rect(p->x / TS - 2, p->y / TS - 2, 5, 4);
    }
    for (int k = 0; k < 2; k++) {
        for (int tries = 0; tries < 60; tries++) {
            int z = rng_int(&G->rng, G->nzones);
            if (!G->zones[z].open) continue;
            Zone *zn = &G->zones[z];
            int tx = zn->x + 1 + rng_int(&G->rng, zn->w - 2), ty = zn->y + 1 + rng_int(&G->rng, zn->h - 2);
            Tile *t = tile_at(tx, ty);
            if (!t || (t->f & (TF_SOLID | TF_WATER | TF_INTER))) continue;
            random_item(tx * TS + 8, ty * TS + 12, 1, G->round >= 3);
            break;
        }
    }
}

void loot_zombie_drop(float x, float y) {
    if (rng_chance(&G->rng, 0.035f)) random_item(x, y, 0, rng_chance(&G->rng, 0.2f));
}

/* ---------------------------------------------------------------- the bag */
int bag_count(int id) { int n = 0; for (int i = 0; i < BAG_SLOTS; i++) if (G->p.bag[i].id == id) n += G->p.bag[i].n; return n; }
static int bag_room(int id) {
    for (int i = 0; i < BAG_SLOTS; i++) if (G->p.bag[i].id == id && G->p.bag[i].n < CONS[id].stack) return 1;
    for (int i = 0; i < BAG_SLOTS; i++) if (G->p.bag[i].id < 0) return 1;
    return 0;
}
void bag_add(int id, int n) {
    Player *p = &G->p;
    for (int i = 0; i < BAG_SLOTS && n > 0; i++) if (p->bag[i].id == id) { int m = MIN(n, CONS[id].stack - p->bag[i].n); p->bag[i].n += m; n -= m; }
    for (int i = 0; i < BAG_SLOTS && n > 0; i++) if (p->bag[i].id < 0) { p->bag[i].id = id; p->bag[i].n = MIN(n, CONS[id].stack); n -= p->bag[i].n; if (p->bag[p->bag_sel].id < 0) p->bag_sel = i; }
}
void bag_use(int slot) {
    Player *p = &G->p;
    if (slot < 0 || slot >= BAG_SLOTS || p->bag[slot].id < 0 || G->over || p->downed) return;
    int id = p->bag[slot].id, used = 1;
    switch (id) {
    case C_PLASTER: if (p->hp >= p->maxhp) { used = 0; break; } p->hp = MIN(p->maxhp, p->hp + 30); sfx(SFX_PICKUP, 0.5f, 0); break;
    case C_FORBAND: if (p->hp >= p->maxhp) { used = 0; break; } p->hp = p->maxhp; sfx(SFX_PICKUP, 0.6f, 0); break;
    case C_GRANAT: if (p->grenades >= 4) { used = 0; break; } p->grenades++; break;
    case C_SMALLARE: throw_grenade(C_SMALLARE); break;
    case C_MOLOTOV: throw_grenade(C_MOLOTOV); break;
    case C_TEJP: {
        used = 0;
        for (int k = 0; k < 2; k++) if (p->ar[k].def >= 0 && p->ar[k].ap < p->ar[k].max) { p->ar[k].ap = MIN(p->ar[k].max, p->ar[k].ap + p->ar[k].max * 0.5f); used = 1; }
        if (used) sfx(SFX_BOARD_FIX, 0.5f, 0);
        break;
    }
    case C_KAFFE: p->coffee_t = 20; p->stamina = (p->perks & (1u << PK_BLABAR)) ? 8 : 4; sfx(SFX_GULP, 0.6f, 0); break;
    }
    if (!used) { sfx(SFX_DENY, 0.4f, 0); return; }
    if (--p->bag[slot].n <= 0) {
        p->bag[slot].id = -1; p->bag[slot].n = 0;
        for (int k = 1; k <= BAG_SLOTS; k++) { int c = (slot + k) % BAG_SLOTS; if (p->bag[c].id >= 0) { p->bag_sel = c; break; } }
    }
}

/* walking over small things picks them up; weapons and armour wait for the use button (inter.c) */
void items_update(float dt) {
    Player *p = &G->p;
    for (int i = 0; i < MAX_ITEMS; i++) {
        Item *it = &G->items[i];
        if (!it->alive) continue;
        it->t += dt;
        if (it->t > it->life) { it->alive = 0; continue; }
        if (it->kind == IK_WEAPON || it->kind == IK_ARMOR || p->downed || G->over) continue;
        if (dist2f(it->x, it->y, p->x, p->y) > 12 * 12) continue;
        switch (it->kind) {
        case IK_AMMO: {
            Weapon *w = &p->w[p->cur];
            if (w->def < 0 || w->reserve >= weapon_reserve_max(w)) { int any = 0; for (int k = 0; k < p->nslots; k++) if (p->w[k].def >= 0 && p->w[k].reserve < weapon_reserve_max(&p->w[k])) { w = &p->w[k]; any = 1; break; } if (!any) continue; }
            int add = MAX(weapon_mag(w), weapon_reserve_max(w) / 3);
            w->reserve = (int16_t)MIN(weapon_reserve_max(w), w->reserve + add);
            msg(0xe0e0a0, "%s +%d (%s)", tr("Ammo"), add, weapon_name(w));
            break;
        }
        case IK_CASH: add_kr(it->n, 0); { char b[24]; snprintf(b, sizeof b, "+%d kr", it->n); float_text(it->x, it->y - 10, 0xf0d040, b); } break;
        case IK_CONS:
            if (it->id == C_GRANAT && p->grenades < 4) { p->grenades++; break; }
            if (!bag_room(it->id)) { if (it->t > 0.5f && (int)(it->t * 2) % 6 == 0) msg(0xff8060, "%s", tr("Bag full")); continue; }
            bag_add(it->id, it->n);
            msg(0xe0e0e0, "%s %s", tr("Picked up"), CONS[it->id].name);
            break;
        }
        it->alive = 0;
        sfx(SFX_PICKUP, 0.5f, 0);
    }
}

/* ---------------------------------------------------------------- power-ups */
static void deck_shuffle(void) {
    G->deck_n = 0;
    for (int k = 0; k < PU_COUNT; k++) {
        if (k == PU_FIRESALE && !G->box_moves) continue;     /* Fire Sale only after the box has moved */
        G->deck[G->deck_n++] = k;
    }
    for (int i = G->deck_n - 1; i > 0; i--) { int j = rng_int(&G->rng, i + 1); int t = G->deck[i]; G->deck[i] = G->deck[j]; G->deck[j] = t; }
    G->deck_i = 0;
}
void powerup_spawn(int kind, float x, float y) {
    for (int i = 0; i < MAX_POWERUPS; i++) if (!G->pu[i].alive) {
        PowerUp *p = &G->pu[i]; p->alive = 1; p->kind = kind; p->x = x; p->y = y; p->t = 0;
        sfx_at(SFX_POWERUP_SPAWN, x, y, 0.8f);
        return;
    }
}
void powerup_try_drop(float x, float y) {
    if (G->drops_round >= 4) return;
    if (!G->zones[zone_at(x, y)].open) return;
    if (G->deck_i >= G->deck_n) deck_shuffle();
    int k = G->deck[G->deck_i++];
    if (k == PU_CARPENTER) {                                 /* only worth it if windows are broken */
        int broken = 0; for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_WINDOW && G->it[i].state < 6) broken++;
        if (broken < 3) k = PU_DOUBLE;
    }
    if (k == PU_FIRESALE && !G->box_moves) k = PU_MAXAMMO;
    G->drops_round++;
    powerup_spawn(k, x, y);
}

static const char *PU_NAME[PU_COUNT] = { "MAX AMMO", "INSTA-KILL", "DOUBLE POINTS", "KABOOM", "CARPENTER", "FIRE SALE" };
static void powerup_take(PowerUp *pu) {
    Player *p = &G->p;
    banner(0x60ff60, tr(PU_NAME[pu->kind]), 0);
    sfx(SFX_POWERUP, 1, 0);
    spawn_parts(PT_CONFETTI, pu->x, pu->y - 6, 20, 0x80ff80, 80);
    switch (pu->kind) {
    case PU_MAXAMMO:
        for (int k = 0; k < p->nslots; k++) if (p->w[k].def >= 0) p->w[k].reserve = (int16_t)weapon_reserve_max(&p->w[k]);
        p->grenades = 4;
        break;
    case PU_INSTAKILL: G->insta_t = 30; break;
    case PU_DOUBLE: G->double_t = 30; break;
    case PU_KABOOM: G->flash_t = 0.4f; shake(8); sfx(SFX_KABOOM, 1, 0); kill_all_zombies(400); break;
    case PU_CARPENTER:
        for (int i = 0; i < G->nit; i++) if (G->it[i].type == IT_WINDOW) G->it[i].state = 6;
        add_kr(200, 0); sfx(SFX_BOARD_FIX, 1, 0);
        break;
    case PU_FIRESALE: G->firesale_t = 30; break;
    }
}
void powerups_update(float dt) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        PowerUp *pu = &G->pu[i];
        if (!pu->alive) continue;
        pu->t += dt;
        if (pu->t > 26.5f) { pu->alive = 0; continue; }        /* 15 s solid, then it blinks for 11.5 s */
        if (!G->p.downed && !G->over && dist2f(pu->x, pu->y, G->p.x, G->p.y) < 14 * 14) { powerup_take(pu); pu->alive = 0; }
    }
}
