// data.c: the weapons, armour, consumables and perks. Wall prices are Black Ops' (500 / 1000 / 1200 / 1500), and
// the perks are Black Ops' Perk-a-Colas as Swedish food: their prices and effects follow the originals.
#include "game.h"

/*  name            pap name               icon         class        fire     proj
 *  dmg   rpm  mag  res  reload spread pel pierce range splash wall box  tracer */
const WeaponDef WEAPONS[W_COUNT] = {
    { "Pist 88", "Tvillingarna", "w_pist88", WC_PISTOL, FM_SEMI, PR_BULLET,
      40, 380, 17, 85, 1.4f, 2.0f, 1, 0, 220, 0, 500, 4, 0xfff0a0 },
    { "Revolver", "Kungens revolver", "w_revolver", WC_PISTOL, FM_SEMI, PR_BULLET,
      150, 160, 6, 42, 2.2f, 1.0f, 1, 1, 260, 0, 900, 12, 0xfff0a0 },
    { "Kpist m/45", "Carl Gustafs raseri", "w_kpist", WC_SMG, FM_AUTO, PR_BULLET,
      45, 600, 36, 216, 2.2f, 5.0f, 1, 0, 200, 0, 1000, 16, 0xffe890 },
    { "Ak 5", "Ragnarök", "w_ak5", WC_RIFLE, FM_AUTO, PR_BULLET,
      70, 660, 30, 210, 2.4f, 3.0f, 1, 0, 260, 0, 1200, 16, 0xffe890 },
    { "Ak 4", "Fenrisulven", "w_ak4", WC_RIFLE, FM_AUTO, PR_BULLET,
      115, 480, 20, 140, 2.6f, 2.5f, 1, 1, 280, 0, 1800, 14, 0xffe890 },
    { "Hagelgevär", "Hagelstormen", "w_hagel", WC_SHOTGUN, FM_PUMP, PR_BULLET,
      45, 70, 6, 54, 2.8f, 16.0f, 8, 0, 120, 0, 1500, 14, 0xffd070 },
    { "Älgstudsare", "Älgkungen", "w_studsare", WC_SNIPER, FM_BOLT, PR_BULLET,
      650, 45, 5, 40, 3.0f, 0.3f, 1, 4, 340, 0, 0, 12, 0xffffff },
    { "Ksp 58", "Tordönet", "w_ksp58", WC_LMG, FM_AUTO, PR_BULLET,
      90, 650, 100, 400, 5.0f, 4.0f, 1, 1, 280, 0, 0, 12, 0xffe890 },
    { "Pansarskott", "Pansarsmällen", "w_pskott", WC_LAUNCHER, FM_SEMI, PR_ROCKET,
      1500, 40, 1, 8, 3.0f, 0.5f, 1, 0, 300, 56, 0, 10, 0xffa040 },
    { "Strålpistol", "Norrskenet", "w_stral", WC_WONDER, FM_SEMI, PR_PLASMA,
      1000, 200, 20, 160, 2.8f, 1.0f, 1, 0, 260, 26, 0, 6, 0x60ff60 },
    { "Åskvigg", "Mjölner", "w_aska", WC_WONDER, FM_SEMI, PR_LIGHTNING,
      5000, 60, 3, 12, 3.2f, 0, 1, 0, 160, 0, 0, 3, 0xa0d0ff },
    { "Snöblåsare", "Fimbulvintern", "w_sno", WC_WONDER, FM_AUTO, PR_FROST,
      180, 600, 40, 200, 3.0f, 22.0f, 1, 0, 90, 0, 0, 4, 0xd0f0ff },
};

/* armour points: what a piece absorbs before it breaks */
const ArmorDef ARMORS[A_COUNT] = {
    { "Mössa", "a_mossa", AR_HEAD, 12 },
    { "Cykelhjälm", "a_cykel", AR_HEAD, 25 },
    { "Hockeyhjälm", "a_hockey", AR_HEAD, 40 },
    { "Kravallhjälm", "a_kravallh", AR_HEAD, 60 },
    { "Reflexväst", "a_reflex", AR_BODY, 20 },
    { "Täckjacka", "a_tacke", AR_BODY, 35 },
    { "Skinnjacka", "a_skinn", AR_BODY, 50 },
    { "Skyddsväst", "a_skydd", AR_BODY, 75 },
    { "Kravallrustning", "a_kravall", AR_BODY, 110 },
};

const ConsDef CONS[C_COUNT] = {
    { "Plåster", "c_plaster", 5 },
    { "Förbandslåda", "c_forband", 3 },
    { "Granat", "c_granat", 4 },
    { "Smällare", "c_smallare", 3 },
    { "Brandbomb", "c_molotov", 3 },
    { "Silvertejp", "c_tejp", 3 },
    { "Termos", "c_termos", 3 },
};

/*  name             icon          cost  machine   label */
const PerkDef PERKS[PK_COUNT] = {
    { "Julmust", "pk_julmust", 2500, 0x7a1e14, 0xd8b040 },
    { "Snabbkaffe", "pk_kaffe", 3000, 0x3a2a1e, 0xe8e0d0 },
    { "Salmiak", "pk_salmiak", 2000, 0x1e1e24, 0xe8e8e8 },
    { "Kanelbulle", "pk_bulle", 500, 0xc8862a, 0x6a3a1a },
    { "Blåbärssoppa", "pk_blabar", 2000, 0x3a2a6a, 0x8a6ad8 },
    { "Lingondricka", "pk_lingon", 2000, 0xa81e2a, 0xf0f0f0 },
    { "Kaviar", "pk_kaviar", 4000, 0x2a5a9a, 0xf0d020 },
};

const char *RARITY_NAME[RAR_COUNT] = { "Common", "Uncommon", "Rare", "Epic", "Legendary" };
const uint32_t RARITY_COL[RAR_COUNT] = { 0xc8c8c8, 0x5ad05a, 0x4a9af0, 0xc05af0, 0xf0a020 };
