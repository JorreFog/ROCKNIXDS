// job.c: the package manager's jobs (device/rocknixds-store), one at a time in a thread, as Döda Kvarter's update.c
// runs its updater. Long jobs say each step (STEP ...) and end with DONE <version> or FAIL <why>; refresh ends with OK or
// OFFLINE. The app shows the step while it runs and the result once it's done.
#define _GNU_SOURCE
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int state = JOB_IDLE, joinable;
static char cur_args[160], cur_what[96], cur_step[160];
static pthread_t th;

const char *store_cli(void) {
    static char p[600];
    const char *e = getenv("RNDS_STORE_CLI");
    if (e && *e) return e;
    if (!p[0]) {
        char self[512]; ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
        if (n > 0) {
            self[n] = 0; char *sl = strrchr(self, '/'); if (sl) *sl = 0;
            snprintf(p, sizeof p, "%s/rocknixds-store", self);
        } else snprintf(p, sizeof p, "rocknixds-store");
    }
    return p;
}

static void trim(char *l) { size_t n = strlen(l); while (n && (l[n - 1] == '\n' || l[n - 1] == '\r' || l[n - 1] == ' ')) l[--n] = 0; }

static void *run(void *arg) {
    (void)arg;
    char cmd[900]; snprintf(cmd, sizeof cmd, "'%s' %s 2>/dev/null", store_cli(), cur_args);
    FILE *f = popen(cmd, "r");
    int st = JOB_FAILED; char res[160] = "It didn't run";
    if (f) {
        char l[300];
        while (fgets(l, sizeof l, f)) {
            trim(l);
            if (!strncmp(l, "STEP ", 5)) {
                pthread_mutex_lock(&mu); snprintf(cur_step, sizeof cur_step, "%s", l + 5); pthread_mutex_unlock(&mu);
            } else if (!strncmp(l, "DONE", 4)) { st = JOB_DONE; snprintf(res, sizeof res, "%s", l[4] ? l + 5 : ""); }
            else if (!strcmp(l, "OK")) { st = JOB_DONE; res[0] = 0; }
            else if (!strcmp(l, "OFFLINE")) { st = JOB_FAILED; snprintf(res, sizeof res, "No network"); }
            else if (!strncmp(l, "FAIL", 4)) { st = JOB_FAILED; snprintf(res, sizeof res, "%s", l[4] ? l + 5 : "It failed"); }
        }
        pclose(f);
    }
    pthread_mutex_lock(&mu);
    state = st;
    snprintf(cur_step, sizeof cur_step, "%s", res);
    pthread_mutex_unlock(&mu);
    plat_log("job %s: %s %s", cur_args, st == JOB_DONE ? "done" : "failed", res);
    return 0;
}

void job_start(const char *args, const char *what) {
    pthread_mutex_lock(&mu);
    int busy = state == JOB_RUNNING;
    pthread_mutex_unlock(&mu);
    if (busy) return;
    if (joinable) { pthread_join(th, 0); joinable = 0; }
    snprintf(cur_args, sizeof cur_args, "%s", args);
    snprintf(cur_what, sizeof cur_what, "%s", what);
    snprintf(cur_step, sizeof cur_step, "Starting");
    state = JOB_RUNNING;
    plat_log("job %s", args);
    if (pthread_create(&th, 0, run, 0) == 0) joinable = 1;
    else { state = JOB_FAILED; snprintf(cur_step, sizeof cur_step, "No thread"); }
}

int job_state(char *step, int sn, char *what, int wn) {
    pthread_mutex_lock(&mu);
    int st = state;
    if (step) snprintf(step, (size_t)sn, "%s", cur_step);
    if (what) snprintf(what, (size_t)wn, "%s", cur_what);
    pthread_mutex_unlock(&mu);
    return st;
}

const char *job_args(void) { return cur_args; }

void job_ack(void) {
    pthread_mutex_lock(&mu);
    if (state != JOB_RUNNING) state = JOB_IDLE;
    pthread_mutex_unlock(&mu);
}

void job_wait(void) {
    if (joinable) { pthread_join(th, 0); joinable = 0; }
}
