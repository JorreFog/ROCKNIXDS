// audio.c: the audio pump. DraStic's audio callback is called by our own real-time thread at a steady rate,
// instead of by SDL's audio thread.
//
// Why: DraStic paces its frames on audio. After each frame it waits until its audio callback has drained what
// it produced. SDL's audio thread (-> pulse server -> PipeWire) calls that callback whenever the audio stack
// gets around to it, and under any extra system load (a GPU shader pass, a background process, even a plain
// CPU spinner on one core) those calls become bursty. DraStic's frames then come out uneven and pairs of frames
// land in one refresh (measured: zero-copy mode went from ~0.1 to 5-7 dropped frames/s with a CPU spinner, and
// DraStic's main thread spent that time spinning in its audio wait).
//
// How: a SCHED_FIFO thread wakes on an absolute timer every `chunk` samples and calls DraStic's callback, so
// the drain DraStic sees is perfectly even. The samples go into a ring buffer; SDL's device (opened with our
// own callback) drains the ring at whatever pace the audio stack manages, and the ring (`target` samples,
// ~35 ms) absorbs that jitter. A slow control loop on the ring level trims the pump rate by a few hundred ppm
// at most, so it follows the real audio clock and the ring neither runs dry nor overflows.
// DraStic only uses SDL_OpenAudio + SDL_PauseAudio (no SDL_LockAudio): its callback is only ever called from
// one thread at a time, which stays true here (the pump instead of SDL's thread).
//
// Output: by default the ring is drained by our own writer thread straight into ALSA's "default" device
// (PipeWire's native ALSA plugin), not by SDL. Measured: SDL's pulse backend drained only ~43,600 of 44,100
// samples/s (the device itself is exact: 20.00 s of audio took 19.99 s through ALSA), so locking the pump to it
// ran DraStic 1.1% slow. With the ALSA writer the rate trim locks the pump to the true device clock.
//
// Env: DSFLIP_AUDIO_PUMP=0 (off: SDL calls DraStic's callback as before), DSFLIP_PUMP_CHUNK (samples, 256),
//      DSFLIP_PUMP_TARGET (ring level in samples, 1536), DSFLIP_AUDIO_OUT=sdl (drain via SDL instead of ALSA),
//      DSFLIP_ALSA_LATENCY (us, 30000).
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <dlfcn.h>
#include <math.h>

void dsflip_log(const char *fmt, ...);

struct SDL_AudioSpec_ { int freq; unsigned short format; unsigned char channels, silence;
    unsigned short samples, padding; uint32_t size; void (*callback)(void *, unsigned char *, int); void *userdata; };

#define RING 16384                       /* frames, power of two */
static void (*dcb)(void *, unsigned char *, int);   /* DraStic's callback */
static void *dud;
static int fb, freq, chunk = 256, target = 1536;   /* bytes per frame, rate, pump chunk, ring target (frames) */
static unsigned char *ring;
static volatile uint32_t head, tail;     /* frames written by the pump / read by SDL (free-running) */
static volatile int paused = 1, primed;
static volatile int st_under, st_over, st_lmin = RING, st_lmax, st_pump, st_sdl;
static volatile double st_ppm;
static volatile long long st_drained, st_pumped, st_t0;   /* frames per log window, for measured rates */
static unsigned char silence_byte;
/* speaker output level per pump chunk (last 32 chunks = ~186 ms), for the mic's echo gate */
static volatile float out_hist[32]; static volatile uint32_t out_pos;

static long long mono_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }

static void sdl_cb(void *u, unsigned char *out, int len) {   /* SDL's audio thread: drain the ring */
    (void)u;
    int n = len / fb, level = (int)(head - tail);
    st_sdl++;
    if (!primed) {
        if (level < target) { memset(out, silence_byte, len); return; }
        primed = 1;
    }
    int k = level < n ? level : n;
    for (int i = 0; i < k; ) {           /* up to two copies around the ring's end */
        uint32_t pos = (tail + i) & (RING - 1);
        int run = RING - (int)pos; if (run > k - i) run = k - i;
        memcpy(out + (size_t)i * fb, ring + (size_t)pos * fb, (size_t)run * fb);
        i += run;
    }
    __atomic_store_n(&tail, tail + k, __ATOMIC_RELEASE);
    st_drained += n;                     /* what SDL asked for, silence included: the device's real rate */
    if (k < n) { memset(out + (size_t)k * fb, silence_byte, (size_t)(n - k) * fb); st_under++; primed = 0; }
}

/* ---- one sound effect (the RetroAchievements unlock sound), mixed over DraStic's output in the pump ---- */
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO_WRITE
#include <assert.h>                   /* stb_vorbis's own includes, first: they must stay default-visible */
#include <limits.h>
#include <malloc.h>
#include <alloca.h>
#pragma GCC visibility push(hidden)   /* a preloaded library must not export these over anyone else's */
#include "stb_vorbis.c"
#pragma GCC visibility pop
static short *sfx_raw; static int sfx_raw_frames, sfx_raw_ch, sfx_raw_rate;   /* as decoded */
static int16_t *sfx; static int sfx_frames;                                    /* at the pump's rate and channels */
static volatile int sfx_pos = -1;                                              /* next frame to mix; -1: idle */

/* decode an Ogg Vorbis file (any thread). 0 on success. */
int audio_sfx_load(const char *path) {
    int ch, rate; short *pcm;
    int n = stb_vorbis_decode_filename(path, &ch, &rate, &pcm);
    if (n < 2 || ch < 1 || rate < 8000) { dsflip_log("[audio] can't decode %s\n", path); return -1; }
    sfx_raw = pcm; sfx_raw_frames = n; sfx_raw_ch = ch; sfx_raw_rate = rate;
    dsflip_log("[audio] sound effect %s: %.2f s, %d Hz, %d ch\n", path, (double)n / rate, rate, ch);
    return 0;
}

/* start the sound effect (restarts it if it is playing). Converts it to the pump's format on first use. */
void audio_sfx_play(void) {
    if (!sfx_raw || !dcb) return;        /* nothing loaded, or no pump (DSFLIP_AUDIO_PUMP=0) */
    if (!sfx) {
        int och = fb / 2, n = (int)((long long)sfx_raw_frames * freq / sfx_raw_rate);
        int16_t *o = malloc((size_t)n * och * sizeof *o);
        if (!o) return;
        for (int i = 0; i < n; i++) {    /* linear resampling; mono -> both channels, stereo -> mono by averaging */
            double x = (double)i * sfx_raw_rate / freq; int j = (int)x; double f = x - j;
            if (j + 1 >= sfx_raw_frames) { j = sfx_raw_frames - 2; f = 1; }
            for (int c = 0; c < och; c++) {
                double v = 0; int k = 0;
                for (int sc = 0; sc < sfx_raw_ch; sc++) {
                    if (och == 2 && sfx_raw_ch >= 2 && sc != c) continue;
                    v += sfx_raw[j * sfx_raw_ch + sc] * (1 - f) + sfx_raw[(j + 1) * sfx_raw_ch + sc] * f; k++;
                }
                o[i * och + c] = (int16_t)(v / k);
            }
        }
        sfx_frames = n; __atomic_store_n(&sfx, o, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&sfx_pos, 0, __ATOMIC_RELEASE);
    dsflip_log("[audio] sound effect: playing (%d frames at %d Hz)\n", sfx_frames, freq);
}

static void sfx_mix(unsigned char *buf, int frames) {
    int p = __atomic_load_n(&sfx_pos, __ATOMIC_ACQUIRE);
    if (p < 0) return;
    int16_t *d = (int16_t *)buf; const int och = fb / 2;
    int n = sfx_frames - p < frames ? sfx_frames - p : frames;
    for (int i = 0; i < n * och; i++) {
        int v = d[i] + sfx[p * och + i] * 3 / 4;   /* a little under full level: the unlock sounds are loud */
        d[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    int next = p + n >= sfx_frames ? -1 : p + n;
    __atomic_compare_exchange_n(&sfx_pos, &p, next, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);   /* unless restarted */
}

static void *pump(void *a) {             /* steady calls into DraStic's callback */
    (void)a;
    struct sched_param sp = { .sched_priority = 20 };   /* above DraStic (and its presenter), below PipeWire */
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp)) dsflip_log("[audio] pump: no SCHED_FIFO\n");
    unsigned char *tmp = malloc((size_t)chunk * fb);
    double integ = 0;
    long long next = mono_ns();
    for (;;) {
        if (paused) {                     /* DraStic paused audio: its callback isn't called, like with SDL */
            struct timespec d = { 0, 5000000 }; nanosleep(&d, 0);
            next = mono_ns(); continue;
        }
        dcb(dud, tmp, chunk * fb);
        sfx_mix(tmp, chunk);
        st_pump++; st_pumped += chunk;
        { const int16_t *sm = (const int16_t *)tmp; int ns = chunk * fb / 2; double acc = 0;
          for (int i = 0; i < ns; i++) { float v = sm[i] * (1.0f / 32768); acc += v * v; }
          out_hist[out_pos++ & 31] = (float)sqrt(acc / ns); }
        int level = (int)(head - __atomic_load_n(&tail, __ATOMIC_ACQUIRE));
        if (level + chunk <= RING) {
            for (int i = 0; i < chunk; ) {
                uint32_t pos = (head + i) & (RING - 1);
                int run = RING - (int)pos; if (run > chunk - i) run = chunk - i;
                memcpy(ring + (size_t)pos * fb, tmp + (size_t)i * fb, (size_t)run * fb);
                i += run;
            }
            __atomic_store_n(&head, head + chunk, __ATOMIC_RELEASE);
            level += chunk;
        } else st_over++;
        if (level < st_lmin) st_lmin = level;
        if (level > st_lmax) st_lmax = level;
        /* rate trim: ring above target -> pump slower. Only while SDL is draining (primed). */
        double adj = 0;
        if (primed) {
            double err = (double)(level - target) / target;
            integ += err * 2e-5; if (integ > 2e-2) integ = 2e-2; if (integ < -2e-2) integ = -2e-2;
            adj = 5e-4 * err + integ; if (adj > 2e-2) adj = 2e-2; if (adj < -2e-2) adj = -2e-2;
        }
        st_ppm = adj * 1e6;
        next += (long long)(chunk * 1e9 / freq * (1 + adj));
        long long now = mono_ns();
        if (next < now - 50000000LL) next = now;   /* fell far behind (stopped/suspended): don't burst */
        struct timespec ts = { next / 1000000000LL, next % 1000000000LL };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, 0);
    }
    return 0;
}

/* ---- ALSA writer (libasound dlopen()ed) ---- */
static void *pcm;
static int (*a_open)(void **, const char *, int, int);
static int (*a_set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
static long (*a_writei)(void *, const void *, unsigned long);
static long (*a_readi)(void *, void *, unsigned long);
static int (*a_recover)(void *, int, int);
static const char *(*a_strerror)(int);
static volatile int st_xrun;
static int alsa_syms(void) {
    void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!h) return -1;
    *(void **)&a_open = dlsym(h, "snd_pcm_open"); *(void **)&a_set_params = dlsym(h, "snd_pcm_set_params");
    *(void **)&a_writei = dlsym(h, "snd_pcm_writei"); *(void **)&a_recover = dlsym(h, "snd_pcm_recover");
    *(void **)&a_strerror = dlsym(h, "snd_strerror");
    *(void **)&a_readi = dlsym(h, "snd_pcm_readi");
    return a_open && a_set_params && a_writei && a_readi && a_recover ? 0 : -1;
}
static int alsa_open(void) {
    if (alsa_syms()) return -1;
    int e = a_open(&pcm, "default", 0 /* SND_PCM_STREAM_PLAYBACK */, 0);
    if (e < 0) { dsflip_log("[audio] ALSA open: %s\n", a_strerror ? a_strerror(e) : "?"); return -1; }
    const char *l = getenv("DSFLIP_ALSA_LATENCY");
    e = a_set_params(pcm, 2 /* SND_PCM_FORMAT_S16_LE */, 3 /* SND_PCM_ACCESS_RW_INTERLEAVED */, fb / 2, freq,
                     1 /* soft resample */, l ? (unsigned)atoi(l) : 30000);
    if (e < 0) { dsflip_log("[audio] ALSA params: %s\n", a_strerror ? a_strerror(e) : "?"); return -1; }
    return 0;
}
static void *writer(void *a) {           /* blocking writes pace this thread at the device's true rate */
    (void)a;
    struct sched_param sp = { .sched_priority = 20 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    unsigned char *buf = malloc((size_t)chunk * fb);
    for (;;) {
        sdl_cb(0, buf, chunk * fb);      /* the same ring drain (silence while priming or on underrun) */
        long r = a_writei(pcm, buf, chunk);
        if (r < 0) { st_xrun++; a_recover(pcm, (int)r, 1); }
    }
    return 0;
}

int audio_pump_enabled(void) { const char *e = getenv("DSFLIP_AUDIO_PUMP"); return !(e && *e == '0'); }

/* SDL_OpenAudio in pump mode. real: SDL's own SDL_OpenAudio. */
int audio_pump_open(struct SDL_AudioSpec_ *want, struct SDL_AudioSpec_ *have, int (*real)(struct SDL_AudioSpec_ *, struct SDL_AudioSpec_ *)) {
    if (!(want->format == 0x8010 || want->format == 0x0010) || want->channels < 1 || want->channels > 2 || !want->callback)
        return real(want, have);         /* only 16-bit PCM (what DraStic uses) */
    const char *c = getenv("DSFLIP_PUMP_CHUNK"), *t = getenv("DSFLIP_PUMP_TARGET");
    if (c && atoi(c) >= 32) chunk = atoi(c);
    if (t && atoi(t) >= chunk) target = atoi(t);
    dcb = want->callback; dud = want->userdata;
    fb = 2 * want->channels; freq = want->freq; silence_byte = 0;
    ring = calloc(RING, fb);
    const char *o = getenv("DSFLIP_AUDIO_OUT");
    int use_alsa = !(o && !strcmp(o, "sdl")) && alsa_open() == 0;
    if (!use_alsa) {                     /* drain via SDL's device (our callback) */
        struct SDL_AudioSpec_ w = *want;
        w.callback = sdl_cb; w.userdata = 0; w.samples = 512;
        int r = real(&w, 0);             /* obtained=NULL: SDL converts to exactly this format */
        if (r) return r;
    }
    if (have) { *have = *want; have->size = (uint32_t)want->samples * fb; have->silence = 0; }
    pthread_t th; pthread_create(&th, 0, pump, 0);
    if (use_alsa) pthread_create(&th, 0, writer, 0);
    dsflip_log("[audio] pump: %d Hz, DraStic's callback every %d samples (%.2f ms), ring target %d (%.1f ms), output %s\n",
               freq, chunk, chunk * 1000.0 / freq, target, target * 1000.0 / freq, use_alsa ? "ALSA default" : "SDL");
    return 0;
}

void audio_pump_pause(int on) { paused = on; }

/* one line for the 10 s log; resets the window */
void audio_pump_log(void) {
    if (!dcb) return;
    long long now = mono_ns(); double el = st_t0 ? (now - st_t0) / 1e9 : 0;
    dsflip_log("[audio] pump %.1f Hz, drain %.1f Hz, ring %d..%d, underruns %d, overflows %d, xruns %d, trim %+.0f ppm\n",
               el > 0 ? st_pumped / el : 0, el > 0 ? st_drained / el : 0, st_lmin, st_lmax, st_under, st_over, st_xrun, st_ppm);
    st_pump = st_sdl = st_under = st_over = st_xrun = st_lmax = 0; st_lmin = RING; st_drained = st_pumped = 0; st_t0 = now;
}

/* ---- microphone ----
 * DraStic has no mic input of its own here: its "fake microphone" control (Scroll Lock, control code 327) plays
 * microphone.wav into the DS mic while held. ROCKNIX's libdrastouch turned the real mic into that key; this is
 * the same logic, reverse-engineered from drastouch's mic_audio_callback: per block, the RMS level of the
 * 16-bit samples (scaled to +-1.0); the first 60 blocks average a noise floor, which then keeps tracking slowly
 * (floor = 0.999 floor + 0.001 level); level > floor + threshold holds the key, dropping below releases it.
 * The threshold is ES's "microphone sensitivity" (DSHOOK_MIC_THRESH: high 0.03, medium 0.15, low 0.3; off 0).
 * Capture goes straight through ALSA (PipeWire's mic source). */
void dsflip_mic_key(int down);
static void *mic_thread(void *a) {
    float thresh = *(float *)a;
    void *cap = 0;
    for (int i = 0; i < 100 && !dcb && audio_pump_enabled(); i++) { struct timespec d = { 0, 20000000 }; nanosleep(&d, 0); }
    if (alsa_syms() || a_open(&cap, "default", 1 /* SND_PCM_STREAM_CAPTURE */, 0) < 0 ||
        a_set_params(cap, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */, 1, 44100, 1, 100000) < 0) {
        dsflip_log("[mic] no capture device\n");
        return 0;
    }
    dsflip_log("[mic] listening, threshold %.3f, echo gate %s\n", thresh, dcb ? "on" : "off (no audio pump)");
    float coup = 0; int coup_n = 0;     /* how much speaker output leaks into the mic (mic rms / output rms) */
    int16_t buf[1024];
    float floor_ = 0, peak = 0; int n = 0, down = 0, blocks = 0, presses = 0;
    for (;;) {
        long r = a_readi(cap, buf, 1024);
        if (r < 0) { a_recover(cap, (int)r, 1); continue; }
        if (r == 0) continue;
        double acc = 0;
        for (long i = 0; i < r; i++) { float v = buf[i] * (1.0f / 32768); acc += v * v; }
        float level = (float)sqrt(acc / r);
        if (n < 60) { floor_ = (floor_ * n + level) / (n + 1); n++; continue; }
        floor_ = floor_ * 0.999f + level * 0.001f;
        /* echo gate: the mic hears the game's own sound from the speaker (measured: bleed peaks 0.13-0.23, far
         * above the "high" threshold 0.03, so the key fired constantly with nobody talking). The pump knows what
         * is played: learn the leak (mic/output ratio while not triggered) and require the mic to be clearly above
         * the expected bleed. out: the loudest output chunk of the last ~186 ms (covers the output latency). */
        float out = 0; for (int i = 0; i < 32; i++) if (out_hist[i] > out) out = out_hist[i];
        float bleed = coup * out;
        int loud = level > floor_ + thresh && (!dcb || level > 3 * bleed + thresh);
        if (!loud && out > 0.01f) {      /* learn the coupling only from blocks that aren't a real blow */
            float r = level / out; if (r > 4) r = 4;
            coup = coup_n < 50 ? (coup * coup_n + r) / (coup_n + 1) : coup * 0.99f + r * 0.01f; coup_n++;
        }
        if (loud != down) { down = loud; dsflip_mic_key(down); presses += down; }
        if (level > peak) peak = level;
        if (++blocks >= 431) {           /* ~10 s: levels for bug reports and tuning */
            dsflip_log("[mic] 10 s: noise floor %.4f, peak %.4f, speaker leak x%.3f, %d presses\n", floor_, peak, coup, presses);
            blocks = presses = 0; peak = 0;
        }
    }
    return 0;
}
void audio_mic_start(void) {
    const char *t = getenv("DSHOOK_MIC_THRESH");
    static float thresh;
    thresh = t ? (float)atof(t) : 0;
    if (thresh <= 0) { dsflip_log("[mic] off (ES: microphone sensitivity)\n"); return; }
    pthread_t th; pthread_create(&th, 0, mic_thread, &thresh);
}
