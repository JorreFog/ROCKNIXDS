// audio_alsa.c: the mixer's output through ALSA's "default" device (PipeWire's ALSA plugin on ROCKNIX), libasound
// loaded at run time. A writer thread asks the mixer for a block and writes it; the blocking write paces it.
#define _GNU_SOURCE
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

static void *pcm;
static int (*a_open)(void **, const char *, int, int);
static int (*a_set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
static long (*a_writei)(void *, const void *, unsigned long);
static int (*a_recover)(void *, int, int);
static int (*a_close)(void *);
static const char *(*a_strerror)(int);
static void (*mixer)(int16_t *, int);
#define BLOCK 512

static void *writer(void *arg) {
    (void)arg;
    struct sched_param sp = { .sched_priority = 10 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);   /* if allowed: no gaps under load */
    int16_t *buf = malloc(BLOCK * 4);
    for (;;) {
        mixer(buf, BLOCK);
        long r = a_writei(pcm, buf, BLOCK);
        if (r < 0 && a_recover(pcm, (int)r, 1) < 0) { struct timespec ts = { 0, 5000000 }; nanosleep(&ts, 0); }
    }
    return 0;
}

int alsa_start(int rate, void (*mix)(int16_t *, int)) {
    void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!h) { plat_log("audio: no libasound"); return -1; }
    *(void **)&a_open = dlsym(h, "snd_pcm_open"); *(void **)&a_set_params = dlsym(h, "snd_pcm_set_params");
    *(void **)&a_writei = dlsym(h, "snd_pcm_writei"); *(void **)&a_recover = dlsym(h, "snd_pcm_recover");
    *(void **)&a_strerror = dlsym(h, "snd_strerror"); *(void **)&a_close = dlsym(h, "snd_pcm_close");
    if (!a_open || !a_set_params || !a_writei || !a_recover) return -1;
    static const char *devs[] = { "default", "plughw:0,0" };
    for (int i = 0; i < 2; i++) {
        int e = a_open(&pcm, devs[i], 0 /* playback */, 0);
        if (e < 0) { plat_log("audio: open %s: %s", devs[i], a_strerror ? a_strerror(e) : "?"); pcm = 0; continue; }
        e = a_set_params(pcm, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */, 2, (unsigned)rate, 1, 40000);
        if (e < 0) { plat_log("audio: params on %s: %s", devs[i], a_strerror ? a_strerror(e) : "?"); if (a_close) a_close(pcm); pcm = 0; continue; }
        plat_log("audio: ALSA %s, %d Hz", devs[i], rate);
        break;
    }
    if (!pcm) return -1;
    mixer = mix;
    pthread_t th;
    if (pthread_create(&th, 0, writer, 0)) return -1;
    pthread_setname_np(th, "dk-audio");
    pthread_detach(th);
    return 0;
}
