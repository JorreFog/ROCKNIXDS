// menu.h: the screens around a run, and the application's state.
#pragma once
#include "save.h"

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_GAMEOVER, ST_NAME, ST_SCORES, ST_SETTINGS, ST_HOWTO };
typedef struct {
    int state, sel, page, settings_from, howto_from;
    float t;
    int name_pos, letters[3], rank;
    Score last;
    int quit;
    uint64_t seed_override;
    int bot_w, bot_h;
    int has_save, save_checked, save_round, last_rstate;   /* a saved run on the title */
    char save_town[32];
} App;
extern App A;

void app_update(const Input *in, const Input *prev, float dt);
void app_new_run(void);                 /* a new run now (--start) */
int app_run_in_progress(void);          /* saved when the program is quit */
void app_render(Surf *top, Surf *bot);
void title_top(Surf *s);
