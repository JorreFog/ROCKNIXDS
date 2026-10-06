// plat.c: picks a backend and holds what all of them share (time, the data directory, the log).
#define _GNU_SOURCE
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

static const Backend *be;
static FILE *logf_;

void plat_log(const char *fmt, ...) {
    va_list ap;
    if (!logf_) {
        char p[512]; snprintf(p, sizeof p, "%s/dodakvarter.log", plat_data_dir());
        logf_ = fopen(p, "w");
        if (logf_) setvbuf(logf_, 0, _IOLBF, 0);
    }
    va_start(ap, fmt);
    if (logf_) { vfprintf(logf_, fmt, ap); fputc('\n', logf_); }
    va_end(ap);
    if (getenv("DK_VERBOSE")) { va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }
}

static void mkdirs(const char *path) {
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

/* DK_DATA, else ROCKNIX's config partition, else XDG_DATA_HOME or ~/.local/share */
const char *plat_data_dir(void) {
    static char dir[512];
    if (dir[0]) return dir;
    const char *e = getenv("DK_DATA");
    struct stat st;
    if (e && *e) snprintf(dir, sizeof dir, "%s", e);
    else if (stat("/storage/.config", &st) == 0) snprintf(dir, sizeof dir, "/storage/.config/dodakvarter");
    else if ((e = getenv("XDG_DATA_HOME")) && *e) snprintf(dir, sizeof dir, "%s/dodakvarter", e);
    else if ((e = getenv("HOME")) && *e) snprintf(dir, sizeof dir, "%s/.local/share/dodakvarter", e);
    else snprintf(dir, sizeof dir, "/tmp/dodakvarter");
    mkdirs(dir);
    return dir;
}

double plat_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

int plat_init(const char *name, PlatInfo *info) {
    memset(info, 0, sizeof *info);
    const Backend *order[3]; int n = 0;
    if (name && !strcmp(name, "kms")) order[n++] = &backend_kms;
    else if (name && !strcmp(name, "sdl")) order[n++] = &backend_sdl;
    else if (name && !strcmp(name, "headless")) order[n++] = &backend_headless;
    else {   /* auto: the panels if we may have them, else a window */
        order[n++] = &backend_kms; order[n++] = &backend_sdl;
    }
    for (int i = 0; i < n; i++) {
        if (order[i]->init(info) == 0) { be = order[i]; return 0; }
    }
    return -1;
}
void plat_poll(Input *in) { be->poll(in); }
void plat_present(Surf *top, Surf *bot) { be->present(top, bot); }
void plat_shutdown(void) { if (be && be->shutdown) be->shutdown(); }
int plat_audio_start(int rate, void (*mix)(int16_t *, int)) { return be && be->audio ? be->audio(rate, mix) : -1; }
void plat_set_title(const char *s) { (void)s; }
