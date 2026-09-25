// drastic-vsync: LD_PRELOAD frame pacer for DraStic on the RG DS (ROCKNIX/sway).
//
// DraStic paces itself with gettimeofday + SDL_Delay(ms) after each present and
// never waits for vblank, so its presents sweep through the panel's refresh cycle
// (60.000 Hz emulator vs ~60.1 Hz panels) and frames periodically double up / drop.
//
// This shim:
//  * replaces the post-present SDL_Delay with a precise absolute sleep that wakes
//    just late enough for emulation + present to finish right before the
//    compositor's latch point for the next vblank (read from DRM), so every frame
//    lands on consecutive refreshes with minimal queueing;
//  * scales gettimeofday so DraStic's own scheduler sees exactly 60 fps while it
//    actually runs at the panel rate;
//  * opens audio at freq * panel_hz / 60 so audio is consumed as fast as it is
//    produced (pitch change ~3 cents at 60.1 Hz).
//
// Where the frame must be ready is learned, not assumed: every frame asks the
// compositor (wp_presentation) when it was actually shown; a frame that lands a
// refresh late grows the lead by 1 ms, on-time frames shrink it by 1 us, so it
// settles at the smallest lead the GPU + compositor can sustain.
//
// The launcher sets sway's max_render_time to 6 ms while DraStic runs: with the
// default ("off") sway composites right after each vblank, which costs a frame.
//
// Env: DVSYNC=0 disables everything; DVSYNC_PACE=0 keeps only logging;
//      DVSYNC_RENDER_US (6000, must match sway); DVSYNC_GPU_US (initial lead, 6000);
//      DVSYNC_AUDIO_PPM (5000); DVSYNC_AUDIO_SAMPLES (512);
//      DVSYNC_LOG=1 keeps per-frame logs, written to /tmp/dvsync.log on SIGUSR1.

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int uint32_t;
struct timespec { long tv_sec; long tv_nsec; };
struct timeval { long tv_sec; long tv_usec; };

#define RTLD_NEXT ((void *)-1l)
extern void *dlsym(void *, const char *);
extern int clock_gettime(int, struct timespec *);
extern int clock_nanosleep(int, int, const struct timespec *, struct timespec *);
extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern int close(int);
extern int ioctl(int, unsigned long, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern char *getenv(const char *);
extern long atol(const char *);
extern long syscall(long, ...);
extern int prctl(int, ...);
typedef void (*sighandler_t)(int);
extern sighandler_t signal(int, sighandler_t);

#define CLOCK_MONOTONIC 1
#define TIMER_ABSTIME 1
#define PR_SET_TIMERSLACK 29
#define SYS_gettid 178
#define DRM_IOCTL_WAIT_VBLANK 0xC018643AUL
#define DRM_VBLANK_RELATIVE 0x1

static int enabled = 1, pace = 1, logging, audio_adj = 1, warp_adj = 1;
static long safety;            // defined with the pacer below
static long extra_delays;     // main-thread SDL_Delays we did not pace (diagnostic)
static int main_tid;
static long render_us = 6000, margin_us = 500;   // render_us must match sway's max_render_time (set by the launcher)
static long gpu_us = 6000;      // learned: present call -> buffer usable by the compositor
static long cur_target;         // vblank the current frame is aimed at
static long eff_target;         // vblank it will actually make (later if presented after its latch)
static long fb_hits, fb_late, fb_discards, fb_cpu_late;
static int converged;
static long catchups;
static long short_wakes;       // frames woken without the full lead (heavy work)
static int cur_short;           // the current frame was one of them
            // back-to-back presents we held to the next refresh
static long stall_every, stall_us, frame_no;   // test-only: simulate heavy frames
static int catchup_fix = 1;
static double audio_extra = 1.005;   // consume audio 0.5% faster than frames produce it
static int audio_samples = 512;      // smaller chunks keep DraStic's audio wait short

static long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}
static void sleep_until(long us) {
    struct timespec ts = { us / 1000000L, (us % 1000000L) * 1000L };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, 0) == 4 /* EINTR */) {}
}

// ---- DRM vblank ----
struct wvbl { unsigned int type; unsigned int seq; long sec; long usec; };
static int drmfd = -1;
// last vblank timestamp (CLOCK_MONOTONIC us) of pipe; blocking=1 waits for the next one
static long vblank(int pipe, int blocking, unsigned int *seq) {
    if (drmfd < 0) drmfd = open("/dev/dri/card0", 02 /* O_RDWR */ | 02000000 /* O_CLOEXEC */);
    if (drmfd < 0) return -1;
    struct wvbl v = { DRM_VBLANK_RELATIVE | ((unsigned)pipe << 1), (unsigned)blocking, 0, 0 };
    if (ioctl(drmfd, DRM_IOCTL_WAIT_VBLANK, &v) != 0) return -1;
    if (seq) *seq = v.seq;
    return v.sec * 1000000L + v.usec;
}

static double period_us;          // measured panel refresh period
static void measure_period(void) {
    if (period_us > 0) return;
    unsigned int s0, s1;
    long t0 = vblank(1, 1, &s0);
    for (int i = 0; i < 12; i++) vblank(1, 1, 0);
    long t1 = vblank(1, 0, &s1);
    if (t0 < 0 || t1 < 0 || s1 == s0) { enabled = 0; return; }
    period_us = (double)(t1 - t0) / (double)(s1 - s0);
    if (period_us < 16000 || period_us > 17400) enabled = 0;   // only lock near 60 Hz
}

// ---- time warp: DraStic's clock runs at 60/panel_hz so it sees exactly 60 fps ----
static double warp = 1.0;
static long warp_real0, warp_virt0, warp_last;
static int warp_on;

int gettimeofday(struct timeval *tv, void *tz) {
    static int (*real)(struct timeval *, void *);
    if (!real) real = dlsym(RTLD_NEXT, "gettimeofday");
    int r = real(tv, tz);
    if (warp_on && r == 0) {
        long t = tv->tv_sec * 1000000L + tv->tv_usec;
        long v = warp_virt0 + (long)((double)(t - warp_real0) * warp);
        if (v < warp_last) v = warp_last;          // never let DraStic's clock run backwards
        warp_last = v;
        tv->tv_sec = v / 1000000L; tv->tv_usec = v % 1000000L;
    }
    return r;
}
static long virt_now(void) {
    struct timeval tv; gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}
static void enable_warp(void) {
    static int (*real)(struct timeval *, void *);
    if (!real) real = dlsym(RTLD_NEXT, "gettimeofday");
    struct timeval tv; real(&tv, 0);
    warp_real0 = warp_virt0 = tv.tv_sec * 1000000L + tv.tv_usec;
    warp = (1000000.0 / 60.0) / period_us;
    warp_on = 1;
}

#include "pfb.inc"

// ---- per-frame log ----
#define NLOG 20000
static struct { long wake, pin, pout, latch, v0, v1; int req, lead; long call; } lg[NLOG];
static int nlg;
static int ncb_ext(void); static long cb_t(int); static int cb_len(int); static long gaps_ext(int);
static void dump(int s) {
    (void)s;
    int fd = open("/tmp/dvsync.log", 01 | 0100 | 01000, 0644);
    char b[200];
    int l = snprintf(b, sizeof b, "# period_us=%.3f warp=%.6f render_us=%ld margin_us=%ld extra_delays=%ld gpu_us=%ld hits=%ld late=%ld cpu_late=%ld discards=%ld catchups=%ld safety=%ld short_wakes=%ld\n", period_us, warp, render_us, margin_us, extra_delays, gpu_us, fb_hits, fb_late, fb_cpu_late, fb_discards, catchups, safety, short_wakes);
    write(fd, b, l);
    for (int i = 0; i < nlg && i < NLOG; i++) {
        l = snprintf(b, sizeof b, "%ld %ld %ld %ld %ld %ld %d %d %ld\n", lg[i].wake, lg[i].pin, lg[i].pout, lg[i].latch, lg[i].v0, lg[i].v1, lg[i].req, lg[i].lead, lg[i].call);
        write(fd, b, l);
    }
    l = snprintf(b, sizeof b, "gaps %ld nonsilent_cbs %ld pf_state %d pf_dbg %d\n", gaps_ext(0), gaps_ext(1), pf_state, pf_dbg); write(fd, b, l);
    for (int i = 0; i < npf; i++) {
        l = snprintf(b, sizeof b, "pf %ld %ld %u %d %ld\n", pf[i].submit, pf[i].shown, pf[i].seq, pf[i].status, pf[i].target);
        write(fd, b, l);
    }
    for (int i = 0; i < ncb_ext() && i < 20000; i++) {
        l = snprintf(b, sizeof b, "cb %ld %d %d\n", cb_t(i), cb_len(i) & 0xffff, cb_len(i) >> 16);
        write(fd, b, l);
    }
    close(fd);
}

// ---- pacing ----
static int pending;            // a present happened; the next main-thread SDL_Delay is the frame sleep
static long t_wake = -1;       // when the last paced sleep ended
static long last_latch;        // latch time the current frame is aimed at
#define WN 120
static long wring[WN]; static int wpos;
static long work_est = 4000;   // ~p99 of (wake -> present returned) over the last 2 s
static long safety = 500;      // grows on misses, decays slowly
static int cur_req, cur_lead;
static long cur_v0, cur_v1, cur_pin, cur_call;

__attribute__((constructor)) static void init(void) {
    char *e;
    if ((e = getenv("DVSYNC")) && e[0] == '0') { enabled = 0; return; }
    if ((e = getenv("DVSYNC_RENDER_US"))) render_us = atol(e);
    if ((e = getenv("DVSYNC_GPU_US"))) gpu_us = atol(e);
    if ((e = getenv("DVSYNC_TEST_STALL_EVERY"))) stall_every = atol(e);
    if ((e = getenv("DVSYNC_TEST_STALL_US"))) stall_us = atol(e);
    if ((e = getenv("DVSYNC_CATCHUP")) && e[0] == '0') catchup_fix = 0;
    if ((e = getenv("DVSYNC_MARGIN_US"))) margin_us = atol(e);
    if ((e = getenv("DVSYNC_LOG")) && e[0] == '1') { logging = 1; signal(10, dump); }
    if ((e = getenv("DVSYNC_AUDIO")) && e[0] == '0') audio_adj = 0;
    if ((e = getenv("DVSYNC_WARP")) && e[0] == '0') warp_adj = 0;
    if ((e = getenv("DVSYNC_PACE")) && e[0] == '0') { pace = 0; warp_adj = 0; audio_adj = 0; }
    if ((e = getenv("DVSYNC_AUDIO_PPM"))) audio_extra = 1.0 + (double)atol(e) / 1e6;
    if ((e = getenv("DVSYNC_AUDIO_SAMPLES"))) audio_samples = (int)atol(e);
    main_tid = (int)syscall(SYS_gettid);
    prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);   // precise wakeups on the main thread
    measure_period();
    if (enabled && warp_adj) enable_warp();
}

void SDL_RenderPresent(void *r) {
    static void (*real)(void *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    if (!enabled || (int)syscall(SYS_gettid) != main_tid) { real(r); return; }
    if (stall_every && ++frame_no % stall_every == 0) sleep_until(now_us() + stall_us);
    cur_pin = now_us();
    if (pace && catchup_fix && pending && last_latch > 0) {
        // DraStic presented again without its frame sleep: its clock says it is behind
        // (a heavy frame) and it is catching up. Don't let two frames land in one
        // refresh (one would be discarded and a refresh repeated): hold this one for
        // the next refresh, and give DraStic that frame of time back (its clock is
        // held still for one frame) so it returns to sleeping normally.
        double P = period_us;
        long back = gpu_us + render_us;
        long V = eff_target + (long)P;
        while (V - back - margin_us < cur_pin - (long)P) V += (long)P;   // far behind: don't chase old refreshes
        long when = V - back - margin_us;
        if (cur_pin < when) sleep_until(when);
        cur_target = V; last_latch = V - back;
        if (warp_on) warp_virt0 -= (long)(1000000.0 / 60.0);             // clamp in gettimeofday keeps it monotonic
        catchups++;
        t_wake = -1;                                                      // not a paced frame: no work sample
        cur_pin = now_us();
    }
    if (pace && cur_target && period_us > 0) {
        // Presented after its latch (heavy frame): it will show on a later refresh.
        // Schedule the following frames after that refresh (eff_target) so they don't
        // collide with it, but report the original target to the feedback loop so the
        // lead estimate still sees the miss.
        // Only when clearly past the latch: presents a little after it usually still
        // make the refresh, and assuming otherwise would skip one ourselves.
        long back = gpu_us + render_us;
        eff_target = cur_target;
        while (eff_target - back + 1500 < cur_pin) eff_target += (long)period_us;   // measured: >1 ms past the latch always misses
        if (eff_target != cur_target) last_latch = eff_target - back;
    }
    if (pace) pf_request(r, cur_pin, cur_target); else if (logging) pf_request(r, cur_pin, 0);
    real(r);
    long pout = now_us();
    pf_poll();
    if (t_wake > 0) {
        long work = pout - t_wake;
        // work estimate: 2nd-largest of the last 120 frames (~p99 over 2 s)
        wring[wpos++ % WN] = work;
        long m1 = 0, m2 = 0;
        for (int i = 0; i < WN; i++) { long w = wring[i]; if (w > m1) { m2 = m1; m1 = w; } else if (w > m2) m2 = w; }
        work_est = m2 > 0 ? m2 : m1;
        long over = pout - (last_latch - margin_us / 2);
        if (over > 0 && over < (long)(period_us / 2) && !cur_short) {    // near miss: lead a bit short
            safety += 500; if (safety > 6000) safety = 6000;
        } else if (safety > 300) safety -= 3;                             // one-off heavy frames don't count
        if (logging && nlg < NLOG) {
            lg[nlg].wake = t_wake; lg[nlg].pin = cur_pin; lg[nlg].pout = pout; lg[nlg].latch = last_latch;
            lg[nlg].v0 = cur_v0; lg[nlg].v1 = cur_v1; lg[nlg].req = cur_req; lg[nlg].lead = cur_lead; lg[nlg].call = cur_call; nlg++;
        }
        t_wake = -1;
    }
    pending = 1;
}

void SDL_Delay(uint32_t ms) {
    static void (*real)(uint32_t);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_Delay");
    if (!enabled || !pace || (int)syscall(SYS_gettid) != main_tid) { real(ms); return; }
    // SDL_Delay(0) is DraStic yielding while it polls other threads; the frame
    // sleep is the first non-zero delay after a present.
    if (ms == 0) { real(ms); return; }
    if (!pending) { extra_delays++; real(ms); return; }
    pending = 0;

    double P = period_us;
    long v0 = vblank(0, 0, 0), v1 = vblank(1, 0, 0);
    if (v0 < 0 || v1 < 0) { real(ms); return; }
    // Aim at the panel whose vblank comes first in the cycle; the other one then
    // picks up the same buffer a couple of ms later in the same refresh.
    long d = (long)(v0 - v1) % (long)P; if (d < 0) d += (long)P;   // pipe0 vblank this long after pipe1's
    long base = (d < P / 2) ? v1 : v0;

    long now = now_us();
    cur_call = now;
    long lead = work_est + safety + margin_us;
    long back = gpu_us + render_us;                 // present must happen this long before the vblank
    // Aim at the earliest refresh that is still free (one frame per refresh) and
    // whose latch hasn't passed. If there isn't time for the usual lead, wake at
    // once rather than aiming a refresh later: skipping ahead meant sleeping on top
    // of frames that were already long (hires 3D), which dropped DraStic to 40-50 fps.
    long V = base + (long)P;
    while (V - back - margin_us < now) V += (long)P;
    while (V - back < last_latch + (long)(P / 2)) V += (long)P;     // one frame per refresh
    // after a long stall (menu, loading), re-sync instead of chasing old targets
    if (V - back - now > 3 * (long)P) V = base + (long)P;
    long latch = V - back;
    last_latch = latch;
    cur_target = V;
    cur_req = (int)ms; cur_lead = (int)lead; cur_v0 = v0; cur_v1 = v1;
    cur_short = latch - lead < now;
    if (cur_short) short_wakes++;
    // DraStic loops on its own deadline (virtual now + ms). Make sure that when we
    // wake it, its clock already reads past that deadline, or it sleeps again.
    long v_deadline = virt_now() + (long)ms * 1000L + 1000L;
    if (latch - lead > now) sleep_until(latch - lead);
    t_wake = now_us();
    long behind = v_deadline - virt_now();
    if (behind > 0) warp_virt0 += behind;
}

// Presentation feedback: a frame shown a refresh later than aimed means the GPU /
// compositor needed more time than gpu_us; back off fast, creep back slowly
// (1 us per on-time frame => a probing miss roughly every 1000 frames at most).
static void pf_result(long target, long shown, int status, long submit) {
    if (!pace || target == 0) return;
    if (status == 2) { fb_discards++; return; }
    long late = shown - target;
    // presented after its latch: the CPU side ran long (a heavy frame), not the GPU
    if (late > (long)(period_us / 2) && submit > target - gpu_us - render_us) { fb_cpu_late++; return; }
    if (late > (long)(period_us / 2)) {
        fb_late++; gpu_us += 1000; if (gpu_us > 14000) gpu_us = 14000;
        converged = 1;                                   // found the edge: probe slowly from now on
    } else {
        fb_hits++;
        gpu_us -= converged ? 1 : 30;                    // fast search first, then ~1 ms per 1000 frames
        if (gpu_us < 0) gpu_us = 0;
    }
}

// ---- audio: consume at the rate frames are actually produced ----
struct SDL_AudioSpec { int freq; unsigned short format; unsigned char channels; unsigned char silence;
    unsigned short samples; unsigned short padding; uint32_t size; void (*callback)(void *, unsigned char *, int); void *userdata; };

static void (*app_cb)(void *, unsigned char *, int);
#define NCB 20000
static long cbt[NCB]; static int cbl[NCB]; static int ncb; static long gaps, nonsilent_cbs;
static void cb_wrap(void *u, unsigned char *buf, int len) {
    if (ncb < NCB) { cbt[ncb] = now_us(); cbl[ncb] = len; ncb++; }
    app_cb(u, buf, len);
    if (ncb <= NCB) cbl[ncb - 1] |= (int)((now_us() - cbt[ncb - 1]) << 16);   // high bits: callback duration us
    // gap detector: runs of >=64 exactly-silent stereo frames inside non-silent audio
    short *smp = (short *)buf; int n = len / 4, zr = 0, nz = 0;
    for (int i = 0; i < n; i++) {
        if (smp[2 * i] == 0 && smp[2 * i + 1] == 0) zr++;
        else { if (zr >= 64 && nz) gaps++; zr = 0; nz++; }
    }
    if (nz) nonsilent_cbs++;
}

int SDL_OpenAudio(struct SDL_AudioSpec *want, struct SDL_AudioSpec *have) {
    static int (*real)(struct SDL_AudioSpec *, struct SDL_AudioSpec *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_OpenAudio");
    if (!enabled || !audio_adj) return real(want, have);
    struct SDL_AudioSpec w = *want;
    if (logging) { app_cb = want->callback; w.callback = cb_wrap; }
    if (audio_samples > 0) w.samples = (unsigned short)audio_samples;
    w.freq = (int)((double)want->freq * (1000000.0 / 60.0) / period_us * audio_extra + 0.5);
    // obtained=NULL makes SDL convert to exactly what we asked for
    int r = real(&w, 0);
    if (r == 0 && have) { *have = *want; have->size = w.size; have->silence = w.silence; }
    return r;
}

static int ncb_ext(void) { return ncb; }
static long cb_t(int i) { return cbt[i]; }
static int cb_len(int i) { return cbl[i]; }
static long gaps_ext(int w) { return w ? nonsilent_cbs : gaps; }
