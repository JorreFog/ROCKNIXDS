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
} App;
extern App A;

void app_update(const Input *in, const Input *prev, float dt);
void app_render(Surf *top, Surf *bot);
void title_top(Surf *s);
