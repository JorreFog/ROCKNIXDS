/* Host test for the CPU governor's clock decision. The RK3566 steps below are the ones the
 * 1.4 logs used; 816 MHz is in the table and must stay unused while the floor is 1104. */
#include <stdio.h>
#include <string.h>
#include "cpugov_decide.h"

static const int FREQS[] = { 816000, 1104000, 1416000, 1608000, 1800000, 1992000 };
static const int NF = 6;
static const int FMIN = 1104000;
static const int FMAX = 1992000;
static int failed;

static void expect_eq(const char *what, long long got, long long want) {
    if (got == want) return;
    fprintf(stderr, "FAIL %s: got %lld want %lld\n", what, got, want);
    failed = 1;
}

static void expect_str(const char *what, const char *got, const char *want) {
    if ((got == 0 && want == 0) || (got && want && !strcmp(got, want))) return;
    fprintf(stderr, "FAIL %s: got %s want %s\n", what, got ? got : "(null)", want ? want : "(null)");
    failed = 1;
}

static struct cpugov_decision decide(int cur, double umax, double peak, int dropped, double fps,
                                     int low, long long now, long long *bad, int *strikes) {
    return cpugov_decide(FREQS, NF, FMIN, FMAX, bad, strikes, cur, umax, peak, dropped, fps, low, now);
}

static void clear(long long *bad, int *strikes) {
    memset(bad, 0, sizeof(long long) * NF);
    memset(strikes, 0, sizeof(int) * NF);
}

int main(void) {
    long long bad[8];
    int strikes[8];
    struct cpugov_decision d;
    const long long t0 = 1000000000000LL;

    clear(bad, strikes);
    d = decide(1104000, 0.90, 0.20, 0, 60, 0, t0, bad, strikes);
    expect_eq("busy sets", d.set_khz, 1);
    expect_eq("busy clock", d.khz, 1416000);
    expect_str("busy why", d.why, "busy");
    expect_eq("busy low", d.low, 0);

    /* a heavy frame jumps the same way even when the average is comfortable */
    d = decide(1104000, 0.50, 0.96, 0, 60, 0, t0, bad, strikes);
    expect_eq("heavy clock", d.khz, 1416000);
    expect_str("heavy why", d.why, "heavy frame");

    /* fitting a higher clock is not itself a reason to go up */
    d = decide(1104000, 0.40, 0.95, 0, 60, 4, t0, bad, strikes);
    expect_eq("near-late no raise", d.set_khz, 0);
    expect_eq("near-late want", d.want, 1416000);
    expect_eq("near-late resets low", d.low, 0);

    /* load high enough to skip a step: 1104 -> 1608, and 1608 -> the top */
    d = decide(1104000, 0.99, 0.10, 0, 60, 0, t0, bad, strikes);
    expect_eq("busy skip", d.khz, 1608000);
    d = decide(1608000, 0.99, 0.10, 0, 60, 0, t0, bad, strikes);
    expect_eq("busy top", d.khz, 1992000);
    d = decide(1992000, 0.99, 0.10, 0, 60, 3, t0, bad, strikes);
    expect_eq("already top", d.set_khz, 0);
    expect_eq("already top low", d.low, 0);

    /* a drop steps up one, however light the frames look, and bans that clock */
    clear(bad, strikes);
    d = decide(1416000, 0.40, 0.40, 1, 60, 5, t0, bad, strikes);
    expect_eq("drop clock", d.khz, 1608000);
    expect_str("drop why", d.why, "dropped");
    expect_eq("drop low", d.low, 0);
    expect_eq("first ban s", (bad[2] - t0) / 1000000000LL, 30);
    expect_eq("first strike", strikes[2], 1);
    d = decide(1416000, 0.40, 0.40, 1, 60, 0, t0, bad, strikes);
    expect_eq("second ban s", (bad[2] - t0) / 1000000000LL, 60);
    strikes[2] = 4;
    d = decide(1416000, 0.40, 0.40, 1, 60, 0, t0, bad, strikes);
    expect_eq("fifth ban s", (bad[2] - t0) / 1000000000LL, 480);
    strikes[2] = 5;
    d = decide(1416000, 0.40, 0.40, 1, 60, 0, t0, bad, strikes);
    expect_eq("capped ban s", (bad[2] - t0) / 1000000000LL, 600);
    strikes[2] = 9;
    d = decide(1416000, 0.40, 0.40, 1, 60, 0, t0, bad, strikes);
    expect_eq("still capped", (bad[2] - t0) / 1000000000LL, 600);

    /* busy wins over a drop in the same window: raise for load, don't ban the clock */
    clear(bad, strikes);
    d = decide(1104000, 0.90, 0.20, 1, 60, 0, t0, bad, strikes);
    expect_str("busy over drop", d.why, "busy");
    expect_eq("busy over drop clock", d.khz, 1416000);
    expect_eq("no ban while busy", strikes[1], 0);
    d = decide(1104000, 0.50, 0.96, 1, 60, 0, t0, bad, strikes);
    expect_str("heavy over drop", d.why, "heavy frame");
    expect_eq("no ban while heavy", strikes[1], 0);

    /* below full speed, one step up, but not while paused (fps <= 5) or barely loaded */
    d = decide(1104000, 0.60, 0.10, 0, 50, 2, t0, bad, strikes);
    expect_eq("slow clock", d.khz, 1416000);
    expect_str("slow why", d.why, "slow");
    expect_eq("slow low", d.low, 0);
    d = decide(1416000, 0.60, 0.10, 0, 5, 4, t0, bad, strikes);
    expect_eq("paused no raise", d.set_khz, 0);
    expect_eq("paused resets low", d.low, 0);
    d = decide(1416000, 0.50, 0.10, 0, 40, 4, t0, bad, strikes);
    expect_eq("half load no raise", d.set_khz, 0);
    d = decide(1104000, 0.60, 0.10, 0, 58.5, 0, t0, bad, strikes);
    expect_eq("58.5 is not slow", d.set_khz, 0);

    /* eight light windows step down one; a dip, or a window that still fits, starts over */
    clear(bad, strikes);
    d = decide(1608000, 0.30, 0.20, 0, 60, 0, t0, bad, strikes);
    expect_eq("light 1", d.set_khz, 0);
    expect_eq("light low 1", d.low, 1);
    d = decide(1608000, 0.30, 0.20, 0, 60, 6, t0, bad, strikes);
    expect_eq("light 7 holds", d.set_khz, 0);
    expect_eq("light low 7", d.low, 7);
    d = decide(1608000, 0.30, 0.20, 0, 60, 7, t0, bad, strikes);
    expect_eq("light 8 steps", d.set_khz, 1);
    expect_eq("light clock", d.khz, 1416000);
    expect_str("light why", d.why, "light");
    expect_eq("light low reset", d.low, 0);
    d = decide(1608000, 0.30, 0.20, 0, 58.9, 7, t0, bad, strikes);
    expect_eq("58.9 no step down", d.set_khz, 0);
    expect_eq("58.9 resets low", d.low, 0);
    d = decide(1416000, 0.60, 0.10, 0, 60, 7, t0, bad, strikes);
    expect_eq("fits current", d.set_khz, 0);
    expect_eq("fits resets low", d.low, 0);
    expect_eq("fits want", d.want, 1416000);

    /* the clock a drop just banned is not the one we step back onto */
    clear(bad, strikes);
    bad[2] = t0 + 30000000000LL;             /* 1416 still banned */
    d = decide(1608000, 0.30, 0.20, 0, 60, 7, t0, bad, strikes);
    expect_eq("skip banned", d.set_khz, 1);
    expect_eq("stay above ban", d.khz, 1608000);
    bad[2] = t0;                             /* ban expires when now_ns == bad_until */
    d = decide(1608000, 0.30, 0.20, 0, 60, 7, t0, bad, strikes);
    expect_eq("ban expired", d.khz, 1416000);

    /* the floor is 1104: a light game at the floor stays there, 816 is not a choice */
    d = decide(1104000, 0.20, 0.10, 0, 60, 7, t0, bad, strikes);
    expect_eq("floor holds", d.set_khz, 0);
    expect_eq("floor clock", d.khz, 1104000);
    expect_eq("816 not indexed as a target", cpugov_step_down(FREQS, NF, FMIN, bad, t0, 1104000), 1104000);

    if (failed) return 1;
    printf("cpugov_decide_test ok\n");
    return 0;
}
