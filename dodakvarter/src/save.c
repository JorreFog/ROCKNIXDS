// save.c: settings and the high score list, as plain text in the data directory (settings.txt, scores.txt), and the
// run in progress (run.sav) so that quitting doesn't end it.
#include "game.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <pthread.h>

Settings S = { .volume = 80, .music = 1, .shake = 1, .assist = 2, .scheme = 0, .season = 0, .lang = LANG_EN, .swap_ab = 0, .show_fps = 0, .touch_aim = 1, .diff = DIFF_MEDIUM, .radio = 1,
               .bind = { { B_A, B_B, B_Y, B_X, B_R1, B_L1, B_L2, -1, B_SELECT, B_R2 }, { -1, B_R1, B_SELECT, B_R2, -1, B_L1, B_L2, -1, -1, -1 } } };

/* ---------------------------------------------------------------- the buttons */
static const int DEFAULT_BIND[LAYOUT_COUNT][ACT_COUNT] = { { B_A, B_B, B_Y, B_X, B_R1, B_L1, B_L2, -1, B_SELECT, B_R2 }, { -1, B_R1, B_SELECT, B_R2, -1, B_L1, B_L2, -1, -1, -1 } };
static const char *BIND_KEY[LAYOUT_COUNT][ACT_COUNT] = {
    { "c_fire", "c_use", "c_reload", "c_swap", "c_knife", "c_sprint", "c_grenade", "c_item", "c_next", "c_lock" },
    { 0, "t_use", "t_reload", "t_swap", 0, "t_sprint", "t_grenade", 0, 0, 0 },
};
void binds_default(int layout) { memcpy(S.bind[layout], DEFAULT_BIND[layout], sizeof S.bind[layout]); }
int bind_allowed(int layout, int b) {
    if (layout == LAYOUT_TWIN) return b == B_L1 || b == B_R1 || b == B_L2 || b == B_R2 || b == B_SELECT;   /* ABXY fire */
    return b == B_A || b == B_B || b == B_X || b == B_Y || b == B_L1 || b == B_R1 || b == B_L2 || b == B_R2 || b == B_SELECT;
}
int binds_valid(int layout) {
    for (int a = 0; a < ACT_COUNT; a++) {
        int b = S.bind[layout][a];
        if (DEFAULT_BIND[layout][a] < 0) { if (b >= 0) return 0; continue; }   /* an action this layout does without stays without */
        if (b < 0 || !bind_allowed(layout, b)) return 0;
        for (int o = 0; o < a; o++) if (S.bind[layout][o] == b) return 0;
    }
    return 1;
}
int bind_set(int layout, int act, int b) {
    if (DEFAULT_BIND[layout][act] < 0 || !bind_allowed(layout, b)) return 0;
    for (int o = 0; o < ACT_COUNT; o++) if (o != act && S.bind[layout][o] == b) S.bind[layout][o] = S.bind[layout][act];
    S.bind[layout][act] = b;
    return 1;
}
int bind_of(int act) { return S.bind[S.scheme == 1 ? LAYOUT_TWIN : LAYOUT_CLASSIC][act]; }
const char *btn_name(int b) {
    static const char *n[B_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "L2", "R2", "SEL", "START", "MENU", "L3" };
    return b >= 0 && b < B_COUNT ? n[b] : "-";
}
const char *act_name(int act) {
    static const char *n[ACT_COUNT] = { "Fire", "Use", "Reload", "Swap weapon", "Knife", "Sprint", "Grenade", "Use item", "Next item (hold: use)", "Lock on (hold)" };
    return act >= 0 && act < ACT_COUNT ? n[act] : "";
}
Score scores[MAX_SCORES]; int nscores;

static void path(char *out, size_t n, const char *file) { snprintf(out, n, "%s/%s", plat_data_dir(), file); }

void settings_load(void) {
    char p[600]; path(p, sizeof p, "settings.txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    char k[64]; int v, binds = 0;
    while (fscanf(f, "%63s %d", k, &v) == 2) {
        int bound = 0;
        for (int l = 0; l < LAYOUT_COUNT && !bound; l++)
            for (int a = 0; a < ACT_COUNT && !bound; a++)
                if (BIND_KEY[l][a] && !strcmp(k, BIND_KEY[l][a])) { S.bind[l][a] = v; bound = 1; binds++; }
        if (bound) continue;
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
        else if (!strcmp(k, "effects")) S.effects = CLAMP(v, 0, 2);
        else if (!strcmp(k, "difficulty")) S.diff = CLAMP(v, 0, DIFF_COUNT - 1);
        else if (!strcmp(k, "radio")) S.radio = !!v;
        else if (!strcmp(k, "radio_heard")) S.radio_heard = !!v;
        else if (!strcmp(k, "radio_tip")) S.radio_tip = MAX(0, v);
    }
    fclose(f);
    for (int l = 0; l < LAYOUT_COUNT; l++) if (!binds_valid(l)) binds_default(l);   /* a file edited by hand, or an older one */
    /* 0.1's "Swap A/B" also swapped fire and use: a player who had it on keeps that (the option is the menus' now) */
    if (!binds && S.swap_ab) { S.bind[LAYOUT_CLASSIC][ACT_FIRE] = B_B; S.bind[LAYOUT_CLASSIC][ACT_USE] = B_A; }
}

void settings_save(void) {
    char p[600], t[610]; path(p, sizeof p, "settings.txt"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    fprintf(f, "volume %d\nmusic %d\nshake %d\nassist %d\nscheme %d\nseason %d\nlang %d\nswap_ab %d\nshow_fps %d\ntouch_aim %d\neffects %d\ndifficulty %d\nradio %d\nradio_heard %d\nradio_tip %d\n",
            S.volume, S.music, S.shake, S.assist, S.scheme, S.season, S.lang, S.swap_ab, S.show_fps, S.touch_aim, S.effects, S.diff, S.radio, S.radio_heard, S.radio_tip);
    for (int l = 0; l < LAYOUT_COUNT; l++)
        for (int a = 0; a < ACT_COUNT; a++)
            if (BIND_KEY[l][a]) fprintf(f, "%s %d\n", BIND_KEY[l][a], S.bind[l][a]);
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
        s->diff = DIFF_HARD;                            /* (0.1 had no difficulty: its game is hard) */
        unsigned long long seed; long long date;
        if (sscanf(line, "%15s %d %d %d %d %d %llu %lld %31s %d %d", s->name, &s->round, &s->kills, &s->kr, &s->secs, &s->season, &seed, &date, s->town, &s->daily, &s->diff) >= 8) {
            s->diff = CLAMP(s->diff, 0, DIFF_COUNT - 1);
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
        fprintf(f, "%s %d %d %d %d %d %llu %lld %s %d %d\n", s->name[0] ? s->name : "???", s->round, s->kills, s->kr, s->secs, s->season,
                (unsigned long long)s->seed, (long long)s->date, town, s->daily, s->diff);
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

/* ---------------------------------------------------------------- all the runs, added up */
Stats ST;
static const char *STAT_KEYS[] = { "runs", "kills", "rounds", "best_round", "secs", "kr", "boxes", "downs", "crits", "dailies", "bosses" };
void stats_load(void) {
    char p[600]; path(p, sizeof p, "stats.txt");
    memset(&ST, 0, sizeof ST);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char k[64]; long long v; int *fields = (int *)&ST;
    while (fscanf(f, "%63s %lld", k, &v) == 2)
        for (int i = 0; i < (int)ARRAY_LEN(STAT_KEYS); i++) if (!strcmp(k, STAT_KEYS[i])) fields[i] = (int)CLAMP(v, 0, 2000000000LL);
    fclose(f);
}
static void stats_save(void) {
    char p[600], t[610]; path(p, sizeof p, "stats.txt"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    const int *fields = (const int *)&ST;
    for (int i = 0; i < (int)ARRAY_LEN(STAT_KEYS); i++) fprintf(f, "%s %d\n", STAT_KEYS[i], fields[i]);
    fclose(f);
    rename(t, p);
}
void stats_add_run(void) {
    const Player *p = &G->p;
    ST.runs++; ST.kills += p->kills; ST.rounds += MAX(0, G->round - 1); ST.best_round = MAX(ST.best_round, G->round);
    ST.secs += (int)G->time; ST.kr += p->kr_total; ST.boxes += p->boxes; ST.downs += p->downs; ST.crits += p->crits;
    if (G->daily) ST.dailies++;
    ST.bosses += G->boss_kills;
    stats_save();
}

/* ---------------------------------------------------------------- the run in progress
 * The Game struct as it stands: plain data but for the painted town (painted again on loading) and where each zombie
 * was last drawn. A header ties it to this build's layout; a checksum catches a file cut short. Written to a temporary
 * file, synced, then renamed over the old one: a power cut leaves the old save or the new one, never half of one. */
typedef struct { char magic[8]; uint32_t layout, size, sum, pad; } RunHeader;

static uint32_t fnv(const void *p, size_t n, uint32_t h) { const uint8_t *b = p; while (n--) h = (h ^ *b++) * 16777619u; return h; }
static uint32_t layout_id(void) {
    size_t v[] = { sizeof(Game), sizeof(Zombie), sizeof(Player), sizeof(Item), sizeof(Inter), sizeof(Prop), sizeof(Tile),
                   sizeof(Weapon), sizeof(Armor), offsetof(Game, p), offsetof(Game, z), offsetof(Game, items), MAPW_MAX, MAPH_MAX,
                   W_COUNT, A_COUNT, C_COUNT, PK_COUNT, ZT_COUNT, PU_COUNT, IT_COUNT, P_COUNT };
    uint32_t h = fnv(v, sizeof v, 2166136261u);
    return fnv(DK_VERSION, strlen(DK_VERSION), h);
}

static int write_run(const Game *g) {
    char p[600], t[610]; path(p, sizeof p, "run.sav"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb");
    if (!f) return -1;
    RunHeader h; memset(&h, 0, sizeof h);
    memcpy(h.magic, "DKRUN01", 8); h.layout = layout_id(); h.size = sizeof(Game); h.sum = fnv(g, sizeof(Game), 2166136261u);
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(g, sizeof(Game), 1, f) == 1 && fflush(f) == 0;
    if (ok) fsync(fileno(f));
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(t, p) != 0) { unlink(t); return -1; }
    return 0;
}

static pthread_t saver; static int saver_on; static Game *saver_buf;
static void saver_wait(void) { if (saver_on) { pthread_join(saver, 0); saver_on = 0; } }
static void *saver_main(void *g) { write_run(g); return 0; }

int run_save(void) {
    saver_wait();
    int r = write_run(G);
    plat_log(r ? "could not save the run" : "saved the run (round %d)", G->round);
    return r;
}
void run_autosave(void) {
    saver_wait();
    if (!saver_buf && !(saver_buf = malloc(sizeof(Game)))) return;
    memcpy(saver_buf, G, sizeof(Game));
    if (pthread_create(&saver, 0, saver_main, saver_buf) == 0) saver_on = 1;
    else write_run(saver_buf);
}
void run_discard(void) {
    saver_wait();
    char p[600]; path(p, sizeof p, "run.sav");
    unlink(p);
}

/* reads and checks the file; into `into` when given */
static int read_run(Game *into) {
    char p[600]; path(p, sizeof p, "run.sav");
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    RunHeader h; int ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "DKRUN01", 8) && h.layout == layout_id() && h.size == sizeof(Game);
    if (ok && into) ok = fread(into, sizeof(Game), 1, f) == 1 && fnv(into, sizeof(Game), 2166136261u) == h.sum;
    fclose(f);
    return ok ? 0 : -1;
}
int run_saved(void) { saver_wait(); return read_run(0) == 0; }
int run_peek(char *town, int n, int *round) {
    saver_wait();
    Game *g = malloc(sizeof(Game));
    int r = g ? read_run(g) : -1;
    if (!r) { snprintf(town, (size_t)n, "%s", g->town); *round = g->round; }
    free(g);
    return r;
}

int run_load(void) {
    saver_wait();
    Game *g = malloc(sizeof(Game));
    if (!g) return -1;
    if (read_run(g)) { free(g); plat_log("the saved run doesn't fit this build: left alone"); return -1; }
    int vw = G->view_w, vh = G->view_h;
    game_free();
    memcpy(G, g, sizeof(Game));
    free(g);
    G->world = 0; G->view_w = vw; G->view_h = vh;
    for (int i = 0; i < MAX_ZOMBIES; i++) G->z[i].rimg = 0;
    world_paint();                                      /* the town as it is now, power and all */
    if (G->wave_on) world_power_wave_resume();          /* the lamps still follow the ring */
    return 0;
}
