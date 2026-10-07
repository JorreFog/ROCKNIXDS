// update.c: the game's own updates, without a ROCKNIXDS release. The work is done by device/update.sh (curl, sha256sum,
// the swap of the files), which the session names in DK_UPDATER; the game runs it in a thread and shows what it says.
// "check" answers one line (UPDATE <version>, UPTODATE <version> or OFFLINE); "install" says each step (STEP ...) and
// ends with DONE <version> or FAIL <why>. After DONE the game quits with status 75 and the session starts it again,
// the new one. Without DK_UPDATER (a computer, the tests) there are no updates.
#include "game.h"
#include "update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int state = UPD_IDLE, busy, joinable;
static char ver[24], step[48];
static pthread_t th;

static void set(int st, const char *v, const char *s) {
    pthread_mutex_lock(&mu);
    state = st;
    if (v) snprintf(ver, sizeof ver, "%s", v);
    if (s) snprintf(step, sizeof step, "%s", s);
    pthread_mutex_unlock(&mu);
}

static void trim(char *l) { size_t n = strlen(l); while (n && (l[n - 1] == '\n' || l[n - 1] == '\r' || l[n - 1] == ' ')) l[--n] = 0; }

static void *run(void *arg) {
    int install = arg != 0;
    const char *u = getenv("DK_UPDATER");
    char cmd[700]; snprintf(cmd, sizeof cmd, "'%s' %s 2>/dev/null", u, install ? "install" : "check");
    FILE *f = popen(cmd, "r");
    /* the steps show as they come; the answer only once the updater is done, with busy cleared in the same step,
       so a press on what it says always finds the updater free */
    int st = install ? UPD_FAILED : UPD_OFFLINE; char v[24] = "", why[48] = "";
    snprintf(why, sizeof why, "%s", install ? "the update didn't run" : "");
    if (f) {
        char l[160];
        while (fgets(l, sizeof l, f)) {
            trim(l);
            if (!strncmp(l, "UPDATE ", 7)) { st = UPD_AVAILABLE; snprintf(v, sizeof v, "%s", l + 7); }
            else if (!strncmp(l, "UPTODATE", 8)) { st = UPD_LATEST; snprintf(v, sizeof v, "%s", l[8] ? l + 9 : ""); }
            else if (!strncmp(l, "OFFLINE", 7)) st = UPD_OFFLINE;
            else if (!strncmp(l, "STEP ", 5)) set(UPD_INSTALLING, 0, l + 5);
            else if (!strncmp(l, "DONE", 4)) { st = UPD_DONE; snprintf(v, sizeof v, "%s", l[4] ? l + 5 : ""); }
            else if (!strncmp(l, "FAIL", 4)) { st = UPD_FAILED; snprintf(why, sizeof why, "%s", l[4] ? l + 5 : "the update failed"); }
        }
        pclose(f);
    }
    pthread_mutex_lock(&mu);
    state = st;
    if (v[0]) snprintf(ver, sizeof ver, "%s", v);
    if (st == UPD_FAILED) snprintf(step, sizeof step, "%s", why);
    busy = 0;
    pthread_mutex_unlock(&mu);
    plat_log("update %s: state %d %s %s", install ? "install" : "check", st, v, why);
    return 0;
}

static void start(int install) {
    pthread_mutex_lock(&mu);
    int b = busy;
    pthread_mutex_unlock(&mu);
    if (b || !update_supported()) return;
    if (joinable) { pthread_join(th, 0); joinable = 0; }
    busy = 1;
    set(install ? UPD_INSTALLING : UPD_CHECKING, 0, install ? "Starting" : 0);
    if (pthread_create(&th, 0, run, install ? (void *)1 : 0) == 0) joinable = 1;
    else { busy = 0; set(install ? UPD_FAILED : UPD_OFFLINE, 0, "no thread"); }
}

int update_supported(void) { const char *u = getenv("DK_UPDATER"); return u && *u; }
void update_check(void) { if (state != UPD_INSTALLING && state != UPD_DONE) start(0); }
void update_install(void) { if (state == UPD_AVAILABLE || state == UPD_FAILED) start(1); }
int update_state(char *v, int vn, char *s, int sn) {
    pthread_mutex_lock(&mu);
    int st = update_supported() ? state : UPD_UNSUPPORTED;
    if (v) snprintf(v, (size_t)vn, "%s", ver);
    if (s) snprintf(s, (size_t)sn, "%s", step);
    pthread_mutex_unlock(&mu);
    return st;
}
