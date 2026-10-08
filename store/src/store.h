// store.h: the ROCKNIXDS Store, the app. It draws both screens on Döda Kvarter's platform layer (plat.h, gfx.h: the
// panels through KMS, a window, or headless for the tests) and leaves every real job to the package manager,
// device/rocknixds-store, which it runs in a thread and whose lines it shows (job.c).
#pragma once
#include "plat.h"

#define MAX_APPS 64

enum { TAB_GAMES, TAB_APPS, TAB_INSTALLED, TAB_COUNT };
enum { ST_INSTALL, ST_UPDATE, ST_INSTALLED, ST_UNAVAILABLE, ST_UNSUPPORTED, ST_NEEDS };

typedef struct {
    char id[40], kind[12], name[72], dev[56], have[24], latest[24], state_s[32], summary[200], genre[56], players[12];
    char desc[1600];
    long size;
    uint32_t accent;
    int state;
    Img icon, shot;                     /* empty (px 0) without pictures */
} App;

typedef struct {
    App apps[MAX_APPS];
    int n;
    long refreshed;                     /* when the newest versions were last fetched (0: never) */
} Catalog;

/* job.c: one job of the package manager at a time, in a thread */
enum { JOB_IDLE, JOB_RUNNING, JOB_DONE, JOB_FAILED };
void job_start(const char *args, const char *what);   /* args: "install bank", "refresh"... */
int job_state(char *step, int sn, char *what, int wn); /* JOB_*, the last step (or result, or why it failed) */
const char *job_args(void);
void job_ack(void);                     /* a finished job seen: back to idle */
void job_wait(void);                    /* (tests) until it ends */
const char *store_cli(void);            /* the package manager: RNDS_STORE_CLI, else beside the app */

/* catalog.c: rocknixds-store list, read into a Catalog, with the pictures refresh made */
void catalog_load(Catalog *c);
void catalog_free(Catalog *c);
int app_tab_has(const App *a, int tab);
int updates_count(const Catalog *c);
