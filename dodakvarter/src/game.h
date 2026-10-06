// game.h: the game's state and the functions the modules share.
#pragma once
#include "gfx.h"
#include "plat.h"
#include "art.h"

#define GAME_TITLE "Döda Kvarter"
#ifndef DK_VERSION
#define DK_VERSION "dev"
#endif

#define TS 16                           /* tile size in pixels */
#define MAPW_MAX 136
#define MAPH_MAX 96
#define MAX_ZONES 16
#define MAX_BUILDINGS 160
#define MAX_PROPS 1400
#define MAX_INTER 320
#define MAX_SPAWNS 160
#define MAX_ZOMBIES 48
#define MAX_SHOTS 96
#define MAX_PARTS 1200
#define MAX_ITEMS 96
#define MAX_POWERUPS 8
#define MAX_TEXTS 48
#define MAX_LIGHTS 400
#define MAX_GRENADES 12
#define MAX_CLOUDS 12
#define BAG_SLOTS 6

/* ---------------------------------------------------------------- map */
enum {                                  /* ground */
    G_ASPHALT, G_SIDEWALK, G_COBBLE, G_GRASS, G_GRAVEL, G_SAND, G_DIRT, G_WATER, G_DECK, G_TURF, G_SOIL,
    G_PLAZA, G_FLOOR, G_RAIL, G_ROCK, G_FOREST, G_ROOF, G_PARKING, G_SCHOOLYARD, G_COUNT
};
enum {                                  /* tile flags */
    TF_SOLID = 1,                       /* blocks walking */
    TF_OPAQUE = 2,                      /* blocks bullets and light */
    TF_WATER = 4,
    TF_ROAD = 8,                        /* a car could drive here: manholes, markings */
    TF_RESERVED = 16,                   /* mapgen: keep free (paths) */
    TF_BUILDING = 32,
    TF_INTER = 64,                      /* an interactable stands here */
    TF_PATH = 128,                      /* mapgen: a carved way between ports and a district's middle: never blocked */
};
enum {                                  /* flat decorations painted into the ground */
    D_NONE, D_LINE_H, D_LINE_V, D_ZEBRA_H, D_ZEBRA_V, D_MANHOLE, D_DRAIN, D_LEAVES, D_PUDDLE, D_CRACK, D_PARKING_H,
    D_PARKING_V, D_FLOWERS, D_HOPSCOTCH, D_PITCH_LINE_H, D_PITCH_LINE_V, D_PITCH_CIRCLE, D_STAIN, D_BIKE_LANE,
    D_SNOWPILE, D_ARROW, D_GRAVE_PLOT, D_CURB_H, D_CURB_V, D_COUNT
};
typedef struct {
    uint8_t g, f, zone, deco;
    uint8_t var;                        /* variation for the painter (rotation, which stone...) */
    uint8_t bld;                        /* building index + 1 */
    uint16_t inter;                     /* interactable index + 1 */
} Tile;

enum {                                  /* districts */
    Z_GARDEN,                           /* miljonprogrammets gård: lamellhus around a yard */
    Z_TORG,                             /* the centrum: shops around a square, the tunnelbana */
    Z_VILLA,                            /* villaområde: Falu red houses, gardens, flagpoles */
    Z_OLDTOWN,                          /* gamla stan: gränder between plastered houses */
    Z_ALLOT,                            /* kolonilotter */
    Z_PARK,                             /* park, bedrock, a lake with a jetty and a sauna */
    Z_SCHOOL,                           /* school, schoolyard, artificial turf */
    Z_CHURCH,                           /* church, bell tower, graveyard */
    Z_HARBOR,                           /* warehouses, containers, the quay */
    Z_STATION,                          /* pendeltåg station, tracks, bus terminal */
    Z_MALL,                             /* big-box store and its car park */
    Z_COUNT
};
typedef struct {
    int x, y, w, h;                     /* tiles */
    int type, open, dist;               /* dist: steps from the start zone */
    char name[32];
    int cx, cy;                         /* a walkable tile near the middle */
} Zone;

enum {                                  /* building styles */
    BS_LAMELL, BS_LAMELL_BRICK, BS_VILLA, BS_OLDTOWN, BS_SHOP, BS_CHURCH, BS_TOWER, BS_SCHOOL, BS_KIOSK, BS_SHED,
    BS_WAREHOUSE, BS_STATION, BS_COTTAGE, BS_TVATT, BS_GREENHOUSE, BS_MALL, BS_SAUNA, BS_BELLTOWER, BS_COUNT
};
typedef struct {
    int x, y, w, h, fh;                 /* footprint (tiles) and how many of its bottom rows are facade */
    int style;
    uint32_t wall, wall2, roof, trim;
    char sign[20];
    uint32_t seed;
    int lit;                            /* windows light up with the power */
} Building;

enum {                                  /* props: drawn standing up, sorted by their feet */
    P_BIRCH, P_PINE, P_APPLE, P_BUSH, P_LAMP, P_BENCH, P_BIN, P_BUSSTOP, P_CAR, P_CAR_V, P_BIKES, P_FLAGPOLE,
    P_TRAMPOLINE, P_SWINGS, P_SLIDE, P_CLIMBER, P_MAILBOXES, P_RECYCLE, P_CART, P_GRAVE, P_FOUNTAIN, P_WELL,
    P_CONTAINER, P_CONTAINER_V, P_PALLETS, P_MAYPOLE, P_SIGN_MOOSE, P_SIGN_T, P_SIGN_BUS, P_ROCK, P_PLANTER,
    P_GOAL, P_LAMP_WALL, P_HYDRANT, P_PHONEBOX, P_STATUE, P_BARREL, P_COMPOST, P_WASHLINE, P_BONFIRE, P_KICKBIKE,
    P_FENCE_H, P_FENCE_V, P_HEDGE_H, P_HEDGE_V, P_WALL_H, P_WALL_V, P_PICKET_H, P_PICKET_V, P_RAILING_H,
    P_TABLE, P_SANDBOX, P_SNOWMAN, P_BOAT, P_FORKLIFT, P_CRANE_LEG, P_TRUCK, P_TICKET, P_SPRUCE_SMALL, P_COUNT
};
typedef struct {
    int16_t x, y;                       /* pixels: bottom centre */
    uint8_t kind, var, solid, lit;
    uint8_t loot;                       /* a loot container: 1 = stocked, 2 = searched */
    uint16_t inter;
} Prop;

enum { IT_BARRIER, IT_WALLBUY, IT_BOX, IT_PERK, IT_PAP, IT_POWER, IT_WINDOW, IT_LOOT, IT_ARMORY, IT_COUNT };
typedef struct {
    int type, state;
    int tx, ty, tw, th;                 /* tiles it covers */
    float x, y;                         /* where the player stands to use it (pixels) */
    int cost, a, b, c;                  /* type-specific: zones (barrier), weapon (wall buy), perk... */
    float t, t2;
    int prop;                           /* loot: the prop index */
} Inter;

enum { SP_WINDOW, SP_MANHOLE, SP_GROUND, SP_GRAVE, SP_DOOR };
typedef struct { int type, zone, inter; float x, y; } Spawn;

/* ---------------------------------------------------------------- items */
enum { RAR_COMMON, RAR_UNCOMMON, RAR_RARE, RAR_EPIC, RAR_LEGENDARY, RAR_COUNT };
enum {
    W_PIST88, W_REVOLVER, W_KPIST, W_AK5, W_AK4, W_HAGEL, W_STUDSARE, W_KSP58, W_PSKOTT, W_STRAL, W_ASKA, W_SNO,
    W_COUNT
};
enum { WC_PISTOL, WC_SMG, WC_RIFLE, WC_SHOTGUN, WC_SNIPER, WC_LMG, WC_LAUNCHER, WC_WONDER };
enum { FM_SEMI, FM_AUTO, FM_BOLT, FM_PUMP };
enum { PR_BULLET, PR_ROCKET, PR_PLASMA, PR_LIGHTNING, PR_FROST };
typedef struct {
    const char *name, *pap_name, *icon;
    int cls, fire, proj;
    float dmg, rpm; int mag, reserve; float reload, spread; int pellets, pierce; float range, splash;
    int wall_price, box_weight;
    uint32_t tracer;
} WeaponDef;
typedef struct { int16_t def; int8_t rar, pap; int16_t mag, reserve; } Weapon;   /* def < 0: empty slot */

enum { AR_HEAD, AR_BODY };
enum {
    A_MOSSA, A_CYKELHJALM, A_HOCKEYHJALM, A_KRAVALLHJALM,
    A_REFLEXVAST, A_TACKJACKA, A_SKINNJACKA, A_SKYDDSVAST, A_KRAVALL, A_COUNT
};
typedef struct { const char *name, *icon; int slot; float ap; } ArmorDef;
typedef struct { int16_t def; int8_t rar; float ap, max; } Armor;

enum { C_PLASTER, C_FORBAND, C_GRANAT, C_SMALLARE, C_MOLOTOV, C_TEJP, C_KAFFE, C_COUNT };
typedef struct { const char *name, *icon; int stack; } ConsDef;
typedef struct { int16_t id, n; } BagSlot;                                /* id < 0: empty */

enum { IK_WEAPON, IK_ARMOR, IK_CONS, IK_AMMO, IK_CASH };
typedef struct {
    int alive, kind, id, rar, n;
    Weapon w; Armor ar;
    float x, y, t, life;
} Item;

enum { PK_JULMUST, PK_SNABBKAFFE, PK_SALMIAK, PK_KANELBULLE, PK_BLABAR, PK_LINGON, PK_KAVIAR, PK_COUNT };
typedef struct { const char *name, *icon; int cost; uint32_t color, color2; } PerkDef;
#define PERK_LIMIT 4

enum { PU_MAXAMMO, PU_INSTAKILL, PU_DOUBLE, PU_KABOOM, PU_CARPENTER, PU_FIRESALE, PU_COUNT };
typedef struct { int alive, kind; float x, y, t; } PowerUp;

/* ---------------------------------------------------------------- actors */
enum { ZT_WALKER, ZT_BRUTE, ZT_BLOATER, ZT_WOLF, ZT_MOOSE, ZT_COUNT };
enum { ZS_RISE, ZS_WINDOW, ZS_CHASE, ZS_ATTACK, ZS_DEAD, ZS_CHARGE, ZS_WINDUP, ZS_STUN };
typedef struct {
    int alive, type, variant, state, dir;
    float x, y, vx, vy, speed, hp, maxhp;
    float t, anim, flash, slow, burn, atk_cd, lure;
    int window;                         /* spawn: the window it climbs through (inter index) */
    float cx, cy;                       /* moose: charge direction */
    int crit_kill;
    float stuck_t, lastx, lasty;
    float swipe;                        /* in a window: a swipe through the gap, winding up */
    const Img *rimg; int16_t rx, ry, rclip; uint8_t rflip;   /* how it was last drawn (its eyes glow on top) */
} Zombie;

typedef struct {
    float x, y, vx, vy, aim, aim_lock;
    int dir;                            /* 0 down 1 up 2 right 3 left */
    float hp, maxhp, regen_t, hurt_t, invuln;
    int downed; float down_t;
    Weapon w[3]; int cur, nslots;
    float fire_cd, reload_t, reload_len, swap_t, melee_cd, melee_t, melee_ang, burst_t;
    int reloading, fired_this_press, pump;
    Armor ar[2];
    BagSlot bag[BAG_SLOTS]; int bag_sel;
    int grenades;
    uint32_t perks; int nperks, bulle_used;
    int kr, kr_total, kills, crits, knifes, downs, revives, boards, doors, boxes;
    float anim, muzzle_t, step_t, coffee_t, axe;
    int has_axe;
    float use_t; int use_target;        /* holding B on something */
    float last_hit_t;
    float stamina; int sprinting;
    int repair_kr;                      /* board money this round (capped at 50 x round, 500 at most) */
} Player;

/* ---------------------------------------------------------------- effects */
enum { PT_BLOOD, PT_GIB, PT_SPARK, PT_SMOKE, PT_SHELL, PT_FIRE, PT_DUST, PT_WOOD, PT_SNOW, PT_ELEC, PT_FROST, PT_GAS, PT_GLOW, PT_LEAF, PT_CONFETTI, PT_WATER };
typedef struct { int alive, type; float x, y, z, vx, vy, vz, life, max; uint32_t col; float size; } Part;
typedef struct { int alive, type; float x, y, vx, vy, life, dmg, splash; int pierce, hits; Weapon w; float x0, y0; uint32_t col; } Shot;
typedef struct { int alive; float x0, y0, x1, y1, life; uint32_t col; int kind; } Tracer;
typedef struct { int alive; float x, y, vx, vy, z, vz, t; int kind; } Grenade;   /* kind: C_GRANAT, C_SMALLARE, C_MOLOTOV */
typedef struct { int alive; float x, y, r, t, dur; int kind; } Cloud;            /* fire or stink gas on the ground */
typedef struct { int alive; float x, y, vy, t; char s[24]; uint32_t col; int screen; } FloatText;
typedef struct { float x, y, r; uint32_t col; float k; } Light;

enum { SEASON_AUTUMN, SEASON_WINTER, SEASON_SUMMER, SEASON_COUNT };
enum { RS_INTRO, RS_ACTIVE, RS_BREAK };

typedef struct {
    /* map */
    int w, h;
    Tile t[MAPH_MAX][MAPW_MAX];
    Zone zones[MAX_ZONES]; int nzones, start_zone, zcols, zrows;
    Building b[MAX_BUILDINGS]; int nb;
    Prop props[MAX_PROPS]; int nprops;
    Inter it[MAX_INTER]; int nit;
    Spawn spawns[MAX_SPAWNS]; int nspawns;
    int box_spots[8], nbox_spots, box_at, box_uses, box_moves, box_moving; float box_t;
    char town[32];
    uint32_t *world; int ww, wh;        /* the painted ground, buildings and flat props */
    uint8_t lightmask[MAPH_MAX][MAPW_MAX];
    uint16_t flow[MAPH_MAX][MAPW_MAX];  /* steps to the player (flow field) */
    int flow_x, flow_y; float flow_t;
    uint16_t lureflow[MAPH_MAX][MAPW_MAX]; int lure_on; float lure_x, lure_y;
    /* run */
    uint64_t seed; int season;
    Rng rng, fx;
    float time;
    int round, rstate; float rtime;
    int to_spawn, spawned, special; float spawn_cd;
    int drops_round; int drop_target, drop_inc; int deck[PU_COUNT * 2], deck_n, deck_i;
    int wolf_next, wolf_rounds, moose_next, moose_count, moose_pending;
    int drop_pending;                   /* the points-based power-up drop waits for the next kill */
    float insta_t, double_t, firesale_t;
    int power_on; float power_t;
    int over; float over_t;
    /* actors */
    Player p;
    Zombie z[MAX_ZOMBIES];
    Shot shots[MAX_SHOTS];
    Tracer tr[64];
    Part parts[MAX_PARTS];
    Item items[MAX_ITEMS];
    PowerUp pu[MAX_POWERUPS];
    Grenade gr[MAX_GRENADES];
    Cloud clouds[MAX_CLOUDS];
    FloatText ft[MAX_TEXTS];
    Light lights[MAX_LIGHTS]; int nlights;
    /* view */
    float camx, camy, shake, flash_t, hurt_flash;
    int view_w, view_h;
    char msg[4][64]; float msg_t[4]; uint32_t msg_col[4];
    char banner[48], banner2[48]; float banner_t; uint32_t banner_col;
    char prompt[64]; int prompt_cost_ok;
    float round_flash;
    int stats_zombies_by_type[ZT_COUNT];
} Game;

extern Game *G;
extern const WeaponDef WEAPONS[W_COUNT];
extern const ArmorDef ARMORS[A_COUNT];
extern const ConsDef CONS[C_COUNT];
extern const PerkDef PERKS[PK_COUNT];
extern const char *RARITY_NAME[RAR_COUNT];
extern const uint32_t RARITY_COL[RAR_COUNT];

/* settings (save.c) */
typedef struct {
    int volume, music, shake, assist, scheme, season, lang, swap_ab, show_fps, touch_aim;
} Settings;
extern Settings S;

/* tiles */
static inline Tile *tile_at(int x, int y) { return (x >= 0 && y >= 0 && x < G->w && y < G->h) ? &G->t[y][x] : 0; }
static inline int solid_at(int x, int y) { Tile *t = tile_at(x, y); return !t || (t->f & TF_SOLID); }
static inline int opaque_at(int x, int y) { Tile *t = tile_at(x, y); return !t || (t->f & TF_OPAQUE); }

/* mapgen.c */
void map_generate(uint64_t seed, int season);
int zone_at(float x, float y);
void zone_open(int z);
void refresh_walls(void);

/* world.c: the painted world */
void world_paint(void);
void world_repaint_rect(int tx, int ty, int tw, int th);
void world_decal_blood(float x, float y, int amount);
void world_decal_scorch(float x, float y, float r);
uint32_t ground_color(int g, int season);

/* props.c */
void prop_draw(Surf *s, const Prop *p, int sx, int sy);
void prop_bounds(const Prop *p, int *w, int *h);
void inter_draw(Surf *s, Inter *it, int sx, int sy);
void props_paint_flat(void);
int prop_is_tall(int kind);
void prop_lights(void);

/* game.c */
void game_new(uint64_t seed, int season);
void game_update(const Input *in, const Input *prev, float dt);
void game_free(void);
void msg(uint32_t col, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void banner(uint32_t col, const char *a, const char *b);
void add_kr(int n, int scaled);
void spawn_parts(int type, float x, float y, int n, uint32_t col, float speed);
void float_text(float x, float y, uint32_t col, const char *s);
void shake(float amount);
float player_speed(void);
int line_clear(float x0, float y0, float x1, float y1);
int window_tile(int tx, int ty);
int shot_clear(float x0, float y0, float x1, float y1);
int move_actor(float *x, float *y, float dx, float dy, float r);
int walk_clear(float x0, float y0, float x1, float y1, float r);
void player_hurt(float dmg, float fx, float fy);
int btn_fire(void);
int btn_use(void);

/* weapons.c */
Weapon weapon_make(int def, int rar);
float weapon_dmg(const Weapon *w);
float weapon_rpm(const Weapon *w);
int weapon_mag(const Weapon *w);
int weapon_reserve_max(const Weapon *w);
float weapon_reload(const Weapon *w);
const char *weapon_name(const Weapon *w);
void weapon_fire(void);
void shots_update(float dt);
void damage_zombie(Zombie *z, float dmg, int crit, int melee, float kx, float ky);
void explode(float x, float y, float r, float dmg, int from_player);
void grenades_update(float dt);
void throw_grenade(int kind);
void melee_attack(void);

/* zombies.c */
void zombies_update(float dt);
void flow_update(int force);
int zombies_alive(void);
void round_start(int n);
void round_update(float dt);
void spawn_zombie(int type);
float zombie_hp_for_round(int r);
int zombies_for_round(int r);
void kill_all_zombies(int give_kr);

/* loot.c */
int roll_rarity(int bonus);
void item_drop(int kind, int id, int rar, int n, float x, float y);
void item_drop_weapon(Weapon w, float x, float y);
void item_drop_armor(Armor a, float x, float y);
void loot_container(Inter *it);
void loot_restock(int count);
void loot_zombie_drop(float x, float y);
void powerup_try_drop(float x, float y);
void powerup_spawn(int kind, float x, float y);
void items_update(float dt);
void powerups_update(float dt);
void bag_add(int id, int n);
int bag_count(int id);
void bag_use(int slot);
Armor armor_make(int def, int rar);

/* inter.c: buying and using things */
void inter_update(const Input *in, const Input *prev, float dt);
const char *inter_prompt(int i, int *ok);
int nearest_inter(float x, float y, float r);
int inter_target(void);
void box_place(int spot);

/* render.c */
void render_game(Surf *top);
void render_init(void);
/* hud.c */
void render_hud(Surf *bot, const Input *in);
int hud_touch(int x, int y);
/* audio.c */
void audio_init(void);
void sfx(int id, float vol, float pan);
void sfx_at(int id, float x, float y, float vol);
void music_play(int track);
void audio_set_volume(int v);
enum {
    SFX_PISTOL, SFX_SMG, SFX_RIFLE, SFX_SHOTGUN, SFX_SNIPER, SFX_LMG, SFX_ROCKET, SFX_RAY, SFX_ZAP, SFX_FROST,
    SFX_EXPLODE, SFX_RELOAD, SFX_EMPTY, SFX_KNIFE, SFX_HIT, SFX_GROAN1, SFX_GROAN2, SFX_GROAN3, SFX_ZATTACK,
    SFX_HURT, SFX_WOLF, SFX_MOOSE, SFX_BOARD_BREAK, SFX_BOARD_FIX, SFX_BUY, SFX_DENY, SFX_BOX, SFX_HORSE,
    SFX_POWERUP_SPAWN, SFX_POWERUP, SFX_ROUND_START, SFX_ROUND_END, SFX_GAMEOVER, SFX_SWAP, SFX_PICKUP,
    SFX_MENU_MOVE, SFX_MENU_OK, SFX_MENU_BACK, SFX_PERK, SFX_PAP, SFX_POWER, SFX_DOOR, SFX_SPLAT, SFX_THROW,
    SFX_BEEP, SFX_GULP, SFX_STEP, SFX_KABOOM, SFX_COUNT
};
enum { MUS_NONE, MUS_TITLE, MUS_BOX, MUS_GAMEOVER };

/* lang.c */
enum { LANG_EN, LANG_SV };
const char *tr(const char *en);
