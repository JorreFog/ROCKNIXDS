// dsscenes ARM9: 3D feature-coverage scenes for testing a DS 3D renderer against DraStic's.
//
// Each scene exercises a group of DS 3D features the way games use them. The config block (crt9.s, patched by
// build.py) picks the scene: mode 0 = fixed scene `level`, mode 1 = cycle through all scenes, `ramp` frames each.
// Scenes animate, and toggle their feature switches every half scene, so a run covers both states.
//   0 texture formats    7 formats incl. 4x4 compressed, colour-0 transparency, alpha test
//   1 texture wrapping   repeat / flip / clamp in S and T, non-square and large textures, texcoord transform
//   2 translucency       overlapping alpha 1..30, polygon ids, translucent depth update, depth-equal test, blending
//   3 shading            modulate / decal / toon / highlight on a lit mesh, vertex colours
//   4 fog + edges        terrain with fog (colour and alpha-only), edge marking, anti-aliasing
//   5 shadows            shadow volume mask + shadow polygons over a floor
//   6 2D-in-3D           orthographic screen-aligned sprites (alpha textures), 1-dot polygons, wireframe, lines
//   7 field              game-like: textured ground tiles, billboard characters, moving camera, w-buffering
//   8 rear plane         clear-image bitmap with scroll, polygons in front
//   9 geometry edge cases near-plane clipping, huge / tiny / thin polygons, many-bin spans, 10-vertex clips
#include "tables.h"

typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8; typedef int s32; typedef short s16;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a)  (*(volatile u8 *)(a))

extern volatile struct { u32 magic, mode, level, ramp, max, cpu; } cfg;

#define POWCNT1     R32(0x04000304)
#define DISPCNT_A   R32(0x04000000)
#define DISPCNT_B   R32(0x04001000)
#define VCOUNT      R16(0x04000006)
#define VRAMCNT(n)  R8(0x04000240 + (n))
#define DISP3DCNT   R32(0x04000060)
#define EDGE_COLOR(i) R16(0x04000330 + 2 * (i))
#define ALPHA_TEST_REF R8(0x04000340)
#define CLEAR_COLOR R32(0x04000350)
#define CLEAR_DEPTH R16(0x04000354)
#define CLRIMAGE_OFFSET R16(0x04000356)
#define FOG_COLOR   R32(0x04000358)
#define FOG_OFFSET  R16(0x0400035C)
#define FOG_TABLE(i) R8(0x04000360 + (i))
#define TOON_TABLE(i) R16(0x04000380 + 2 * (i))
#define MTX_MODE    R32(0x04000440)
#define MTX_PUSH    R32(0x04000444)
#define MTX_POP     R32(0x04000448)
#define MTX_IDENT   R32(0x04000454)
#define MTX_LOAD44  R32(0x04000458)
#define MTX_SCALE   R32(0x0400046C)
#define MTX_MULT33  R32(0x04000468)
#define MTX_TRANS   R32(0x04000470)
#define GX_COLOR    R32(0x04000480)
#define GX_NORMAL   R32(0x04000484)
#define GX_TEXCOORD R32(0x04000488)
#define GX_VTX16    R32(0x0400048C)
#define POLY_ATTR   R32(0x040004A4)
#define TEX_PARAM   R32(0x040004A8)
#define PLTT_BASE   R32(0x040004AC)
#define DIF_AMB     R32(0x040004C0)
#define SPE_EMI     R32(0x040004C4)
#define LIGHT_VEC   R32(0x040004C8)
#define LIGHT_COL   R32(0x040004CC)
#define BEGIN_VTXS  R32(0x04000500)
#define END_VTXS    R32(0x04000504)
#define SWAP_BUF    R32(0x04000540)
#define VIEWPORT    R32(0x04000580)

#define NSCENES 10
#define FX 4096

void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

/* EABI division helpers (no libgcc): shift-subtract */
typedef unsigned long long u64;
static u32 udiv(u32 n, u32 d, u32 *r) {
    u32 q = 0, rem = 0;
    if (!d) { *r = n; return 0xFFFFFFFFu; }
    for (int i = 31; i >= 0; i--) { rem = rem << 1 | ((n >> i) & 1); if (rem >= d) { rem -= d; q |= 1u << i; } }
    *r = rem; return q;
}
u32 __aeabi_uidiv(u32 n, u32 d) { u32 r; return udiv(n, d, &r); }
u64 __aeabi_uidivmod(u32 n, u32 d) { u32 r, q = udiv(n, d, &r); return q | (u64)r << 32; }
s32 __aeabi_idiv(s32 n, s32 d) { u32 r, q = udiv(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, &r); return (n < 0) != (d < 0) ? -(s32)q : (s32)q; }
u64 __aeabi_idivmod(s32 n, s32 d) {
    u32 r, q = udiv(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, &r);
    s32 qs = (n < 0) != (d < 0) ? -(s32)q : (s32)q, rs = n < 0 ? -(s32)r : (s32)r;
    return (u32)qs | (u64)(u32)rs << 32;
}

static inline s32 sn(s32 a) { return sintab[a & 255]; }
static inline s32 cs(s32 a) { return sintab[(a + 64) & 255]; }
static inline u32 n10(s32 v) { if (v > 511) v = 511; if (v < -511) v = -511; return (u32)v & 0x3FF; }
static inline u16 rgb(u32 r, u32 g, u32 b) { return (u16)((r & 31) | (g & 31) << 5 | (b & 31) << 10); }
static u32 seed = 12345;
static u32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 16; }

static void wait_vblank(void) { while (VCOUNT >= 192) ; while (VCOUNT < 192) ; }

/* ---- textures: slot 0 = bank A, slot 1 = bank B (4x4 index data), slot 2/3 = C/D (rear plane), palettes = E ---- */
#define T_DIRECT   0x0000   /* 32x32 direct */
#define T_A3I5     0x0800   /* 32x32 */
#define T_4COL     0x0C00
#define T_16COL    0x0D00
#define T_256COL   0x0F00
#define T_A5I3     0x1300
#define T_4X4      0x1700   /* 32x32, 64 blocks; index data at slot 1 + 0x1700/2 */
#define T_CHECK64  0x2000   /* 64x64 direct */
#define T_WIDE     0x4000   /* 128x32 direct */
#define T_TERRAIN  0x8000   /* 256x256 16-colour */
#define T_CHAR     0x10000  /* 32x64 A5I3 character */
#define P_256 0x000
#define P_16  0x200
#define P_4   0x240
#define P_A3  0x300
#define P_A5  0x380
#define P_4X4 0x400
#define P_TER 0x500
#define P_CHR 0x540

static void textures(void) {
    VRAMCNT(0) = 0x80; VRAMCNT(1) = 0x80; VRAMCNT(2) = 0x80; VRAMCNT(3) = 0x80; VRAMCNT(4) = 0x80;  /* LCDC */
    volatile u8 *a = (volatile u8 *)0x06800000, *b = (volatile u8 *)0x06820000;
    volatile u16 *pal = (volatile u16 *)0x06880000;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        int i = y * 32 + x;
        ((volatile u16 *)(a + T_DIRECT))[i] = (u16)(((x ^ y) & 4 ? 0x8000 : 0) | rgb(x, y, 31 - x));
        a[T_A3I5 + i] = (u8)((x & 31) | ((y >> 2) & 7) << 5);
        a[T_256COL + i] = (u8)(x * 8 + y);
        a[T_A5I3 + i] = (u8)((((x + y) >> 3) & 7) | (((x * 2) & 31) << 3));
    }
    for (int i = 0; i < 32 * 32 / 4; i++) a[T_4COL + i] = (u8)(i * 37 + (i >> 3));
    for (int i = 0; i < 32 * 32 / 2; i++) a[T_16COL + i] = (u8)((i & 15) | ((i >> 5) & 15) << 4);
    for (int k = 0; k < 64; k++) {                       /* 4x4: texel words + per-block palette/mode */
        ((volatile u32 *)(a + T_4X4))[k] = 0x1B1B1B1Bu * (u32)(k & 3) + 0xE4E4E4E4u * (u32)((k >> 2) & 1) + (u32)k * 0x01010101u;
        ((volatile u16 *)(b + T_4X4 / 2))[k] = (u16)(((k * 2) & 0x3FFF) | ((k & 3) << 14));
    }
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
        ((volatile u16 *)(a + T_CHECK64))[y * 64 + x] = (u16)(0x8000 | (((x >> 3) ^ (y >> 3)) & 1 ? rgb(31, x / 2, 4) : rgb(2, y / 3, 20)) | (x == 0 || y == 0 ? 0x7FFF : 0));
    for (int y = 0; y < 32; y++) for (int x = 0; x < 128; x++)
        ((volatile u16 *)(a + T_WIDE))[y * 128 + x] = (u16)(0x8000 | rgb(x / 4, (x + y) & 31, y));
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x += 2) {                       /* terrain tiles */
        int t = ((x >> 4) * 7 + (y >> 4) * 3) & 3, n = (x * 13 + y * 7 + (x * y >> 5)) & 3;
        int c0 = t * 4 + n, c1 = t * 4 + ((n + ((x ^ y) & 8 ? 1 : 0)) & 3);
        a[T_TERRAIN + (y * 256 + x) / 2] = (u8)(c0 | c1 << 4);
    }
    volatile u8 *ch = (volatile u8 *)0x06810000;                                             /* character: A5I3 */
    for (int y = 0; y < 64; y++) for (int x = 0; x < 32; x++) {
        int dx = x - 16, dy = (y < 24 ? y - 12 : y - 42), r2 = dx * dx * (y < 24 ? 4 : 1) + dy * dy;
        int in = y < 24 ? r2 < 400 : (dx > -9 && dx < 9 && y < 62);
        int alpha = in ? (r2 > 300 && y < 24 ? 16 : 31) : 0;
        ch[y * 32 + x] = (u8)((alpha << 3) | ((y < 24) ? (dx > 3 && dy < 0 ? 1 : 2) : ((x + y) & 4 ? 3 : 4)));
    }
    for (int i = 0; i < 256; i++) pal[P_256 / 2 + i] = rgb(i >> 3, (i * 3) >> 3, 31 - (i >> 3));
    for (int i = 0; i < 16; i++) pal[P_16 / 2 + i] = rgb(i * 2, 31 - i * 2, (i * 5) & 31);
    pal[P_4 / 2 + 0] = rgb(31, 0, 0); pal[P_4 / 2 + 1] = rgb(0, 31, 0); pal[P_4 / 2 + 2] = rgb(0, 0, 31); pal[P_4 / 2 + 3] = rgb(31, 31, 31);
    for (int i = 0; i < 32; i++) pal[P_A3 / 2 + i] = rgb(i, 16, 31 - i);
    for (int i = 0; i < 8; i++) pal[P_A5 / 2 + i] = rgb(i * 4, 31, i * 2);
    for (int i = 0; i < 160; i++) pal[P_4X4 / 2 + i] = rgb(i * 7, i * 3, i * 11);
    for (int i = 0; i < 16; i++) {
        static const u16 g[4][4] = {{0x0E86, 0x0F07, 0x1388, 0x0A65}, {0x2D6B, 0x318C, 0x294A, 0x35AD},
                                    {0x7E40, 0x7A20, 0x7E60, 0x7600}, {0x1A2D, 0x1E4E, 0x162C, 0x226F}};
        pal[P_TER / 2 + i] = g[i >> 2][i & 3];
    }
    pal[P_CHR / 2 + 0] = 0; pal[P_CHR / 2 + 1] = rgb(31, 31, 31); pal[P_CHR / 2 + 2] = rgb(31, 24, 18);
    pal[P_CHR / 2 + 3] = rgb(4, 8, 28); pal[P_CHR / 2 + 4] = rgb(28, 4, 4);
    /* rear plane: colour (slot 2, bank C) and depth+fog (slot 3, bank D), 256x256 */
    volatile u16 *rc = (volatile u16 *)0x06840000, *rd = (volatile u16 *)0x06860000;
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
        rc[y * 256 + x] = (u16)(((x >> 5) + (y >> 5)) & 1 ? 0x8000 : 0) | rgb(x >> 3, y >> 3, 12);
        rd[y * 256 + x] = (u16)(((x + y) & 64 ? 0x8000 : 0) | ((x * 128 + y * 64) & 0x7FFF));
    }
    VRAMCNT(0) = 0x83; VRAMCNT(1) = 0x8B; VRAMCNT(2) = 0x93; VRAMCNT(3) = 0x9B; VRAMCNT(4) = 0x83;
}

static u32 tex(u32 off, int sz_s, int sz_t, int fmt, u32 flags) { return (off >> 3) | (u32)sz_s << 20 | (u32)sz_t << 23 | (u32)fmt << 26 | flags; }
#define REP_S (1u << 16)
#define REP_T (1u << 17)
#define FLIP_S (1u << 18)
#define FLIP_T (1u << 19)
#define COL0 (1u << 29)
static u32 pa(u32 mode, u32 alpha, u32 id, u32 extra) { return 0xC0 | mode << 4 | alpha << 16 | id << 24 | extra; }

/* ---- matrices ---- */
static void load_proj(s32 fovy_mul, int ortho) {
    MTX_MODE = 0; MTX_IDENT = 0;
    if (ortho) {                                           /* x,y in [-128,128]x[-96,96] units of 1/16 -> NDC */
        static const s32 o[16] = {FX / 8, 0, 0, 0, 0, FX / 6, 0, 0, 0, 0, -FX / 32, 0, 0, 0, 0, FX};
        for (int k = 0; k < 16; k++) MTX_LOAD44 = (u32)o[k];
    } else {
        for (int k = 0; k < 16; k++) MTX_LOAD44 = (u32)(k == 0 || k == 5 ? proj[k] * fovy_mul / 4 : proj[k]);
    }
    MTX_MODE = 2; MTX_IDENT = 0;
}
static void rotx(s32 a) { s32 c = cs(a), s = sn(a); MTX_MULT33 = FX; MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = c; MTX_MULT33 = s; MTX_MULT33 = 0; MTX_MULT33 = -s; MTX_MULT33 = c; }
static void roty(s32 a) { s32 c = cs(a), s = sn(a); MTX_MULT33 = c; MTX_MULT33 = 0; MTX_MULT33 = -s; MTX_MULT33 = 0; MTX_MULT33 = FX; MTX_MULT33 = 0; MTX_MULT33 = s; MTX_MULT33 = 0; MTX_MULT33 = c; }
static void rotz(s32 a) { s32 c = cs(a), s = sn(a); MTX_MULT33 = c; MTX_MULT33 = s; MTX_MULT33 = 0; MTX_MULT33 = -s; MTX_MULT33 = c; MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = FX; }
static void trans(s32 x, s32 y, s32 z) { MTX_TRANS = (u32)x; MTX_TRANS = (u32)y; MTX_TRANS = (u32)z; }
static void scale(s32 x, s32 y, s32 z) { MTX_SCALE = (u32)x; MTX_SCALE = (u32)y; MTX_SCALE = (u32)z; }

static inline void vtx(s32 x, s32 y, s32 z) { GX_VTX16 = ((u32)x & 0xFFFF) | ((u32)y << 16); GX_VTX16 = (u32)z & 0xFFFF; }
static inline void tc(s32 s, s32 t) { GX_TEXCOORD = ((u32)s & 0xFFFF) | ((u32)t << 16); }

/* a unit quad in the xy plane (+-0.5), texcoords 0..ts x 0..tt texels */
static void quad(s32 ts, s32 tt, u16 c0, u16 c1, u16 c2, u16 c3) {
    BEGIN_VTXS = 1;
    GX_COLOR = c0; tc(0, 0);             vtx(-FX / 2, FX / 2, 0);
    GX_COLOR = c1; tc(0, tt * 16);       vtx(-FX / 2, -FX / 2, 0);
    GX_COLOR = c2; tc(ts * 16, tt * 16); vtx(FX / 2, -FX / 2, 0);
    GX_COLOR = c3; tc(ts * 16, 0);       vtx(FX / 2, FX / 2, 0);
    END_VTXS = 0;
}
static void cube(s32 ts) {                                  /* +-0.5, normals for lighting */
    static const s32 f[6][4][3] = {
        {{-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}}, {{1, 1, -1}, {1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}},
        {{1, 1, 1}, {1, -1, 1}, {1, -1, -1}, {1, 1, -1}}, {{-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}},
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}}, {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}}};
    static const s32 nrm[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    BEGIN_VTXS = 1;
    for (int i = 0; i < 6; i++) {
        GX_NORMAL = n10(nrm[i][0] * 511) | n10(nrm[i][1] * 511) << 10 | n10(nrm[i][2] * 511) << 20;
        for (int k = 0; k < 4; k++) {
            tc(k >= 2 ? ts * 16 : 0, (k == 1 || k == 2) ? ts * 16 : 0);
            vtx(f[i][k][0] * FX / 2, f[i][k][1] * FX / 2, f[i][k][2] * FX / 2);
        }
    }
    END_VTXS = 0;
}
/* lat/long sphere-ish mesh, radius 1, with normals */
static void sphere(int seg, s32 ts) {
    for (int j = 0; j < seg / 2; j++) {
        BEGIN_VTXS = 3;
        for (int i = 0; i <= seg; i++)
            for (int k = 0; k < 2; k++) {
                s32 la = (j + k) * 128 / (seg / 2) - 64, lo = i * 256 / seg;
                s32 x = cs(la) * cs(lo) >> 12, y = sn(la), z = cs(la) * sn(lo) >> 12;
                GX_NORMAL = n10(x >> 3) | n10(y >> 3) << 10 | n10(z >> 3) << 20;
                tc(i * ts * 16 / seg, (j + k) * ts * 32 / seg);
                vtx(x, y, z);
            }
        END_VTXS = 0;
    }
}

static void lights(void) {
    LIGHT_VEC = n10(-200) | n10(-250) << 10 | n10(-350) << 20;
    LIGHT_COL = 0x7FFF;
    LIGHT_VEC = (1u << 30) | n10(300) | n10(-100) << 10 | n10(-300) << 20;
    LIGHT_COL = (1u << 30) | rgb(31, 12, 4);
    DIF_AMB = rgb(24, 24, 24) | 1u << 15 | (u32)rgb(6, 6, 8) << 16;
    SPE_EMI = rgb(20, 20, 20) | (u32)rgb(2, 2, 2) << 16;
}

/* ---- scenes ---- */

static void s_formats(u32 t, int half) {
    struct { u32 off; int fmt; u32 pal; } T[7] = {
        {T_DIRECT, 7, 0}, {T_A3I5, 1, P_A3}, {T_4COL, 2, P_4}, {T_16COL, 3, P_16}, {T_256COL, 4, P_256},
        {T_A5I3, 6, P_A5}, {T_4X4, 5, P_4X4}};
    DISP3DCNT = 1 | (half ? (1 << 2) | (1 << 3) : (1 << 3));
    ALPHA_TEST_REF = 12;
    load_proj(4, 0);
    for (int i = 0; i < 7; i++) {
        MTX_PUSH = 0;
        trans((i % 4) * 5000 - 7500, (i / 4) * -5000 + 2500, -12000);
        roty((s32)(t * 2 + i * 30)); rotx((s32)(sn(t + i * 20) >> 7));
        scale(4200, 4200, 4200);
        POLY_ATTR = pa(0, 31, i, 0);
        TEX_PARAM = tex(T[i].off, 2, 2, T[i].fmt, (half && (i == 2 || i == 3 || i == 4) ? COL0 : 0));
        PLTT_BASE = T[i].fmt == 2 ? T[i].pal >> 3 : T[i].pal >> 4;
        quad(32, 32, 0x7FFF, rgb(31, 20, 20), 0x7FFF, rgb(20, 31, 20));
        MTX_POP = 1;
    }
}

static void s_wrap(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 3);
    load_proj(4, 0);
    static const u32 modes[9] = {0, REP_S, REP_T, REP_S | REP_T, REP_S | FLIP_S, REP_T | FLIP_T,
                                 REP_S | REP_T | FLIP_S | FLIP_T, REP_S | FLIP_S | REP_T, REP_T | FLIP_T | REP_S};
    for (int i = 0; i < 9; i++) {
        MTX_PUSH = 0;
        trans((i % 3) * 4800 - 4800, (i / 3) * -3800 + 3800, -11000 - (half ? 3000 : 0));
        rotz((s32)(t + i * 9)); rotx((s32)(sn(t * 2 + i * 7) >> 6));
        scale(3800, 3800, 3800);
        POLY_ATTR = pa(0, 31, 0, 0);
        int wide = i & 1;
        TEX_PARAM = tex(wide ? T_WIDE : T_CHECK64, wide ? 4 : 3, wide ? 2 : 3, 7, modes[i]);
        BEGIN_VTXS = 1;
        s32 o = (s32)(sn(t * 3) >> 4), r = 160 + (s32)(cs(t) >> 6);
        GX_COLOR = 0x7FFF;
        tc(-r * 16 + o, -r * 16);  vtx(-FX / 2, FX / 2, 0);
        tc(-r * 16 + o, r * 16);   vtx(-FX / 2, -FX / 2, 0);
        tc(r * 16 + o, r * 16);    vtx(FX / 2, -FX / 2, 0);
        tc(r * 16 + o, -r * 16);   vtx(FX / 2, FX / 2, 0);
        END_VTXS = 0;
        MTX_POP = 1;
    }
    /* texcoord transform via the texture matrix: a scrolling, rotating floor */
    MTX_MODE = 3; MTX_IDENT = 0; trans((s32)t * 256, (s32)t * 128, 0); rotz((s32)t); MTX_MODE = 2;
    MTX_PUSH = 0;
    trans(0, -6000, -14000); rotx(-48); scale(40000, 40000, 40000);
    TEX_PARAM = tex(T_CHECK64, 3, 3, 7, REP_S | REP_T | (1u << 30));
    POLY_ATTR = pa(0, 31, 1, 0);
    quad(256, 256, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF);
    MTX_POP = 1;
    MTX_MODE = 3; MTX_IDENT = 0; MTX_MODE = 2;
}

static void s_translucent(u32 t, int half) {
    DISP3DCNT = 1 | (half ? (1 << 3) : 0);
    load_proj(4, 0);
    MTX_PUSH = 0;                                           /* opaque backdrop */
    trans(0, 0, -16000); scale(30000, 22000, 4096);
    POLY_ATTR = pa(0, 31, 5, 0); TEX_PARAM = tex(T_CHECK64, 3, 3, 7, REP_S | REP_T);
    quad(256, 192, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF);
    MTX_POP = 1;
    for (int i = 0; i < 12; i++) {
        MTX_PUSH = 0;
        trans((s32)(sn(t * 2 + i * 21) * 2), (s32)(cs(t * 3 + i * 37) * 1), -9000 - i * 400);
        rotz((s32)(t * (i & 1 ? 1 : -1) + i * 20));
        scale(7000, 7000, 7000);
        u32 alpha = 1 + (u32)(i * 29 / 11), id = (i < 6) ? 7 : (u32)(10 + i);
        u32 extra = (i % 3 == 0 ? (1u << 11) : 0) | (i % 4 == 1 ? (1u << 14) : 0);
        POLY_ATTR = pa(0, alpha, id, extra);
        TEX_PARAM = i & 1 ? tex(T_DIRECT, 2, 2, 7, 0) : 0;
        quad(32, 32, rgb(31, i * 2, 0), rgb(0, 31, i * 2), rgb(i * 2, 0, 31), 0x7FFF);
        MTX_POP = 1;
    }
    MTX_PUSH = 0;                                           /* translucent cube with depth update */
    trans(0, 0, -7000); roty((s32)t); rotx((s32)t / 2); scale(3000, 3000, 3000);
    POLY_ATTR = 0 | (3 << 6) | (15u << 16) | (40u << 24) | (1u << 11);
    TEX_PARAM = 0; GX_COLOR = rgb(31, 31, 0);
    cube(8);
    MTX_POP = 1;
}

static void s_shading(u32 t, int half) {
    DISP3DCNT = 1 | (half ? (1 << 1) : 0) | (1 << 3);
    for (int i = 0; i < 32; i++) TOON_TABLE(i) = rgb(i < 10 ? 6 : i < 22 ? 18 : 31, i < 10 ? 4 : i < 22 ? 14 : 28, i < 16 ? 10 : 24);
    load_proj(4, 0);
    lights();
    for (int m = 0; m < 4; m++) {
        MTX_PUSH = 0;
        trans((m & 1) * 7000 - 3500, (m >> 1) * -5200 + 2600, -12000);
        roty((s32)(t * 2 + m * 40)); rotx((s32)(t + m * 10));
        scale(2400, 2400, 2400);
        u32 mode = m == 3 ? 2 : (u32)m;                   /* modulate, decal, toon, toon (lit) */
        POLY_ATTR = (m == 2 ? 0 : 3) | 0xC0 | mode << 4 | (m == 1 ? 24u : 31u) << 16 | (u32)(m + 1) << 24;
        TEX_PARAM = m == 2 ? 0 : tex(m == 1 ? T_A3I5 : T_DIRECT, 2, 2, m == 1 ? 1 : 7, REP_S | REP_T);
        PLTT_BASE = P_A3 >> 4;
        GX_COLOR = rgb(31, 16 + m * 4, 8);
        if (m == 2) {                                      /* unlit toon: per-vertex colours */
            BEGIN_VTXS = 0;
            for (int k = 0; k < 8; k++) {
                s32 a0 = k * 32, a1 = a0 + 32;
                GX_COLOR = rgb(k * 4, 31 - k * 4, 16); vtx(0, 0, FX / 2);
                GX_COLOR = rgb(31, k * 4, 0);          vtx(cs(a0), sn(a0), 0);
                GX_COLOR = rgb(0, 8, 31);              vtx(cs(a1), sn(a1), 0);
            }
            END_VTXS = 0;
        } else sphere(16, 64);
        MTX_POP = 1;
    }
}

static void terrain(u32 t, u32 extra_attr, s32 zoff) {
    static s16 h[17][17];
    for (int y = 0; y <= 16; y++) for (int x = 0; x <= 16; x++)
        h[y][x] = (s16)((sn(x * 19 + (s32)t) * cs(y * 23 + (s32)t / 2)) >> 13);
    MTX_PUSH = 0;
    trans(0, -3000, zoff); rotx(-40); roty((s32)t / 3);
    POLY_ATTR = 0xC0 | 1 | (31u << 16) | extra_attr;
    TEX_PARAM = tex(T_TERRAIN, 5, 5, 3, REP_S | REP_T);
    PLTT_BASE = P_TER >> 4;
    for (int y = 0; y < 16; y++) {
        BEGIN_VTXS = 3;
        for (int x = 0; x <= 16; x++) for (int k = 0; k < 2; k++) {
            int yy = y + k;
            GX_NORMAL = n10((h[yy][x > 0 ? x - 1 : x] - h[yy][x < 16 ? x + 1 : x]) >> 3) | n10(400) << 10 | n10((h[yy > 0 ? yy - 1 : yy][x] - h[yy < 16 ? yy + 1 : yy][x]) >> 3) << 20;
            tc(x * 32 * 16, yy * 32 * 16);
            vtx((x - 8) * 2048, h[yy][x] / 2, (yy - 8) * 2048);
        }
        END_VTXS = 0;
    }
    MTX_POP = 1;
}

static void s_fog_edges(u32 t, int half) {
    u32 phase = (t / 64) & 1;
    DISP3DCNT = 1 | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 7) | (half ? (1 << 6) : 0) | (phase ? 6u : 9u) << 8;
    FOG_COLOR = rgb(20, 22, 28) | (u32)(half ? 20 : 31) << 16;
    FOG_OFFSET = (u16)(0x6000 + (phase ? 0x400 : 0));
    for (int i = 0; i < 32; i++) FOG_TABLE(i) = (u8)(i * 4);
    for (int i = 0; i < 8; i++) EDGE_COLOR(i) = rgb(i * 4, 31 - i * 4, i & 1 ? 31 : 0);
    load_proj(4, 0);
    lights();
    terrain(t, (1u << 15) | (2u << 24), -9000);
    for (int i = 0; i < 6; i++) {                            /* fogged and non-fogged cubes, distinct ids */
        MTX_PUSH = 0;
        trans((i - 3) * 2600 + 1300, -500, -6000 - i * 2500);
        roty((s32)t * 2 + i * 20); scale(1800, 1800 + i * 300, 1800);
        POLY_ATTR = 0xC0 | 1 | (31u << 16) | (u32)(8 + i * 8) << 24 | (i & 1 ? (1u << 15) : 0);
        TEX_PARAM = 0; GX_COLOR = rgb(31, i * 5, 8);
        cube(8);
        MTX_POP = 1;
    }
}

static void s_shadow(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 3);
    load_proj(4, 0);
    lights();
    MTX_PUSH = 0;                                           /* floor */
    trans(0, -2500, -10000); rotx(-64); scale(16000, 16000, 16000);
    POLY_ATTR = pa(0, 31, 3, 0); TEX_PARAM = tex(T_CHECK64, 3, 3, 7, REP_S | REP_T);
    quad(256, 256, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF);
    MTX_POP = 1;
    MTX_PUSH = 0;                                           /* caster */
    trans((s32)sn(t * 2) * 2, 0, -10000); roty((s32)t * 3); scale(1800, 1800, 1800);
    POLY_ATTR = 0xC0 | 1 | (31u << 16) | (9u << 24); TEX_PARAM = 0; GX_COLOR = rgb(31, 20, 4);
    cube(8);
    MTX_POP = 1;
    for (int pass = 0; pass < 2; pass++) {                  /* shadow volume: mask (id 0, back faces), then shadow */
        MTX_PUSH = 0;
        trans((s32)sn(t * 2) * 2, -2000, -10000); scale(2600, 3000, 2600);
        POLY_ATTR = (3u << 4) | (pass ? 0x80 : 0x40) | (u32)(half ? 20 : 12) << 16 | (pass ? (5u << 24) : 0) | (pass && half ? (1u << 15) : 0);
        GX_COLOR = 0;
        cube(8);
        MTX_POP = 1;
    }
}

static void s_2d(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 2) | (1 << 3) | (half ? (1 << 5) : 0);
    ALPHA_TEST_REF = (u8)(half ? 8 : 0);
    for (int i = 0; i < 8; i++) EDGE_COLOR(i) = rgb(31, 31, 31);
    load_proj(4, 1);
    for (int i = 0; i < 24; i++) {                         /* sprites: integer pixel positions, like game UIs */
        MTX_PUSH = 0;
        s32 x = ((i * 37 + (s32)t) % 220) - 110, y = ((i * 53 + (s32)t / 2) % 160) - 80;
        trans(x * FX / 16, y * FX / 16, -FX / 2 - i * 8);
        s32 w = 16 + (i & 3) * 8, hh = 16 + ((i >> 2) & 1) * 16;
        scale(w * FX / 16, hh * FX / 16, FX);
        POLY_ATTR = 0xC0 | (i & 3 ? 31u : 20u) << 16 | (u32)(i + 1) << 24 | (1u << 13);
        TEX_PARAM = i & 1 ? tex(T_CHAR, 2, 3, 6, 0) : tex(T_A3I5, 2, 2, 1, 0);
        PLTT_BASE = (i & 1 ? P_CHR : P_A3) >> 4;
        quad(32, i & 1 ? 64 : 32, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF);
        MTX_POP = 1;
    }
    TEX_PARAM = 0;
    MTX_PUSH = 0;                                           /* wireframe (alpha 0) polygons and lines */
    POLY_ATTR = 0xC0 | (0u << 16) | (50u << 24);
    BEGIN_VTXS = 0;
    for (int i = 0; i < 10; i++) {
        s32 a = (s32)t + i * 25;
        GX_COLOR = rgb(31, i * 3, 0); vtx(0, 0, -FX);
        GX_COLOR = rgb(0, 31, i * 3); vtx(cs(a) * 5, sn(a) * 5, -FX);
        GX_COLOR = rgb(i * 3, 0, 31); vtx(cs(a + 8) * 5, sn(a + 8) * 5, -FX);
    }
    END_VTXS = 0;
    POLY_ATTR = 0xC0 | (31u << 16) | (51u << 24);
    BEGIN_VTXS = 0;                                         /* lines: degenerate triangles */
    for (int i = 0; i < 8; i++) {
        s32 y = (i - 4) * 900 + (s32)sn(t + i * 30) / 4;
        GX_COLOR = rgb(31, 31, i * 4); vtx(-6 * FX, y, -FX); vtx(6 * FX, y + i * 200, -FX); vtx(6 * FX, y + i * 200, -FX);
    }
    END_VTXS = 0;
    POLY_ATTR = 0xC0 | (31u << 16) | (52u << 24) | (1u << 13);   /* 1-dot polygons */
    BEGIN_VTXS = 0;
    for (int i = 0; i < 40; i++) {
        s32 x = (s32)(rnd() % 3000) * 4 - 6000, y = (s32)(rnd() % 2000) * 4 - 4000;
        GX_COLOR = (u16)rnd(); vtx(x, y, -FX); vtx(x + 2, y, -FX); vtx(x, y + 2, -FX);
    }
    END_VTXS = 0;
    MTX_POP = 1;
}

static void s_field(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 2) | (1 << 3) | (1 << 4);
    ALPHA_TEST_REF = 4;
    load_proj(3, 0);
    lights();
    MTX_PUSH = 0;
    rotx(18); trans((s32)sn(t) * 2, -4500, -12000 + (s32)cs(t / 2) * 2);
    for (int ty = 0; ty < 10; ty++) {                       /* ground: 10x10 tiles, 16-colour terrain texture */
        BEGIN_VTXS = 1;
        POLY_ATTR = 0x80 | 1 | (31u << 16) | (1u << 24);
        TEX_PARAM = tex(T_TERRAIN, 5, 5, 3, REP_S | REP_T);
        PLTT_BASE = P_TER >> 4;
        GX_NORMAL = n10(0) | n10(511) << 10 | n10(0) << 20;
        for (int tx = 0; tx < 10; tx++) {
            s32 x0 = (tx - 5) * 2400, z0 = (ty - 7) * 2400, s0 = tx * 512 * 16 / 10, t0 = ty * 512 * 16 / 10;
            tc(s0, t0); vtx(x0, 0, z0);
            tc(s0, t0 + 820); vtx(x0, 0, z0 + 2400);
            tc(s0 + 820, t0 + 820); vtx(x0 + 2400, 0, z0 + 2400);
            tc(s0 + 820, t0); vtx(x0 + 2400, 0, z0);
        }
        END_VTXS = 0;
    }
    for (int i = 0; i < 8; i++) {                           /* billboard characters */
        MTX_PUSH = 0;
        trans((i % 4) * 3000 - 4500 + (s32)sn(t * 2 + i * 32) / 3, 1600, (i / 4) * -4000 - 2000);
        rotx(-18); scale(1600, 3200, 1600);
        POLY_ATTR = 0xC0 | (31u << 16) | (u32)(20 + i) << 24 | (1u << 11);
        TEX_PARAM = tex(T_CHAR, 2, 3, 6, (i & 1) ? FLIP_S | REP_S : 0);
        PLTT_BASE = P_CHR >> 4;
        quad(i & 1 ? 64 : 32, 64, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF);
        MTX_POP = 1;
    }
    MTX_PUSH = 0;                                           /* a translucent water pond */
    trans(2500, 30, -6000); rotx(-64); scale(5000, 3000, 4096);
    POLY_ATTR = 0xC0 | (14u << 16) | (60u << 24); TEX_PARAM = 0;
    quad(8, 8, rgb(4, 12, 31), rgb(6, 16, 31), rgb(4, 12, 28), rgb(8, 20, 31));
    MTX_POP = 1;
    MTX_POP = 1;
    (void)half;
}

static void s_rear(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 3) | (1 << 14) | (half ? (1 << 7) | (1 << 5) : 0);
    CLRIMAGE_OFFSET = (u16)((t & 0xFF) | ((t / 2) & 0xFF) << 8);
    FOG_COLOR = rgb(31, 0, 31) | 31u << 16; FOG_OFFSET = 0x4000;
    for (int i = 0; i < 32; i++) FOG_TABLE(i) = (u8)(i * 4);
    load_proj(4, 0);
    lights();
    for (int i = 0; i < 4; i++) {
        MTX_PUSH = 0;
        trans((i - 2) * 3000 + 1500, (s32)sn(t + i * 64), -8000 - i * 1500);
        roty((s32)t * 2 + i * 32); rotx((s32)t);
        scale(2000, 2000, 2000);
        POLY_ATTR = 0xC0 | 1 | (i == 3 ? 16u : 31u) << 16 | (u32)(i + 1) << 24 | (1u << 15);
        TEX_PARAM = tex(T_DIRECT, 2, 2, 7, REP_S | REP_T); GX_COLOR = 0x7FFF;
        cube(32);
        MTX_POP = 1;
    }
}

static void s_edge_cases(u32 t, int half) {
    DISP3DCNT = 1 | (1 << 3) | (half ? (1 << 4) : 0);
    load_proj(4, 0);
    MTX_PUSH = 0;                                           /* a floor crossing the near plane */
    trans(0, -1200, -1000); rotx(-60); roty((s32)t); scale(60000, 60000, 60000);
    POLY_ATTR = pa(0, 31, 1, 1u << 12); TEX_PARAM = tex(T_CHECK64, 3, 3, 7, REP_S | REP_T);
    quad(1024, 1024, 0x7FFF, rgb(31, 0, 0), 0x7FFF, rgb(0, 0, 31));
    MTX_POP = 1;
    MTX_PUSH = 0;                                           /* rotating cube clipped by the near plane: n-gon clips */
    trans(0, 0, -1200 + (s32)sn(t * 2) / 3); roty((s32)t * 3); rotx((s32)t * 2); scale(2500, 2500, 2500);
    POLY_ATTR = pa(0, 31, 2, 0); TEX_PARAM = tex(T_DIRECT, 2, 2, 7, 0); GX_COLOR = 0x7FFF;
    cube(32);
    MTX_POP = 1;
    TEX_PARAM = 0;
    MTX_PUSH = 0;                                           /* thin slivers and tiny triangles */
    trans(0, 0, -6000);
    POLY_ATTR = pa(0, 31, 3, 0);
    BEGIN_VTXS = 0;
    for (int i = 0; i < 64; i++) {
        s32 a = (s32)t + i * 4, r0 = 200 + (i & 7) * 40;
        GX_COLOR = rgb(i, 31 - (i >> 1), (i * 3) & 31);
        vtx(cs(a) * r0 >> 9, sn(a) * r0 >> 9, 0);
        vtx(cs(a) * (r0 + 3000) >> 12, sn(a) * (r0 + 3000) >> 12, -200);
        vtx(cs(a + 1) * (r0 + 3000) >> 12, sn(a + 1) * (r0 + 3000) >> 12, -200);
    }
    for (int i = 0; i < 64; i++) {
        s32 x = (s32)(rnd() % 8000) - 4000, y = (s32)(rnd() % 6000) - 3000, s = 4 + (i & 15);
        GX_COLOR = (u16)rnd(); vtx(x, y, 100); vtx(x + s, y, 100); vtx(x, y + s, 100);
    }
    END_VTXS = 0;
    MTX_POP = 1;
    MTX_PUSH = 0;                                           /* a huge triangle spanning all bins, half off-screen */
    trans(0, 0, -5000); rotz((s32)t);
    POLY_ATTR = pa(0, 20, 4, 0);
    BEGIN_VTXS = 0;
    GX_COLOR = rgb(31, 0, 0); vtx(-30000, -30000, 0);
    GX_COLOR = rgb(0, 31, 0); vtx(30000, -30000, -4000);
    GX_COLOR = rgb(0, 0, 31); vtx(0, 30000, 2000);
    END_VTXS = 0;
    MTX_POP = 1;
}

int main(void) {
    POWCNT1 = 0x820F;
    DISPCNT_A = 0x10000 | (1 << 8) | (1 << 3);
    DISPCNT_B = 0;
    CLEAR_COLOR = rgb(4, 4, 10) | (31u << 16) | (63u << 24);
    CLEAR_DEPTH = 0x7FFF;
    VIEWPORT = 0 | (0 << 8) | (255u << 16) | (191u << 24);
    textures();
    MTX_MODE = 0; MTX_IDENT = 0; MTX_MODE = 1; MTX_IDENT = 0; MTX_MODE = 3; MTX_IDENT = 0; MTX_MODE = 2; MTX_IDENT = 0;

    u32 frame = 0;
    for (;;) {
        u32 scene = cfg.level % NSCENES, in_scene = frame;
        if (cfg.mode == 1) { scene = (frame / cfg.ramp) % NSCENES; in_scene = frame % cfg.ramp; }
        int half = in_scene * 2 >= cfg.ramp;
        if (cfg.mode == 0) half = (frame / 120) & 1;
        MTX_MODE = 2; MTX_IDENT = 0;
        u32 swap = 0;
        switch (scene) {
        case 0: s_formats(frame, half); break;
        case 1: s_wrap(frame, half); break;
        case 2: s_translucent(frame, half); swap = half ? 1 : 0; break;   /* manual translucent sort half the time */
        case 3: s_shading(frame, half); break;
        case 4: s_fog_edges(frame, half); break;
        case 5: s_shadow(frame, half); break;
        case 6: s_2d(frame, half); break;
        case 7: s_field(frame, half); swap = 2; break;                    /* w-buffering */
        case 8: s_rear(frame, half); break;
        case 9: s_edge_cases(frame, half); swap = half ? 2 : 0; break;
        }
        SWAP_BUF = swap;
        wait_vblank();
        frame++;
    }
}
