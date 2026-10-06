// main.c: Döda Kvarter, a zombie roguelike for the Anbernic RG DS. The top screen is the town at night, the bottom
// screen the inventory. The loop runs at 60 ticks a second, paced by the panels' refresh.
//
//   dodakvarter [--backend kms|sdl|headless] [--seed N] [--start] [--bot] [--frames N]
//               [--snap DIR --snap-every N] [--size WxH] [--selftest]
#include "game.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void bot_input(Input *in);
int selftest(const PlatInfo *pi, Surf *top, Surf *bot);
static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }

int main(int argc, char **argv) {
    const char *backend = 0, *snapdir = 0;
    int bot = 0, start = 0, frames = -1, snap_every = 0, season = -1, test = 0;
    uint64_t seed = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--backend") && v) { backend = v; i++; }
        else if (!strcmp(a, "--seed") && v) { seed = strtoull(v, 0, 10); i++; }
        else if (!strcmp(a, "--season") && v) { season = atoi(v); i++; }
        else if (!strcmp(a, "--bot")) bot = 1;
        else if (!strcmp(a, "--start")) start = 1;
        else if (!strcmp(a, "--frames") && v) { frames = atoi(v); i++; }
        else if (!strcmp(a, "--snap") && v) { snapdir = v; i++; }
        else if (!strcmp(a, "--snap-every") && v) { snap_every = atoi(v); i++; }
        else if (!strcmp(a, "--size") && v) { setenv("DK_HEADLESS_SIZE", v, 1); setenv("DK_WINDOW_SIZE", v, 1); i++; }
        else if (!strcmp(a, "--selftest")) { test = 1; setenv("DK_VERBOSE", "1", 1); }
        else if (!strcmp(a, "--version")) { printf("Döda Kvarter %s\n", DK_VERSION); return 0; }
        else if (!strcmp(a, "--help")) { printf("usage: %s [--backend kms|sdl|headless] [--seed N] [--start] [--bot]\n", argv[0]); return 0; }
    }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGHUP, on_signal);
    plat_log("Döda Kvarter %s", DK_VERSION);
    PlatInfo pi;
    if (plat_init(backend, &pi)) { fprintf(stderr, "dodakvarter: no display (tried %s)\n", backend ? backend : "kms, sdl"); plat_log("no display"); return 3; }
    plat_log("display: %s, panels %dx%d, scale %d, logical %dx%d", pi.backend, pi.panel_w, pi.panel_h, pi.scale, pi.top_w, pi.top_h);
    int headless = !strcmp(pi.backend, "headless");
    Surf top, bot_s;
    surf_alloc(&top, pi.top_w, pi.top_h);
    surf_alloc(&bot_s, pi.bot_w, pi.bot_h);
    G = calloc(1, sizeof *G);
    G->view_w = pi.top_w; G->view_h = pi.top_h;
    settings_load();
    if (season >= 0) S.season = season + 1;
    scores_load();
    stats_load();
    render_init();
    audio_init();
    audio_set_volume(S.volume);
    A.state = ST_TITLE; A.rank = -1; A.seed_override = seed;
    A.bot_w = pi.bot_w; A.bot_h = pi.bot_h;
    if (test) { int r = selftest(&pi, &top, &bot_s); plat_shutdown(); return r; }
    music_play(MUS_TITLE);
    if (start) app_new_run();
    Input in, prev; memset(&in, 0, sizeof in); memset(&prev, 0, sizeof prev);
    double t0 = plat_now(), acc = 0, fps_t = t0;
    int fps_n = 0; float fps = 0;
    int prof = getenv("DK_PROFILE") ? atoi(getenv("DK_PROFILE")) : 0; double pu = 0, pr = 0, pp = 0, pmax = 0; int pn = 0;   /* ms per frame */
    for (int f = 0; !stop && !A.quit && (frames < 0 || f < frames); f++) {
        plat_poll(&in);
        if (bot) { Input b; bot_input(&b); b.touch[0] = in.touch[0]; in = b; }
        if (in.quit) break;
        double tp0 = plat_now();
        if (headless) {                                     /* tests: exactly one tick a frame */
            app_update(&in, &prev, 1.0f / 60);
            prev = in;
        } else {
            double now = plat_now();
            acc += now - t0; t0 = now;
            if (acc > 0.1) acc = 0.1;                       /* after a stall, don't run the clock away */
            int steps = 0;
            while (acc >= 1.0 / 60 - 0.0005) {            /* a little slack: a 60.0x Hz panel still gets one per frame */
                app_update(&in, &prev, 1.0f / 60);
                prev = in;
                acc -= 1.0 / 60; steps++;
                if (acc < 0) acc = 0;
            }
            (void)steps;
        }
        double tp1 = plat_now();
        app_render(&top, &bot_s);
        double tp2 = plat_now();
        if (!headless && A.state == ST_PLAY) render_frame_cost((float)((tp2 - tp0) * 1000));
        if (S.show_fps) { char b[24]; snprintf(b, sizeof b, "%.0f FPS", fps); text_ol(&top, FONT_SMALL, top.w - 4 - text_w(FONT_SMALL, b), 2, 0x80ff80, 0x000000, b); }
        if (snapdir && snap_every > 0 && f % snap_every == 0) {
            char p[600]; snprintf(p, sizeof p, "%s/frame%06d.png", snapdir, f);
            headless_snapshot(p);
        }
        plat_present(&top, &bot_s);
        if (prof) {
            double tp3 = plat_now();
            pu += tp1 - tp0; pr += tp2 - tp1; pp += tp3 - tp2; pn++;
            if (tp2 - tp0 > pmax) pmax = tp2 - tp0;
            if (prof > 1 && tp2 - tp0 > 0.002) plat_log("slow frame %d: update %.2f ms, render %.2f ms (state %d, round %d, %d zombies)", f, (tp1 - tp0) * 1000, (tp2 - tp1) * 1000, A.state, G->round, zombies_alive());
            if (pn == 600) {
                plat_log("profile: update %.2f ms, render %.2f ms, present %.2f ms, worst update+render %.2f ms (state %d, round %d, %d zombies)",
                         pu / pn * 1000, pr / pn * 1000, pp / pn * 1000, pmax * 1000, A.state, G->round, zombies_alive());
                pu = pr = pp = pmax = 0; pn = 0;
            }
        }
        fps_n++;
        double now = plat_now();
        if (now - fps_t >= 1.0) { fps = (float)(fps_n / (now - fps_t)); fps_n = 0; fps_t = now; }
    }
    if (app_run_in_progress()) { plat_log("quit during round %d", G->round); run_save(); }   /* Continue on the title */
    settings_save();
    plat_shutdown();
    plat_log("bye");
    return 0;
}
