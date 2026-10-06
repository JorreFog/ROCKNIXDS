// menu.c: everything around a run. The title (a night over a Swedish suburb), pause, settings, how to play, the
// game over ("YOU SURVIVED 7 ROUNDS", as Call of Duty says it), initials for the high score list, and the list.
#include "game.h"
#include "save.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

App A;

static int pressed(const Input *in, const Input *prev, int b) { return (in->held & BIT(b)) && !(prev->held & BIT(b)); }
static int ok_pressed(const Input *in, const Input *prev) { return pressed(in, prev, btn_fire()) || pressed(in, prev, B_START); }
static int back_pressed(const Input *in, const Input *prev) { return pressed(in, prev, btn_use()); }
static int touch_down(const Input *in, const Input *prev) { return in->touch[1] && !prev->touch[1]; }

/* ---------------------------------------------------------------- drawing helpers */
static void bg_bottom(Surf *s) {
    fill(s, 0x12151c);
    for (int y = 0; y < s->h; y++) for (int x = (y & 1) ? 1 : 0; x < s->w; x += 2) if (((x + y) & 7) == 0) pset(s, x, y, 0x181c26);
}
typedef struct { int y, h; } Row;
static Row rows[16]; static int nrows;
static void menu_list(Surf *s, const char **items, int n, int sel, int y0) {
    int cx = s->w / 2;
    nrows = n;
    for (int i = 0; i < n; i++) {
        int y = y0 + i * 22;
        rows[i].y = y; rows[i].h = 20;
        int w = 200;
        if (i == sel) {
            rectf(s, cx - w / 2, y, w, 18, 0x3a1416);
            rect_line(s, cx - w / 2, y, w, 18, 0xc81818);
            rectf(s, cx - w / 2 + 2, y + 2, 3, 14, 0xc81818);
            rectf(s, cx + w / 2 - 5, y + 2, 3, 14, 0xc81818);
        } else rect_line(s, cx - w / 2, y, w, 18, 0x2a3040);
        text_center(s, FONT_NORMAL, cx, y + 5, i == sel ? 0xffffff : 0xa0a8b8, i == sel ? 0x000000 : 0, items[i]);
    }
}
static int touch_row(const Input *in, int y0, int n) {
    if (!in->touch[1]) return -1;
    for (int i = 0; i < n; i++) if (in->ty[1] >= y0 + i * 22 && in->ty[1] < y0 + i * 22 + 20) return i;
    return -1;
}

/* the title's picture: sky, moon, the blocks of a förort, and the dead walking past */
static void skyline(Surf *s, float t, int dim) {
    int h = s->h, w = s->w;
    int winter = 1;
    for (int y = 0; y < h; y++) {
        uint32_t c = col_mix(0x060810, 0x1c2640, y * 256 / h);
        hline(s, 0, w - 1, y, c);
    }
    for (int i = 0; i < 70; i++) {                         /* stars, twinkling */
        uint32_t hh = hash3(i, 7, 11);
        int x = (int)(hh % (unsigned)w), y = (int)((hh >> 10) % (unsigned)(h / 2));
        int tw = ((int)(t * 2 + i) % 7) == 0;
        pset(s, x, y, tw ? 0xffffff : 0x6a7088);
    }
    int mx = w - 58, my = 34;                              /* the moon */
    circlef(s, mx, my, 14, 0xe8e4d0); circlef(s, mx + 4, my - 3, 3, 0xd0ccb8); circlef(s, mx - 5, my + 4, 2, 0xd0ccb8); circlef(s, mx + 2, my + 7, 2, 0xd8d4c0);
    circle_blend(s, mx, my, 22, 0xe8e4d0, 18);
    /* far: skivhus and lamellhus against the sky */
    int base = h - 52;
    Rng r; rng_seed(&r, 42, 9);
    int x = -10;
    while (x < w) {
        int bw = rng_range(&r, 28, 70), bh = rng_chance(&r, 0.3f) ? rng_range(&r, 70, 110) : rng_range(&r, 34, 56);
        uint32_t bc = 0x0e121c;
        rectf(s, x, base - bh, bw, bh + 60, bc);
        for (int wy = base - bh + 5; wy < base - 4; wy += 7)
            for (int wx = x + 4; wx < x + bw - 4; wx += 6) {
                uint32_t hh = hash3(wx, wy, 5);
                if (hh % 7 == 0) rectf(s, wx, wy, 3, 4, (hh >> 8) % 3 ? 0x8a6a30 : 0xc8a050);
                else if (hh % 5 == 0) rectf(s, wx, wy, 3, 4, 0x161c2a);
            }
        if (bh > 60) rectf(s, x + bw / 2 - 2, base - bh - 6, 4, 6, bc);        /* a chimney on the roof */
        x += bw + rng_range(&r, 2, 14);
    }
    /* the church spire and birches nearer */
    int cx = w / 5;
    for (int k = 0; k < 40; k++) hline(s, cx - k / 5, cx + k / 5, base - 90 + k, 0x0a0d14);
    rectf(s, cx - 8, base - 50, 16, 60, 0x0a0d14);
    for (int k = 0; k < 4; k++) {
        int bx = 40 + k * 84 + (k & 1) * 20;
        vline(s, bx, base - 30, base + 10, 0x8a8a90);
        for (int j = 0; j < 6; j++) { int yy = base - 28 + j * 5; line(s, bx, yy, bx + ((j & 1) ? 7 : -7), yy - 4, 0x5a5a62); }
    }
    /* the ground */
    rectf(s, 0, base + 8, w, h - base - 8, winter ? 0x9aa6b8 : 0x22262e);
    for (int i = 0; i < w; i += 3) pset(s, i + (int)(hash3(i, 1, 3) % 3), base + 8 + (int)(hash3(i, 2, 3) % 6), 0xc8d2e0);
    /* lamp posts with their light */
    for (int k = 0; k < 3; k++) {
        int lx = 70 + k * 110;
        vline(s, lx, base - 26, base + 12, 0x1a1c22);
        hline(s, lx - 5, lx, base - 26, 0x1a1c22);
        hline(s, lx - 7, lx - 3, base - 24, 0xfff0c0);
        ellipse_blend(s, lx - 5, base + 12, 26, 6, 0xf2c070, 60);
        for (int j = 0; j < 30; j++) line_blend(s, lx - 5, base - 23, lx - 5 - 16 + j, base + 10, 0xf2c070, 6);
    }
    /* the dead, walking past */
    const char *pals[3] = { "zv1", "zv0", "zv4" };
    for (int k = 0; k < 6; k++) {
        float speed = 9 + k * 1.7f;
        float px = fmodf(t * speed + k * 70, (float)(w + 40)) - 20;
        char name[24]; snprintf(name, sizeof name, "zombie_side_%d", ((int)(t * 4 + k)) % 3);
        const Img *im = art_pal(name, pals[k % 3]);
        if (!im) continue;
        int zy = base + 14 + (k % 3) * 4;
        blit_silhouette(s, im, (int)px, zy - im->h, 0, 0x06080c, 255);
        pset(s, (int)px + 10, zy - im->h + 5, 0xffd23c);
    }
    /* snow */
    for (int i = 0; i < 80; i++) {
        uint32_t hh = hash3(i, 3, 17);
        float fx = (float)(hh % 400), fy = (float)((hh >> 9) % 300), sp = 10 + (hh >> 20) % 20;
        int px = (int)fmodf(fx + sinf(t + i) * 8, (float)w), py = (int)fmodf(fy + t * sp, (float)h);
        pblend(s, px, py, 0xf0f4ff, 180);
    }
    if (dim) rect_blend(s, 0, 0, w, h, 0x000000, dim);
}

/* the logo with blood running down from the letters */
static void logo(Surf *s, int cy, float t) {
    const char *a = "DÖDA", *b = "KVARTER";
    int k1 = s->w >= 340 ? 5 : 4, k2 = s->w >= 340 ? 4 : 3;
    int w1 = text_w(FONT_NORMAL, a) * k1, w2 = text_w(FONT_NORMAL, b) * k2;
    int x1 = s->w / 2 - w1 / 2, x2 = s->w / 2 - w2 / 2;
    text_big(s, x1 + 2, cy + 2, k1, 0x000000, 0, a);
    text_big(s, x1, cy, k1, 0xb81414, 0x2a0404, a);
    text_big(s, x2 + 2, cy + 9 * k1 + 2, k2, 0x000000, 0, b);
    text_big(s, x2, cy + 9 * k1, k2, 0xd8d4c8, 0x101010, b);
    /* drips from the bottom of DÖDA */
    for (int i = 0; i < w1; i += k1) {
        uint32_t hh = hash3(i, 1, 99);
        if (hh % 5) continue;
        int col = x1 + i + (int)(hh >> 8) % k1;
        int bottom = cy + 7 * k1;
        /* only under ink */
        if (s->px[(size_t)CLAMP(bottom - 1, 0, s->h - 1) * s->pitch + CLAMP(col, 0, s->w - 1)] != 0xb81414) continue;
        int len = 3 + (int)((hh >> 12) % 10) + (int)(fmodf(t * (0.6f + (hh >> 20) % 5 * 0.2f), 6.0f));
        vline(s, col, bottom, bottom + len, 0x8a0c0c);
        pset(s, col, bottom + len + 1, 0xb81414);
    }
}

/* ---------------------------------------------------------------- screens */
/* the title's entries: Continue (and New run) when a run is saved */
enum { T_CONTINUE, T_PLAY, T_NEW, T_DAILY, T_SCORES, T_SETTINGS, T_HOWTO, T_QUIT };
static int title_items(int *acts, const char **labels) {
    static const char *en[] = { "CONTINUE", "PLAY", "NEW RUN", "TODAY'S TOWN", "HIGH SCORES", "SETTINGS", "HOW TO PLAY", "QUIT" };
    int n = 0;
    if (A.has_save) { acts[n++] = T_CONTINUE; acts[n++] = T_NEW; } else acts[n++] = T_PLAY;
    acts[n++] = T_DAILY; acts[n++] = T_SCORES; acts[n++] = T_SETTINGS; acts[n++] = T_HOWTO; acts[n++] = T_QUIT;
    for (int i = 0; labels && i < n; i++) labels[i] = tr(en[acts[i]]);
    return n;
}
static int title_index(int act) { int acts[8], n = title_items(acts, 0); for (int i = 0; i < n; i++) if (acts[i] == act) return i; return 0; }
static int title_y0(int h, int n) { return h / 2 - 60 - (n - 5) * 11; }

/* today's town: the same seed for everyone on the same day (the local date) */
static int today(void) { time_t t = time(0); struct tm tm; localtime_r(&t, &tm); return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday; }
static uint64_t daily_seed(int date) { uint64_t x = (uint64_t)date * 0x9E3779B97F4A7C15ull + 0xD0DA; x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; return (x ^ (x >> 32)) | 1; }

static void draw_title_bottom(Surf *s) {
    bg_bottom(s);
    int acts[8]; const char *items[8]; int n = title_items(acts, items);
    menu_list(s, items, n, A.sel, title_y0(s->h, n));
    if (A.has_save && acts[A.sel] == T_CONTINUE) {
        char b[96]; snprintf(b, sizeof b, "%s, %s %d", A.save_town, tr("Round"), A.save_round);
        text_center(s, FONT_SMALL, s->w / 2, title_y0(s->h, n) - 10, 0x9aa4b8, 0, b);
    }
    if (acts[A.sel] == T_DAILY) {                          /* which town, and the best anyone here has done in it */
        char town[32], b[96]; int d = today(), best = 0;
        town_name(daily_seed(d), town, sizeof town);
        for (int i = 0; i < nscores; i++) if (scores[i].daily == d) best = MAX(best, scores[i].round);
        if (best) snprintf(b, sizeof b, "%s - %s: %s %d", town, tr("best today"), tr("Round"), best);
        else snprintf(b, sizeof b, "%s - %s", town, tr("the same town for everyone today"));
        text_center(s, FONT_SMALL, s->w / 2, title_y0(s->h, n) - 10, 0xd8b040, 0, b);
    }
    char b[96];
    if (nscores) { snprintf(b, sizeof b, "%s: %s - %s %d", tr("HIGH SCORES"), scores[0].name, tr("Round"), scores[0].round); text_center(s, FONT_SMALL, s->w / 2, s->h - 30, 0xd8b040, 0, b); }
    snprintf(b, sizeof b, "v%s", DK_VERSION);
    text(s, FONT_SMALL, 4, s->h - 8, 0x4a5262, b);
    text(s, FONT_SMALL, s->w - 4 - text_w(FONT_SMALL, "ROCKNIXDS"), s->h - 8, 0x4a5262, "ROCKNIXDS");
}

#define SETTINGS_N 12                   /* rows, BACK the last */
#define SETTINGS_BACK (SETTINGS_N - 1)
#define SETTINGS_ROW 19
static void draw_settings(Surf *s) {
    bg_bottom(s);
    static const char *assist[3] = { "Off", "Low", "High" }, *season[4] = { "Random", "Autumn", "Winter", "Midsummer" };
    static const char *effects[3] = { "Auto", "Full", "Light" };
    char items[SETTINGS_N][48];
    snprintf(items[0], 48, "%s: %d", tr("Volume"), S.volume);
    snprintf(items[1], 48, "%s: %s", tr("Music"), tr(S.music ? "On" : "Off"));
    snprintf(items[2], 48, "%s: %s", tr("Screen shake"), tr(S.shake ? "On" : "Off"));
    snprintf(items[3], 48, "%s: %s", tr("Aim assist"), tr(assist[S.assist]));
    snprintf(items[4], 48, "%s: %s", tr("Controls"), tr(S.scheme ? "Twin buttons" : "Classic"));
    snprintf(items[5], 48, "%s: %s", tr("Swap A/B"), tr(S.swap_ab ? "On" : "Off"));
    snprintf(items[6], 48, "%s: %s", tr("Touch aiming"), tr(S.touch_aim ? "On" : "Off"));
    snprintf(items[7], 48, "%s: %s", tr("Season"), tr(season[S.season]));
    snprintf(items[8], 48, "%s: %s", tr("Language"), S.lang ? "Svenska" : "English");
    snprintf(items[9], 48, "%s: %s", tr("Show FPS"), tr(S.show_fps ? "On" : "Off"));
    snprintf(items[10], 48, "%s: %s%s", tr("Effects"), tr(effects[S.effects]), S.effects == FX_AUTO && render_fx_light() ? tr(" (light now)") : "");
    snprintf(items[SETTINGS_BACK], 48, "%s", tr("BACK"));
    const char *p[SETTINGS_N]; for (int i = 0; i < SETTINGS_N; i++) p[i] = items[i];
    /* compact rows */
    int y0 = 6;
    for (int i = 0; i < SETTINGS_N; i++) {
        int y = y0 + i * SETTINGS_ROW, w = 240, cx = s->w / 2;
        if (i == A.sel) { rectf(s, cx - w / 2, y, w, 17, 0x3a1416); rect_line(s, cx - w / 2, y, w, 17, 0xc81818); }
        else rect_line(s, cx - w / 2, y, w, 17, 0x2a3040);
        text_center(s, FONT_NORMAL, cx, y + 4, i == A.sel ? 0xffffff : 0xa0a8b8, 0, p[i]);
        if (i == A.sel && i < SETTINGS_BACK) { text(s, FONT_NORMAL, cx - w / 2 + 4, y + 4, 0xc81818, "\xe2\x97\x80"); text(s, FONT_NORMAL, cx + w / 2 - 9, y + 4, 0xc81818, "\xe2\x96\xb6"); }
    }
}

static void settings_change(int i, int d) {
    switch (i) {
    case 0: S.volume = CLAMP(S.volume + d * 10, 0, 100); audio_set_volume(S.volume); break;
    case 1: S.music = !S.music; music_play(S.music ? (A.state == ST_SETTINGS && A.settings_from == ST_TITLE ? MUS_TITLE : MUS_NONE) : MUS_NONE); break;
    case 2: S.shake = !S.shake; break;
    case 3: S.assist = (S.assist + d + 3) % 3; break;
    case 4: S.scheme = !S.scheme; break;
    case 5: S.swap_ab = !S.swap_ab; break;
    case 6: S.touch_aim = !S.touch_aim; break;
    case 7: S.season = (S.season + d + 4) % 4; break;
    case 8: S.lang = !S.lang; break;
    case 9: S.show_fps = !S.show_fps; break;
    case 10: S.effects = (S.effects + d + 3) % 3; break;
    }
    sfx(SFX_MENU_MOVE, 0.5f, 0);
    settings_save();
}

/* how to play: pages of text on the bottom screen, a picture on the top */
static const char *HOWTO_EN[] = {
    "SURVIVE\n\nThe dead come in rounds, more and tougher each time. Survive as many rounds as you can: that is your score.\n\nEvery hit is +10 kr, a kill +60, a critical +100, the knife +130.\n\nQuitting keeps the run: Continue it from the title.",
    "SPEND YOUR KRONOR\n\nClear barriers to open new districts. Chalk outlines on walls are guns for sale. Lådan (the Mystery Box, 950 kr) gives a random weapon, until the Dalahäst carries it away.",
    "THE POWER\n\nFind the Elcentral and switch the power on. Then the perk machines and Smedjan (5 000 kr: Pack-a-Punch) work, the street lamps light up, and the elstängsel by a gap between districts (1 000 kr) shocks whatever crosses it.",
    "PERKS (max 4)\n\nJulmust 2 500: 250 health. Snabbkaffe 3 000: fast reloads. Salmiak 2 000: fire faster, hit harder. Kanelbulle 500: get back up. Blåbärssoppa 2 000: run. Lingondricka 2 000: shock on reload. Kaviar 4 000: a third gun.",
    "LOOT\n\nSearch bins, cars, mailboxes and sheds. Rarity: grey, green, blue, purple, gold. It gets better every round. Helmets and vests take hits for you. Wolves come on Vargnatt, and the moose... runs.",
};
static const char *HOWTO_SV[] = {
    "ÖVERLEV\n\nDe döda kommer i rundor, fler och starkare varje gång. Överlev så många rundor du kan: det är din poäng.\n\nVarje träff ger +10 kr, att döda +60, en kritisk träff +100, kniven +130.\n\nAvslutar du sparas spelet: fortsätt från titelskärmen.",
    "SPENDERA KRONOR\n\nRöj barrikader för att öppna nya kvarter. Kritkonturer på väggarna är vapen till salu. Lådan (950 kr) ger ett slumpvapen, tills Dalahästen bär iväg den.",
    "STRÖMMEN\n\nHitta elcentralen och slå på strömmen. Då fungerar automaterna och Smedjan (5 000 kr: uppgradera vapnet), gatlyktorna tänds, och elstängslet vid en passage mellan kvarteren (1 000 kr) ger alla som går igenom en stöt.",
    "FÖRMÅNER (max 4)\n\nJulmust 2 500: 250 hälsa. Snabbkaffe 3 000: snabb omladdning. Salmiak 2 000: skjut snabbare, hårdare. Kanelbulle 500: res dig igen. Blåbärssoppa 2 000: spring. Lingondricka 2 000: stöt vid omladdning. Kaviar 4 000: ett tredje vapen.",
    "BYTE\n\nSök i soptunnor, bilar, brevlådor och bodar. Sällsynthet: grå, grön, blå, lila, guld. Det blir bättre varje runda. Hjälmar och västar tar träffar åt dig. Vargarna kommer på Vargnatt, och älgen... springer.",
};
#define HOWTO_PAGES 5

static void draw_howto(Surf *s) {
    bg_bottom(s);
    const char *t = (S.lang ? HOWTO_SV : HOWTO_EN)[A.page];
    rect_line(s, 8, 8, s->w - 16, s->h - 30, 0x2a3040);
    text_wrap(s, FONT_NORMAL, 16, 16, s->w - 32, 0xd8dce4, t);
    char b[32]; snprintf(b, sizeof b, "\xe2\x97\x80 %d / %d \xe2\x96\xb6", A.page + 1, HOWTO_PAGES);
    text_center(s, FONT_NORMAL, s->w / 2, s->h - 17, 0xa0a8b8, 0, b);
}

static void draw_stats(Surf *s) {
    bg_bottom(s);
    text_center(s, FONT_NORMAL, s->w / 2, 12, 0xd8b040, 0, tr("STATISTICS"));
    char v[10][32];
    snprintf(v[0], 32, "%d", ST.runs); fmt_num(v[1], ST.kills); snprintf(v[2], 32, "%d", ST.rounds); snprintf(v[3], 32, "%d", ST.best_round);
    snprintf(v[4], 32, "%d:%02d", ST.secs / 3600, ST.secs / 60 % 60); fmt_num(v[5], ST.kr); strcat(v[5], " kr"); snprintf(v[6], 32, "%d", ST.boxes);
    snprintf(v[7], 32, "%d", ST.crits); snprintf(v[8], 32, "%d", ST.downs); snprintf(v[9], 32, "%d", ST.dailies);
    static const char *lab[10] = { "Runs", "Zombies killed", "Rounds survived", "Best round", "Time played", "Kronor earned",
                                   "Mystery Boxes", "Critical hits", "Times downed", "Days' towns played" };
    for (int i = 0; i < 10; i++) {
        int y = 34 + i * 17;
        rectf(s, s->w / 2 - 130, y - 3, 260, 15, i & 1 ? 0x161a22 : 0x1a1e28);
        text(s, FONT_NORMAL, s->w / 2 - 124, y, 0xa0a8b8, tr(lab[i]));
        text(s, FONT_NORMAL, s->w / 2 + 124 - text_w(FONT_NORMAL, v[i]), y, 0xe8ecf4, v[i]);
    }
}
static void draw_scores(Surf *s) {
    if (A.page == 1) { draw_stats(s); text_center(s, FONT_NORMAL, s->w / 2, s->h - 14, 0x5a6272, 0, "\xe2\x97\x80 2 / 2 \xe2\x96\xb6"); return; }
    bg_bottom(s);
    text_center(s, FONT_NORMAL, s->w / 2, s->h - 14, 0x5a6272, 0, "\xe2\x97\x80 1 / 2 \xe2\x96\xb6");
    int x = s->w / 2 - 150, y = 10;
    text(s, FONT_SMALL, x + 4, y, 0x7a8494, "#");
    text(s, FONT_SMALL, x + 22, y, 0x7a8494, "NAMN");
    text(s, FONT_SMALL, x + 70, y, 0x7a8494, S.lang ? "RUNDA" : "ROUND");
    text(s, FONT_SMALL, x + 118, y, 0x7a8494, S.lang ? "DÖDA" : "KILLS");
    text(s, FONT_SMALL, x + 160, y, 0x7a8494, "KR");
    text(s, FONT_SMALL, x + 214, y, 0x7a8494, S.lang ? "STAD" : "TOWN");
    if (!nscores) text_center(s, FONT_NORMAL, s->w / 2, s->h / 2, 0x7a8494, 0, tr("no scores yet"));
    for (int i = 0; i < nscores; i++) {
        Score *sc = &scores[i];
        int yy = y + 12 + i * 19;
        int hl = A.rank == i && A.state == ST_SCORES && ((int)(A.t * 4) & 1);
        rectf(s, x, yy - 3, 300, 17, i & 1 ? 0x161a22 : 0x1a1e28);
        if (A.rank == i) rect_line(s, x, yy - 3, 300, 17, 0xd8b040);
        uint32_t c = i == 0 ? 0xf0d040 : i == 1 ? 0xd0d0d8 : i == 2 ? 0xd08a50 : 0xd8dce4;
        char b[48];
        snprintf(b, sizeof b, "%d", i + 1); text(s, FONT_NORMAL, x + 4, yy + 1, c, b);
        text(s, FONT_NORMAL, x + 22, yy + 1, hl ? 0xffffff : c, sc->name);
        snprintf(b, sizeof b, "%d", sc->round); text(s, FONT_NORMAL, x + 70, yy + 1, 0xd81818, b);
        snprintf(b, sizeof b, "%d", sc->kills); text(s, FONT_NORMAL, x + 118, yy + 1, c, b);
        fmt_num(b, sc->kr); text(s, FONT_NORMAL, x + 160, yy + 1, c, b);
        Surf cl = *s; surf_clip(&cl, x + 212, yy - 2, 86, 14);
        if (sc->daily) { text(&cl, FONT_SMALL, x + 214, yy + 3, 0xd8b040, "\xe2\x98\x85"); text(&cl, FONT_SMALL, x + 221, yy + 3, 0xd8b040, sc->town); }
        else text(&cl, FONT_SMALL, x + 214, yy + 3, 0x8a94a4, sc->town);
    }
}

static const char *KEYS = "ABCDEFGHIJKLMNOPQRSTUVWXYZÅÄÖ0123456789";
static int key_count(void) { int n = 0; for (const char *p = KEYS; *p; ) { unsigned char c = (unsigned char)*p; p += c < 0x80 ? 1 : 2; n++; } return n; }
static void key_str(int i, char *out) {
    const char *p = KEYS;
    for (int k = 0; *p; k++) {
        unsigned char c = (unsigned char)*p; int l = c < 0x80 ? 1 : 2;
        if (k == i) { memcpy(out, p, (size_t)l); out[l] = 0; return; }
        p += l;
    }
    out[0] = 0;
}
static void draw_name(Surf *s) {
    bg_bottom(s);
    text_center(s, FONT_NORMAL, s->w / 2, 10, 0xd8b040, 0, tr("Enter your initials"));
    for (int i = 0; i < 3; i++) {
        int x = s->w / 2 - 45 + i * 32, y = 26;
        rectf(s, x, y, 26, 30, i == A.name_pos ? 0x3a1416 : 0x1a1e28);
        rect_line(s, x, y, 26, 30, i == A.name_pos ? 0xc81818 : 0x2a3040);
        char ch[4]; key_str(A.letters[i], ch);
        text_big(s, x + 6, y + 6, 3, 0xffffff, 0x000000, ch);
    }
    int n = key_count(), cols = 13;
    for (int i = 0; i < n; i++) {
        int x = s->w / 2 - cols * 11 + (i % cols) * 22, y = 72 + (i / cols) * 22;
        char ch[4]; key_str(i, ch);
        int sel = A.letters[A.name_pos] == i;
        rectf(s, x, y, 20, 20, sel ? 0x3a1416 : 0x1a1e28); rect_line(s, x, y, 20, 20, sel ? 0xc81818 : 0x2a3040);
        text_center(s, FONT_NORMAL, x + 10, y + 6, 0xd8dce4, 0, ch);
    }
    int oky = 72 + ((n + cols - 1) / cols) * 22 + 8;
    rectf(s, s->w / 2 - 40, oky, 80, 20, 0x14301a); rect_line(s, s->w / 2 - 40, oky, 80, 20, 0x40c060);
    text_center(s, FONT_NORMAL, s->w / 2, oky + 6, 0xffffff, 0, "OK");
    text_center(s, FONT_SMALL, s->w / 2, s->h - 10, 0x7a8494, 0, S.lang ? "UPP/NER: BOKSTAV  VÄNSTER/HÖGER: PLATS  A: KLAR" : "UP/DOWN: LETTER  LEFT/RIGHT: PLACE  A: DONE");
}

static void draw_gameover_bottom(Surf *s) {
    bg_bottom(s);
    Player *p = &G->p;
    char b[64], n[24];
    int y = 14, x = s->w / 2 - 110;
    text_center(s, FONT_NORMAL, s->w / 2, y, 0xd81818, 0, G->town);
    y += 18;
    struct { const char *k; int v; int kr; } st[] = {
        { "Rounds", G->round, 0 }, { "Kills", p->kills, 0 }, { "Score", p->kr_total, 1 }, { "Downs", p->downs, 0 },
        { "Doors", p->doors, 0 }, { "Boxes", p->boxes, 0 }, { "Knife", p->knifes, 0 },
    };
    for (int i = 0; i < ARRAY_LEN(st); i++) {
        rectf(s, x, y - 2, 220, 14, i & 1 ? 0x161a22 : 0x1a1e28);
        text(s, FONT_NORMAL, x + 6, y + 1, 0xa0a8b8, tr(st[i].k));
        if (st[i].kr) { fmt_num(n, st[i].v); snprintf(b, sizeof b, "%s kr", n); } else snprintf(b, sizeof b, "%d", st[i].v);
        text(s, FONT_NORMAL, x + 214 - text_w(FONT_NORMAL, b), y + 1, 0xffffff, b);
        y += 15;
    }
    int t = (int)G->time; snprintf(b, sizeof b, "%s %d:%02d", tr("Time"), t / 60, t % 60);
    text_center(s, FONT_NORMAL, s->w / 2, y + 6, 0x7a8494, 0, b);
    if (A.t > 2 && ((int)(A.t * 2) & 1)) text_center(s, FONT_NORMAL, s->w / 2, s->h - 18, 0xffffff, 0, tr("Press A"));
}

void title_top(Surf *s) {
    skyline(s, A.t, 0);
    logo(s, 30, A.t);
    text_center(s, FONT_NORMAL, s->w / 2, 30 + 9 * 4 + 9 * 3 + 8, 0x9aa4b8, 0x000000, tr("a zombie roguelike in Swedish suburbia"));
}

/* ---------------------------------------------------------------- the state machine (main.c calls) */
#define PAUSE_ITEMS 5
static void refresh_save_info(void) { A.has_save = run_peek(A.save_town, sizeof A.save_town, &A.save_round) == 0; }
static void new_run(uint64_t seed, int daily) {
    run_discard(); A.has_save = 0;
    render_fx_reset();
    int season = S.season && !daily ? S.season - 1 : (int)((seed * 0x9E3779B97F4A7C15ull >> 40) % SEASON_COUNT);   /* a seed is a whole run */
    game_new(seed, season);
    G->daily = daily;
    A.state = ST_PLAY; A.t = 0; A.last_rstate = G->rstate;
    music_play(MUS_NONE);
}
void app_new_run(void) { new_run(A.seed_override ? A.seed_override : ((uint64_t)time(0) * 2654435761u) ^ (uint64_t)clock(), 0); }
static void continue_run(void) {
    if (run_load()) { refresh_save_info(); return; }
    A.state = ST_PAUSE; A.sel = 0; A.t = 0; A.last_rstate = G->rstate;   /* back where you left it, paused */
    music_play(MUS_NONE);
}
/* a run is going on (playing, paused, or in a menu opened from the pause) */
int app_run_in_progress(void) {
    if (!G || G->over || G->round <= 0) return 0;
    return A.state == ST_PLAY || A.state == ST_PAUSE || (A.state == ST_SETTINGS && A.settings_from == ST_PAUSE) ||
           (A.state == ST_HOWTO && A.howto_from == ST_PAUSE);
}

static void end_run(void) {
    run_discard(); A.has_save = 0;
    A.state = ST_GAMEOVER; A.t = 0; G->over = 1;
    memset(&A.last, 0, sizeof A.last);
    A.last.round = G->round; A.last.kills = G->p.kills; A.last.kr = G->p.kr_total; A.last.secs = (int)G->time;
    A.last.season = G->season; A.last.seed = G->seed; A.last.date = (long long)time(0); A.last.daily = G->daily;
    stats_add_run();
    snprintf(A.last.town, sizeof A.last.town, "%s", G->town);
    A.rank = score_rank(&A.last);
}

void app_update(const Input *in, const Input *prev, float dt) {
    A.t += dt;
    /* the night outside, under a run: rain in autumn, wind in winter, crickets and birds at midsummer */
    audio_ambience(!app_run_in_progress() ? AMB_NONE : G->season == SEASON_WINTER ? AMB_WIND : G->season == SEASON_SUMMER ? AMB_SUMMER : AMB_RAIN);
    int up = pressed(in, prev, B_UP), down = pressed(in, prev, B_DOWN), left = pressed(in, prev, B_LEFT), right = pressed(in, prev, B_RIGHT);
    switch (A.state) {
    case ST_TITLE: {
        if (!A.save_checked) { refresh_save_info(); A.save_checked = 1; }
        int acts[8], n = title_items(acts, 0);
        if (A.sel >= n) A.sel = 0;
        if (up) { A.sel = (A.sel + n - 1) % n; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        if (down) { A.sel = (A.sel + 1) % n; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        int tr_ = touch_down(in, prev) ? touch_row(in, title_y0(A.bot_h, n), n) : -1;
        if (tr_ >= 0) A.sel = tr_;
        if (ok_pressed(in, prev) || tr_ >= 0) {
            sfx(SFX_MENU_OK, 0.6f, 0);
            switch (acts[A.sel]) {
            case T_CONTINUE: continue_run(); break;
            case T_PLAY: case T_NEW: app_new_run(); break;
            case T_DAILY: { int d = today(); new_run(daily_seed(d), d); break; }
            case T_SCORES: A.state = ST_SCORES; A.rank = -1; A.page = 0; break;
            case T_SETTINGS: A.state = ST_SETTINGS; A.settings_from = ST_TITLE; A.sel = 0; break;
            case T_HOWTO: A.state = ST_HOWTO; A.page = 0; break;
            case T_QUIT: A.quit = 1; break;
            }
        }
        break;
    }
    case ST_PLAY:
        if (pressed(in, prev, B_START) || pressed(in, prev, B_MENU)) { A.state = ST_PAUSE; A.sel = 0; sfx(SFX_MENU_BACK, 0.5f, 0); break; }
        if (in->touch[1] && !prev->touch[1]) hud_touch(in->tx[1], in->ty[1]);
        game_update(in, prev, dt);
        if (G->rstate == RS_BREAK && A.last_rstate != RS_BREAK && !G->over) run_autosave();   /* a round won: kept */
        A.last_rstate = G->rstate;
        if (G->over && G->over_t > 1.5f) end_run();
        break;
    case ST_PAUSE: {
        if (up) { A.sel = (A.sel + PAUSE_ITEMS - 1) % PAUSE_ITEMS; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        if (down) { A.sel = (A.sel + 1) % PAUSE_ITEMS; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        int tr_ = touch_down(in, prev) ? touch_row(in, A.bot_h / 2 - 56, PAUSE_ITEMS) : -1;
        if (tr_ >= 0) A.sel = tr_;
        if (pressed(in, prev, B_START) || back_pressed(in, prev)) { A.state = ST_PLAY; break; }
        if (pressed(in, prev, btn_fire()) || tr_ >= 0) {
            sfx(SFX_MENU_OK, 0.6f, 0);
            if (A.sel == 0) A.state = ST_PLAY;
            else if (A.sel == 1) { A.state = ST_SETTINGS; A.settings_from = ST_PAUSE; A.sel = 0; }
            else if (A.sel == 2) { A.state = ST_HOWTO; A.page = 0; A.howto_from = ST_PAUSE; }
            else if (A.sel == 3) {                           /* save and quit: the run waits on the title */
                run_save(); refresh_save_info();
                A.state = ST_TITLE; A.sel = 0; A.t = 0; music_play(MUS_TITLE);
            } else { G->over = 1; end_run(); }
        }
        break;
    }
    case ST_SETTINGS: {
        if (up) { A.sel = (A.sel + SETTINGS_N - 1) % SETTINGS_N; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        if (down) { A.sel = (A.sel + 1) % SETTINGS_N; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        if (in->touch[1] && !prev->touch[1]) {
            int r = (in->ty[1] - 6) / SETTINGS_ROW;
            if (r >= 0 && r < SETTINGS_N) { A.sel = r; if (r < SETTINGS_BACK) settings_change(r, in->tx[1] < A.bot_w / 2 ? -1 : 1); else goto settings_back; }
        }
        if (A.sel < SETTINGS_BACK && (left || right)) settings_change(A.sel, left ? -1 : 1);
        if (A.sel < SETTINGS_BACK && pressed(in, prev, btn_fire())) settings_change(A.sel, 1);
        if (back_pressed(in, prev) || pressed(in, prev, B_START) || (A.sel == SETTINGS_BACK && pressed(in, prev, btn_fire()))) {
        settings_back:
            sfx(SFX_MENU_BACK, 0.5f, 0);
            A.state = A.settings_from; A.sel = A.settings_from == ST_PAUSE ? 1 : title_index(T_SETTINGS);
        }
        break;
    }
    case ST_HOWTO:
        if (left || (in->touch[1] && !prev->touch[1] && in->tx[1] < A.bot_w / 3)) { A.page = (A.page + HOWTO_PAGES - 1) % HOWTO_PAGES; sfx(SFX_MENU_MOVE, 0.5f, 0); }
        else if (right || ok_pressed(in, prev) || (in->touch[1] && !prev->touch[1] && in->tx[1] > A.bot_w * 2 / 3)) {
            if (A.page == HOWTO_PAGES - 1 && !right) { A.state = A.howto_from == ST_PAUSE ? ST_PAUSE : ST_TITLE; A.howto_from = 0; break; }
            A.page = (A.page + 1) % HOWTO_PAGES; sfx(SFX_MENU_MOVE, 0.5f, 0);
        }
        if (back_pressed(in, prev)) { A.state = A.howto_from == ST_PAUSE ? ST_PAUSE : ST_TITLE; A.howto_from = 0; sfx(SFX_MENU_BACK, 0.5f, 0); }
        break;
    case ST_GAMEOVER:
        game_update(in, prev, dt);
        if (A.t > 2 && (ok_pressed(in, prev) || touch_down(in, prev))) {
            sfx(SFX_MENU_OK, 0.6f, 0);
            if (A.rank >= 0) { A.state = ST_NAME; A.name_pos = 0; A.letters[0] = A.letters[1] = A.letters[2] = 0; }
            else { A.state = ST_SCORES; A.t = 0; music_play(MUS_TITLE); }
        }
        break;
    case ST_NAME: {
        int n = key_count();
        if (up) { A.letters[A.name_pos] = (A.letters[A.name_pos] + n - 1) % n; sfx(SFX_MENU_MOVE, 0.4f, 0); }
        if (down) { A.letters[A.name_pos] = (A.letters[A.name_pos] + 1) % n; sfx(SFX_MENU_MOVE, 0.4f, 0); }
        if (left) A.name_pos = (A.name_pos + 2) % 3;
        if (right) A.name_pos = (A.name_pos + 1) % 3;
        int done = ok_pressed(in, prev);
        if (in->touch[1] && !prev->touch[1]) {               /* tap a key, or OK */
            int cols = 13, tx = in->tx[1], ty = in->ty[1];
            int c = (tx - (A.bot_w / 2 - cols * 11)) / 22, r = (ty - 72) / 22;
            int oky = 72 + ((n + cols - 1) / cols) * 22 + 8;
            if (ty >= oky && ty < oky + 20 && abs(tx - A.bot_w / 2) < 40) done = 1;
            else if (c >= 0 && c < cols && r >= 0 && r * cols + c < n && ty >= 72) { A.letters[A.name_pos] = r * cols + c; A.name_pos = MIN(2, A.name_pos + 1); sfx(SFX_MENU_MOVE, 0.4f, 0); }
        }
        if (done) {
            char nm[16] = { 0 };
            for (int i = 0; i < 3; i++) { char ch[4]; key_str(A.letters[i], ch); strcat(nm, ch); }
            snprintf(A.last.name, sizeof A.last.name, "%s", nm);
            score_insert(&A.last, A.rank);
            A.state = ST_SCORES; A.t = 0;
            sfx(SFX_MENU_OK, 0.6f, 0);
            music_play(MUS_TITLE);
        }
        break;
    }
    case ST_SCORES:
        if (left || right) { A.page = !A.page; sfx(SFX_MENU_MOVE, 0.5f, 0); break; }
        if (touch_down(in, prev) && in->ty[1] > A.bot_h - 24) { A.page = !A.page; sfx(SFX_MENU_MOVE, 0.5f, 0); break; }
        if (A.t > 0.3f && (ok_pressed(in, prev) || back_pressed(in, prev) || touch_down(in, prev))) {
            A.state = ST_TITLE; A.sel = 0; A.rank = -1; sfx(SFX_MENU_BACK, 0.5f, 0);
            music_play(MUS_TITLE);
        }
        break;
    }
}

void app_render(Surf *top, Surf *bot) {
    switch (A.state) {
    case ST_TITLE: case ST_SCORES: case ST_HOWTO:
        if (A.state == ST_HOWTO && A.howto_from == ST_PAUSE) { render_game(top); rect_blend(top, 0, 0, top->w, top->h, 0x000000, 150); }
        else {
            title_top(top);
            if (A.state == ST_SCORES) { rect_blend(top, 0, top->h - 34, top->w, 26, 0x000000, 150); text_center(top, FONT_NORMAL, top->w / 2, top->h - 26, 0xf0d040, 0x000000, tr("HIGH SCORES")); }
        }
        if (A.state == ST_TITLE) draw_title_bottom(bot);
        else if (A.state == ST_SCORES) draw_scores(bot);
        else draw_howto(bot);
        break;
    case ST_SETTINGS:
        if (A.settings_from == ST_PAUSE) { render_game(top); rect_blend(top, 0, 0, top->w, top->h, 0x000000, 150); }
        else title_top(top);
        draw_settings(bot);
        break;
    case ST_PLAY:
        render_game(top);
        render_hud(bot, 0);
        break;
    case ST_PAUSE: {
        render_game(top);
        rect_blend(top, 0, 0, top->w, top->h, 0x000000, 140);
        text_big(top, top->w / 2 - text_w(FONT_NORMAL, tr("PAUSED")) * 3 / 2, top->h / 2 - 12, 3, 0xffffff, 0x000000, tr("PAUSED"));
        bg_bottom(bot);
        const char *items[PAUSE_ITEMS] = { tr("RESUME"), tr("SETTINGS"), tr("HOW TO PLAY"), tr("SAVE AND QUIT"), tr("GIVE UP") };
        menu_list(bot, items, PAUSE_ITEMS, A.sel, bot->h / 2 - 56);
        break;
    }
    case ST_GAMEOVER: case ST_NAME: {
        render_game(top);
        /* the screen fades, and the line everyone remembers */
        int a = (int)MIN(190.0f, A.t * 120);
        rect_blend(top, 0, 0, top->w, top->h, 0x000000, a);
        if (A.t > 0.6f) {
            const char *l1 = tr("YOU SURVIVED");
            char l2[32]; snprintf(l2, sizeof l2, "%d %s", G->round, G->round == 1 ? (S.lang ? "RUNDA" : "ROUND") : tr("ROUNDS"));
            int k = 2;
            text_big(top, top->w / 2 - text_w(FONT_NORMAL, l1) * k / 2, top->h / 2 - 34, k, 0xd8d8d8, 0x000000, l1);
            text_big(top, top->w / 2 - text_w(FONT_NORMAL, l2) * 3 / 2, top->h / 2 - 8, 3, 0xc81818, 0x200000, l2);
            if (A.rank >= 0 && A.t > 1.2f) text_center(top, FONT_NORMAL, top->w / 2, top->h / 2 + 26, 0xf0d040, 0x000000, tr("New high score!"));
        }
        if (A.state == ST_NAME) draw_name(bot); else draw_gameover_bottom(bot);
        break;
    }
    }
}
