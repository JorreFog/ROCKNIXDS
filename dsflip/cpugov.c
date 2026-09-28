// cpugov.c: the lowest CPU clock that keeps DraStic at full speed (plan-1.4 section 7).
//
// ROCKNIX runs DS games with the CPU governor "performance": all four cores at 1992 MHz for the whole session. Most
// of that is heat. Measured (HeartGold at 2x, walking, 60 s each): the busiest DraStic thread is 46% busy at 1992
// MHz and 66% at 1104, still 60 fps with no clear extra drops, while the battery current fell by ~100 mA and the SoC ran
// 5 C cooler. schedutil doesn't get there (average 1771 MHz: DraStic's several busy threads keep it high).
//
// So this thread sets the clock itself, from what the game needs: every 250 ms it reads the CPU time of each of
// DraStic's threads (per-thread CPU clocks, ns precision), the heaviest single frame's CPU time on DraStic's main
// thread (measured at each present), the frames presented and the frames the queue dropped. The busiest thread's
// load u at the current clock f predicts the clock that would load it to TARGET: f * u / TARGET (work scales with
// 1/f; memory-bound parts scale less, which errs on the fast side); the heaviest frame must fit PEAK_TARGET of a
// refresh the same way. The average alone was not enough: at 1104 MHz HeartGold averaged 66% yet single frames ran
// over 16.7 ms, DraStic caught up in a burst and the queue dropped frames (0.2-0.26/s instead of ~0.05). Up at once
// when a window needs it (busy, a heavy frame, a drop, or below full speed); down one step only after DOWN_AFTER
// windows in a row that would all fit lower. The clock is set through scaling_max_freq under the "performance" governor (this kernel has no
// "userspace" governor); session.sh saves the limit before the game and restore.sh puts it back.
//
// DSFLIP_CPUGOV=0: off (the clock stays as ROCKNIX set it). DSFLIP_CPU_MIN / DSFLIP_CPU_MAX (kHz): bounds.
// DSFLIP_CPUGOV_LOG=1: one log line per decision window instead of per change.
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void dsflip_log(const char *fmt, ...);
extern volatile int dsflip_presents;        /* dsflip.c: SDL_RenderPresent calls, monotonic */
extern volatile long long dsflip_frame_work_max;   /* dsflip.c: heaviest frame's main-thread CPU ns since taken */
extern volatile int dsflip_queue_drops;     /* dsflip.c: frames dropped because DraStic fell behind and caught up */

#define POL "/sys/devices/system/cpu/cpufreq/policy0/"
#define WINDOW_MS 250
#define TARGET 0.72                          /* load the busiest thread may reach at the chosen clock */
#define HIGH 0.85                            /* above this at the current clock: go up now */
#define PEAK_TARGET 0.80                     /* the heaviest frame in a window may take this much of 16.7 ms */
#define PEAK_HIGH 0.95                       /* a frame this close to late: go up now */
#define BAD_FOR_S 30                         /* a clock that dropped a frame isn't tried again for this long */
#define FPS_WINDOWS 4                        /* the frame rate is judged over 1 s: 250 ms holds only ~15 frames */
#define DOWN_AFTER 8                         /* windows (2 s) in a row that fit a lower clock before stepping down */
#define MAXT 64

static int freqs[24], nf, cur, fmin_, fmax_;
static long long bad_until[24];              /* per clock: don't step down to it before this (CLOCK_MONOTONIC ns) */
static long long now_ns;
static int verbose;

static int rd_int(const char *p) { FILE *f = fopen(p, "r"); int v = 0; if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); } return v; }
static int wr_int(const char *p, int v) { FILE *f = fopen(p, "w"); if (!f) return -1; fprintf(f, "%d\n", v); return fclose(f); }
static void rd_str(const char *p, char *out, size_t n) {
    FILE *f = fopen(p, "r"); out[0] = 0;
    if (f) { if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0; fclose(f); }
}

static double last_peak;
static void set_clock(int khz, const char *why, double u, double fps) {
    if (khz == cur) return;
    /* raising: max first is enough (performance follows it); lowering: the same single write */
    if (wr_int(POL "scaling_max_freq", khz)) { dsflip_log("[cpugov] can't set %d kHz\n", khz); return; }
    dsflip_log("[cpugov] %d -> %d MHz (%s: busiest thread %.0f%%, heaviest frame %.0f%% of 16.7 ms, %.1f fps)\n",
               cur / 1000, khz / 1000, why, u * 100, last_peak * 100, fps);
    cur = khz;
}

/* the lowest available clock >= khz, within the bounds */
static int fit(double khz) {
    for (int i = 0; i < nf; i++) if (freqs[i] >= fmin_ && freqs[i] <= fmax_ && freqs[i] >= khz) return freqs[i];
    return fmax_;
}
static int idx(int khz) { for (int i = 0; i < nf; i++) if (freqs[i] == khz) return i; return -1; }
/* one step down, unless that clock dropped a frame recently: stepping down to it again, dropping, and going back up
 * was where most of the remaining drops came from (measured: ds-crisp 0.13/s bouncing 1416 <-> 1608) */
static int step_down(int khz) {
    int best = khz;
    for (int i = 0; i < nf; i++) if (freqs[i] < khz && freqs[i] >= fmin_ && (best == khz || freqs[i] > best)) best = freqs[i];
    int b = idx(best);
    return b >= 0 && bad_until[b] > now_ns ? khz : best;
}

static long long thread_ns(int tid) {
    struct timespec t;
    clockid_t c = ((~(clockid_t)tid) << 3) | 6;         /* CPUCLOCK_PERTHREAD | CPUCLOCK_SCHED: any thread of ours */
    if (clock_gettime(c, &t)) return -1;
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void *gov_thread(void *a) {
    (void)a;
    int tids[MAXT]; long long last[MAXT]; int nt = 0;
    struct timespec w = { 0, WINDOW_MS * 1000000L };
    int drops_prev = dsflip_queue_drops;
    long long t_prev = 0, fps_t[FPS_WINDOWS] = { 0 }; int fps_p[FPS_WINDOWS] = { 0 }, fi = 0, low = 0, rescan = 0;
    for (;;) {
        if (--rescan <= 0) {                             /* DraStic's threads (not ours: dsf-*), once a second */
            DIR *d = opendir("/proc/self/task"); struct dirent *e; int n = 0; int nt_[MAXT]; long long nl[MAXT];
            while (d && (e = readdir(d)) && n < MAXT) {
                int tid = atoi(e->d_name); if (tid <= 0) continue;
                char p[64], comm[32]; snprintf(p, sizeof p, "/proc/self/task/%d/comm", tid); rd_str(p, comm, sizeof comm);
                if (!strncmp(comm, "dsf-", 4) || !strncmp(comm, "data-loop", 9) || !strncmp(comm, "pw-", 3)) continue;
                nt_[n] = tid; nl[n] = -1;
                for (int k = 0; k < nt; k++) if (tids[k] == tid) nl[n] = last[k];
                n++;
            }
            if (d) closedir(d);
            memcpy(tids, nt_, sizeof(int) * n); memcpy(last, nl, sizeof(long long) * n); nt = n; rescan = 1000 / WINDOW_MS;
        }
        nanosleep(&w, 0);
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long long t = now.tv_sec * 1000000000LL + now.tv_nsec, dt = t_prev ? t - t_prev : 0;
        now_ns = t;
        t_prev = t;
        double umax = 0;
        for (int k = 0; k < nt; k++) {
            long long ns = thread_ns(tids[k]);
            if (ns >= 0 && last[k] >= 0 && dt > 0) { double u = (double)(ns - last[k]) / dt; if (u > umax) umax = u; }
            last[k] = ns;
        }
        /* presents over the last FPS_WINDOWS windows: one present of jitter in 250 ms alone reads as 56 or 64 fps */
        int pres = dsflip_presents; double fps = 60;
        if (fps_t[fi]) fps = (pres - fps_p[fi]) * 1e9 / (t - fps_t[fi]);
        fps_t[fi] = t; fps_p[fi] = pres; fi = (fi + 1) % FPS_WINDOWS;
        if (!dt) continue;
        /* the heaviest frame decides as much as the average: a single frame over 16.7 ms makes DraStic late, it
         * catches up with a burst, and the queue drops a frame (measured: all extra drops at low clocks were those) */
        double peak = __atomic_exchange_n(&dsflip_frame_work_max, 0, __ATOMIC_RELAXED) / 16.67e6;
        last_peak = peak;
        int drops = dsflip_queue_drops, dropped = drops - drops_prev; drops_prev = drops;
        double need = cur * umax / TARGET, needp = cur * peak / PEAK_TARGET;
        int want = fit(need > needp ? need : needp);
        const char *why = 0;
        if (umax > HIGH) why = "busy";
        else if (peak > PEAK_HIGH) why = "heavy frame";
        else if (dropped && peak > 0.6) {
            why = "dropped"; if (want <= cur) want = fit(cur + 1);
            int c = idx(cur); if (c >= 0) bad_until[c] = t + BAD_FOR_S * 1000000000LL;
        }
        else if (fps < 58.5 && fps > 5 && umax > 0.5) { why = "slow"; if (want <= cur) want = fit(cur + 1); }   /* +1 step */
        if (why && want > cur) { set_clock(want, why, umax, fps); low = 0; }
        else if (want < cur) { if (++low >= DOWN_AFTER) { set_clock(step_down(cur), "light", umax, fps); low = 0; } }
        else low = 0;
        if (verbose) dsflip_log("[cpugov] window: busiest %.0f%% peak frame %.0f%% fps %.1f drops %d at %d MHz, fits %d\n",
                                umax * 100, peak * 100, fps, dropped, cur / 1000, want / 1000);
    }
    return 0;
}

void cpugov_start(void) {
    const char *e = getenv("DSFLIP_CPUGOV");
    if (e && *e == '0') { dsflip_log("[cpugov] off (DSFLIP_CPUGOV=0)\n"); return; }
    verbose = getenv("DSFLIP_CPUGOV_LOG") != 0;
    char gov[32], avail[256];
    rd_str(POL "scaling_governor", gov, sizeof gov);
    if (strcmp(gov, "performance")) { dsflip_log("[cpugov] off: governor is %s, not performance\n", gov); return; }
    rd_str(POL "scaling_available_frequencies", avail, sizeof avail);
    for (char *s = strtok(avail, " "); s && nf < 24; s = strtok(0, " ")) freqs[nf++] = atoi(s);
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && freqs[j] < freqs[j - 1]; j--) { int x = freqs[j]; freqs[j] = freqs[j - 1]; freqs[j - 1] = x; }
    cur = rd_int(POL "scaling_max_freq");
    /* the hardware's top, not the current limit: a session that crashed before restore.sh must not cap this one */
    fmax_ = getenv("DSFLIP_CPU_MAX") ? atoi(getenv("DSFLIP_CPU_MAX")) : rd_int(POL "cpuinfo_max_freq");
    fmin_ = getenv("DSFLIP_CPU_MIN") ? atoi(getenv("DSFLIP_CPU_MIN")) : 816000;
    if (nf < 2 || cur <= 0 || access(POL "scaling_max_freq", W_OK)) { dsflip_log("[cpugov] off: no writable cpufreq\n"); return; }
    dsflip_log("[cpugov] on: %d..%d MHz, target %.0f%% load of the busiest DraStic thread\n", fmin_ / 1000, fmax_ / 1000, TARGET * 100);
    pthread_t th;
    if (!pthread_create(&th, 0, gov_thread, 0)) pthread_setname_np(th, "dsf-cpugov");
}
