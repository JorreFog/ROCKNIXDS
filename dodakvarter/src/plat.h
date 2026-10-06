// plat.h: what the game needs from the machine. Three backends:
//   kms       the RG DS itself: both panels straight through DRM/KMS (no compositor), evdev gamepad and touch,
//             ALSA audio. Needs the display (ES and sway stopped: see device/session.sh).
//   sdl       a window (a desktop, or a compositor on the device); SDL2 is loaded at run time.
//   headless  no display: scripted input and PNG snapshots, for tests and screenshots.
#pragma once
#include "gfx.h"

enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_X, B_Y, B_L1, B_R1, B_L2, B_R2, B_SELECT, B_START, B_MENU,
    B_COUNT
};
#define BIT(b) (1u << (b))

typedef struct {
    uint32_t held;                      /* B_* bits */
    float lx, ly, rx, ry;               /* sticks, -1..1 with the dead zone removed; 0 without sticks */
    int has_sticks;
    int touch[2], tx[2], ty[2];         /* [0] top screen, [1] bottom: down, position in that screen's logical pixels */
    int mouse;                          /* desktop: the mouse is over the top screen (aim at mx, my) */
    float mx, my;
    int quit;                           /* window closed, SIGTERM */
    int last_kbd;                       /* desktop: a key was the last input (button glyphs) */
} Input;

typedef struct {
    int top_w, top_h, bot_w, bot_h;     /* logical size of each screen */
    int scale;                          /* panel pixels per logical pixel */
    int panel_w, panel_h;
    double hz;
    const char *backend;
} PlatInfo;

int plat_init(const char *backend, PlatInfo *info);
void plat_poll(Input *in);
void plat_present(Surf *top, Surf *bot);      /* shows both screens; paced to the display's refresh */
double plat_now(void);                        /* seconds, monotonic */
void plat_shutdown(void);
/* audio: mix() is called from the audio thread for `frames` stereo S16 frames */
int plat_audio_start(int rate, void (*mix)(int16_t *buf, int frames));
void plat_set_title(const char *s);
const char *plat_data_dir(void);              /* where scores and settings live (created) */
void plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* backends */
typedef struct {
    int (*init)(PlatInfo *info);
    void (*poll)(Input *in);
    void (*present)(Surf *top, Surf *bot);
    void (*shutdown)(void);
    int (*audio)(int rate, void (*mix)(int16_t *, int));
} Backend;
extern const Backend backend_kms, backend_sdl, backend_headless;

/* headless control (tests): input for the next frames and where snapshots go */
void headless_set_input(const Input *in);
void headless_snapshot(const char *path);     /* the next present writes both screens to a PNG */
int png_write(const char *path, const uint32_t *px, int w, int h, int pitch);
/* the shared audio path of the kms backend (ALSA through libasound, loaded at run time) */
int alsa_start(int rate, void (*mix)(int16_t *, int));
