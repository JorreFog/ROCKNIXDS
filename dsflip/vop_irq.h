/* VOP interrupt count and the storm check libdsflip uses when it takes the panels.
 * A controller stuck in an underrun raises ~84,000 interrupts/s (~60% of a core) until a
 * modeset that actually power-cycles the panels. A normal controller raises at most a few
 * interrupts in 30 ms; more than 100 in that window is the storm.
 * dsflip/vop_irq_test.c includes this same parser.
 */
#ifndef VOP_IRQ_H
#define VOP_IRQ_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sum of every CPU's count on the fe040000.vop line, or -1 if that line isn't there */
static long long vop_irq_count_fp(FILE *f) {
    if (!f) return -1;
    char line[512]; long long n = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, "fe040000.vop")) continue;
        char *p = strchr(line, ':'); if (!p) break;
        n = 0; p++;
        for (;;) { char *e; long long v = strtoll(p, &e, 10); if (e == p) break; n += v; p = e; }
        break;
    }
    return n;
}

static int vop_irq_storm(long long before, long long after) {
    return before >= 0 && after - before > 100;
}

#endif
