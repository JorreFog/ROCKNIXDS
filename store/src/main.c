// main.c: the ROCKNIXDS Store. The top screen shows the app under the cursor (its icon, name, maker, versions, a
// screenshot and what it is); the bottom screen is the shelf: Games, Apps and Installed, a row per app with what A
// does to it. Every job (refresh, install, update, remove) is the package manager's (device/rocknixds-store), run in a
// thread while the screens keep going.
//
//   store [--backend kms|sdl|headless] [--size WxH] [--script "down; a; job; shot out.png; quit"]
//
// --script drives it without a person (tests, the README's pictures): button names (up down left right a b x y l r
// select start) press once, "wait N" waits N frames, "job" waits until the package manager is done, "tap X Y" touches
// the bottom screen, "shot <file>" writes both screens to a PNG, "quit" ends it.
#define _GNU_SOURCE
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#ifndef STORE_VERSION
#define STORE_VERSION "dev"
#endif
#define UPDATE_EXIT 75                  /* the Store updated itself: the session starts the new one */

/* ROCKNIXDS Pixel's dark palette, near enough */
#define C_BG      0x15171d
#define C_PANEL   0x20232b
#define C_PANEL2  0x2a2e38
#define C_LINE    0x363b47
#define C_TEXT    0xeceef2
#define C_DIM     0x8c93a3
#define C_FAINT   0x5c6270
#define C_GREEN   0x2eae62
#define C_BLUE    0x4c8ee0
#define C_RED     0xe25a5a
#define C_YELLOW  0xf2c94c

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }

typedef struct {
    Catalog cat;
    int tab, sel[TAB_COUNT], scroll[TAB_COUNT];
    int view[MAX_APPS], nview;          /* the current tab's apps, as indexes into cat */
    int about;                          /* the top screen shows the whole description */
    int confirm;                        /* 1: remove the selected app? */
    int modal;                          /* a job's box on the bottom screen (install, update, remove) */
    int refreshing, offline;
    int restart;                        /* the Store updated itself: start the new one on A */
    char pending[160], pending_what[96];/* a job waiting for the refresh to end */
    char toast[160]; int toast_t;
    int changed;                        /* something was installed or removed: ES shows it when it's back */
    int frame;
    int W, H, BW, BH;
} UI;

static UI U;

/* ---- helpers ------------------------------------------------------------------------------------------------------- */
static App *cur(void) { return U.nview ? &U.cat.apps[U.view[U.sel[U.tab]]] : 0; }

static void build_view(void) {
    U.nview = 0;
    for (int i = 0; i < U.cat.n; i++) if (app_tab_has(&U.cat.apps[i], U.tab)) U.view[U.nview++] = i;
    if (U.sel[U.tab] >= U.nview) U.sel[U.tab] = U.nview ? U.nview - 1 : 0;
}

static void reload(void) {
    char keep[40] = "";
    App *a = cur(); if (a) snprintf(keep, sizeof keep, "%s", a->id);
    catalog_load(&U.cat);
    build_view();
    for (int i = 0; i < U.nview; i++) if (!strcmp(U.cat.apps[U.view[i]].id, keep)) U.sel[U.tab] = i;
}

static void toast(const char *s) { snprintf(U.toast, sizeof U.toast, "%s", s); U.toast_t = 240; }

static void fmt_size(char *o, size_t n, long b) {
    if (b <= 0) { o[0] = 0; return; }
    if (b >= 1024L * 1024) snprintf(o, n, "%.1f MB", b / 1048576.0);
    else snprintf(o, n, "%ld KB", (b + 1023) / 1024);
}

static uint32_t darken(uint32_t c, int k) { return col_scale(c, k); }

static const char *action_label(const App *a) {
    switch (a->state) {
    case ST_INSTALL: return "INSTALL";
    case ST_UPDATE: return "UPDATE";
    case ST_INSTALLED: return "INSTALLED";
    case ST_UNSUPPORTED: return "NOT FOR THIS";
    case ST_NEEDS: return "NEEDS UPDATE";
    default: return "SOON";
    }
}

static void start_job(const char *verb, App *a) {
    char args[160], what[96];
    snprintf(args, sizeof args, "%s %s", verb, a ? a->id : "");
    if (!strcmp(verb, "install")) snprintf(what, sizeof what, "Installing %s", a->name);
    else if (!strcmp(verb, "update") && a) snprintf(what, sizeof what, "Updating %s", a->name);
    else if (!strcmp(verb, "update")) snprintf(what, sizeof what, "Updating everything");
    else snprintf(what, sizeof what, "Removing %s", a->name);
    if (U.refreshing) {                 /* one job at a time: after the refresh */
        snprintf(U.pending, sizeof U.pending, "%s", args);
        snprintf(U.pending_what, sizeof U.pending_what, "%s", what);
        U.modal = 1;
        return;
    }
    job_start(args, what);
    U.modal = 1;
}

static void refresh(void) {
    if (job_state(0, 0, 0, 0) == JOB_RUNNING) return;
    job_start("refresh", "Checking for updates");
    U.refreshing = 1;
}

/* ---- input --------------------------------------------------------------------------------------------------------- */
static void press_a(void) {
    App *a = cur();
    if (!a) return;
    if (a->state == ST_INSTALL || a->state == ST_UPDATE) start_job(a->state == ST_INSTALL ? "install" : "update", a);
    else if (a->state == ST_INSTALLED) toast("Installed: its tile is on the menu's home screen");
    else if (a->state == ST_NEEDS) toast("It needs a newer ROCKNIXDS: update it from the menu first");
    else if (a->state == ST_UNSUPPORTED) toast("It isn't made for this handheld");
    else toast(U.offline ? "No network: connect to Wi-Fi, then START" : "No release of it yet");
}

static void handle(uint32_t pressed, const Input *in, const Input *prev) {
    int jst = job_state(0, 0, 0, 0);
    /* a job's box: A or B closes it once the job is done */
    if (U.modal) {
        if (jst == JOB_RUNNING || U.pending[0]) return;
        if (pressed & (BIT(B_A) | BIT(B_B)) || (in->touch[1] && !prev->touch[1])) {
            U.modal = 0;
            job_ack();
            if (U.restart) stop = 2;
        }
        return;
    }
    if (U.confirm) {
        if (pressed & (BIT(B_A) | BIT(B_X))) { U.confirm = 0; start_job("remove", cur()); }
        else if (pressed & BIT(B_B)) U.confirm = 0;
        return;
    }
    int n = U.nview, *sel = &U.sel[U.tab];
    if (pressed & BIT(B_UP) && n) *sel = (*sel + n - 1) % n;
    if (pressed & BIT(B_DOWN) && n) *sel = (*sel + 1) % n;
    if (pressed & (BIT(B_LEFT) | BIT(B_L1))) { U.tab = (U.tab + TAB_COUNT - 1) % TAB_COUNT; build_view(); U.about = 0; }
    if (pressed & (BIT(B_RIGHT) | BIT(B_R1))) { U.tab = (U.tab + 1) % TAB_COUNT; build_view(); U.about = 0; }
    if (pressed & BIT(B_A)) press_a();
    if (pressed & BIT(B_X)) {
        App *a = cur();
        if (a && a->have[0] && strcmp(a->id, "store")) U.confirm = 1;
        else if (a && a->have[0]) toast("The Store stays: it's how apps get here");
    }
    if (pressed & BIT(B_Y)) {
        if (updates_count(&U.cat)) start_job("update", 0);
        else toast("Everything is up to date");
    }
    if (pressed & BIT(B_SELECT)) U.about = !U.about;
    if (pressed & BIT(B_START)) refresh();
    if (pressed & BIT(B_B)) stop = 1;
    /* touch on the bottom screen: a tab, a row, the row's button */
    if (in->touch[1] && !prev->touch[1]) {
        int x = in->tx[1], y = in->ty[1];
        if (y < 20) { int t = x * TAB_COUNT / U.BW; if (t != U.tab) { U.tab = CLAMP(t, 0, TAB_COUNT - 1); build_view(); U.about = 0; } }
        else if (y >= 22 && y < U.BH - 16) {
            int row = (y - 22) / 36 + U.scroll[U.tab];
            if (row < n) {
                if (row == *sel && x > U.BW - 78) press_a();
                *sel = row;
            }
        } else if (y >= U.BH - 16) {
            if (x < U.BW / 4) press_a(); else if (x > U.BW - 48) stop = 1;
        }
    }
    if (*sel < U.scroll[U.tab]) U.scroll[U.tab] = *sel;
    int rows = (U.BH - 16 - 22) / 36;
    if (*sel >= U.scroll[U.tab] + rows) U.scroll[U.tab] = *sel - rows + 1;
}

/* a job that ended: what it did, and the shelf read again */
static void job_poll(void) {
    char step[160];
    int st = job_state(step, sizeof step, 0, 0);
    if (st == JOB_RUNNING || st == JOB_IDLE) return;
    if (U.refreshing) {
        U.refreshing = 0;
        U.offline = st != JOB_DONE;
        job_ack();
        reload();
        if (U.pending[0]) { job_start(U.pending, U.pending_what); U.pending[0] = 0; }
        else if (U.offline) toast("No network: showing what the Store knew");
        return;
    }
    if (!U.modal) { job_ack(); return; }
    /* install/update/remove: the box stays with the result until A */
    static char done_for[160];
    if (strcmp(done_for, job_args())) {
        snprintf(done_for, sizeof done_for, "%s", job_args());
        if (st == JOB_DONE) {
            U.changed = 1;
            if (!strcmp(job_args(), "install store") || !strcmp(job_args(), "update store")) U.restart = 1;
            if (!strcmp(job_args(), "update ")) {
                /* every update: the Store's own among them restarts it */
                for (int i = 0; i < U.cat.n; i++)
                    if (!strcmp(U.cat.apps[i].id, "store") && U.cat.apps[i].state == ST_UPDATE) U.restart = 1;
            }
        }
        reload();
    }
}

/* ---- drawing ------------------------------------------------------------------------------------------------------- */
static void panel(Surf *s, int x, int y, int w, int h, uint32_t face, uint32_t edge) {
    rectf(s, x + 1, y, w - 2, h, face); rectf(s, x, y + 1, w, h - 2, face);
    hline(s, x + 1, x + w - 2, y, edge); hline(s, x + 1, x + w - 2, y + h - 1, darken(edge, 160));
    vline(s, x, y + 1, y + h - 2, edge); vline(s, x + w - 1, y + 1, y + h - 2, darken(edge, 160));
}

static void icon_box(Surf *s, const App *a, int x, int y, int k) {
    int sz = 32 * k;
    if (a->icon.px) {
        if (k == 1) blit(s, &a->icon, x + (32 - a->icon.w) / 2, y + (32 - a->icon.h) / 2, 0);
        else blit_scaled(s, &a->icon, x + (sz - a->icon.w * k) / 2, y + (sz - a->icon.h * k) / 2, k);
    } else {
        panel(s, x, y, sz, sz, a->accent, col_add(a->accent, 0x303030));
        char l[8] = { (char)toupper((unsigned char)a->name[0]), 0 };
        if (k == 1) text_center(s, FONT_NORMAL, x + 16, y + 12, C_TEXT, 0, l);
        else text_big(s, x + sz / 2 - 5 * k / 2, y + sz / 2 - 7 * k / 2, k, C_TEXT, 0, l);
    }
}

/* text cut to w pixels, "..." at the end when it doesn't fit */
static void text_fit(Surf *s, int font, int x, int y, int w, uint32_t c, const char *str) {
    char b[200]; snprintf(b, sizeof b, "%s", str);
    if (text_w(font, b) <= w) { text(s, font, x, y, c, b); return; }
    size_t n = strlen(b);
    while (n > 0) {
        b[--n] = 0;
        while (n > 0 && (b[n - 1] & 0xC0) == 0x80) b[--n] = 0;   /* whole UTF-8 characters */
        char t[210]; snprintf(t, sizeof t, "%s...", b);
        if (text_w(font, t) <= w) { text(s, font, x, y, c, t); return; }
    }
}

static void draw_top(Surf *s) {
    int W = s->w, H = s->h;
    fill(s, C_BG);
    App *a = cur();
    if (!a) {
        text_big(s, 16, 20, 2, C_TEXT, 0, "ROCKNIXDS Store");
        text_wrap(s, FONT_NORMAL, 16, 56, W - 32, C_DIM,
                  U.cat.n ? "Nothing here yet." : "The catalog isn't here yet. Connect to Wi-Fi and press START.");
        return;
    }
    uint32_t acc = a->accent;
    /* header: the accent, dark, with a lighter strip at its foot */
    for (int y = 0; y < 84; y++) hline(s, 0, W - 1, y, col_mix(darken(acc, 70), C_BG, y * 256 / 84));
    hline(s, 0, W - 1, 84, darken(acc, 180));
    icon_box(s, a, 10, 10, 2);
    /* the name: twice the size when it fits, else once */
    if (text_w(FONT_NORMAL, a->name) * 2 <= W - 92) text_big(s, 84, 12, 2, C_TEXT, darken(acc, 50), a->name);
    else text_fit(s, FONT_NORMAL, 84, 16, W - 92, C_TEXT, a->name);
    char line[200];
    snprintf(line, sizeof line, "%s%s%s", a->dev, a->genre[0] ? "  -  " : "", a->genre);
    text_fit(s, FONT_NORMAL, 84, 36, W - 92, C_DIM, line);
    char sz[24]; fmt_size(sz, sizeof sz, a->size);
    if (a->have[0] && a->latest[0] && a->state == ST_UPDATE) snprintf(line, sizeof line, "%s  ->  %s", a->have, a->latest);
    else if (a->have[0]) snprintf(line, sizeof line, "Version %s", a->have);
    else if (a->latest[0]) snprintf(line, sizeof line, "Version %s", a->latest);
    else snprintf(line, sizeof line, "Not released yet");
    int x = text(s, FONT_NORMAL, 84, 50, a->state == ST_UPDATE ? C_YELLOW : C_TEXT, line);
    if (sz[0]) { text(s, FONT_NORMAL, x + 8, 50, C_DIM, sz); }
    /* the state, as a pill */
    const char *st = a->state == ST_UPDATE ? "UPDATE OUT" : a->state == ST_INSTALLED ? "INSTALLED" :
                     a->state == ST_INSTALL ? (!strcmp(a->kind, "game") ? "GAME" : "APP") : action_label(a);
    uint32_t pc = a->state == ST_UPDATE ? C_YELLOW : a->state == ST_INSTALLED ? C_GREEN : C_PANEL2;
    int pw = text_w(FONT_SMALL, st) + 10;
    panel(s, 84, 64, pw, 11, pc, col_add(pc, 0x202020));
    text(s, FONT_SMALL, 89, 67, pc == C_PANEL2 ? C_TEXT : 0x101010, st);
    /* below: the screenshot and the summary, or the whole description */
    int y0 = 92;
    surf_clip(s, 0, y0, W, H - 14 - y0);
    if (U.about || !a->shot.px) {
        int y = y0;
        if (!U.about) { y += text_wrap(s, FONT_NORMAL, 10, y, W - 20, C_TEXT, a->summary) + 4; }
        text_wrap(s, FONT_NORMAL, 10, y, W - 20, U.about ? C_TEXT : C_DIM, a->desc[0] ? a->desc : a->summary);
    } else {
        int sw = a->shot.w, sh = a->shot.h;
        rectf(s, 9, y0 + 1, sw + 2, sh + 2, 0x000000);
        rect_line(s, 8, y0, sw + 2, sh + 2, C_LINE);
        blit(s, &a->shot, 9, y0 + 1, 0);
        int tx = sw + 20;
        text_wrap(s, FONT_NORMAL, tx, y0, W - tx - 8, C_TEXT, a->summary);
    }
    surf_noclip(s);
    /* the foot: what the Store knows */
    rectf(s, 0, H - 13, W, 13, C_PANEL);
    char foot[160];
    if (U.refreshing) snprintf(foot, sizeof foot, "Checking for updates%.*s", (U.frame / 20) % 4, "...");
    else if (U.offline) snprintf(foot, sizeof foot, "No network: what the Store knew last time");
    else if (U.cat.refreshed) {
        long ago = (long)time(0) - U.cat.refreshed;
        if (ago < 120) snprintf(foot, sizeof foot, "Up to date: checked just now");
        else if (ago < 7200) snprintf(foot, sizeof foot, "Checked %ld minutes ago", ago / 60);
        else snprintf(foot, sizeof foot, "Checked %ld hours ago", ago / 3600);
    } else snprintf(foot, sizeof foot, "Not checked yet: START checks");
    text(s, FONT_NORMAL, 6, H - 11, C_DIM, foot);
    const char *hint = U.about ? "SELECT: back" : "SELECT: about";
    text(s, FONT_NORMAL, W - 6 - text_w(FONT_NORMAL, hint), H - 11, C_FAINT, hint);
}

static void help(Surf *s, int x, int y, const char *items) {   /* "A Install|X Remove": a button glyph and its word */
    char b[200]; snprintf(b, sizeof b, "%s", items);
    for (char *t = strtok(b, "|"); t; t = strtok(0, "|")) {
        char key[8]; int k = 0; while (t[k] && t[k] != ' ' && k < 7) { key[k] = t[k]; k++; } key[k] = 0;
        int kw = MAX(9, text_w(FONT_SMALL, key) + 4);
        panel(s, x, y, kw, 9, C_PANEL2, C_LINE);
        text(s, FONT_SMALL, x + (kw - text_w(FONT_SMALL, key)) / 2 + 0, y + 2, C_TEXT, key);
        x += kw + 3;
        x = text(s, FONT_NORMAL, x, y + 1, C_DIM, t + k + (t[k] == ' ')) + 8;
    }
}

static void draw_bottom(Surf *s) {
    int W = s->w, H = s->h;
    fill(s, C_BG);
    /* tabs */
    static const char *names[TAB_COUNT] = { "Games", "Apps", "Installed" };
    int nup = updates_count(&U.cat);
    for (int t = 0; t < TAB_COUNT; t++) {
        int x0 = t * W / TAB_COUNT, x1 = (t + 1) * W / TAB_COUNT;
        int on = t == U.tab;
        rectf(s, x0, 0, x1 - x0, 19, on ? C_PANEL2 : C_PANEL);
        if (on) rectf(s, x0, 17, x1 - x0, 2, C_GREEN);
        char lb[32]; snprintf(lb, sizeof lb, "%s", names[t]);
        int tw = text_w(FONT_NORMAL, lb) + (t == TAB_INSTALLED && nup ? 14 : 0);
        int x = text(s, FONT_NORMAL, (x0 + x1) / 2 - tw / 2, 5, on ? C_TEXT : C_DIM, lb);
        if (t == TAB_INSTALLED && nup) {
            char nb[8]; snprintf(nb, sizeof nb, "%d", nup);
            circlef(s, x + 7, 8, 5, C_YELLOW);
            text_center(s, FONT_SMALL, x + 7, 6, 0x101010, 0, nb);
        }
    }
    vline(s, W / TAB_COUNT, 2, 16, C_LINE); vline(s, 2 * W / TAB_COUNT, 2, 16, C_LINE);
    /* rows */
    int rows = (H - 16 - 22) / 36;
    if (!U.nview) {
        const char *e = U.tab == TAB_INSTALLED ? "Nothing installed from the Store yet." :
                        U.tab == TAB_GAMES ? "No games in the catalog yet." : "No apps in the catalog yet.";
        text_center(s, FONT_NORMAL, W / 2, H / 2 - 10, C_DIM, 0, e);
    }
    surf_clip(s, 0, 21, W, H - 16 - 21);
    for (int r = 0; r < rows + 1; r++) {
        int i = U.scroll[U.tab] + r;
        if (i >= U.nview) break;
        App *a = &U.cat.apps[U.view[i]];
        int y = 22 + r * 36, on = i == U.sel[U.tab];
        panel(s, 3, y, W - 6, 34, on ? C_PANEL2 : C_PANEL, on ? a->accent : C_LINE);
        if (on) { vline(s, 3, y + 2, y + 31, a->accent); vline(s, 4, y + 2, y + 31, a->accent); }
        icon_box(s, a, 8, y + 1, 1);
        int bw = 70;
        text_fit(s, FONT_NORMAL, 46, y + 6, W - 46 - bw - 12, C_TEXT, a->name);
        char sub[160];
        if (a->state == ST_UPDATE) snprintf(sub, sizeof sub, "%s -> %s", a->have, a->latest);
        else snprintf(sub, sizeof sub, "%s", a->summary[0] ? a->summary : a->dev);
        text_fit(s, FONT_NORMAL, 46, y + 19, W - 46 - bw - 12, a->state == ST_UPDATE ? C_YELLOW : C_DIM, sub);
        /* the button: what A does */
        const char *lb = action_label(a);
        uint32_t bc = a->state == ST_INSTALL ? C_BLUE : a->state == ST_UPDATE ? C_YELLOW : C_PANEL;
        uint32_t tc = a->state == ST_INSTALL ? C_TEXT : a->state == ST_UPDATE ? 0x101010 : C_DIM;
        int bx = W - 6 - bw - 4, by = y + 10;
        panel(s, bx, by, bw, 14, bc, a->state <= ST_UPDATE ? col_add(bc, 0x303030) : C_LINE);
        text_center(s, FONT_SMALL, bx + bw / 2, by + 5, tc, 0, lb);
    }
    surf_noclip(s);
    if (U.nview > rows) {               /* a scroll bar */
        int th = (H - 16 - 22) * rows / U.nview, ty = 22 + (H - 16 - 22 - th) * U.scroll[U.tab] / MAX(1, U.nview - rows);
        rectf(s, W - 2, ty, 2, th, C_LINE);
    }
    /* help */
    rectf(s, 0, H - 14, W, 14, C_PANEL);
    App *a = cur();
    char h[160];
    snprintf(h, sizeof h, "%s%s%sB Quit",
             a && a->state == ST_INSTALL ? "A Install|" : a && a->state == ST_UPDATE ? "A Update|" : "",
             a && a->have[0] && strcmp(a->id, "store") ? "X Remove|" : "", nup ? "Y Update all|" : "");
    help(s, 4, H - 12, h);
    /* a toast */
    if (U.toast_t > 0 && !U.modal && !U.confirm) {
        int tw = MIN(W - 16, text_w(FONT_NORMAL, U.toast) + 16), th = text_wrap(0, FONT_NORMAL, 0, 0, tw - 16, 0, U.toast) + 10;
        int ty = H - 20 - th;
        panel(s, (W - tw) / 2, ty, tw, th, 0x111318, C_GREEN);
        text_wrap(s, FONT_NORMAL, (W - tw) / 2 + 8, ty + 5, tw - 16, C_TEXT, U.toast);
    }
    /* a question, or a job's box */
    if (U.confirm || U.modal) {
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            uint32_t *p = &s->px[(size_t)y * s->pitch + x]; *p = col_scale(*p, 90);
        }
        int bw = W - 40, bh = 96, bx = 20, by = (H - bh) / 2;
        panel(s, bx, by, bw, bh, C_PANEL, C_LINE);
        char step[160], what[96], t[200];
        if (U.confirm) {
            App *c = cur();
            snprintf(t, sizeof t, "Remove %s?", c ? c->name : "");
            text_fit(s, FONT_NORMAL, bx + 10, by + 10, bw - 20, C_TEXT, t);
            text_wrap(s, FONT_NORMAL, bx + 10, by + 28, bw - 20, C_DIM,
                      "Its tile leaves the menu. What it keeps (saves, settings, your bank) stays on the card.");
            help(s, bx + 8, by + bh - 14, "A Remove|B Keep it");
        } else {
            int st = job_state(step, sizeof step, what, sizeof what);
            if (U.pending[0]) { st = JOB_RUNNING; snprintf(what, sizeof what, "%s", U.pending_what); snprintf(step, sizeof step, "Waiting for the check to end"); }
            text_fit(s, FONT_NORMAL, bx + 10, by + 10, bw - 20, C_TEXT, what);
            if (st == JOB_RUNNING) {
                text_fit(s, FONT_NORMAL, bx + 10, by + 30, bw - 20, C_DIM, step);
                /* a bar that runs back and forth: the steps have no percentages */
                int track = bw - 20, seg = track / 4, ph = U.frame % 120, off = ph < 60 ? ph : 120 - ph;
                rectf(s, bx + 10, by + 52, track, 6, C_BG);
                rectf(s, bx + 10 + (track - seg) * off / 60, by + 52, seg, 6, C_GREEN);
                text(s, FONT_NORMAL, bx + 10, by + bh - 16, C_FAINT, "Keep the Store open until it's done.");
            } else if (st == JOB_DONE) {
                if (U.restart) snprintf(t, sizeof t, "The Store is updated. A starts the new one.");
                else if (!strncmp(job_args(), "remove", 6)) snprintf(t, sizeof t, "Removed.");
                else snprintf(t, sizeof t, "Done%s%s. Its tile is on the menu's home screen when you leave the Store.",
                              step[0] ? ": version " : "", step);
                rectf(s, bx + 10, by + 30, 6, 6, C_GREEN);
                text_wrap(s, FONT_NORMAL, bx + 22, by + 28, bw - 32, C_TEXT, t);
                help(s, bx + 8, by + bh - 14, "A OK");
            } else {
                snprintf(t, sizeof t, "It didn't work: %s. Nothing was changed.", step);
                rectf(s, bx + 10, by + 30, 6, 6, C_RED);
                text_wrap(s, FONT_NORMAL, bx + 22, by + 28, bw - 32, C_TEXT, t);
                help(s, bx + 8, by + bh - 14, "A OK");
            }
        }
    }
}

/* ---- the script (tests and pictures) --------------------------------------------------------------------------- */
typedef struct { char buf[2048]; char *p; int wait; } Script;

static int btn_of(const char *w) {
    static const struct { const char *n; int b; } m[] = {
        {"up", B_UP}, {"down", B_DOWN}, {"left", B_LEFT}, {"right", B_RIGHT}, {"a", B_A}, {"b", B_B}, {"x", B_X},
        {"y", B_Y}, {"l", B_L1}, {"r", B_R1}, {"select", B_SELECT}, {"start", B_START} };
    for (int i = 0; i < ARRAY_LEN(m); i++) if (!strcmp(w, m[i].n)) return m[i].b;
    return -1;
}

/* the next step of the script into *in; 1 when it says quit */
static int script_step(Script *sc, Input *in) {
    memset(in, 0, sizeof *in);
    if (sc->wait > 0) { sc->wait--; return 0; }
    if (job_state(0, 0, 0, 0) == JOB_RUNNING && sc->wait < 0) return 0;
    sc->wait = 0;
    while (sc->p && *sc->p) {
        char tok[600]; int n = 0;
        while (isspace((unsigned char)*sc->p) || *sc->p == ';') sc->p++;
        while (*sc->p && *sc->p != ';' && n < 599) tok[n++] = *sc->p++;
        while (n && isspace((unsigned char)tok[n - 1])) n--;
        tok[n] = 0;
        if (!n) continue;
        int b = btn_of(tok);
        if (b >= 0) { in->held = BIT(b); sc->wait = 1; return 0; }
        if (!strncmp(tok, "wait ", 5)) { sc->wait = atoi(tok + 5); return 0; }
        if (!strcmp(tok, "job")) { sc->wait = -1; return 0; }
        if (!strncmp(tok, "tap ", 4)) { in->touch[1] = 1; sscanf(tok + 4, "%d %d", &in->tx[1], &in->ty[1]); sc->wait = 1; return 0; }
        if (!strncmp(tok, "shot ", 5)) { headless_snapshot(tok + 5); return 0; }
        if (!strcmp(tok, "quit")) return 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *backend = 0, *script = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--backend") && v) { backend = v; i++; }
        else if (!strcmp(a, "--size") && v) { setenv("DK_HEADLESS_SIZE", v, 1); setenv("DK_WINDOW_SIZE", v, 1); i++; }
        else if (!strcmp(a, "--script") && v) { script = v; i++; }
        else if (!strcmp(a, "--version")) { printf("ROCKNIXDS Store %s\n", STORE_VERSION); return 0; }
        else if (!strcmp(a, "--help")) { printf("usage: %s [--backend kms|sdl|headless] [--size WxH] [--script ...]\n", argv[0]); return 0; }
    }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGHUP, on_signal);
    if (!getenv("DK_LOG_NAME")) setenv("DK_LOG_NAME", "store.log", 1);
    plat_log("ROCKNIXDS Store %s", STORE_VERSION);
    PlatInfo pi;
    if (plat_init(backend, &pi)) { fprintf(stderr, "store: no display\n"); plat_log("no display"); return 3; }
    plat_log("display: %s, panels %dx%d, scale %d, logical %dx%d", pi.backend, pi.panel_w, pi.panel_h, pi.scale, pi.top_w, pi.top_h);
    Surf top, bot;
    surf_alloc(&top, pi.top_w, pi.top_h);
    surf_alloc(&bot, pi.bot_w, pi.bot_h);
    U.W = pi.top_w; U.H = pi.top_h; U.BW = pi.bot_w; U.BH = pi.bot_h;
    reload();
    if (!getenv("RNDS_STORE_NO_REFRESH")) refresh();
    Script sc = {0};
    if (script) { snprintf(sc.buf, sizeof sc.buf, "%s", script); sc.p = sc.buf; }
    Input in, prev; memset(&in, 0, sizeof in); memset(&prev, 0, sizeof prev);
    while (!stop) {
        if (script) { if (script_step(&sc, &in)) break; }
        else plat_poll(&in);
        if (in.quit) break;
        uint32_t pressed = in.held & ~prev.held;
        job_poll();
        handle(pressed, &in, &prev);
        prev = in;
        draw_top(&top);
        draw_bottom(&bot);
        plat_present(&top, &bot);
        U.frame++;
        if (U.toast_t > 0) U.toast_t--;
    }
    /* an install still running (a quit from the hotkey): let it end, a half-done install is worse than a late quit.
       A refresh is left to finish on its own (it only writes the Store's cache) */
    if (!U.refreshing) job_wait();
    plat_shutdown();
    plat_log("bye%s", U.changed ? " (apps changed: the menu shows them when it starts)" : "");
    catalog_free(&U.cat);
    surf_free(&top); surf_free(&bot);
    return stop == 2 ? UPDATE_EXIT : 0;
}
