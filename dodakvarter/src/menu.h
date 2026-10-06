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
    int counted;                        /* the run in G is over and counted: no save left, stats and score kept */
    int unnamed;                        /* its high score waits for initials (kept with the letters so far on quitting) */
    int confirm;                        /* NEW RUN or TODAY'S TOWN pressed once: again, and the saved run goes */
    int then;                           /* the run chosen on the title, to start after the saved one's game over */
} App;
extern App A;

void app_update(const Input *in, const Input *prev, float dt);
void app_new_run(void);                 /* a new run now (--start) */
int app_run_in_progress(void);          /* a run is going on */
void app_quit(void);                    /* the program is quitting: the run saved, a score kept */
void app_render(Surf *top, Surf *bot);
void title_top(Surf *s);
