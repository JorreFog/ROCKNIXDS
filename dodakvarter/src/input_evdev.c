// input_evdev.c: the gamepad, both touchscreens (and a USB keyboard) read straight from evdev, for the kms
// backend. Nothing is grabbed: ROCKNIX's own hotkeys keep working. Devices that go away (suspend, a driver
// rebind) are dropped and searched for again. A node is opened once: one that isn't ours (the power key, the
// volume keys, the headphone jack...) is remembered and left alone until it changes (see scan).
//
// The RG DS pad (retrogame_joypad), by the letters on the handheld: A=305 B=304 X=307 Y=308 L=310 R=311 L2=312 R2=313
// SELECT=314 START=315 MODE=316, the d-pad as BTN_DPAD_* or a hat; sticks as ABS_X/Y and ABS_RX/RY when it has them.
// The codes go by position: the button labelled A is the right one, BTN_EAST, and B the bottom one, BTN_SOUTH, as in
// ROCKNIX's es_input.cfg and drastic.cfg (0.1.0 had A and B the other way round: found on an RG DS Plus).
// The bottom touchscreen is the one on fe5e0000.i2c (DK_TOUCH_BOTTOM), any other one is the top's.
#define _GNU_SOURCE
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>

#define MAXDEV 12
enum { D_PAD, D_TOUCH, D_KBD };
typedef struct {
    int fd, kind, screen;               /* touch: 0 top, 1 bottom */
    char path[64];
    int axmin[ABS_CNT > 64 ? 64 : ABS_CNT], axmax[64], axflat[64];
    int tx, ty, down, slot0;
    uint32_t keys;
} Dev;
static Dev dev[MAXDEV]; static int ndev;
static int pw_[2], ph_[2], sc_[2], ox_[2], oy_[2];
static uint32_t held;
static float ax[4];                     /* lx ly rx ry */
static int hat_x, hat_y, have_sticks;
static double rescan_t;
static double now_s(void) { return plat_now(); }

static int test_bit(const unsigned long *b, int n) { return (b[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1; }

static void open_dev(const char *path) {
    for (int i = 0; i < ndev; i++) if (!strcmp(dev[i].path, path)) return;
    if (ndev >= MAXDEV) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    unsigned long ev[(EV_MAX + 1) / (8 * sizeof(long)) + 1] = { 0 }, keys[(KEY_MAX + 1) / (8 * sizeof(long)) + 1] = { 0 }, abs_[(ABS_MAX + 1) / (8 * sizeof(long)) + 1] = { 0 };
    ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
    if (test_bit(ev, EV_KEY)) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    if (test_bit(ev, EV_ABS)) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_), abs_);
    char name[128] = { 0 }; ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
    Dev *d = &dev[ndev];
    memset(d, 0, sizeof *d);
    d->fd = fd; snprintf(d->path, sizeof d->path, "%s", path);
    if (test_bit(abs_, ABS_MT_POSITION_X)) {
        d->kind = D_TOUCH;
        /* which panel: by the device's place in sysfs */
        char sys[300], real[PATH_MAX]; snprintf(sys, sizeof sys, "/sys/class/input/%s/device", strrchr(path, '/') + 1);
        const char *want = getenv("DK_TOUCH_BOTTOM"); if (!want) want = "fe5e0000.i2c";
        d->screen = (realpath(sys, real) && strstr(real, want)) ? 1 : 0;
        struct input_absinfo ai;
        if (!ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai)) { d->axmin[0] = ai.minimum; d->axmax[0] = ai.maximum; }
        if (!ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai)) { d->axmin[1] = ai.minimum; d->axmax[1] = ai.maximum; }
        if (d->axmax[0] <= 0) d->axmax[0] = pw_[d->screen] - 1;
        if (d->axmax[1] <= 0) d->axmax[1] = ph_[d->screen] - 1;
        plat_log("input: touch %s (%s) -> %s, %dx%d", path, name, d->screen ? "bottom" : "top", d->axmax[0] + 1, d->axmax[1] + 1);
    } else if (test_bit(keys, BTN_SOUTH) || test_bit(keys, BTN_GAMEPAD) || test_bit(keys, BTN_DPAD_UP) || strstr(name, "joypad") || strstr(name, "gamepad")) {
        d->kind = D_PAD;
        for (int a = 0; a < 6; a++) {
            static const int codes[6] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X, ABS_HAT0Y };
            struct input_absinfo ai;
            if (test_bit(abs_, codes[a]) && !ioctl(fd, EVIOCGABS(codes[a]), &ai)) {
                d->axmin[a] = ai.minimum; d->axmax[a] = ai.maximum; d->axflat[a] = ai.flat;
                if (a < 4 && ai.maximum > ai.minimum) have_sticks = 1;
            }
        }
        plat_log("input: pad %s (%s)%s", path, name, have_sticks ? " with sticks" : "");
    } else if (test_bit(keys, KEY_A) && test_bit(keys, KEY_SPACE)) {
        d->kind = D_KBD;
        plat_log("input: keyboard %s (%s)", path, name);
    } else { close(fd); return; }
    ndev++;
}

/* The nodes looked at so far, with what told them apart then. The search runs every 3 s on the game's own thread,
 * and it used to open every node that isn't ours again each time, to close it again: closing an evdev node waits
 * for the kernel (an RCU grace period), 7-24 ms each on an RG DS Plus, five of them: the game stood still for
 * 50-100 ms every 3 s (measured: 3 to 6 frames lost each time). Now a node is opened when it is new or was made
 * again (a device back from suspend), and a search that finds nothing new costs a directory listing. */
#define MAXSEEN 48
typedef struct { char path[64]; ino_t ino; long long ctime_ns; } Seen;
static Seen seen[MAXSEEN]; static int nseen;

static void forget(const char *path) {
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i].path, path)) { seen[i] = seen[--nseen]; return; }
}

static void scan(void) {
    glob_t g;
    rescan_t = now_s();
    if (glob("/dev/input/event*", 0, 0, &g)) return;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *path = g.gl_pathv[i];
        struct stat st;
        if (stat(path, &st)) continue;
        long long c = (long long)st.st_ctim.tv_sec * 1000000000ll + st.st_ctim.tv_nsec;
        int k = 0;
        while (k < nseen && strcmp(seen[k].path, path)) k++;
        if (k < nseen && seen[k].ino == st.st_ino && seen[k].ctime_ns == c) continue;   /* looked at, and the same node still */
        if (k == nseen) {
            if (nseen == MAXSEEN) continue;
            snprintf(seen[nseen++].path, sizeof seen[0].path, "%s", path);
        }
        seen[k].ino = st.st_ino; seen[k].ctime_ns = c;
        open_dev(path);
    }
    globfree(&g);
}

void evdev_init(int pw[2], int ph[2], int scale[2], int ox[2], int oy[2]) {
    for (int i = 0; i < 2; i++) { pw_[i] = pw[i]; ph_[i] = ph[i]; sc_[i] = scale[i]; ox_[i] = ox[i]; oy_[i] = oy[i]; }
    scan();
}
void evdev_shutdown(void) { for (int i = 0; i < ndev; i++) close(dev[i].fd); ndev = 0; nseen = 0; }

static int pad_bit(int code) {
    switch (code) {
    case BTN_EAST: return B_A;           /* 305: the button labelled A (see the top) */
    case BTN_SOUTH: return B_B;          /* 304: the one labelled B */
    case BTN_NORTH: return B_X;          /* 307 */
    case BTN_WEST: return B_Y;           /* 308 */
    case BTN_TL: return B_L1;
    case BTN_TR: return B_R1;
    case BTN_TL2: return B_L2;
    case BTN_TR2: return B_R2;
    case BTN_SELECT: return B_SELECT;
    case BTN_START: return B_START;
    case BTN_MODE: return B_MENU;
    case BTN_DPAD_UP: return B_UP;
    case BTN_DPAD_DOWN: return B_DOWN;
    case BTN_DPAD_LEFT: return B_LEFT;
    case BTN_DPAD_RIGHT: return B_RIGHT;
    case BTN_THUMBL: return B_L3;        /* clicking the left stick sprints too (game.c) */
    }
    return -1;
}
static int kbd_bit(int code) {
    switch (code) {
    case KEY_UP: case KEY_W: return B_UP;
    case KEY_DOWN: case KEY_S: return B_DOWN;
    case KEY_LEFT: case KEY_A: return B_LEFT;
    case KEY_RIGHT: case KEY_D: return B_RIGHT;
    case KEY_J: case KEY_Z: return B_A;
    case KEY_K: case KEY_X: return B_B;
    case KEY_I: case KEY_C: return B_X;
    case KEY_U: case KEY_V: return B_Y;
    case KEY_Q: return B_L1;
    case KEY_E: return B_R1;
    case KEY_1: return B_L2;
    case KEY_3: return B_R2;
    case KEY_BACKSPACE: case KEY_TAB: return B_SELECT;
    case KEY_ENTER: return B_START;
    case KEY_ESC: return B_MENU;
    }
    return -1;
}

static float axis(Dev *d, int a, int v) {
    int lo = d->axmin[a], hi = d->axmax[a];
    if (hi <= lo) return 0;
    float c = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
    float x = (v - c) / half;
    float dz = MAX(0.18f, d->axflat[a] / half);
    if (fabsf(x) < dz) return 0;
    x = (x - SGN(x) * dz) / (1 - dz);
    return clampf(x, -1, 1);
}

static Input touch_state;

void evdev_poll(Input *in) {
    if (now_s() - rescan_t > 3) scan();                 /* new devices (a USB keyboard, a touch controller back) */
    struct input_event ev[64];
    for (int i = 0; i < ndev; i++) {
        Dev *d = &dev[i];
        for (;;) {
            ssize_t n = read(d->fd, ev, sizeof ev);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) break;
                /* gone: release what it held, forget it */
                plat_log("input: %s went away", d->path);
                forget(d->path);                    /* searched for again, whatever its node looks like */
                if (d->kind == D_PAD || d->kind == D_KBD) held &= ~d->keys;
                if (d->kind == D_TOUCH) touch_state.touch[d->screen] = 0;
                close(d->fd);
                dev[i] = dev[--ndev]; i--;
                goto next;
            }
            if (n == 0) break;
            for (int k = 0; k < (int)(n / sizeof ev[0]); k++) {
                struct input_event *e = &ev[k];
                if (d->kind == D_TOUCH) {
                    if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_X || e->code == ABS_X)) d->tx = e->value;
                    else if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_Y || e->code == ABS_Y)) d->ty = e->value;
                    else if (e->type == EV_KEY && e->code == BTN_TOUCH) d->down = e->value != 0;
                    else if (e->type == EV_ABS && e->code == ABS_MT_TRACKING_ID && d->slot0) d->down = e->value >= 0;
                    else if (e->type == EV_ABS && e->code == ABS_MT_SLOT) d->slot0 = e->value == 0;
                    else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                        int s = d->screen;
                        int px = (int)((long long)(d->tx - d->axmin[0]) * pw_[s] / (d->axmax[0] - d->axmin[0] + 1));
                        int py = (int)((long long)(d->ty - d->axmin[1]) * ph_[s] / (d->axmax[1] - d->axmin[1] + 1));
                        const char *inv = getenv("DK_TOUCH_INVERT");
                        if (inv && strchr(inv, 'x')) px = pw_[s] - 1 - px;
                        if (inv && strchr(inv, 'y')) py = ph_[s] - 1 - py;
                        touch_state.touch[s] = d->down;
                        touch_state.tx[s] = (px - ox_[s]) / sc_[s];
                        touch_state.ty[s] = (py - oy_[s]) / sc_[s];
                    }
                } else if (e->type == EV_KEY) {
                    int b = d->kind == D_PAD ? pad_bit(e->code) : kbd_bit(e->code);
                    if (b < 0) continue;
                    if (e->value) { held |= BIT(b); d->keys |= BIT(b); }
                    else { held &= ~BIT(b); d->keys &= ~BIT(b); }
                } else if (e->type == EV_ABS && d->kind == D_PAD) {
                    switch (e->code) {
                    case ABS_X: ax[0] = axis(d, 0, e->value); break;
                    case ABS_Y: ax[1] = axis(d, 1, e->value); break;
                    case ABS_RX: ax[2] = axis(d, 2, e->value); break;
                    case ABS_RY: ax[3] = axis(d, 3, e->value); break;
                    case ABS_HAT0X: hat_x = SGN(e->value); break;
                    case ABS_HAT0Y: hat_y = SGN(e->value); break;
                    }
                }
            }
        }
    next:;
    }
    memset(in, 0, sizeof *in);
    in->held = held;
    if (hat_x < 0) in->held |= BIT(B_LEFT); else if (hat_x > 0) in->held |= BIT(B_RIGHT);
    if (hat_y < 0) in->held |= BIT(B_UP); else if (hat_y > 0) in->held |= BIT(B_DOWN);
    in->has_sticks = have_sticks;
    in->lx = ax[0]; in->ly = ax[1]; in->rx = ax[2]; in->ry = ax[3];
    for (int s = 0; s < 2; s++) { in->touch[s] = touch_state.touch[s]; in->tx[s] = touch_state.tx[s]; in->ty[s] = touch_state.ty[s]; }
}
