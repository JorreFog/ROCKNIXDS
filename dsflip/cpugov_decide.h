/* Clock decision for cpugov.c. The governor thread and dsflip/cpugov_decide_test.c both include
 * this, so a change to when the clock steps is tested on the host before it ships in libdsflip.so.
 *
 * Up at once when a window needs it (busy, a heavy frame, a drop, or below full speed). Down one
 * step only after DOWN_AFTER windows in a row that would all fit lower, and never onto a clock
 * that dropped a frame recently.
 */
#ifndef CPUGOV_DECIDE_H
#define CPUGOV_DECIDE_H

#define TARGET 0.72                          /* load the busiest thread may reach at the chosen clock */
#define HIGH 0.85                            /* above this at the current clock: go up now */
#define PEAK_TARGET 0.80                     /* the heaviest frame in a window may take this much of 16.7 ms */
#define PEAK_HIGH 0.95                       /* a frame this close to late: go up now */
#define BAD_FOR_S 30                         /* a clock that dropped a frame isn't tried again for this long, doubled
                                               for each further time it drops one (up to BAD_MAX_S) */
#define BAD_MAX_S 600
#define DOWN_AFTER 8                         /* windows (2 s) in a row that fit a lower clock before stepping down */

struct cpugov_decision {
    int set_khz;                             /* 1: call set_clock (it no-ops when khz is already current) */
    int khz;
    const char *why;
    int low;                                 /* consecutive light windows */
    int want;                                /* clock the load fits; the log line prints this */
};

/* the lowest available clock >= khz, within the bounds */
static int cpugov_fit(const int *freqs, int nf, int fmin, int fmax, double khz) {
    for (int i = 0; i < nf; i++) if (freqs[i] >= fmin && freqs[i] <= fmax && freqs[i] >= khz) return freqs[i];
    return fmax;
}

static int cpugov_idx(const int *freqs, int nf, int khz) {
    for (int i = 0; i < nf; i++) if (freqs[i] == khz) return i;
    return -1;
}

/* one step down, unless that clock dropped a frame recently: stepping down to it again, dropping, and going back up
 * was where most of the remaining drops came from (measured: ds-crisp 0.13/s bouncing 1416 <-> 1608) */
static int cpugov_step_down(const int *freqs, int nf, int fmin, const long long *bad_until, long long now_ns, int khz) {
    int best = khz;
    for (int i = 0; i < nf; i++) if (freqs[i] < khz && freqs[i] >= fmin && (best == khz || freqs[i] > best)) best = freqs[i];
    int b = cpugov_idx(freqs, nf, best);
    return b >= 0 && bad_until[b] > now_ns ? khz : best;
}

static struct cpugov_decision cpugov_decide(const int *freqs, int nf, int fmin, int fmax,
                                            long long *bad_until, int *strikes,
                                            int cur, double umax, double peak, int dropped, double fps,
                                            int low, long long now_ns) {
    double need = cur * umax / TARGET, needp = cur * peak / PEAK_TARGET;
    int want = cpugov_fit(freqs, nf, fmin, fmax, need > needp ? need : needp);
    const char *why = 0;
    if (umax > HIGH) why = "busy";
    else if (peak > PEAK_HIGH) why = "heavy frame";
    else if (dropped) {
        /* any drop, however light the frames look: at low clocks frames were dropped with the main thread's
         * heaviest frame at only ~40% of a refresh (measured at 816 MHz in a still HeartGold dialog) */
        why = "dropped"; if (want <= cur) want = cpugov_fit(freqs, nf, fmin, fmax, cur + 1);
        int c = cpugov_idx(freqs, nf, cur);
        if (c >= 0) {
            long long ban = (long long)BAD_FOR_S << (strikes[c] < 5 ? strikes[c] : 5);
            if (ban > BAD_MAX_S) ban = BAD_MAX_S;
            bad_until[c] = now_ns + ban * 1000000000LL; strikes[c]++;
        }
    }
    else if (fps < 58.5 && fps > 5 && umax > 0.5) { why = "slow"; if (want <= cur) want = cpugov_fit(freqs, nf, fmin, fmax, cur + 1); }
    struct cpugov_decision d;
    d.set_khz = 0; d.khz = cur; d.why = 0; d.low = 0; d.want = want;
    if (why && want > cur) { d.set_khz = 1; d.khz = want; d.why = why; d.low = 0; }
    else if (want < cur && fps >= 59) {      /* never while below full speed */
        if (++low >= DOWN_AFTER) { d.set_khz = 1; d.khz = cpugov_step_down(freqs, nf, fmin, bad_until, now_ns, cur); d.why = "light"; d.low = 0; }
        else d.low = low;
    }
    else d.low = 0;
    return d;
}

#endif
