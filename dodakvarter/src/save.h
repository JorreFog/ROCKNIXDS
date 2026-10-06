// save.h: settings and high scores on disk.
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
