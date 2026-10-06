// save.h: settings, high scores and the run in progress on disk.
#pragma once
#include <stdint.h>

#define MAX_SCORES 10
typedef struct {
    char name[16];                      /* three initials (UTF-8: Å Ä Ö take two bytes) */
    int round, kills, kr, secs, season;
    uint64_t seed;
    long long date;
    char town[32];
} Score;
extern Score scores[MAX_SCORES];
extern int nscores;

void settings_load(void);
void settings_save(void);
void scores_load(void);
void scores_save(void);
int score_rank(const Score *s);
void score_insert(const Score *s, int at);

/* the run in progress (run.sav): written when you quit and between rounds, deleted when the run ends */
int run_save(void);                     /* now (quitting); 0 when written */
void run_autosave(void);                /* between rounds: copied now, written by a helper thread */
int run_saved(void);                    /* a saved run is there, from this build */
int run_peek(char *town, int n, int *round);   /* which town and round it is in; 0 when there is one */
int run_load(void);                     /* into G, the town painted again; 0 when loaded */
void run_discard(void);
