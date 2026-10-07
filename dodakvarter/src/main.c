// main.c: Döda Kvarter, a zombie roguelike for the Anbernic RG DS. The top screen is the town at night, the bottom
// screen the inventory. The loop runs at 60 ticks a second, paced by the panels' refresh.
//
//   dodakvarter [--backend kms|sdl|headless] [--seed N] [--start] [--bot | --monkey N] [--frames N]
//               [--snap DIR --snap-every N] [--size WxH] [--selftest]
#include "game.h"
#include "menu.h"
#include "update.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void bot_input(Input *in);
void monkey_input(Input *in, uint64_t seed);
static volatile sig_atomic_t stop;
int selftest(const PlatInfo *pi, Surf *top, Surf *bot, volatile sig_atomic_t *stop);
static void on_signal(int s) { (void)s; stop = 1; }

int main(int argc, char **argv) {
    const char *backend = 0, *snapdir = 0;
    int bot = 0, start = 0, frames = -1, snap_every = 0, season = -1, test = 0;
    uint64_t seed = 0, monkey = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--backend") && v) { backend = v; i++; }
        else if (!strcmp(a, "--seed") && v) { seed = strtoull(v, 0, 10); i++; }
        else if (!strcmp(a, "--season") && v) { season = atoi(v); i++; }
        else if (!strcmp(a, "--bot")) bot = 1;
        else if (!strcmp(a, "--monkey") && v) { monkey = strtoull(v, 0, 10) + 1; i++; }
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
    A.state = getenv("DK_NO_SPLASH") ? ST_TITLE : ST_SPLASH; A.rank = -1; A.seed_override = seed;   /* JorreFog productions first */
    A.bot_w = pi.bot_w; A.bot_h = pi.bot_h;
    if (test) { int r = selftest(&pi, &top, &bot_s, &stop); plat_shutdown(); return r; }
    if (A.state == ST_TITLE) music_play(MUS_TITLE);       /* (after the splash, else) */
    if (start) app_new_run();
    Input in, prev; memset(&in, 0, sizeof in); memset(&prev, 0, sizeof prev);
    double t0 = plat_now(), acc = 0, fps_t = t0, fclk = 0, fclk_prev = 0;
    int fps_n = 0; float fps = 0;
    int prof = getenv("DK_PROFILE") ? atoi(getenv("DK_PROFILE")) : 0; double pu = 0, pr = 0, pp = 0, pmax = 0; int pn = 0;   /* ms per frame */
    int pticks = 0, pnone = 0, pmany = 0; double pt0 = plat_now();   /* game ticks, frames without one, frames with several */
    for (int f = 0; !stop && !A.quit && (frames < 0 || f < frames); f++) {
        plat_poll(&in);
        if (bot) { Input b; bot_input(&b); b.touch[0] = in.touch[0]; in = b; }
        else if (monkey) monkey_input(&in, monkey);
        if (in.quit) break;
        double tp0 = plat_now();
        if (headless) {                                     /* tests: exactly one tick a frame */
            app_update(&in, &prev, 1.0f / 60);
            prev = in;
        } else {
            double now = plat_now(), dt = now - t0;
            t0 = now;
            /* On the handheld the panel's own clock says how long the last frame was up: a whole number of refreshes,
             * so a frame is one tick, or two after a late one. This thread's clock says 16.7 ms give or take the
             * scheduler, and that jitter alone made frames without a tick and frames with two (a hitch each). */
            if (fclk > 0 && fclk_prev > 0 && fclk > fclk_prev && fclk - fclk_prev < 0.2) dt = fclk - fclk_prev;
            fclk_prev = fclk;
            acc += dt;
            if (acc > 0.1) acc = 0.1;                       /* after a stall, don't run the clock away */
            int steps = 0;
            while (acc >= 1.0 / 60 - 0.0005) {            /* a little slack: a 60.0x Hz panel still gets one per frame */
                app_update(&in, &prev, 1.0f / 60);
                prev = in;
                acc -= 1.0 / 60; steps++;
                if (acc < 0) acc = 0;
            }
            pticks += steps; pnone += steps == 0; pmany += steps > 1;
        }
        if (monkey) A.quit = 0;                             /* (it would choose QUIT on the title every few seconds) */
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
        fclk = plat_frame_clock();
        if (prof) {
            double tp3 = plat_now();
            pu += tp1 - tp0; pr += tp2 - tp1; pp += tp3 - tp2; pn++;
            if (tp2 - tp0 > pmax) pmax = tp2 - tp0;
            if (prof > 1 && tp2 - tp0 > 0.009) plat_log("slow frame %d: update %.2f ms, render %.2f ms (state %d, round %d, %d zombies)", f, (tp1 - tp0) * 1000, (tp2 - tp1) * 1000, A.state, G->round, zombies_alive());
            if (pn == 600) {
                plat_log("profile: update %.2f ms, render %.2f ms, present %.2f ms, worst update+render %.2f ms (state %d, round %d, %d zombies); "
                         "600 frames in %.2f s, %d ticks, %d frames without a tick, %d with several%s",
                         pu / pn * 1000, pr / pn * 1000, pp / pn * 1000, pmax * 1000, A.state, G->round, zombies_alive(),
                         tp3 - pt0, pticks, pnone, pmany, plat_pace());
                pu = pr = pp = pmax = 0; pn = 0; pticks = pnone = pmany = 0; pt0 = tp3;
            }
        }
        fps_n++;
        double now = plat_now();
        if (now - fps_t >= 1.0) { fps = (float)(fps_n / (now - fps_t)); fps_n = 0; fps_t = now; }
    }
    app_quit();                                             /* the run waits on the title (Continue), a score is kept */
    settings_save();
    plat_shutdown();
    plat_log("bye");
    return A.quit == 2 ? UPDATE_EXIT : 0;                  /* updated: the session starts the new one */
}
