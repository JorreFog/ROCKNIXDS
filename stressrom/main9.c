// dsstress ARM9: a tunable 3D stress load for DraStic.
//
// Each "layer" is a full-screen 16x12 quad-strip surface (192 quads, 408 vertices) whose
// heights, normals and texture coordinates are recomputed on the ARM9 every frame, then lit
// and textured by the geometry engine. Odd layers are translucent, so every layer adds a full
// screen of overdraw for the rasterizer, plus alpha blending and translucent-polygon sorting.
// Level N draws N layers: N*192 polygons, N*408 vertices (level 10 = 1920 polys, under the
// DS 2048-polygon / 6144-vertex limits).
//
// The config block at the start of the ARM9 binary (crt9.s) is patched by build.py:
//   mode 0 = fixed level, 1 = ramp 1..max, advancing every `ramp` frames, then holding max
//   cpu  = extra ARM9 work per frame (iterations of an integer hash loop), to add CPU load
// Top screen: the 3D scene. Bottom screen: the current level as a bar of blocks, and a block
// that steps one position per emulated frame (a visible frame-drop indicator).
#include "tables.h"

typedef unsigned int u32; typedef unsigned short u16; typedef int s32; typedef short s16;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))

extern volatile struct { u32 magic, mode, level, ramp, max, cpu; } cfg;

#define POWCNT1     R32(0x04000304)
#define DISPCNT_A   R32(0x04000000)
#define DISPCNT_B   R32(0x04001000)
#define BG3CNT_B    R16(0x0400100E)
#define BG3PA_B     R16(0x04001030)
#define BG3PB_B     R16(0x04001032)
#define BG3PC_B     R16(0x04001034)
#define BG3PD_B     R16(0x04001036)
#define VCOUNT      R16(0x04000006)
#define VRAMCNT_A   (*(volatile unsigned char *)0x04000240)
#define VRAMCNT_C   (*(volatile unsigned char *)0x04000242)
#define DISP3DCNT   R32(0x04000060)
#define CLEAR_COLOR R32(0x04000350)
#define CLEAR_DEPTH R32(0x04000354)
#define GXSTAT      R32(0x04000600)
#define MTX_MODE    R32(0x04000440)
#define MTX_PUSH    R32(0x04000444)
#define MTX_POP     R32(0x04000448)
#define MTX_IDENT   R32(0x04000454)
#define MTX_LOAD44  R32(0x04000458)
#define MTX_MULT33  R32(0x04000468)
#define MTX_TRANS   R32(0x04000470)
#define GX_COLOR    R32(0x04000480)
#define GX_NORMAL   R32(0x04000484)
#define GX_TEXCOORD R32(0x04000488)
#define GX_VTX16    R32(0x0400048C)
#define POLY_ATTR   R32(0x040004A4)
#define TEX_PARAM   R32(0x040004A8)
#define DIF_AMB     R32(0x040004C0)
#define SPE_EMI     R32(0x040004C4)
#define LIGHT_VEC   R32(0x040004C8)
#define LIGHT_COL   R32(0x040004CC)
#define BEGIN_VTXS  R32(0x04000500)
#define END_VTXS    R32(0x04000504)
#define SWAP_BUF    R32(0x04000540)
#define VIEWPORT    R32(0x04000580)

#define COLS 16
#define ROWS 12
#define STEP 3072                      /* 0.75 in 4.12 */
#define TEX  64

static s16 h[ROWS + 1][COLS + 1];      /* heights, 4.12 */
static volatile u32 sink;

/* the libc calls clang may emit for struct copies / loops */
void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

static inline s32 sn(s32 a) { return sintab[a & 255]; }
static inline s32 cs(s32 a) { return sintab[(a + 64) & 255]; }
static inline u32 n10(s32 v) { if (v > 511) v = 511; if (v < -511) v = -511; return (u32)v & 0x3FF; }

static void wait_vblank(void) {
    while (VCOUNT >= 192) ;
    while (VCOUNT < 192) ;
}

static void textures(void) {
    volatile u16 *t = (volatile u16 *)0x06800000;           /* bank A, LCDC mapping */
    VRAMCNT_A = 0x80;
    for (int y = 0; y < TEX; y++)                           /* texture 0: coloured checker */
        for (int x = 0; x < TEX; x++) {
            int c = ((x >> 3) ^ (y >> 3)) & 1;
            t[y * TEX + x] = 0x8000 | (c ? (31 | (x >> 1) << 5 | (y >> 1) << 10) : ((y >> 2) << 5 | 8 << 10));
        }
    t += TEX * TEX;
    for (int y = 0; y < TEX; y++)                           /* texture 1: rings */
        for (int x = 0; x < TEX; x++) {
            int dx = x - 32, dy = y - 32, r = (dx * dx + dy * dy) >> 4;
            t[y * TEX + x] = 0x8000 | ((r & 31) << 10) | (((r >> 1) & 31) << 5) | (31 - (r & 31));
        }
    VRAMCNT_A = 0x83;                                        /* texture slot 0 */
}

static void sub_screen_init(void) {
    VRAMCNT_C = 0x84;                                        /* sub BG at 0x06200000 */
    DISPCNT_B = 0x10000 | 5 | (1 << 11);                     /* mode 5, BG3 */
    BG3CNT_B = 0x4084;                                       /* 256x256 direct-colour bitmap */
    BG3PA_B = 0x100; BG3PB_B = 0; BG3PC_B = 0; BG3PD_B = 0x100;
    volatile u16 *p = (volatile u16 *)0x06200000;
    for (int i = 0; i < 256 * 192; i++) p[i] = 0x8000 | (4 << 10) | (2 << 5) | 2;
}

static void rect(int x0, int y0, int w, int hh, u16 c) {
    volatile u16 *p = (volatile u16 *)0x06200000;
    for (int y = y0; y < y0 + hh; y++) for (int x = x0; x < x0 + w; x++) p[y * 256 + x] = c;
}

static void sub_screen_frame(u32 level, u32 frame) {
    for (u32 i = 0; i < 10; i++)                             /* level bar: 10 slots */
        rect(18 + i * 23, 40, 20, 40, i < level ? (0x8000 | (31 - i * 3) | ((i * 3) << 5) | (10 << 10)) : 0x8000 | 0x1084);
    u32 pos = frame & 31, prev = (frame - 1) & 31;           /* frame-step indicator */
    rect(prev * 8, 120, 8, 8, 0x8000 | 0x0421);
    rect(pos * 8, 120, 8, 8, 0xFFFF);
}

static void rot33z(s32 a) {
    s32 c = cs(a), s = sn(a);
    MTX_MULT33 = c; MTX_MULT33 = s; MTX_MULT33 = 0;
    MTX_MULT33 = -s; MTX_MULT33 = c; MTX_MULT33 = 0;
    MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = 4096;
}
static void rot33x(s32 a) {
    s32 c = cs(a), s = sn(a);
    MTX_MULT33 = 4096; MTX_MULT33 = 0; MTX_MULT33 = 0;
    MTX_MULT33 = 0; MTX_MULT33 = c; MTX_MULT33 = s;
    MTX_MULT33 = 0; MTX_MULT33 = -s; MTX_MULT33 = c;
}

static void layer(u32 i, u32 t) {
    /* per-frame surface: CPU work proportional to the vertex count */
    for (int y = 0; y <= ROWS; y++)
        for (int x = 0; x <= COLS; x++)
            h[y][x] = (s16)((sn(x * 16 + t * 3 + i * 40) * cs(y * 21 + t * 2 + i * 17)) >> 13);

    MTX_PUSH = 0;
    MTX_TRANS = 0; MTX_TRANS = 0; MTX_TRANS = (u32)(-(22528 + (s32)i * 700));   /* z = -5.5 - 0.17 i */
    rot33z((s32)(t + i * 26));
    rot33x((s32)(sn(t + i * 9) >> 8));                       /* gentle +-16 step tilt */

    u32 translucent = i & 1;
    POLY_ATTR = 1 | 0xC0 | ((translucent ? 18u : 31u) << 16) | ((i + 1) << 24);
    TEX_PARAM = ((i & 1) ? (TEX * TEX * 2) >> 3 : 0) | (1 << 16) | (1 << 17) | (3 << 20) | (3 << 23) | (7u << 26);
    DIF_AMB = (0x7FFF) | (1 << 15) | (0x2108u << 16);         /* diffuse white (also vertex colour), dim ambient */
    GX_COLOR = 0x7FFF;

    s32 x0 = -COLS * STEP / 2, y0 = -ROWS * STEP / 2;
    for (int y = 0; y < ROWS; y++) {
        BEGIN_VTXS = 3;                                      /* quad strip */
        for (int x = 0; x <= COLS; x++) {
            for (int k = 0; k < 2; k++) {
                int yy = y + k;
                s32 nx = (h[yy][x > 0 ? x - 1 : x] - h[yy][x < COLS ? x + 1 : x]) >> 2;
                s32 ny = (h[yy > 0 ? yy - 1 : yy][x] - h[yy < ROWS ? yy + 1 : yy][x]) >> 2;
                GX_NORMAL = n10(nx) | n10(ny) << 10 | n10(440) << 20;
                GX_TEXCOORD = (u32)((x * 64 + t * 4) & 0xFFFF) | (u32)(yy * 64 + i * 32) << 16;
                s32 vx = x0 + x * STEP, vy = y0 + yy * STEP;
                GX_VTX16 = ((u32)vx & 0xFFFF) | ((u32)vy << 16);
                GX_VTX16 = (u32)h[yy][x] & 0xFFFF;
            }
        }
        END_VTXS = 0;
    }
    MTX_POP = 1;
}

int main(void) {
    POWCNT1 = 0x820F;                                        /* LCDs, 2D A+B, 3D render+geometry, A on top */
    DISPCNT_A = 0x10000 | (1 << 8) | (1 << 3);               /* BG0 = 3D */
    DISP3DCNT = 1 | (1 << 3) | (1 << 4);                     /* textures, alpha blend, anti-alias */
    CLEAR_COLOR = 0x1063 | (31u << 16) | (63u << 24);        /* opaque dark blue, poly id 63 */
    CLEAR_DEPTH = 0x7FFF;
    VIEWPORT = 0 | (0 << 8) | (255u << 16) | (191u << 24);
    textures();
    sub_screen_init();

    MTX_MODE = 0; MTX_IDENT = 0;
    for (int k = 0; k < 16; k++) MTX_LOAD44 = (u32)proj[k];
    MTX_MODE = 2; MTX_IDENT = 0;
    LIGHT_VEC = n10(-150) | n10(-200) << 10 | n10(-420) << 20;
    LIGHT_COL = 0x7FFF;
    SPE_EMI = 0;

    u32 frame = 0, ramp_level = 1, ramp_count = 0;         /* no hardware divide needed */
    for (;;) {
        u32 level = cfg.level;
        if (cfg.mode == 1) {
            if (++ramp_count > cfg.ramp) { ramp_count = 1; if (ramp_level < cfg.max) ramp_level++; }
            level = ramp_level;
        }
        if (level > 10) level = 10;

        MTX_IDENT = 0;
        for (u32 i = 0; i < level; i++) layer(i, frame);
        for (u32 i = 0, x = frame; i < cfg.cpu; i++) x = x * 1103515245u + 12345u, sink = x;
        SWAP_BUF = 0;                                         /* auto-sort translucent, Z-buffering */
        wait_vblank();
        sub_screen_frame(level, frame);
        frame++;
    }
}
