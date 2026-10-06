// plat_headless.c: no display. Input comes from the test harness; a snapshot request writes the two screens
// stacked like the handheld (top above bottom, a strip of hinge between) to a PNG.
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>

static Input cur;
static char snap_path[512];
static PlatInfo *pi;

static int hl_init(PlatInfo *info) {
    const char *sz = getenv("DK_HEADLESS_SIZE");      /* panel size, e.g. 1024x768 for the RG DS Plus */
    int pw = 640, ph = 480;
    if (sz) sscanf(sz, "%dx%d", &pw, &ph);
    info->scale = MAX(1, MIN(pw / 320, ph / 240));
    info->panel_w = pw; info->panel_h = ph;
    info->top_w = info->bot_w = pw / info->scale;
    info->top_h = info->bot_h = ph / info->scale;
    info->hz = 60; info->backend = "headless";
    pi = info;
    return 0;
}
void headless_set_input(const Input *in) { cur = *in; }
void headless_snapshot(const char *path) { snprintf(snap_path, sizeof snap_path, "%s", path); }
static void hl_poll(Input *in) { *in = cur; }
static void hl_present(Surf *top, Surf *bot) {
    if (!snap_path[0]) return;
    int gap = 6, w = MAX(top->w, bot->w), h = top->h + gap + bot->h;
    uint32_t *px = calloc((size_t)w * h, 4);
    for (int y = 0; y < top->h; y++) memcpy(px + (size_t)y * w, top->px + (size_t)y * top->pitch, (size_t)top->w * 4);
    for (int y = 0; y < gap; y++) for (int x = 0; x < w; x++) px[(size_t)(top->h + y) * w + x] = (y == 0 || y == gap - 1) ? 0x202020 : 0x3a3a3a;
    for (int y = 0; y < bot->h; y++) memcpy(px + (size_t)(top->h + gap + y) * w, bot->px + (size_t)y * bot->pitch, (size_t)bot->w * 4);
    png_write(snap_path, px, w, h, w);
    free(px);
    snap_path[0] = 0;
}
static void hl_shutdown(void) {}
static int hl_audio(int rate, void (*mix)(int16_t *, int)) { (void)rate; (void)mix; return -1; }

const Backend backend_headless = { hl_init, hl_poll, hl_present, hl_shutdown, hl_audio };
