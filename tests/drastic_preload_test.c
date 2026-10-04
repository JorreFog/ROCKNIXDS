/* Host check of the LD_PRELOAD filter drastic-launch runs before exec. */
#include "drastic-launch.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        failed = 1;
    }
}

static int lists_equal(char **got, char **want)
{
    for (;;) {
        if (!*got || !*want) return !*got && !*want;
        if (strcmp(*got, *want) != 0) return 0;
        got++;
        want++;
    }
}

static void check_drop(const char *name, char **env, char **want)
{
    drop_preload(env);
    expect(lists_equal(env, want), name);
}

int main(void)
{
    expect(is_preload("LD_PRELOAD="), "empty preload is still a preload");
    expect(is_preload("LD_PRELOAD=/usr/lib/libdrastouch.so"), "libdrastouch entry");
    expect(!is_preload("LD_PRELOAD"), "missing equals stays");
    expect(!is_preload("ld_preload=/usr/lib/libdrastouch.so"), "match is case sensitive");
    expect(!is_preload(" LD_PRELOAD=/usr/lib/libdrastouch.so"), "leading space stays");
    expect(!is_preload("LD_PRELOAD2=/usr/lib/libdrastouch.so"), "a longer name stays");
    expect(!is_preload("LD_LIBRARY_PATH=/tmp"), "library path stays");
    expect(!is_preload(""), "empty entry stays");
    expect(!is_preload("DSHOOK_MIC_THRESH=50"), "mic threshold stays");

    char *mid[] = {"PATH=/bin", "LD_PRELOAD=/usr/lib/libdrastouch.so", "DSHOOK_MIC_THRESH=50", "HOME=/root", 0};
    char *mid_want[] = {"PATH=/bin", "DSHOOK_MIC_THRESH=50", "HOME=/root", 0};
    check_drop("preload in the middle", mid, mid_want);

    char *first[] = {"LD_PRELOAD=/usr/lib/libdrastouch.so", "PATH=/bin", 0};
    char *first_want[] = {"PATH=/bin", 0};
    check_drop("preload first", first, first_want);

    char *last[] = {"PATH=/bin", "LD_PRELOAD=/usr/lib/libdrastouch.so", 0};
    char *last_want[] = {"PATH=/bin", 0};
    check_drop("preload last", last, last_want);

    char *both[] = {"LD_PRELOAD=/a", "PATH=/bin", "LD_PRELOAD=/b", 0};
    char *both_want[] = {"PATH=/bin", 0};
    check_drop("two preload entries", both, both_want);

    char *only[] = {"LD_PRELOAD=/usr/lib/libdrastouch.so", 0};
    char *only_want[] = {0};
    check_drop("nothing but preload", only, only_want);

    char *none[] = {"PATH=/bin", "LD_LIBRARY_PATH=/tmp", 0};
    char *none_want[] = {"PATH=/bin", "LD_LIBRARY_PATH=/tmp", 0};
    check_drop("no preload to drop", none, none_want);

    char *empty[] = {0};
    char *empty_want[] = {0};
    check_drop("empty environment", empty, empty_want);

    /* The kept strings are the same pointers: launch() passes them to execve. */
    char path[] = "PATH=/bin";
    char pre[] = "LD_PRELOAD=/usr/lib/libdrastouch.so";
    char *idents[] = {path, pre, 0};
    drop_preload(idents);
    expect(idents[0] == path, "kept entry is the original pointer");
    expect(idents[1] == 0, "terminator moved down");

    if (failed) return 1;
    return 0;
}
