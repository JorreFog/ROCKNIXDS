// LD_PRELOAD timing probe for DraStic: logs every timing-related call with a
// CLOCK_MONOTONIC timestamp (us) into a ring buffer, dumped to /tmp/probe.log at exit
// or on SIGUSR1. Chains to the next definition via RTLD_NEXT.
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int uint32_t;
struct timespec { long tv_sec; long tv_nsec; };
struct timeval { long tv_sec; long tv_usec; };

#define RTLD_NEXT ((void *)-1l)
extern void *dlsym(void *, const char *);
extern int clock_gettime(int, struct timespec *);
extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern int close(int);
extern int snprintf(char *, size_t, const char *, ...);
typedef void (*sighandler_t)(int);
extern sighandler_t signal(int, sighandler_t);
extern long syscall(long, ...);

#define N 200000
static struct { long t; int kind; long a; int tid; } ev[N];
static volatile int nev;

static long now_us(void) {
    struct timespec ts; clock_gettime(1, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}
static void rec(int kind, long a) {
    int i = __atomic_fetch_add(&nev, 1, __ATOMIC_RELAXED);
    if (i < N) { ev[i].t = now_us(); ev[i].kind = kind; ev[i].a = a; ev[i].tid = (int)syscall(178); }
}
static const char *names[] = {"delay_in", "delay_out", "gtod", "clock", "ticks", "present_in", "present_out", "cwait_in", "cwait_out", "csignal", "audio_cb", "vbl0", "vbl1"};


extern int ioctl(int, unsigned long, ...);
struct wvbl { unsigned int type; unsigned int seq; long sec; long usec; };
static int drmfd = -1;
static long vbl_ts(int pipe, unsigned int *seq) {
    if (drmfd < 0) drmfd = open("/dev/dri/card0", 02);
    struct wvbl v = { 0x1 | (pipe ? (pipe << 1) : 0), 0, 0, 0 };
    if (ioctl(drmfd, 0xC018643AUL, &v) != 0) return -1;
    if (seq) *seq = v.seq;
    return v.sec * 1000000L + v.usec;
}
static void dump(void) {
    int fd = open("/tmp/probe.log", 01 | 0100 | 01000, 0644);
    char b[128];
    int n = nev < N ? nev : N;
    for (int i = 0; i < n; i++) {
        int l = snprintf(b, sizeof b, "%ld %d %s %ld\n", ev[i].t, ev[i].tid, names[ev[i].kind], ev[i].a);
        write(fd, b, l);
    }
    close(fd);
}
static void on_usr1(int s) { (void)s; dump(); }
__attribute__((constructor)) static void init(void) { signal(10, on_usr1); }

void SDL_Delay(uint32_t ms) {
    static void (*real)(uint32_t);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_Delay");
    rec(0, ms); real(ms); rec(1, ms);
}
int gettimeofday(struct timeval *tv, void *tz) {
    static int (*real)(struct timeval *, void *);
    if (!real) real = dlsym(RTLD_NEXT, "gettimeofday");
    int r = real(tv, tz); rec(2, 0); return r;
}
long clock(void) {
    static long (*real)(void);
    if (!real) real = dlsym(RTLD_NEXT, "clock");
    long r = real(); rec(3, r); return r;
}
uint32_t SDL_GetTicks(void) {
    static uint32_t (*real)(void);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_GetTicks");
    uint32_t r = real(); rec(4, r); return r;
}
void SDL_RenderPresent(void *r) {
    static void (*real)(void *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    rec(5, 0); rec(11, vbl_ts(0, 0)); rec(12, vbl_ts(1, 0)); real(r); rec(6, 0);
}
int pthread_cond_wait(void *c, void *m) {
    static int (*real)(void *, void *);
    if (!real) real = dlsym(RTLD_NEXT, "pthread_cond_wait");
    rec(7, (long)c); int r = real(c, m); rec(8, (long)c); return r;
}
int pthread_cond_signal(void *c) {
    static int (*real)(void *);
    if (!real) real = dlsym(RTLD_NEXT, "pthread_cond_signal");
    rec(9, (long)c); return real(c);
}

// wrap the audio callback so we can see when SDL pulls audio
struct SDL_AudioSpec { int freq; unsigned short format; unsigned char channels; unsigned char silence;
    unsigned short samples; unsigned short padding; uint32_t size; void (*callback)(void *, unsigned char *, int); void *userdata; };
static void (*app_cb)(void *, unsigned char *, int);
static void cb_wrap(void *u, unsigned char *s, int len) { rec(10, len); app_cb(u, s, len); }
int SDL_OpenAudio(struct SDL_AudioSpec *want, struct SDL_AudioSpec *have) {
    static int (*real)(struct SDL_AudioSpec *, struct SDL_AudioSpec *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_OpenAudio");
    app_cb = want->callback; want->callback = cb_wrap;
    int r = real(want, have);
    char b[128]; int fd = open("/tmp/probe-audio.txt", 01 | 0100 | 01000, 0644);
    int l = snprintf(b, sizeof b, "want freq=%d samples=%d ch=%d fmt=%x; have freq=%d samples=%d\n", want->freq, want->samples, want->channels, want->format, have ? have->freq : -1, have ? have->samples : -1);
    write(fd, b, l); close(fd);
    return r;
}
