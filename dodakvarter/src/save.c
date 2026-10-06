// save.c: settings and the high score list, as plain text in the data directory (settings.txt, scores.txt).
#include "game.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>

Settings S = { .volume = 80, .music = 1, .shake = 1, .assist = 2, .scheme = 0, .season = 0, .lang = LANG_EN, .swap_ab = 0, .show_fps = 0, .touch_aim = 1 };
Score scores[MAX_SCORES]; int nscores;

static void path(char *out, size_t n, const char *file) { snprintf(out, n, "%s/%s", plat_data_dir(), file); }

void settings_load(void) {
    char p[600]; path(p, sizeof p, "settings.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    char k[64]; int v;
    while (fscanf(f, "%63s %d", k, &v) == 2) {
        if (!strcmp(k, "volume")) S.volume = CLAMP(v, 0, 100);
        else if (!strcmp(k, "music")) S.music = !!v;
        else if (!strcmp(k, "shake")) S.shake = !!v;
        else if (!strcmp(k, "assist")) S.assist = CLAMP(v, 0, 2);
        else if (!strcmp(k, "scheme")) S.scheme = CLAMP(v, 0, 1);
        else if (!strcmp(k, "season")) S.season = CLAMP(v, 0, 3);
        else if (!strcmp(k, "lang")) S.lang = CLAMP(v, 0, 1);
        else if (!strcmp(k, "swap_ab")) S.swap_ab = !!v;
        else if (!strcmp(k, "show_fps")) S.show_fps = !!v;
        else if (!strcmp(k, "touch_aim")) S.touch_aim = !!v;
    }
    fclose(f);
}

void settings_save(void) {
    char p[600], t[610]; path(p, sizeof p, "settings.txt"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    fprintf(f, "volume %d\nmusic %d\nshake %d\nassist %d\nscheme %d\nseason %d\nlang %d\nswap_ab %d\nshow_fps %d\ntouch_aim %d\n",
            S.volume, S.music, S.shake, S.assist, S.scheme, S.season, S.lang, S.swap_ab, S.show_fps, S.touch_aim);
    fclose(f);
    rename(t, p);
}

/* one score a line: initials round kills kr seconds season seed date town */
void scores_load(void) {
    char p[600]; path(p, sizeof p, "scores.txt");
    nscores = 0;
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[256];
    while (nscores < MAX_SCORES && fgets(line, sizeof line, f)) {
        Score *s = &scores[nscores];
        memset(s, 0, sizeof *s);
        unsigned long long seed; long long date;
        if (sscanf(line, "%15s %d %d %d %d %d %llu %lld %31s", s->name, &s->round, &s->kills, &s->kr, &s->secs, &s->season, &seed, &date, s->town) >= 8) {
            s->seed = seed; s->date = date;
            for (char *c = s->town; *c; c++) if (*c == '_') *c = ' ';
            nscores++;
        }
    }
    fclose(f);
}

void scores_save(void) {
    char p[600], t[610]; path(p, sizeof p, "scores.txt"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    for (int i = 0; i < nscores; i++) {
        Score *s = &scores[i];
        char town[32]; snprintf(town, sizeof town, "%s", s->town[0] ? s->town : "-");
        for (char *c = town; *c; c++) if (*c == ' ') *c = '_';
        fprintf(f, "%s %d %d %d %d %d %llu %lld %s\n", s->name[0] ? s->name : "???", s->round, s->kills, s->kr, s->secs, s->season,
                (unsigned long long)s->seed, (long long)s->date, town);
    }
    fclose(f);
    rename(t, p);
}

static int better(const Score *a, const Score *b) {
    if (a->round != b->round) return a->round > b->round;
    if (a->kills != b->kills) return a->kills > b->kills;
    return a->kr > b->kr;
}
/* where this score would go in the list, -1 if it doesn't make it */
int score_rank(const Score *s) {
    for (int i = 0; i < nscores; i++) if (better(s, &scores[i])) return i;
    return nscores < MAX_SCORES ? nscores : -1;
}
void score_insert(const Score *s, int at) {
    if (at < 0) return;
    if (nscores < MAX_SCORES) nscores++;
    for (int i = nscores - 1; i > at; i--) scores[i] = scores[i - 1];
    scores[at] = *s;
    scores_save();
}
