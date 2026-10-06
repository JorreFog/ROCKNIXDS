// plat_sdl.c: a window, for a desktop (or a compositor on the device). SDL2 is loaded at run time, so the
// binary needs no SDL to start. The two screens are stacked like the handheld (DK_SDL_LAYOUT=side puts them side
// by side, as sway lays out the RG DS's panels), at the biggest whole-pixel scale that fits.
// Keys: arrows/WASD move, J or Z fire (A), K or X use (B), I or C swap (X), U or V reload (Y), Q sprint (L),
// E knife (R), 1 item (L2), 3 grenade (R2), Tab next item (SELECT), Enter pause (START), Esc menu.
// The mouse aims on the top screen (left button fires) and taps the bottom one.
#define _GNU_SOURCE
#include "plat.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define FN(n) static __typeof__(n) *p_##n
FN(SDL_Init); FN(SDL_Quit); FN(SDL_CreateWindow); FN(SDL_CreateRenderer); FN(SDL_CreateTexture); FN(SDL_UpdateTexture);
FN(SDL_RenderClear); FN(SDL_RenderCopy); FN(SDL_RenderPresent); FN(SDL_PollEvent); FN(SDL_SetHint); FN(SDL_GetError);
FN(SDL_GameControllerOpen); FN(SDL_IsGameController); FN(SDL_NumJoysticks); FN(SDL_GameControllerGetAxis);
FN(SDL_GameControllerGetButton); FN(SDL_OpenAudioDevice); FN(SDL_PauseAudioDevice); FN(SDL_GetDesktopDisplayMode);
FN(SDL_SetRenderDrawColor); FN(SDL_GetWindowSize); FN(SDL_DestroyRenderer); FN(SDL_DestroyWindow); FN(SDL_GetRendererOutputSize);
FN(SDL_SetWindowTitle); FN(SDL_GameControllerClose);

static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex;
static int lw, lh, side, gap, scale;            /* the composite's logical size and layout */
static uint32_t *comp;
static uint32_t keys;
static int mouse_x, mouse_y, mouse_l, mouse_r, mouse_seen;
static SDL_GameController *pad;
static int quit;
static PlatInfo *pinfo;

static int load(void) {
    static const char *names[] = { "libSDL2-2.0.so.0", "libSDL2.so", "libSDL2-2.0.so" };
    void *h = 0;
    for (int i = 0; i < 3 && !h; i++) h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
    if (!h) return -1;
#define L(n) if (!(*(void **)&p_##n = dlsym(h, #n))) return -1
    L(SDL_Init); L(SDL_Quit); L(SDL_CreateWindow); L(SDL_CreateRenderer); L(SDL_CreateTexture); L(SDL_UpdateTexture);
    L(SDL_RenderClear); L(SDL_RenderCopy); L(SDL_RenderPresent); L(SDL_PollEvent); L(SDL_SetHint); L(SDL_GetError);
    L(SDL_GameControllerOpen); L(SDL_IsGameController); L(SDL_NumJoysticks); L(SDL_GameControllerGetAxis);
    L(SDL_GameControllerGetButton); L(SDL_OpenAudioDevice); L(SDL_PauseAudioDevice); L(SDL_GetDesktopDisplayMode);
    L(SDL_SetRenderDrawColor); L(SDL_GetWindowSize); L(SDL_DestroyRenderer); L(SDL_DestroyWindow); L(SDL_GetRendererOutputSize);
    L(SDL_SetWindowTitle); L(SDL_GameControllerClose);
#undef L
    return 0;
}

static int s_init(PlatInfo *info) {
    if (load()) { plat_log("sdl: no SDL2"); return -1; }
    if (p_SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) { plat_log("sdl: %s", p_SDL_GetError()); return -1; }
    const char *lay = getenv("DK_SDL_LAYOUT");
    side = lay && !strcmp(lay, "side");
    int pw = 640, ph = 480;
    const char *sz = getenv("DK_WINDOW_SIZE");
    if (sz) sscanf(sz, "%dx%d", &pw, &ph);
    int s = MAX(1, MIN(pw / 320, ph / 240));
    info->top_w = info->bot_w = pw / s; info->top_h = info->bot_h = ph / s;
    gap = side ? 0 : 6;
    lw = side ? info->top_w * 2 : info->top_w;
    lh = side ? info->top_h : info->top_h * 2 + gap;
    /* the biggest whole-pixel window that fits the desktop */
    SDL_DisplayMode dm; scale = s;
    if (!p_SDL_GetDesktopDisplayMode(0, &dm)) while (scale > 1 && (lw * scale > dm.w * 0.95 || lh * scale > dm.h * 0.9)) scale--;
    if (side) scale = s;                              /* on the device: each panel at its own resolution */
    p_SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    p_SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    win = p_SDL_CreateWindow("Döda Kvarter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, lw * scale, lh * scale, SDL_WINDOW_RESIZABLE);
    if (!win) { plat_log("sdl: window: %s", p_SDL_GetError()); return -1; }
    ren = p_SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = p_SDL_CreateRenderer(win, -1, 0);
    if (!ren) { plat_log("sdl: renderer: %s", p_SDL_GetError()); return -1; }
    tex = p_SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lw, lh);
    comp = calloc((size_t)lw * lh, 4);
    for (int i = 0; i < p_SDL_NumJoysticks(); i++) if (p_SDL_IsGameController(i)) { pad = p_SDL_GameControllerOpen(i); if (pad) break; }
    info->scale = scale; info->panel_w = pw; info->panel_h = ph; info->hz = 60; info->backend = "sdl";
    pinfo = info;
    return 0;
}

static int key_bit(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: case SDLK_w: return B_UP;
    case SDLK_DOWN: case SDLK_s: return B_DOWN;
    case SDLK_LEFT: case SDLK_a: return B_LEFT;
    case SDLK_RIGHT: case SDLK_d: return B_RIGHT;
    case SDLK_j: case SDLK_z: case SDLK_SPACE: return B_A;
    case SDLK_k: case SDLK_x: case SDLK_f: return B_B;
    case SDLK_i: case SDLK_c: return B_X;
    case SDLK_u: case SDLK_v: case SDLK_r: return B_Y;
    case SDLK_q: case SDLK_LSHIFT: return B_L1;
    case SDLK_e: return B_R1;
    case SDLK_1: return B_L2;
    case SDLK_3: case SDLK_g: return B_R2;
    case SDLK_TAB: case SDLK_BACKSPACE: return B_SELECT;
    case SDLK_RETURN: case SDLK_p: return B_START;
    case SDLK_ESCAPE: return B_MENU;
    }
    return -1;
}

/* window pixels to (screen, logical x, y) */
static int map_mouse(int wx, int wy, int *x, int *y) {
    int ww, wh; p_SDL_GetWindowSize(win, &ww, &wh);
    float k = MIN((float)ww / lw, (float)wh / lh);
    int ox = (int)((ww - lw * k) / 2), oy = (int)((wh - lh * k) / 2);
    int lx = (int)((wx - ox) / k), ly = (int)((wy - oy) / k);
    int tw = pinfo->top_w, th = pinfo->top_h;
    if (side) { if (lx < tw) { *x = lx; *y = ly; return 0; } *x = lx - tw; *y = ly; return 1; }
    if (ly < th) { *x = lx; *y = ly; return 0; }
    *x = lx; *y = ly - th - gap; return 1;
}

static void s_poll(Input *in) {
    SDL_Event e;
    while (p_SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: quit = 1; break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            int b = key_bit(e.key.keysym.sym);
            if (b >= 0) { if (e.type == SDL_KEYDOWN) keys |= BIT(b); else keys &= ~BIT(b); }
            mouse_seen = 0;
            break;
        }
        case SDL_MOUSEMOTION: mouse_x = e.motion.x; mouse_y = e.motion.y; mouse_seen = 1; break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_l = e.type == SDL_MOUSEBUTTONDOWN;
            if (e.button.button == SDL_BUTTON_RIGHT) mouse_r = e.type == SDL_MOUSEBUTTONDOWN;
            mouse_x = e.button.x; mouse_y = e.button.y; mouse_seen = 1;
            break;
        case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = p_SDL_GameControllerOpen(e.cdevice.which); break;
        }
    }
    memset(in, 0, sizeof *in);
    in->held = keys;
    in->quit = quit;
    int sx, sy, scr = map_mouse(mouse_x, mouse_y, &sx, &sy);
    if (mouse_seen && scr == 0) {
        in->mouse = 1; in->mx = (float)sx; in->my = (float)sy;
        if (mouse_l) in->held |= BIT(B_A);
        if (mouse_r) in->held |= BIT(B_R1);
    } else if (scr == 1 && mouse_l) { in->touch[1] = 1; in->tx[1] = sx; in->ty[1] = sy; }
    if (pad) {
        static const struct { int b, bit; } map[] = {
            { SDL_CONTROLLER_BUTTON_A, B_A }, { SDL_CONTROLLER_BUTTON_B, B_B }, { SDL_CONTROLLER_BUTTON_X, B_X }, { SDL_CONTROLLER_BUTTON_Y, B_Y },
            { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, B_L1 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, B_R1 }, { SDL_CONTROLLER_BUTTON_BACK, B_SELECT },
            { SDL_CONTROLLER_BUTTON_START, B_START }, { SDL_CONTROLLER_BUTTON_GUIDE, B_MENU }, { SDL_CONTROLLER_BUTTON_DPAD_UP, B_UP },
            { SDL_CONTROLLER_BUTTON_DPAD_DOWN, B_DOWN }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, B_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, B_RIGHT },
            { SDL_CONTROLLER_BUTTON_LEFTSTICK, B_L1 },
        };
        for (int i = 0; i < ARRAY_LEN(map); i++) if (p_SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)map[i].b)) in->held |= BIT(map[i].bit);
        if (p_SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) in->held |= BIT(B_L2);
        if (p_SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) in->held |= BIT(B_R2);
        float a[4]; static const int ax[4] = { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY };
        for (int i = 0; i < 4; i++) {
            float v = p_SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)ax[i]) / 32767.0f;
            a[i] = fabsf(v) < 0.2f ? 0 : (v - SGN(v) * 0.2f) / 0.8f;
        }
        in->has_sticks = 1; in->lx = a[0]; in->ly = a[1]; in->rx = a[2]; in->ry = a[3];
        if (in->rx != 0 || in->ry != 0) in->mouse = 0;
    }
}

static void s_present(Surf *top, Surf *bot) {
    for (size_t i = 0; i < (size_t)lw * lh; i++) comp[i] = 0xff0c0c10;
    for (int y = 0; y < top->h; y++) for (int x = 0; x < top->w; x++) comp[(size_t)y * lw + x] = 0xff000000 | top->px[(size_t)y * top->pitch + x];
    int bx = side ? top->w : 0, by = side ? 0 : top->h + gap;
    for (int y = 0; y < bot->h; y++) for (int x = 0; x < bot->w; x++) comp[(size_t)(by + y) * lw + bx + x] = 0xff000000 | bot->px[(size_t)y * bot->pitch + x];
    if (!side) for (int y = top->h; y < top->h + gap; y++) for (int x = 0; x < lw; x++) comp[(size_t)y * lw + x] = (y == top->h || y == top->h + gap - 1) ? 0xff202020 : 0xff3a3a3a;
    p_SDL_UpdateTexture(tex, 0, comp, lw * 4);
    int ow, oh; p_SDL_GetRendererOutputSize(ren, &ow, &oh);
    int k = MAX(1, MIN(ow / lw, oh / lh));
    SDL_Rect dst = { (ow - lw * k) / 2, (oh - lh * k) / 2, lw * k, lh * k };
    if (ow < lw || oh < lh) { float f = MIN((float)ow / lw, (float)oh / lh); dst.w = (int)(lw * f); dst.h = (int)(lh * f); dst.x = (ow - dst.w) / 2; dst.y = (oh - dst.h) / 2; }
    p_SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    p_SDL_RenderClear(ren);
    p_SDL_RenderCopy(ren, tex, 0, &dst);
    p_SDL_RenderPresent(ren);
}

static void s_shutdown(void) {
    if (pad) p_SDL_GameControllerClose(pad);
    if (ren) p_SDL_DestroyRenderer(ren);
    if (win) p_SDL_DestroyWindow(win);
    p_SDL_Quit();
}

static void (*s_mix)(int16_t *, int);
static void audio_cb(void *u, Uint8 *stream, int len) { (void)u; s_mix((int16_t *)stream, len / 4); }
static int s_audio(int rate, void (*mix)(int16_t *, int)) {
    s_mix = mix;
    SDL_AudioSpec want, have; memset(&want, 0, sizeof want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    SDL_AudioDeviceID d = p_SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (!d) { plat_log("sdl audio: %s", p_SDL_GetError()); return alsa_start(rate, mix); }
    p_SDL_PauseAudioDevice(d, 0);
    return 0;
}

const Backend backend_sdl = { s_init, s_poll, s_present, s_shutdown, s_audio };
