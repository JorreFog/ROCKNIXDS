// hud.c: the bottom screen. The weapon in hand and its ammo, health and armour, perks, the bag, grenades, the
// round and the kronor, and a map of the town. Laid out for 320x240 and centred on bigger screens; the bag and
// the weapons can be tapped.
#include "game.h"
#include <stdio.h>

#define PANEL 0x161a22
#define PANEL2 0x1e242e
#define EDGE 0x2c3442
#define TXT 0xd8dce4
#define DIM 0x7a8494

static int ox, oy;                                         /* where the 320x240 layout sits */

typedef struct { int x, y, w, h; } Rect;
static Rect r_weapon, r_alt[2], r_bag[BAG_SLOTS], r_map;

static void panel(Surf *s, int x, int y, int w, int h, const char *title) {
    rectf(s, x, y, w, h, PANEL2);
    rect_line(s, x, y, w, h, EDGE);
    hline(s, x + 1, x + w - 2, y + 1, 0x262e3a);
    if (title) {
        int tw = text_w(FONT_SMALL, title);
        rectf(s, x + 4, y - 3, tw + 4, 6, PANEL);
        text(s, FONT_SMALL, x + 6, y - 3, DIM, title);
    }
}

static void bar(Surf *s, int x, int y, int w, int h, float k, uint32_t c, uint32_t bg) {
    rectf(s, x, y, w, h, bg);
    int f = (int)(w * clampf(k, 0, 1));
    rectf(s, x, y, f, h, c);
    hline(s, x, x + f - 1, y, col_scale(c, 330));
    if (h > 3) hline(s, x, x + f - 1, y + h - 1, col_scale(c, 170));
    for (int i = x + 8; i < x + w; i += 8) vline(s, i, y, y + h - 1, col_mix(bg, 0x000000, 60));
}

static void tally_small(Surf *s, int x, int y, int n, uint32_t c) {
    if (n > 10) { char b[16]; snprintf(b, sizeof b, "%d", n); text_big(s, x, y - 2, 2, c, 0x200000, b); return; }
    for (int g = 0; g < (n + 4) / 5; g++) {
        int m = MIN(5, n - g * 5), gx = x + g * 16;
        for (int k = 0; k < MIN(m, 4); k++) vline(s, gx + k * 3, y, y + 10, c), vline(s, gx + k * 3 + 1, y + 1, y + 10, c);
        if (m == 5) line(s, gx - 1, y + 8, gx + 11, y + 2, c);
    }
}

static void weapon_card(Surf *s) {
    Player *p = &G->p;
    int x = ox + 4, y = oy + 22, w = 158, h = 74;
    r_weapon = (Rect){ x, y, w, h };
    panel(s, x, y, w, h, tr("Weapon"));
    Weapon *wp = &p->w[p->cur];
    if (wp->def < 0) { text(s, FONT_NORMAL, x + 8, y + 10, DIM, tr("Knife")); return; }
    const WeaponDef *d = &WEAPONS[wp->def];
    uint32_t rc = RARITY_COL[wp->rar];
    rectf(s, x + 3, y + 3, w - 6, 2, rc);                  /* the rarity stripe */
    const Img *im = art(d->icon);
    if (im) {
        int k = im->w <= 36 ? 2 : 1;
        int ix = x + 6, iy = y + 10;
        blit_scaled(s, im, ix, iy, k);
        if (wp->pap) for (int j = 0; j < im->h * k; j += 2) for (int i = 0; i < im->w * k; i += 3) if ((i + j + (int)(G->time * 20)) % 9 == 0) pblend(s, ix + i, iy + j, 0xd080ff, 140);
    }
    text(s, FONT_NORMAL, x + 6, y + 36, wp->pap ? 0xe0a0ff : 0xffffff, weapon_name(wp));
    text(s, FONT_SMALL, x + 6, y + 47, rc, tr(RARITY_NAME[wp->rar]));
    if (wp->pap) text(s, FONT_SMALL, x + 10 + text_w(FONT_SMALL, tr(RARITY_NAME[wp->rar])), y + 47, 0xd080ff, "SMEDJAN");
    /* ammo: the magazine big, the rest small */
    char b[32];
    snprintf(b, sizeof b, "%d", wp->mag);
    uint32_t ac = wp->mag == 0 ? 0xff5040 : wp->mag <= weapon_mag(wp) / 4 ? 0xffb040 : 0xffffff;
    int ax = x + w - 8 - text_w(FONT_NORMAL, b) * 2;
    text_big(s, ax, y + 10, 2, ac, 0x000000, b);
    snprintf(b, sizeof b, "/ %d", wp->reserve);
    text(s, FONT_NORMAL, x + w - 6 - text_w(FONT_NORMAL, b), y + 30, wp->reserve ? TXT : 0xff5040, b);
    /* the magazine as bullets */
    int mag = weapon_mag(wp), bx = x + 6, by = y + 57, bw = w - 12;
    int per = MAX(1, (mag + bw / 3 - 1) / (bw / 3));
    for (int i = 0; i * per < mag && i < bw / 3; i++) {
        int full = i * per < wp->mag;
        rectf(s, bx + i * 3, by, 2, 6, full ? 0xd8b040 : 0x3a3e46);
        if (full) pset(s, bx + i * 3, by, 0xfff0a0);
    }
    if (p->reloading && p->reload_len > 0) {
        bar(s, x + 6, y + 66, w - 12, 4, 1 - p->reload_t / p->reload_len, 0xf0f0f0, 0x2a2e36);
        text(s, FONT_SMALL, x + w / 2 - 14, y + 66, 0x101010, tr("Reloading"));
    } else {
        /* stats */
        snprintf(b, sizeof b, "DMG %.0f  RPM %.0f", weapon_dmg(wp), weapon_rpm(wp));
        text(s, FONT_SMALL, x + 6, y + 66, DIM, b);
    }
}

static void alt_weapons(Surf *s) {
    Player *p = &G->p;
    int n = 0;
    for (int k = 0; k < p->nslots && n < 2; k++) {
        if (k == p->cur) continue;
        int x = ox + 4 + n * 80, y = oy + 100, w = 78, h = 22;
        r_alt[n] = (Rect){ x, y, w, h };
        rectf(s, x, y, w, h, PANEL2); rect_line(s, x, y, w, h, EDGE);
        Weapon *wp = &p->w[k];
        if (wp->def >= 0) {
            rectf(s, x + 1, y + 1, 2, h - 2, RARITY_COL[wp->rar]);
            const Img *im = art(WEAPONS[wp->def].icon);
            if (im) blit(s, im, x + 5, y + 2, 0);
            char b[24]; snprintf(b, sizeof b, "%d/%d", wp->mag, wp->reserve);
            text(s, FONT_SMALL, x + w - 4 - text_w(FONT_SMALL, b), y + 15, TXT, b);
            text(s, FONT_SMALL, x + 5, y + 15, DIM, S.scheme == 1 ? "R2" : "X");
        } else text(s, FONT_SMALL, x + 6, y + 8, 0x4a5262, k == 2 ? "KAVIAR" : "-");
        n++;
    }
    for (; n < 2; n++) r_alt[n] = (Rect){ 0, 0, 0, 0 };
}

static void vitals(Surf *s) {
    Player *p = &G->p;
    int x = ox + 4, y = oy + 128, w = 158;
    char b[32];
    /* health */
    const Img *heart = art("i_heart");
    if (heart) blit(s, heart, x, y, 0);
    float hk = p->maxhp > 0 ? p->hp / p->maxhp : 0;
    uint32_t hc = hk > 0.5f ? 0xd83030 : hk > 0.25f ? 0xe07020 : ((int)(G->time * 4) & 1 ? 0xff3030 : 0x801010);
    bar(s, x + 12, y + 1, w - 54, 7, hk, hc, 0x2a1416);
    snprintf(b, sizeof b, "%.0f/%.0f", p->hp, p->maxhp);
    text(s, FONT_SMALL, x + w - 40, y + 2, TXT, b);
    /* armour: the helmet and the vest */
    for (int k = 0; k < 2; k++) {
        int yy = y + 11 + k * 9;
        Armor *a = &p->ar[k];
        const Img *ic = a->def >= 0 ? art(ARMORS[a->def].icon) : 0;
        if (ic) blit(s, ic, x - 1, yy - 3 + (k ? 0 : 0), 0);
        else text(s, FONT_SMALL, x + 2, yy, 0x4a5262, k ? "B" : "H");
        if (a->def >= 0) {
            bar(s, x + 12, yy + 1, w - 54, 5, a->ap / MAX(1.0f, a->max), 0x4a8ae0, 0x161e2c);
            snprintf(b, sizeof b, "%.0f", a->ap);
            text(s, FONT_SMALL, x + w - 40, yy + 1, RARITY_COL[a->rar], b);
        } else {
            rectf(s, x + 12, yy + 1, w - 54, 5, 0x161a22);
            text(s, FONT_SMALL, x + 14, yy + 1, 0x4a5262, tr(k ? "Body" : "Head"));
        }
    }
    /* stamina */
    float st = p->stamina / ((p->perks & (1u << PK_BLABAR)) ? 8.0f : 4.0f);
    bar(s, x + 12, y + 30, w - 54, 2, st, p->coffee_t > 0 ? 0xe0b060 : 0x60c070, 0x14201a);
}

static void perks_row(Surf *s) {
    Player *p = &G->p;
    int x = ox + 4, y = oy + 166;
    panel(s, x, y, 158, 20, tr("Perks"));
    int n = 0;
    for (int k = 0; k < PK_COUNT; k++) {
        if (!(p->perks & (1u << k))) continue;
        int px = x + 4 + n * 18;
        rectf(s, px - 1, y + 3, 16, 15, PERKS[k].color);
        const Img *im = art(PERKS[k].icon);
        if (im) blit(s, im, px + 1, y + 4, 0);
        n++;
    }
    for (; n < PERK_LIMIT; n++) rect_line(s, x + 4 + n * 18 - 1, y + 3, 16, 15, 0x2a3240);
    if (p->bulle_used && p->bulle_used < 3) { char b[8]; snprintf(b, sizeof b, "%d/3", p->bulle_used); text(s, FONT_SMALL, x + 4 + 4 * 18 + 2, y + 9, 0xc8862a, b); }
    /* grenades */
    const Img *g = art("c_granat");
    int gx = x + 118;
    if (g) blit(s, g, gx, y + 4, 0);
    char b[8]; snprintf(b, sizeof b, "x%d", p->grenades);
    text(s, FONT_NORMAL, gx + 14, y + 6, p->grenades ? TXT : 0x4a5262, b);
}

static void bag(Surf *s) {
    Player *p = &G->p;
    int x = ox + 4, y = oy + 192;
    panel(s, x, y, 158, 30, tr("Bag"));
    for (int i = 0; i < BAG_SLOTS; i++) {
        int bx = x + 4 + i * 25, by = y + 4, bw = 23, bh = 23;
        r_bag[i] = (Rect){ bx, by, bw, bh };
        int sel = i == p->bag_sel;
        rectf(s, bx, by, bw, bh, sel ? 0x2a3446 : 0x1a1e26);
        rect_line(s, bx, by, bw, bh, sel ? 0xd8b040 : 0x2c3442);
        if (p->bag[i].id < 0) continue;
        const Img *im = art(CONS[p->bag[i].id].icon);
        if (im) blit(s, im, bx + (bw - im->w) / 2, by + (bh - im->h) / 2 - 1, 0);
        char b[8]; snprintf(b, sizeof b, "%d", p->bag[i].n);
        text_ol(s, FONT_SMALL, bx + bw - 2 - text_w(FONT_SMALL, b), by + bh - 6, 0xffffff, 0x000000, b);
    }
    if (p->bag[p->bag_sel].id >= 0) {
        char b[48]; snprintf(b, sizeof b, "%s: %s", S.scheme == 1 ? "TOUCH" : "L2", CONS[p->bag[p->bag_sel].id].name);
        text(s, FONT_SMALL, x + 4, y + 29, DIM, b);
    }
}

/* the town map: zones (dark until opened), the machines, the box, you and the dead nearby */
static void minimap(Surf *s) {
    int x = ox + 166, y = oy + 22, w = 150, h = 112;
    r_map = (Rect){ x, y, w, h };
    panel(s, x, y, w, h, tr("Map"));
    float k = MIN((w - 6) / (float)G->w, (h - 6) / (float)G->h);
    int mw = (int)(G->w * k), mh = (int)(G->h * k), mx = x + (w - mw) / 2, my = y + (h - mh) / 2 + 1;
    for (int j = 0; j < mh; j++)
        for (int i = 0; i < mw; i++) {
            int tx = (int)(i / k), ty = (int)(j / k);
            Tile *t = tile_at(tx, ty);
            if (!t) continue;
            uint32_t c;
            if (t->f & TF_BUILDING) c = 0x6a6e78;
            else if (t->g == G_WATER) c = 0x2a4a6a;
            else if (t->f & TF_SOLID) c = 0x2a3a2a;
            else if (t->g == G_GRASS || t->g == G_TURF) c = G->season == SEASON_WINTER ? 0x8a929a : 0x3a5a2a;
            else c = 0x4a4e56;
            if (!G->zones[t->zone].open) c = col_scale(c, 90);
            pset(s, mx + i, my + j, c);
        }
    /* barriers still closed */
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        int px = mx + (int)((it->tx + it->tw / 2.0f) * k), py = my + (int)((it->ty + it->th / 2.0f) * k);
        switch (it->type) {
        case IT_BARRIER: if (!it->state) { rectf(s, px - 1, py - 1, 2, 2, 0xe07020); } break;
        case IT_PERK: if (it->a != PK_KANELBULLE || G->p.bulle_used < 3) rectf(s, px - 1, py - 1, 3, 3, PERKS[it->a].color2); break;
        case IT_PAP: rectf(s, px - 1, py - 1, 3, 3, 0xc070ff); break;
        case IT_TRAP: rectf(s, px - 1, py - 1, 2, 2, it->state == 1 ? ((int)(G->time * 8) & 1 ? 0xffffff : 0x8ab0ff) : it->state == 2 ? 0x4a5a8a : 0x8ab0ff); break;
        case IT_POWER: rectf(s, px - 1, py - 1, 3, 3, G->power_on ? 0x40ff60 : ((int)(G->time * 3) & 1 ? 0xffe040 : 0x806010)); break;
        case IT_BOX:
            if (G->box_spots[G->box_at] == i && !G->box_moving) { rectf(s, px - 2, py - 2, 4, 4, 0xffe8a0); pset(s, px - 1, py - 4, 0xd0e8ff); pset(s, px - 1, py - 5, 0xd0e8ff); }
            break;
        }
    }
    for (int i = 0; i < MAX_POWERUPS; i++) if (G->pu[i].alive) rectf(s, mx + (int)(G->pu[i].x / TS * k) - 1, my + (int)(G->pu[i].y / TS * k) - 1, 3, 3, 0x60ff60);
    Player *p = &G->p;
    for (int i = 0; i < MAX_ZOMBIES; i++) {                /* only the ones you could hear: within 14 tiles */
        Zombie *z = &G->z[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        if (dist2f(z->x, z->y, p->x, p->y) > (14 * TS) * (14 * TS)) continue;
        pset(s, mx + (int)(z->x / TS * k), my + (int)(z->y / TS * k), z->type == ZT_MOOSE ? 0xffa040 : 0xff3030);
    }
    int px = mx + (int)(p->x / TS * k), py = my + (int)(p->y / TS * k);
    uint32_t pc = (int)(G->time * 4) & 1 ? 0xffffff : 0x80d0ff;
    pset(s, px, py, pc); pset(s, px - 1, py, pc); pset(s, px + 1, py, pc); pset(s, px, py - 1, pc); pset(s, px, py + 1, pc);
    /* the zone you're in */
    text(s, FONT_SMALL, x + 4, y + h - 7, TXT, G->zones[zone_at(p->x, p->y)].name);
}

static void legend(Surf *s) {
    int x = ox + 166, y = oy + 140, w = 150, h = 82;
    panel(s, x, y, w, h, 0);
    /* the latest messages, newest first */
    int line = 0;
    for (int i = 0; i < 4 && line < 4; i++) {
        if (!G->msg[i][0]) continue;
        uint32_t c = G->msg_t[i] > 0 ? G->msg_col[i] : col_scale(G->msg_col[i], 120);
        Surf c2 = *s; surf_clip(&c2, x + 3, y + 3, w - 6, h - 6);
        text(&c2, FONT_SMALL, x + 4, y + 4 + line * 7, c, G->msg[i]);
        line++;
    }
    /* help for the buttons */
    const char *h1 = S.scheme == 1 ? "ABXY: SKJUT  R: ANVÄND  L: SPRING" : "A: SKJUT  B: ANVÄND  Y: LADDA";
    const char *h2 = S.scheme == 1 ? "L2: GRANAT  R2: BYT  SEL: LADDA" : "X: BYT  R: KNIV  L: SPRING";
    const char *h3 = S.scheme == 1 ? "TRYCK PÅ VÄSKAN: ANVÄND" : "R2: GRANAT  L2: SAK  SEL: NÄSTA";
    if (S.lang == LANG_EN) {
        h1 = S.scheme == 1 ? "ABXY: FIRE  R: USE  L: SPRINT" : "A: FIRE  B: USE  Y: RELOAD";
        h2 = S.scheme == 1 ? "L2: GRENADE  R2: SWAP  SEL: RELOAD" : "X: SWAP  R: KNIFE  L: SPRINT";
        h3 = S.scheme == 1 ? "TAP THE BAG TO USE ITEMS" : "R2: GRENADE  L2: ITEM  SEL: NEXT";
    }
    text(s, FONT_SMALL, x + 4, y + h - 22, 0x5a6476, h1);
    text(s, FONT_SMALL, x + 4, y + h - 15, 0x5a6476, h2);
    text(s, FONT_SMALL, x + 4, y + h - 8, 0x5a6476, h3);
}

static void top_bar(Surf *s) {
    Player *p = &G->p;
    int x = ox, y = oy;
    rectf(s, x, y, 320, 18, 0x0e1016);
    hline(s, x, x + 319, y + 18, EDGE);
    tally_small(s, x + 6, y + 4, G->round, G->rstate == RS_BREAK && ((int)(G->time * 3) & 1) ? 0xf0f0f0 : 0xc81818);
    char b[32], n[16];
    fmt_num(n, p->kr);
    snprintf(b, sizeof b, "%s kr", n);
    int kx = x + 92;
    text_big(s, kx, y + 3, 1, G->double_t > 0 ? 0x60ff60 : 0xf0d040, 0x000000, b);
    if (G->double_t > 0) text(s, FONT_SMALL, kx + text_w(FONT_NORMAL, b) + 4, y + 6, 0x60ff60, "x2");
    snprintf(b, sizeof b, "%s %d", tr("Kills"), p->kills);
    text(s, FONT_NORMAL, x + 176, y + 5, TXT, b);
    int t = (int)G->time;
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    text(s, FONT_NORMAL, x + 316 - text_w(FONT_NORMAL, b), y + 5, DIM, b);
    /* zombies left this round */
    int left = G->to_spawn - G->spawned + zombies_alive();
    if (G->rstate == RS_ACTIVE) { snprintf(b, sizeof b, "%d", left); text(s, FONT_SMALL, x + 248, y + 7, 0xc84040, b); const Img *sk = art("i_skull"); if (sk) blit(s, sk, x + 238, y + 4, 0); }
    else { snprintf(b, sizeof b, "%.0f", 10 - G->rtime); text(s, FONT_SMALL, x + 244, y + 7, 0xf0f0f0, b); }
}

void render_hud(Surf *s, const Input *in) {
    (void)in;
    ox = (s->w - 320) / 2; oy = (s->h - 240) / 2;
    fill(s, PANEL);
    for (int y = 0; y < s->h; y += 4) for (int x = (y / 4) & 1 ? 2 : 0; x < s->w; x += 4) pset(s, x, y, 0x1a1e28);
    top_bar(s);
    weapon_card(s);
    alt_weapons(s);
    vitals(s);
    perks_row(s);
    bag(s);
    minimap(s);
    legend(s);
    if (G->over) rect_blend(s, 0, 0, s->w, s->h, 0x000000, 120);
}

static int in_rect(Rect *r, int x, int y) { return r->w && x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h; }

/* a tap on the bottom screen: 1 if it did something */
int hud_touch(int x, int y) {
    Player *p = &G->p;
    for (int i = 0; i < BAG_SLOTS; i++)
        if (in_rect(&r_bag[i], x, y)) {
            if (p->bag[i].id < 0) return 0;
            if (p->bag_sel == i || S.scheme == 1) bag_use(i); else p->bag_sel = i;
            sfx(SFX_MENU_MOVE, 0.4f, 0);
            return 1;
        }
    int n = 0;
    for (int k = 0; k < p->nslots; k++) {
        if (k == p->cur) continue;
        if (n < 2 && in_rect(&r_alt[n], x, y) && p->w[k].def >= 0) { p->cur = k; p->swap_t = 0.35f; p->reloading = 0; sfx(SFX_SWAP, 0.5f, 0); return 1; }
        n++;
    }
    return 0;
}
