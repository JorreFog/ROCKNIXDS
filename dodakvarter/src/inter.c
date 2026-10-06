// inter.c: spending kronor. Barriers, wall buys, the Mystery Box (Lådan, with a Dalahäst for a teddy bear), perk
// machines, the Pack-a-Punch (Smedjan), the power switch, boarded windows and loot, and picking things up.
#include "game.h"
#include <stdio.h>

#define USE_R 22.0f

static int use_button(void) { return btn_use(); }
static const char *use_label(void) { return btn_name(btn_use()); }

/* the nearest thing to use, or a ground item (returned as -2 - item index) */
static int target(float *outd) {
    Player *p = &G->p;
    float best = USE_R * USE_R; int bi = -1;
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type == IT_BARRIER && it->state) continue;
        if (it->type == IT_BOX && (G->box_spots[G->box_at] != i || G->box_moving)) continue;
        if (it->type == IT_LOOT && (it->prop < 0 || G->props[it->prop].loot != 1)) continue;
        if (it->type == IT_WINDOW && it->state >= 6) continue;
        if (it->type == IT_GNOME && it->state) continue;
        if (it->type == IT_PERK && it->a == PK_KANELBULLE && p->bulle_used >= 3) continue;
        float r2 = it->type == IT_BARRIER ? 30 * 30 : best;
        float d = dist2f(p->x, p->y, it->x, it->y);
        if (it->type == IT_BARRIER) {                      /* anywhere along the barrier's sides */
            float cx = clampf(p->x, it->tx * TS, (it->tx + it->tw) * TS), cy = clampf(p->y, it->ty * TS, (it->ty + it->th) * TS);
            d = dist2f(p->x, p->y, cx, cy);
            r2 = 14 * 14;
        }
        if (d < r2 && d < best) { best = d; bi = i; }
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        Item *it = &G->items[i];
        if (!it->alive || (it->kind != IK_WEAPON && it->kind != IK_ARMOR)) continue;
        float d = dist2f(p->x, p->y, it->x, it->y);
        if (d < 16 * 16 && d < best) { best = d; bi = -2 - i; }
    }
    if (outd) *outd = best;
    return bi;
}

static int has_weapon(int def) { for (int k = 0; k < G->p.nslots; k++) if (G->p.w[k].def == def) return k; return -1; }

/* a new weapon goes into a free slot, else replaces the one in hand (which drops) */
static void give_weapon(Weapon w, int drop_old) {
    Player *p = &G->p;
    for (int k = 0; k < p->nslots; k++) if (p->w[k].def < 0) { p->w[k] = w; p->cur = k; p->reloading = 0; p->swap_t = 0.3f; return; }
    if (drop_old && p->w[p->cur].def >= 0) item_drop_weapon(p->w[p->cur], p->x, p->y + 2);
    p->w[p->cur] = w; p->reloading = 0; p->swap_t = 0.3f;
}

static int pay(int cost) {
    if (G->p.kr < cost) { sfx(SFX_DENY, 0.6f, 0); msg(0xff6040, "%s", tr("Not enough kr")); return 0; }
    G->p.kr -= cost;
    sfx(SFX_BUY, 0.7f, 0);
    return 1;
}

/* ---------------------------------------------------------------- the Mystery Box */
void box_place(int spot) {
    G->box_at = spot; G->box_uses = 0;
    Inter *it = &G->it[G->box_spots[spot]];
    it->state = 0; it->t = 0;
}
static int box_roll(void) {
    Player *p = &G->p;
    int tot = 0;
    for (int i = 0; i < W_COUNT; i++) if (has_weapon(i) < 0) tot += WEAPONS[i].box_weight;
    int x = rng_int(&G->rng, MAX(1, tot));
    for (int i = 0; i < W_COUNT; i++) {
        if (has_weapon(i) >= 0) continue;
        x -= WEAPONS[i].box_weight;
        if (x < 0) return i;
    }
    (void)p;
    return W_KPIST;
}
static int teddy(void) {                                   /* Black Ops' bear odds, counted per location */
    if (G->firesale_t > 0 || G->nbox_spots < 2) return 0;
    int n = G->box_uses;                                    /* this pull's number */
    if (n <= 4) return 0;
    if (G->box_moves == 0) { if (n >= 9) return 1; return rng_chance(&G->rng, 0.15f); }
    if (n <= 8) return rng_chance(&G->rng, 0.15f);
    if (n <= 13) return rng_chance(&G->rng, 0.30f);
    return rng_chance(&G->rng, 0.50f);
}
static void box_update(Inter *it, float dt) {
    it->t += dt;
    if (it->state == 1 && it->t > 3.6f) {                  /* the spin ends */
        if (it->b < 0) {                                    /* the Dalahäst: money back, the box moves on */
            it->state = 3; it->t = 0;
            sfx_at(SFX_HORSE, it->x, it->y, 1); music_play(MUS_NONE);
            add_kr(it->c, 0);
            msg(0xd84a3a, "%s", tr("Box moved"));
        } else { it->state = 2; it->t = 0; music_play(MUS_NONE); }
    } else if (it->state == 2 && it->t > 12) {              /* not taken: it's gone */
        it->state = 0; it->t = 0;
    } else if (it->state == 3 && it->t > 2.5f) {
        G->box_moving = 1; G->box_t = 0; it->state = 0;
        G->box_moves++;
    }
}
static void boxes_tick(float dt) {
    if (G->box_moving) {
        G->box_t += dt;
        if (G->box_t > 3.0f) {
            G->box_moving = 0;
            int n = G->nbox_spots, cur = G->box_at, next = cur;
            for (int k = 0; k < 20 && next == cur; k++) next = rng_int(&G->rng, n);
            box_place(next);
        }
    }
    if (G->nbox_spots) box_update(&G->it[G->box_spots[G->box_at]], dt);
    /* Smedjan works on the weapon */
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type != IT_PAP || !it->state) continue;
        it->t += dt;
        if (it->state == 1 && it->t > 3.0f) { it->state = 2; it->t = 0; sfx_at(SFX_PAP, it->x, it->y, 0.8f); }
        else if (it->state == 2 && it->t > 15) { it->state = 0; it->t = 0; }
    }
}

/* ---------------------------------------------------------------- using things */
/* the elstängsel: zombies in the gap die, you get shocked, sparks; then it charges for a minute */
static void traps_tick(float dt) {
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type != IT_TRAP || !it->state || it->a < 0) continue;
        it->t -= dt;
        if (it->state == 2) { if (it->t <= 0) { it->state = 0; it->t = 0; } continue; }
        const Inter *b = &G->it[it->a];
        float x0 = b->tx * TS, y0 = b->ty * TS, x1 = x0 + b->tw * TS, y1 = y0 + b->th * TS, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            Zombie *z = &G->z[k];
            if (!z->alive || z->state == ZS_DEAD || z->state == ZS_RISE || z->state == ZS_WINDOW) continue;
            if (z->x < x0 - 3 || z->x > x1 + 3 || z->y < y0 || z->y > y1 + 6) continue;
            if (z->type == ZT_MOOSE || z->type == ZT_BOSS) { /* the moose takes it, slowly; a boss burns (and pays nothing for it) */
                if (z->type == ZT_BOSS && (G->boss.hidden || z->variant == BOSS_HAXAN)) continue;   /* (under the ground, or over it on a broom) */
                float dmg = z->type == ZT_BOSS ? z->maxhp * 0.04f * dt : 2500 * dt;
                if (z->type == ZT_BOSS) z->burn = 0.5f;
                if (z->hp - dmg <= 0) damage_zombie(z, dmg, 0, 0, 0, 0); else { z->hp -= dmg; z->flash = 0.05f; }
            } else zombie_shocked(z);
        }
        Player *p = &G->p;
        it->t2 -= dt;
        if (p->x > x0 - 3 && p->x < x1 + 3 && p->y > y0 && p->y < y1 + 6 && it->t2 <= 0) { player_hurt(45, cx, cy); it->t2 = 0.6f; }
        if (rng_chance(&G->fx, dt * 25)) spawn_parts(PT_ELEC, x0 + rng_float(&G->fx) * (x1 - x0), y0 + 4 + rng_float(&G->fx) * (y1 - y0), 2, 0xc0e0ff, 60);
        if (rng_chance(&G->fx, dt * 2.5f)) sfx_at(SFX_ZAP, cx, cy, 0.45f);
        if (it->t <= 0) { it->state = 2; it->t = TRAP_WAIT; }
    }
}

static void use_inter(int i) {
    Player *p = &G->p;
    Inter *it = &G->it[i];
    switch (it->type) {
    case IT_BARRIER:
        if (!pay(it->cost)) return;
        it->state = 1;
        for (int j = 0; j < it->th; j++) for (int k = 0; k < it->tw; k++) {
            Tile *t = tile_at(it->tx + k, it->ty + j);
            t->f &= (uint8_t)~(TF_SOLID | TF_INTER); t->inter = 0;
        }
        zone_open(it->a); zone_open(it->b);
        p->doors++;
        flow_update(1);
        spawn_parts(PT_DUST, it->x, it->y, 16, 0x8a8a8a, 50);
        sfx_at(SFX_DOOR, it->x, it->y, 1);
        msg(0x80d0ff, "%s: %s", tr("Zone opened"), G->zones[G->zones[it->a].open && zone_at(p->x, p->y) == it->a ? it->b : it->a].name);
        break;
    case IT_WALLBUY:
        if (it->a == -1) {                                  /* the axe */
            if (p->has_axe) { msg(0xa0a0a0, "%s", tr("Already have it")); return; }
            if (!pay(it->cost)) return;
            p->has_axe = 1; msg(0xffe080, "%s!", tr("Axe"));
        } else if (it->a == -2) {                           /* grenades */
            if (p->grenades >= 4) { msg(0xa0a0a0, "%s", tr("Already have it")); return; }
            if (!pay(it->cost)) return;
            p->grenades = 4;
        } else {
            int k = has_weapon(it->a);
            if (k >= 0) {                                   /* ammo: half the price, 4500 once upgraded */
                Weapon *w = &p->w[k];
                if (w->reserve >= weapon_reserve_max(w)) { msg(0xa0a0a0, "%s", tr("Already have it")); return; }
                if (!pay(w->pap ? 4500 : it->cost / 2)) return;
                w->reserve = (int16_t)weapon_reserve_max(w);
            } else {
                if (!pay(it->cost)) return;
                give_weapon(weapon_make(it->a, RAR_COMMON), 0);
                msg(0xffffff, "%s", WEAPONS[it->a].name);
            }
        }
        break;
    case IT_BOX:
        if (it->state == 2) {                               /* take what it offers */
            give_weapon(weapon_make(it->b, it->c), 0);
            msg(RARITY_COL[it->c], "%s [%s]", WEAPONS[it->b].name, tr(RARITY_NAME[it->c]));
            it->state = 0; it->t = 0;
            return;
        }
        if (it->state != 0) return;
        {
            int cost = G->firesale_t > 0 ? 10 : 950;
            if (!pay(cost)) return;
            G->box_uses++; p->boxes++;
            it->state = 1; it->t = 0; it->c = cost;
            if (teddy()) it->b = -1;
            else { it->b = box_roll(); it->c = MAX(RAR_UNCOMMON, roll_rarity(1)); }
            sfx_at(SFX_BOX, it->x, it->y, 0.8f);
            music_play(MUS_BOX);
        }
        break;
    case IT_PERK: {
        const PerkDef *pd = &PERKS[it->a];
        if (!G->power_on && it->a != PK_KANELBULLE) { msg(0xa0a0a0, "%s", tr("Needs power")); sfx(SFX_DENY, 0.5f, 0); return; }
        if (p->perks & (1u << it->a)) { msg(0xa0a0a0, "%s", tr("Already have it")); return; }
        if (p->nperks >= PERK_LIMIT) { msg(0xff8060, "%s", tr("Perk limit")); sfx(SFX_DENY, 0.5f, 0); return; }
        if (!pay(pd->cost)) return;
        p->perks |= 1u << it->a; p->nperks++;
        if (it->a == PK_KAVIAR) p->nslots = 3;
        if (it->a == PK_JULMUST) { p->maxhp = 250; p->hp = 250; }
        if (it->a == PK_KANELBULLE) p->bulle_used++;
        banner(pd->color2, pd->name, 0);
        sfx(SFX_PERK, 0.9f, 0); sfx(SFX_GULP, 0.6f, 0);
        break;
    }
    case IT_PAP: {
        if (!G->power_on) { msg(0xa0a0a0, "%s", tr("Needs power")); sfx(SFX_DENY, 0.5f, 0); return; }
        if (it->state == 2) {                               /* take it back */
            Weapon w; memset(&w, 0, sizeof w);
            w.def = (int16_t)it->b; w.rar = (int8_t)it->c; w.pap = 1;
            w.mag = (int16_t)weapon_mag(&w); w.reserve = (int16_t)weapon_reserve_max(&w);
            give_weapon(w, 0);
            msg(0xe080ff, "%s", weapon_name(&w));
            it->state = 0; it->t = 0;
            return;
        }
        if (it->state) return;
        Weapon *w = &p->w[p->cur];
        if (w->def < 0) return;
        if (w->pap) { msg(0xa0a0a0, "%s", tr("Already upgraded")); return; }
        int cost = 5000;
        if (!pay(cost)) return;
        it->b = w->def; it->c = w->rar; it->state = 1; it->t = 0;
        w->def = -1;
        for (int k = 0; k < p->nslots; k++) if (p->w[k].def >= 0) { p->cur = k; break; }
        sfx_at(SFX_PAP, it->x, it->y, 1);
        break;
    }
    case IT_POWER:
        if (G->power_on) return;
        G->power_on = 1; G->power_t = 0;
        banner(0xffe060, tr("POWER ON"), 0);
        sfx(SFX_POWER, 1, 0);
        shake(4);
        world_power_wave(it->x, it->y);                  /* windows, shops and lamps light up, spreading from here */
        prop_lights();
        break;
    case IT_LOOT: loot_container(it); break;
    case IT_GNOME: {                                       /* one more found: a squeak; all three: the song */
        if (it->state) return;
        it->state = 1;
        int found = 0; for (int k = 0; k < G->nit; k++) found += G->it[k].type == IT_GNOME && G->it[k].state;
        sfx_at(SFX_BEEP, it->x, it->y, 0.8f); sfx_at(SFX_PICKUP, it->x, it->y, 0.5f);
        spawn_parts(PT_GLOW, it->x, it->y - 8, 8, 0xffe080, 40);
        char b[8]; snprintf(b, sizeof b, "%d/3", found); float_text(it->x, it->y - 18, 0xffe080, b);
        if (found >= 3 && !G->song) {
            G->song = 1;
            music_play(MUS_SONG);
            banner(0xffe080, "\xe2\x99\xaa Broder Jakob \xe2\x99\xaa", tr("you found all three tomtar"));
            item_drop_weapon(weapon_make(box_roll(), RAR_EPIC), p->x + 10, p->y + 4);   /* and a present */
            add_kr(1000, 0);
        }
        break;
    }
    case IT_TRAP:
        if (it->state) return;
        if (it->a >= 0 && !G->it[it->a].state) { msg(0xa0a0a0, "%s", tr("Clear the way first")); sfx(SFX_DENY, 0.5f, 0); return; }   /* (the gap is still boarded up) */
        if (!G->power_on) { msg(0xa0a0a0, "%s", tr("Needs power")); sfx(SFX_DENY, 0.5f, 0); return; }
        if (!pay(it->cost)) return;
        it->state = 1; it->t = TRAP_ON; it->t2 = 0;
        sfx_at(SFX_POWER, it->x, it->y, 0.7f); sfx_at(SFX_ZAP, it->x, it->y, 0.9f);
        break;
    default: break;
    }
}

static void take_item(int idx) {
    Player *p = &G->p;
    Item *it = &G->items[idx];
    if (it->kind == IK_WEAPON) {
        int k = has_weapon(it->w.def);
        if (k >= 0 && p->w[k].pap == it->w.pap && p->w[k].rar >= it->w.rar) {      /* same gun: take its ammo */
            p->w[k].reserve = (int16_t)MIN(weapon_reserve_max(&p->w[k]), p->w[k].reserve + it->w.reserve + it->w.mag);
            msg(0xe0e0a0, "%s", tr("Ammo"));
        } else {
            Weapon w = it->w;
            it->alive = 0;
            if (k >= 0) { item_drop_weapon(p->w[k], p->x, p->y + 2); p->w[k] = w; p->cur = k; }
            else give_weapon(w, 1);
            msg(RARITY_COL[w.rar], "%s [%s]", weapon_name(&w), tr(RARITY_NAME[w.rar]));
            sfx(SFX_SWAP, 0.6f, 0);
            return;
        }
    } else {
        int slot = ARMORS[it->ar.def].slot;
        Armor old = p->ar[slot];
        p->ar[slot] = it->ar;
        msg(RARITY_COL[it->ar.rar], "%s [%s]", ARMORS[it->ar.def].name, tr(RARITY_NAME[it->ar.rar]));
        it->alive = 0;
        if (old.def >= 0) item_drop_armor(old, p->x, p->y + 2);
        sfx(SFX_PICKUP, 0.6f, 0);
        return;
    }
    it->alive = 0;
    sfx(SFX_PICKUP, 0.5f, 0);
}

const char *inter_prompt(int i, int *ok) {
    static char b[96];
    Player *p = &G->p;
    char n[24];
    *ok = 1;
    if (i <= -2) {
        Item *it = &G->items[-2 - i];
        if (it->kind == IK_WEAPON) snprintf(b, sizeof b, "%s: %s %s", use_label(), tr("Take"), weapon_name(&it->w));
        else snprintf(b, sizeof b, "%s: %s %s", use_label(), tr("Take"), ARMORS[it->ar.def].name);
        return b;
    }
    Inter *it = &G->it[i];
    switch (it->type) {
    case IT_BARRIER: fmt_num(n, it->cost); snprintf(b, sizeof b, "%s: %s - %s kr", use_label(), tr("Clear the way"), n); *ok = p->kr >= it->cost; break;
    case IT_WALLBUY:
        if (it->a == -1) { fmt_num(n, it->cost); snprintf(b, sizeof b, "%s: %s %s - %s kr", use_label(), tr("Buy"), tr("Axe"), n); *ok = p->kr >= it->cost && !p->has_axe; }
        else if (it->a == -2) { fmt_num(n, it->cost); snprintf(b, sizeof b, "%s: %s %s - %s kr", use_label(), tr("Buy"), tr("Grenades"), n); *ok = p->kr >= it->cost; }
        else {
            int k = has_weapon(it->a);
            int cost = k >= 0 ? (p->w[k].pap ? 4500 : it->cost / 2) : it->cost;
            fmt_num(n, cost);
            snprintf(b, sizeof b, "%s: %s %s%s%s - %s kr", use_label(), tr("Buy"), WEAPONS[it->a].name, k >= 0 ? " " : "", k >= 0 ? tr("ammo") : "", n);
            *ok = p->kr >= cost;
        }
        break;
    case IT_GNOME: return 0;                               /* (no word about them) */
    case IT_TRAP:
        if (it->state == 1) { snprintf(b, sizeof b, "%s: %s", tr("Elstängsel"), tr("on")); *ok = 0; break; }
        if (it->state == 2) { snprintf(b, sizeof b, "%s (%s %.0f s)", tr("Elstängsel"), tr("charging"), ceilf(it->t)); *ok = 0; break; }
        if (it->a >= 0 && !G->it[it->a].state) { snprintf(b, sizeof b, "%s (%s)", tr("Elstängsel"), tr("Clear the way first")); *ok = 0; break; }
        if (!G->power_on) { snprintf(b, sizeof b, "%s (%s)", tr("Elstängsel"), tr("Needs power")); *ok = 0; break; }
        fmt_num(n, it->cost); snprintf(b, sizeof b, "%s: %s - %s kr", use_label(), tr("Elstängsel"), n); *ok = p->kr >= it->cost;
        break;
    case IT_BOX:
        if (it->state == 2) { snprintf(b, sizeof b, "%s: %s %s", use_label(), tr("Take"), WEAPONS[it->b].name); break; }
        if (it->state) return 0;
        snprintf(b, sizeof b, "%s: %s - %d kr", use_label(), tr("Mystery Box"), G->firesale_t > 0 ? 10 : 950);
        *ok = p->kr >= (G->firesale_t > 0 ? 10 : 950);
        break;
    case IT_PERK: {
        const PerkDef *pd = &PERKS[it->a];
        fmt_num(n, pd->cost);
        if (!G->power_on && it->a != PK_KANELBULLE) { snprintf(b, sizeof b, "%s (%s)", pd->name, tr("Needs power")); *ok = 0; }
        else if (p->perks & (1u << it->a)) { snprintf(b, sizeof b, "%s: %s", pd->name, tr("Already have it")); *ok = 0; }
        else { snprintf(b, sizeof b, "%s: %s %s - %s kr", use_label(), tr("Buy"), pd->name, n); *ok = p->kr >= pd->cost && p->nperks < PERK_LIMIT; }
        break;
    }
    case IT_PAP:
        if (!G->power_on) { snprintf(b, sizeof b, "%s (%s)", tr("Pack-a-Punch"), tr("Needs power")); *ok = 0; break; }
        if (it->state == 1) { snprintf(b, sizeof b, "%s...", tr("Upgrading")); *ok = 0; break; }
        if (it->state == 2) { snprintf(b, sizeof b, "%s: %s %s", use_label(), tr("Take"), WEAPONS[it->b].pap_name); break; }
        snprintf(b, sizeof b, "%s: %s - 5 000 kr", use_label(), tr("Upgrade"));
        *ok = p->kr >= 5000 && p->w[p->cur].def >= 0 && !p->w[p->cur].pap;
        break;
    case IT_POWER: if (G->power_on) return 0; snprintf(b, sizeof b, "%s: %s", use_label(), tr("Turn on the power")); break;
    case IT_WINDOW: snprintf(b, sizeof b, "%s %s: %s", tr("Hold"), use_label(), tr("Repair")); break;
    case IT_LOOT: snprintf(b, sizeof b, "%s %s: %s", tr("Hold"), use_label(), tr("Search")); break;
    default: return 0;
    }
    return b;
}

int nearest_inter(float x, float y, float r) { (void)x; (void)y; (void)r; return target(0); }
int inter_target(void) { return target(0); }

/* the perk machines play their jingles now and then when you're near (Kanelbulle even without the power), as the
   Perk-a-Colas do: a roll every 9 s, made from the clock, so that a saved run plays them the same */
static void jingles_tick(float dt) {
    int slot = (int)(G->time / 9);
    if (slot == (int)((G->time - dt) / 9)) return;
    for (int i = 0; i < G->nit; i++) {
        const Inter *it = &G->it[i];
        if (it->type != IT_PERK || (!G->power_on && it->a != PK_KANELBULLE)) continue;
        if (dist2f(it->x, it->y, G->p.x, G->p.y) > 120 * 120 || hash3(slot, i, 0x6A) % 4) continue;
        sfx_at(SFX_JINGLE + it->a, it->x, it->y, 0.6f);
        float_text(it->x, it->y - 36, 0xfff0b0, "\xe2\x99\xaa");     /* ♪ (to be seen with the sound off too) */
        break;
    }
}

void inter_update(const Input *in, const Input *prev, float dt) {
    Player *p = &G->p;
    boxes_tick(dt);
    traps_tick(dt);
    jingles_tick(dt);
    G->prompt[0] = 0;
    if (p->downed || G->over) return;
    int t = target(0);
    int ub = use_button();
    int down = (in->held & BIT(ub)) != 0, press = down && !(prev->held & BIT(ub));
    if (t == -1) { p->use_t = 0; p->use_target = -1; return; }
    int ok;
    const char *pr = inter_prompt(t, &ok);
    if (pr) { snprintf(G->prompt, sizeof G->prompt, "%s", pr); G->prompt_cost_ok = ok; }
    if (t <= -2) { if (press) take_item(-2 - t); return; }
    Inter *it = &G->it[t];
    if (it->type == IT_WINDOW || it->type == IT_LOOT) {      /* hold to repair or search */
        if (!down || p->use_target != t) { p->use_t = 0; p->use_target = t; if (!down) return; }
        p->use_t += dt;
        if (it->type == IT_LOOT && p->use_t > 0.45f) { use_inter(t); p->use_t = 0; }
        if (it->type == IT_WINDOW) {
            float need = (p->perks & (1u << PK_SNABBKAFFE)) ? 0.35f : 0.7f;
            if (p->use_t > need && it->state < 6) {
                p->use_t = 0; it->state++; p->boards++;
                sfx_at(SFX_BOARD_FIX, it->x, it->y, 0.6f);
                int cap = MIN(50 * G->round, 500);
                if (p->repair_kr < cap) { add_kr(10, 1); p->repair_kr += 10; float_text(it->x, it->y - 16, 0xf0f0f0, "+10"); }
            }
        }
        return;
    }
    if (press) use_inter(t);
}
