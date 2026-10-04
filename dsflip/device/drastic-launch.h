/* LD_PRELOAD filter for drastic-launch. ROCKNIX's start_drastic.sh runs the launcher with
 * LD_PRELOAD=libdrastouch.so; that constructor crashes any process that isn't DraStic when the mic
 * sensitivity is above 0 (issue 26). The static launcher drops those entries, then execs the script.
 * Host tests compile this header; the aarch64 launcher includes it too. No libc.
 */
#ifndef DRASTIC_LAUNCH_H
#define DRASTIC_LAUNCH_H

static int is_preload(const char *e)
{
    const char *k = "LD_PRELOAD=";
    while (*k) if (*e++ != *k++) return 0;
    return 1;
}

/* Rewrite envp in place: drop every LD_PRELOAD= entry, keep order, keep the NULL terminator. */
static void drop_preload(char **envp)
{
    char **o = envp;
    for (char **e = envp; *e; e++) if (!is_preload(*e)) *o++ = *e;
    *o = 0;
}

#endif
