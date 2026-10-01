/* Host test for the display-controller interrupt parser. A miss leaves a storm burning
 * ~60% of a core until reboot; a false hit power-cycles both panels. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vop_irq.h"

static int failed;

static void expect_eq(const char *what, long long got, long long want) {
    if (got == want) return;
    fprintf(stderr, "FAIL %s: got %lld want %lld\n", what, got, want);
    failed = 1;
}

static long long parse(const char *text) {
    size_t n = strlen(text);
    char *buf = malloc(n + 1);
    memcpy(buf, text, n + 1);
    FILE *f = fmemopen(buf, n, "r");
    long long got = vop_irq_count_fp(f);
    if (f) fclose(f);
    free(buf);
    return got;
}

int main(void) {
    const char *proc =
        "           CPU0       CPU1       CPU2       CPU3\n"
        "  12:       1000        200         30          4     GICv3  12 Level     arch_timer\n"
        "  45:         10         20          0          1     GICv3  45 Level     fe040000.vop\n"
        "  46:          9          9          9          9     GICv3  46 Level     fe040000.vop\n";
    /* 10+20+0+1. The irq number before the colon, and the GICv3 number, are not counts.
     * A later vop line must not replace the first sum. */
    expect_eq("four cpus", parse(proc), 31);
    expect_eq("missing", parse("  12:  1  2  3  4  GICv3  12 Level  arch_timer\n"), -1);
    FILE *empty = fopen("/dev/null", "r");
    expect_eq("empty", vop_irq_count_fp(empty), -1);
    if (empty) fclose(empty);
    expect_eq("null file", vop_irq_count_fp(0), -1);
    expect_eq("no colon", parse("fe040000.vop without a colon\n"), -1);
    expect_eq("zero counts", parse("  45:     GICv3  fe040000.vop\n"), 0);
    expect_eq("no newline", parse("  7:  100  5  fe040000.vop"), 105);

    expect_eq("unreadable", vop_irq_storm(-1, 10000), 0);
    expect_eq("exactly 100", vop_irq_storm(0, 100), 0);
    expect_eq("101", vop_irq_storm(0, 101), 1);
    expect_eq("normal window", vop_irq_storm(5000, 5003), 0);
    expect_eq("storm", vop_irq_storm(1000, 3520), 1);          /* 2520 in 30 ms, ~84k/s */
    expect_eq("counter reset", vop_irq_storm(5000, 100), 0);

    if (failed) return 1;
    printf("vop_irq_test ok\n");
    return 0;
}
