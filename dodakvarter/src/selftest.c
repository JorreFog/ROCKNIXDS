// selftest.c: `dodakvarter --selftest`, for checking a handheld: each screen says which one it is and shows its
// panel's size, a grid and colour bars; touches draw a cross where they land; buttons and sticks are shown live on
// the bottom screen and printed as they change; a beep plays each second. START and SELECT together (or 20 s) end it.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

static const char *BNAME[B_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "L2", "R2", "SELECT", "START", "MODE" };

static void screen(Surf *s, const char *name, const PlatInfo *pi, int bottom, const Input *in) {
    fill(s, 0x101418);
    for (int x = 0; x < s->w; x += 16) vline(s, x, 0, s->h - 1, 0x1e2630);
    for (int y = 0; y < s->h; y += 16) hline(s, 0, s->w - 1, y, 0x1e2630);
    rect_line(s, 0, 0, s->w, s->h, 0xc81818);                /* the very edge of the picture */
    static const uint32_t bars[7] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff };
    for (int k = 0; k < 7; k++) rectf(s, 8 + k * (s->w - 16) / 7, s->h - 28, (s->w - 16) / 7, 20, bars[k]);
    text_big(s, s->w / 2 - text_w(FONT_NORMAL, name), 14, 2, 0xffffff, 0x000000, name);
    char b[96];
    snprintf(b, sizeof b, "%s: panel %dx%d, %dx%d drawn at %dx", pi->backend, pi->panel_w, pi->panel_h, s->w, s->h, pi->scale);
    text_center(s, FONT_NORMAL, s->w / 2, 40, 0xa0a8b8, 0, b);
    if (bottom) {
        int x = 12, y = 60;
        for (int i = 0; i < B_COUNT; i++) {
            int on = (in->held & BIT(i)) != 0;
            int w = text_w(FONT_NORMAL, BNAME[i]) + 6;
            if (x + w > s->w - 8) { x = 12; y += 16; }
            rectf(s, x, y, w, 13, on ? 0xc81818 : 0x2a3040); text(s, FONT_NORMAL, x + 3, y + 3, on ? 0xffffff : 0x7a8494, BNAME[i]);
            x += w + 4;
        }
        snprintf(b, sizeof b, "sticks %s: L %+.2f %+.2f   R %+.2f %+.2f", in->has_sticks ? "yes" : "none", in->lx, in->ly, in->rx, in->ry);
        text(s, FONT_NORMAL, 12, y + 22, 0xa0a8b8, b);
        text(s, FONT_SMALL, 12, y + 40, 0x7a8494, "START + SELECT: END");
    }
    int t = bottom ? 1 : 0;
    if (in->touch[t]) {
        int x = in->tx[t], y = in->ty[t];
        hline(s, x - 10, x + 10, y, 0x40ff60); vline(s, x, y - 10, y + 10, 0x40ff60); circle(s, x, y, 6, 0x40ff60);
        snprintf(b, sizeof b, "touch %d,%d", x, y); text(s, FONT_NORMAL, MIN(x + 8, s->w - 70), MAX(y - 14, 2), 0x40ff60, b);
    }
}

int selftest(const PlatInfo *pi, Surf *top, Surf *bot) {
    printf("Döda Kvarter %s self-test: %s, panels %dx%d at %dx (%dx%d top, %dx%d bottom), %.0f Hz\n", DK_VERSION, pi->backend,
           pi->panel_w, pi->panel_h, pi->scale, pi->top_w, pi->top_h, pi->bot_w, pi->bot_h, pi->hz);
    printf("data in %s; the log says which devices were found\n", plat_data_dir());
    fflush(stdout);
    Input in, prev; memset(&in, 0, sizeof in); memset(&prev, 0, sizeof prev);
    double t0 = plat_now(), beep = t0;
    int frames = 0;
    while (plat_now() - t0 < 20) {
        plat_poll(&in);
        if (in.quit || ((in.held & BIT(B_START)) && (in.held & BIT(B_SELECT)))) break;
        for (int i = 0; i < B_COUNT; i++) if ((in.held ^ prev.held) & BIT(i)) printf("%s %s\n", BNAME[i], in.held & BIT(i) ? "down" : "up");
        for (int k = 0; k < 2; k++) if (in.touch[k] && !prev.touch[k]) printf("touch on the %s screen at %d,%d\n", k ? "bottom" : "top", in.tx[k], in.ty[k]);
        fflush(stdout);
        if (plat_now() - beep >= 1) { beep = plat_now(); sfx(SFX_BEEP, 0.8f, 0); }
        screen(top, "TOP", pi, 0, &in);
        screen(bot, "BOTTOM", pi, 1, &in);
        if (frames == 2 && getenv("DK_SELFTEST_SNAP")) headless_snapshot(getenv("DK_SELFTEST_SNAP"));   /* (tests) */
        plat_present(top, bot);
        prev = in; frames++;
        if (!strcmp(pi->backend, "headless") && frames >= 3) break;
    }
    double secs = plat_now() - t0;
    printf("%d frames in %.1f s (%.1f a second)\n", frames, secs, frames / (secs > 0 ? secs : 1));
    return 0;
}
