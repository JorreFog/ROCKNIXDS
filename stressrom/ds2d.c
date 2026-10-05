// ds2d ARM9: engine A 2D feature scenes, for testing a DS 2D renderer against DraStic's and for profiling it.
//
// Determinism. Every displayed frame is a function of the frame counter (vblanks since boot) and nothing else: no
// input, no timers, no dependence on CPU speed beyond "the vblank work fits in vblank" (a build with DS2D_CHECK
// defined halts with a white screen if it ever does not). All state for the next frame (registers, OAM, palettes,
// VRAM contents and mappings, HBlank-DMA tables) is written in vblank, so the renderer sees one consistent state per
// frame. The deliberate mid-frame changes use the DS's own per-line mechanism, an HBlank DMA (one table entry per
// line, written at the hblank before the line), or, for the OAM multiplexing of T4, a CPU rewrite between lines that
// no sprite of either set touches, so the result does not depend on the exact line of the write. DraStic logs
// register, palette and OAM writes with their line and replays them per line; an HBlank DMA into VRAM (T3), VRAM
// display mode (T3, T9) and main-memory display mode (T3) make it render line by line at each hblank ("catch-up").
// No state crosses a quarter boundary: each quarter starts from reset 2D registers, palettes and OAM, rewrites every
// page of the VRAM it uses, and (T9) submits its own 3D geometry while the display is off. So the frame at (scene,
// quarter, t) is the same in the cycle ROM and in the scene's own ROM, whatever ran before (checked frame by frame).
//
// Content. Tiles, maps, bitmaps, sprite sheets and extended palettes are built once at boot into a library in main
// RAM (LIB, 1.7 MB, ready at frame 36); each scene quarter declares the VRAM bank mapping and which library pages each bank holds
// (plans: all pages of every bank it maps), copied by DMA while the display is off (white: 2 to 5 frames at every
// scene or quarter start, 128K a frame).
//
// The config block (crt9.s, patched by build.py) picks the scene: mode 0 = fixed scene `level`, mode 1 = cycle
// through the ten scenes, `ramp` frames each (build.py: 240 = 4 s). A scene runs in four quarters of ramp/4 frames
// (60 in a fixed-scene ROM, where the quarters repeat every 4 s); each quarter is a variant, and many switch a
// sub-feature at the half. A one-line title (eight 32x8 sprites, OAM 120-127) names the scene and the quarter.
//   T0 text BGs      4bpp + 8bpp tiled layers in all four map sizes (512x512, 512x256, 256x512, 256x256), scrolling
//                    across the map edges, flipped tiles (four states), 16 palette banks; q0 priorities 1,0,2,3, q1
//                    ties (BG0 = BG1) and an 8bpp layer on top, q2 per-line BG3 HOFS wave + backdrop gradient
//                    (HBlank DMA to BG3HOFS and to palette entry 0), q3 all priorities 0 (BG number order) with
//                    DISPCNT.7 (forced blank) set 4 frames in 16.
//   T1 ext palettes  8bpp text BGs with BG extended palettes (bank E): 16 palettes a slot, BG0 slot 0/2 and BG1 slot
//                    1/3 (BGxCNT.13) changed per quarter / every 16 frames in q3, ext palettes off in q1.
//   T2 affine BGs    DISPCNT char/screen base 64K for all layers (q0-q2). q0 mode 2: rotating 256x256 affine BG with
//                    wrap over a zooming 512x512 one without wrap; q1 mode 1: a perspective floor (1024x1024 affine,
//                    wrap) from a per-line HBlank DMA of BG3PA..BG3Y, sky lines from single texels, and DISPCNT per
//                    line (mode 0 without BG3 above line 32); q2 mode 4 / 5: extended affine BGs (16-bit maps
//                    512x512 and 256x256: flips, palette numbers, ext palettes on/off) and a 128x128 affine BG with
//                    wrap toggling; q3 mode 6: the large 8bpp bitmap (a 512x1024 picture of labelled 64x64 cells
//                    in banks A-D), 512x1024 then the same memory as 1024x512, rotating, wrap toggling.
//   T3 bitmap BGs    q0 mode 5: a 16-bit 256x256 bitmap at identity (DraStic's "direct layer"; every other 16 frames
//                    shifted one pixel, which is the general path) over a rotating 8bpp bitmap, then with a window
//                    hiding it and panels alpha-blended over it; q1 mode 3: 16-bit 512x256 scrolling with wrap; q2
//                    mode 4 / 5: rotated/zoomed bitmaps, 16-bit 256x256 over an 8-bit-map affine BG, then 16-bit
//                    128x128 (clipped) over 8bpp 128x128 (wrap) from bank C; q3 in thirds: VRAM
//                    display mode (bank B), main-memory display mode (a start-mode-4 DMA), and an image streamed
//                    into one bitmap row per line by HBlank DMA into VRAM.
//   T4 sprites       the 12 sizes in 4bpp and 8bpp, priorities interleaved with two BG layers, flips; q0 1D mapping
//                    (32- then 64-byte boundary), q1 1D 128- then 256-byte boundary + OBJ ext palettes + 90 sprites
//                    (32 pixels each, in OAM order left to right) on the same lines with "OBJ in hblank" toggling;
//                    q2 2D mapping + OAM multiplexing (OAM 0-11 rewritten at line 96: the 4bpp row above, the 8bpp
//                    row below); q3 2D: overlapping sprites with OAM order against priority, a disabled sprite, X/Y
//                    wraparound at 512/256.
//   T5 OBJ effects   q0 affine sprites (8 parameter groups: rotation, scale, mirror) normal and double size; q1 bitmap
//                    sprites (1D, 128- then 256-byte units; alpha 0..15, flips, affine) blended with the 2nd targets;
//                    q2 semi-transparent tile sprites (OBJ mode 1, 2D mapping) with BLDCNT (2nd target BG3 only,
//                    brightness effect selected) and 2D bitmap sprites (128-dot layout); q3 the full-screen OBJ
//                    bitmap (12 64x64 bitmap sprites tiling the screen: DraStic's OBJ "image" path), the special case
//                    broken (one sprite moved by a pixel) every other 16 frames.
//   T6 windows       q0 WIN0 / WIN1 rectangles moving and overlapping, layers and effects selected per region; q1 the
//                    OBJ window (an affine double-size sprite and a 32x64 sprite in OBJ mode 2) with WIN0 and WIN1
//                    (lines with one, two and all three windows: DraStic's triple-window mask path); q2 edge
//                    cases: X1 > X2, Y1 > Y2, Y2 > 192, X1 = X2, full height; q3 a round spotlight from a per-line
//                    WIN0H (HBlank DMA), darkened outside.
//   T7 effects       q0 alpha BG over BG (EVA/EVB sweep, EVA+EVB > 16, values > 16); q1 OBJ and semi-transparent OBJ
//                    over BG, backdrop as 2nd target; q2 brightness up then down (EVY sweep past 16); q3 master
//                    brightness up / down fades with alpha blending and per-frame palette cycling.
//   T8 mosaic        q0 text BG mosaic (H and V sizes sweeping); q1 affine BG mosaic (mode 2: left half, through WIN0,
//                    next to the same BG without mosaic) then a 16-bit bitmap BG (mode 5); q2 OBJ mosaic, normal and
//                    affine sprites; q3 MOSAIC changed per line (HBlank DMA bands).
//   T9 3D            BG0 = 3D (spinning opaque and translucent quads, transparent clear colour). q0 3D at priority 1
//                    between 2D layers and sprites, BG0HOFS shifting the 3D layer; q1 3D blending (per-pixel alpha
//                    over 2nd targets), brightness on the 3D layer, a window without 3D and one without effects; q2
//                    display capture every frame: a feedback loop through a 16-bit bitmap BG at identity (banks C/D
//                    alternate as capture target and BG; DraStic's hi-res capture path), then a captured frame shown
//                    as the full-screen OBJ bitmap (the screen, then the 3D layer alone); q3 VRAM display mode of
//                    bank D with a blended capture (3D, then the whole screen with VRAM read offset 32K, over the
//                    previous frame): motion trails, every other 8 frames of the second half the VRAM source alone.
// Engine A is on the top LCD; engine B is off (white).
// What DraStic r2.5.2.2 does with these (the oracle, so a replacement must do the same): OBJ-OBJ order is priority
// first, then OAM index; no per-line OBJ limit (all 90 crowd sprites are drawn); no OBJ mosaic; DISPCNT.7 is
// ignored; when the 12 image sprites of T5 q3 / T9 q2 qualify for its full-screen OBJ path, every OBJ pixel takes
// its colour from the image (the 2x capture when there is one): the title and the moving sprites vanish into a
// screen image (they come back in the "broken" frames) and show as silhouettes in the 3D clear colour after the
// capture of the 3D layer alone (T9 q2 from tq 46; the OBJ palette plays no part); an affine OBJ can draw one extra
// pixel at its edge whose texel is fetched up to 64K past the sprite's data (so all OBJ VRAM pages are planned); a
// capture of the VRAM source alone (T9 q3) leaves its destination unchanged (the VRAM display does not scroll).
#include "tables.h"

typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8; typedef int s32; typedef short s16;
typedef unsigned long long u64;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a)  (*(volatile u8 *)(a))

extern volatile struct { u32 magic, mode, level, ramp, max, cpu; } cfg;

/* ---------------------------------------------------------------------------------------------- registers */
#define POWCNT1       R32(0x04000304)
#define DISPCNT       R32(0x04000000)
#define DISPCNT_B     R32(0x04001000)
#define VCOUNT        R16(0x04000006)
#define BGCNT(n)      R16(0x04000008 + 2 * (n))
#define BGHOFS(n)     R16(0x04000010 + 4 * (n))
#define BGVOFS(n)     R16(0x04000012 + 4 * (n))
#define BGPA(n)       R16(0x04000020 + 16 * ((n) - 2))
#define BGPB(n)       R16(0x04000022 + 16 * ((n) - 2))
#define BGPC(n)       R16(0x04000024 + 16 * ((n) - 2))
#define BGPD(n)       R16(0x04000026 + 16 * ((n) - 2))
#define BGX(n)        R32(0x04000028 + 16 * ((n) - 2))
#define BGY(n)        R32(0x0400002C + 16 * ((n) - 2))
#define WIN0H         R16(0x04000040)
#define WIN1H         R16(0x04000042)
#define WIN0V         R16(0x04000044)
#define WIN1V         R16(0x04000046)
#define WININ         R16(0x04000048)
#define WINOUT        R16(0x0400004A)
#define MOSAIC        R16(0x0400004C)
#define BLDCNT        R16(0x04000050)
#define BLDALPHA      R16(0x04000052)
#define BLDY          R16(0x04000054)
#define DISP3DCNT     R32(0x04000060)
#define DISPCAPCNT    R32(0x04000064)
#define MASTER_BRIGHT R16(0x0400006C)
#define DMASAD(c)     R32(0x040000B0 + 12 * (c))
#define DMADAD(c)     R32(0x040000B4 + 12 * (c))
#define DMACNT(c)     R32(0x040000B8 + 12 * (c))
#define VRAMCNT(n)    R8(0x04000240 + (n))          /* 0..6 = banks A..G (7 is WRAMCNT: never written) */
#define CLEAR_COLOR   R32(0x04000350)
#define CLEAR_DEPTH   R16(0x04000354)
#define MTX_MODE      R32(0x04000440)
#define MTX_IDENT     R32(0x04000454)
#define MTX_LOAD44    R32(0x04000458)
#define MTX_MULT33    R32(0x04000468)
#define MTX_TRANS     R32(0x04000470)
#define GX_COLOR      R32(0x04000480)
#define GX_VTX16      R32(0x0400048C)
#define POLY_ATTR     R32(0x040004A4)
#define TEX_PARAM     R32(0x040004A8)
#define BEGIN_VTXS    R32(0x04000500)
#define END_VTXS      R32(0x04000504)
#define SWAP_BUF      R32(0x04000540)
#define VIEWPORT      R32(0x04000580)

#define PAL_BG   ((volatile u16 *)0x05000000)
#define PAL_OBJ  ((volatile u16 *)0x05000200)
#define OBJ_VRAM 0x06400000

/* DISPCNT */
#define DC_MODE(m)    ((u32)(m))
#define DC_3D         (1u << 3)
#define DC_OBJ1D      (1u << 4)            /* tile OBJ 1D mapping */
#define DC_BMP256     (1u << 5)            /* bitmap OBJ 2D mapping 256 dots wide (else 128) */
#define DC_BMP1D      (1u << 6)            /* bitmap OBJ 1D mapping */
#define DC_BG(n)      (1u << (8 + (n)))
#define DC_OBJ        (1u << 12)
#define DC_WIN0       (1u << 13)
#define DC_WIN1       (1u << 14)
#define DC_OBJWIN     (1u << 15)
#define DC_ON         (1u << 16)           /* display mode 1: graphics */
#define DC_VRAMDISP(b) ((2u << 16) | ((u32)(b) << 18))
#define DC_BOUND(n)   ((u32)(n) << 20)     /* tile OBJ 1D boundary 32 << n bytes */
#define DC_HBLANKOBJ  (1u << 23)
#define DC_CHARBASE(n) ((u32)(n) << 24)
#define DC_SCRBASE(n) ((u32)(n) << 27)
#define DC_BGEXT      (1u << 30)
#define DC_OBJEXT     (1u << 31)
/* BGxCNT flags */
#define BG_MOS   0x0040
#define BG_256   0x0080
#define BG_WRAP  0x2000                    /* BG2/3: area overflow wraps around */
#define BG_SLOT  0x2000                    /* BG0/1: extended palette slot 2/3 instead of 0/1 */
#define BG_BMP8  0x0080                    /* extended BG2/3: 256-colour bitmap */
#define BG_BMP16 0x0084                    /* extended BG2/3: direct-colour bitmap */
/* window enables */
#define W_BG(n)  (1u << (n))
#define W_OBJ    0x10
#define W_FX     0x20
/* BLDCNT */
#define BL_ALPHA (1u << 6)
#define BL_UP    (2u << 6)
#define BL_DOWN  (3u << 6)
#define T_BG(n)  (1u << (n))
#define T_OBJ    0x10
#define T_BD     0x20
#define T2(m)    ((u32)(m) << 8)
/* OAM attributes */
#define A0_AFF   (1u << 8)
#define A0_DBL   (1u << 9)                 /* with A0_AFF: double size; without: disabled */
#define A0_SEMI  (1u << 10)
#define A0_WIN   (2u << 10)
#define A0_BMP   (3u << 10)
#define A0_MOS   (1u << 12)
#define A0_8BPP  (1u << 13)
#define A1_HFLIP (1u << 12)
#define A1_VFLIP (1u << 13)
/* DMA */
#define DMA_ON       (1u << 31)
#define DMA_32       (1u << 26)
#define DMA_REPEAT   (1u << 25)
#define DMA_HBLANK   (2u << 27)
#define DMA_DST_FIX  (2u << 21)
#define DMA_DST_RELOAD (3u << 21)

#define NSCENES 10
#define FX 4096

/* the content library in main RAM (built once at boot, copied into the VRAM banks a scene needs) */
#define LIB        0x02100000u
#define L_A0       (LIB + 0x000000)       /* bank A, lower 64K: 4bpp tiles, 8bpp tiles, text and small affine maps */
#define L_A1AFF    (LIB + 0x010000)       /* bank A, upper 64K (T2's DISPCNT bases 64K, other scenes): affine content */
#define L_A1BMP8   (LIB + 0x020000)       /* bank A, upper 64K for T3: 8bpp 256x256 bitmap */
#define L_BMP16    (LIB + 0x030000)       /* 16-bit 256x256 bitmap, 128K */
#define L_BMP16W   (LIB + 0x050000)       /* 16-bit 512x256 bitmap, 256K */
#define L_OBJ1D    (LIB + 0x090000)       /* OBJ VRAM image, 1D mapping, 64K */
#define L_OBJ2D    (LIB + 0x0A0000)       /* OBJ VRAM image, 2D mapping, 64K (L_OBJ1D + 128K = both) */
#define L_OBJFULL  (LIB + 0x0B0000)       /* OBJ VRAM image: 2D sheet + full-screen bitmap (256-wide 2D), 128K */
#define L_EXTBG    (LIB + 0x0D0000)       /* BG extended palettes, 4 slots, 32K */
#define L_EXTOBJ   (LIB + 0x0D8000)       /* OBJ extended palette, 8K (16K page) */
#define L_SMALL    (LIB + 0x0DC000)       /* 16-bit 128x128 bitmap, 8bpp 128x128 bitmap, zeros: 128K */
#define L_STREAM   (LIB + 0x0FC000)       /* 16-bit 256x448 image streamed by HBlank DMA (T3), 224K */
#define L_LARGE    (LIB + 0x134000)       /* 8bpp 512x1024 large bitmap (mode 6), 512K */
#define L_END      (LIB + 0x1B4000)       /* below the ARM7 stub (0x02380000, build.py) and the stack (0x023F0000) */
_Static_assert(L_END <= 0x02380000u, "the library would overwrite the ARM7 stub");

/* ---------------------------------------------------------------------------------- libc and EABI helpers */
void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
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
static inline u16 rgb(u32 r, u32 g, u32 b) { return (u16)((r & 31) | (g & 31) << 5 | (b & 31) << 10); }
static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }
static inline s32 imax(s32 a, s32 b) { return a > b ? a : b; }
static inline s32 clampi(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 isqrt(u32 v) {
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
/* triangle wave 0..n..0 over a period of 2n frames */
static s32 tri(u32 t, s32 n) { s32 k = (s32)(t % (u32)(2 * n)); return k < n ? k : 2 * n - k; }

static void wait_vblank(void) { while (VCOUNT >= 192) {} while (VCOUNT < 192) {} }

static void dma_copy(u32 dst, u32 src, u32 bytes) {               /* immediate, 32-bit units, channel 3 */
    DMASAD(3) = src; DMADAD(3) = dst; DMACNT(3) = DMA_ON | DMA_32 | (bytes >> 2);
    while (DMACNT(3) & DMA_ON) {}
}
/* HBlank DMA: `units` per line from consecutive table entries, the destination rewritten every line */
static void hdma(int c, const void *src, u32 dst, u32 units, u32 flags) {
    DMACNT(c) = 0;
    DMASAD(c) = (u32)src; DMADAD(c) = dst;
    DMACNT(c) = DMA_ON | DMA_REPEAT | DMA_HBLANK | flags | units;
}
static void hdma_off(int c) { DMACNT(c) = 0; }

/* ------------------------------------------------------------------------------------------ the 5x7 font */
static const u8 font[64][7] = {   /* ASCII 32..95, bit 4 = leftmost column */
    {0,0,0,0,0,0,0}, {4,4,4,4,4,0,4}, {10,10,10,0,0,0,0}, {10,10,31,10,31,10,10}, {4,15,20,14,5,30,4},
    {24,25,2,4,8,19,3}, {12,18,20,8,21,18,13}, {12,4,8,0,0,0,0}, {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8},
    {0,4,21,14,21,4,0}, {0,4,4,31,4,4,0}, {0,0,0,0,12,4,8}, {0,0,0,31,0,0,0}, {0,0,0,0,0,12,12},
    {0,1,2,4,8,16,0}, {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {31,2,4,2,1,17,14},
    {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}, {0,12,12,0,12,12,0}, {0,12,12,0,12,4,8}, {2,4,8,16,8,4,2}, {0,0,31,0,31,0,0},
    {8,4,2,1,2,4,8}, {14,17,1,2,4,0,4}, {14,17,1,13,21,21,14}, {14,17,17,17,31,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {28,18,17,17,17,18,28}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31}, {17,27,21,21,17,17,17}, {17,17,25,21,19,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17}, {17,17,17,10,4,4,4}, {31,1,2,4,8,16,31}, {14,8,8,8,8,8,14}, {0,16,8,4,2,1,0},
    {14,2,2,2,2,2,14}, {4,10,17,0,0,0,0}, {0,0,0,0,0,0,31}};
static int gpx(int c, int x, int y) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 95 || x < 0 || x > 4 || y < 0 || y > 6) return 0;
    return (font[c - 32][y] >> (4 - x)) & 1;
}
/* a glyph in an 8x8 cell: colour 15 at columns 1-5 / rows 0-6, its shadow (colour 1) one pixel down-right */
static int gcell(int c, int x, int y) { return gpx(c, x - 1, y) ? 15 : gpx(c, x - 2, y - 1) ? 1 : 0; }
/* plot string s scaled by k with its top left at (x0, y0) into a w x h image (1 or 2 bytes a pixel, `stride` pixels a
   row): the shadow (colour sh, offset max(1, k/2) down-right) first, then the glyph pixels (colour fg) over it */
static void plot_text(void *img, int bpp, int stride, int w, int h, int x0, int y0, const char *s, int k, u32 fg, u32 sh) {
    int off = k > 1 ? k >> 1 : 1;
    for (int pass = 0; pass < 2; pass++)
        for (int n = 0; s[n]; n++)
            for (int gy = 0; gy < 7; gy++)
                for (int gx = 0; gx < 5; gx++) {
                    if (!gpx((u8)s[n], gx, gy)) continue;
                    int px = x0 + (n * 6 + gx) * k + (pass ? 0 : off), py = y0 + gy * k + (pass ? 0 : off);
                    for (int j = 0; j < k; j++)
                        for (int i = 0; i < k; i++) {
                            int X = px + i, Y = py + j;
                            if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                            if (bpp == 1) ((u8 *)img)[Y * stride + X] = (u8)(pass ? fg : sh);
                            else ((u16 *)img)[Y * stride + X] = (u16)(pass ? fg : sh);
                        }
                }
}
static const char hexd[] = "0123456789ABCDEF";

/* ---------------------------------------------------------------------------------------------- palettes */
/* 16 banks of 16 colours: bank = hue, colour 1 = shadow (dark), 2..14 = ramp to the full hue, 15 = light tint */
static const u8 hue[16][3] = {{24,24,24},{31,4,4},{31,16,2},{31,29,4},{18,31,4},{4,26,6},{4,22,18},{6,28,31},
                              {10,18,31},{6,8,31},{16,6,31},{28,4,28},{31,14,22},{20,12,6},{16,18,6},{14,16,22}};
static u16 pal_col(u32 bank, u32 c) {
    const u8 *h = hue[bank & 15];
    c &= 15;
    if (c == 0) return 0;
    if (c == 1) return rgb(h[0] / 6, h[1] / 6, h[2] / 6);
    if (c == 15) return rgb((h[0] + 62) / 3, (h[1] + 62) / 3, (h[2] + 62) / 3);
    return rgb(h[0] * (c + 1) / 15, h[1] * (c + 1) / 15, h[2] * (c + 1) / 15);
}
#define BACKDROP rgb(3, 4, 8)
static void palettes_default(void) {
    for (u32 i = 0; i < 256; i++) { PAL_BG[i] = pal_col(i >> 4, i & 15); PAL_OBJ[i] = pal_col(i >> 4, i & 15); }
    PAL_BG[0] = BACKDROP;
}

/* -------------------------------------------------------------------------------------------- the tile sets */
#define T4_BLANK  0
#define T4_SOLID  1
#define T4_CHECK  2
#define T4_DIAG   3
#define T4_FRAME  4
#define T4_ARROW  5                /* white L in the top-left corner + a triangle: shows the four flip states */
#define T4_CROSS  6
#define T4_RING   7
#define T4_GRAD   8                /* 8..15: a ramp 2..14 across eight tiles */
#define T4_GRID   16
#define T4_DOTS   17
#define T4_BRICK  18
#define T4_WAVE   19
#define T4_TOP    20
#define T4_BOX    64               /* + ASCII 32..95: a glyph on an opaque box (tiles 96..159); plain glyphs: tile = ASCII */
#define T4_PIC    160              /* 160..223: the 64x64 4bpp picture */
#define T8_RAMP   1                /* 1..16: ramp of bank 0..15 */
#define T8_WHITE  17
#define T8_PIC    64               /* 64..127: the 64x64 8bpp picture (colour wheel) */
#define T8_GLYPH  96               /* + ASCII 32..95 (tiles 128..191) */
#define T8_SWATCH 192              /* 192..255: four colours a tile, the whole palette */

static u8 pic4buf[64 * 64], pic8buf[64 * 64];
static void build_pics(void) {
    for (int y = 0; y < 64; y++)                      /* 4bpp, one bank: a framed big "4" on stripes */
        for (int x = 0; x < 64; x++)
            pic4buf[y * 64 + x] = (u8)(x == 0 || y == 0 || x == 63 || y == 63 ? 15 : x == 1 || y == 1 || x == 62 || y == 62 ? 1
                                       : x < 10 && y < 10 ? 2 : ((x + y) >> 2) & 1 ? 5 : 8);
    plot_text(pic4buf, 1, 64, 62, 62, 14, 6, "4", 7, 13, 1);
    for (int y = 0; y < 64; y++)                      /* 8bpp, all banks: a colour wheel with a big "R" */
        for (int x = 0; x < 64; x++) {
            int dx = 2 * x - 63, dy = 2 * y - 63, r2 = dx * dx + dy * dy, best = 0;
            s32 bd = -0x7FFFFFFF;
            for (int k = 0; k < 16; k++) { s32 d = dx * cs(k * 16) + dy * sn(k * 16); if (d > bd) { bd = d; best = k; } }
            pic8buf[y * 64 + x] = (u8)(r2 > 63 * 63 ? 0 : r2 > 57 * 57 ? 0x0F : best * 16 + 2 + (int)isqrt((u32)r2) * 12 / 57);
        }
    plot_text(pic8buf, 1, 64, 64, 64, 19, 14, "R", 5, 0x0F, 0x01);
}
#define pic4(x, y) pic4buf[(y) * 64 + (x)]
#define pic8(x, y) pic8buf[(y) * 64 + (x)]
static int t4px(int t, int x, int y) {
    switch (t) {
    case T4_BLANK: return 0;
    case T4_SOLID: return 10;
    case T4_CHECK: return ((x >> 2) ^ (y >> 2)) & 1 ? 12 : 6;
    case T4_DIAG:  return ((x + y) >> 1) & 2 ? 9 : 4;
    case T4_FRAME: return x == 0 || y == 0 || x == 7 || y == 7 ? 14 : 5;
    case T4_ARROW: return x == 0 || y == 0 ? 15 : x + y <= 6 ? 12 : 0;
    case T4_CROSS: return x == 3 || x == 4 || y == 3 || y == 4 ? 15 : 7;
    case T4_RING:  { int dx = 2 * x - 7, dy = 2 * y - 7, r = dx * dx + dy * dy; return r <= 16 ? 0 : r <= 40 ? 13 : 3; }
    case T4_GRID:  return x == 0 || y == 0 ? 15 : 0;
    case T4_DOTS:  return (x & 3) == 1 && (y & 3) == 1 ? 15 : 0;
    case T4_BRICK: return y == 3 || y == 7 || x == ((y & 4) ? 3 : 7) ? 1 : 11;
    case T4_WAVE:  { static const u8 w[8] = {3, 2, 1, 1, 2, 3, 4, 4}; return y == w[x] || y == w[x] + 1 ? 8 : 0; }
    case T4_TOP:   return y < 4 ? 10 : 0;
    }
    if (t >= T4_GRAD && t < T4_GRAD + 8) return 2 + ((t - T4_GRAD) * 8 + x) * 13 / 64;
    if (t >= 32 && t < 96) return gcell(t, x, y);
    if (t >= 96 && t < 160) { int g = gcell(t - T4_BOX, x, y); return g ? g : 3; }
    if (t >= T4_PIC && t < T4_PIC + 64) return pic4((t - T4_PIC) % 8 * 8 + x, (t - T4_PIC) / 8 * 8 + y);
    return 0;
}
static int t8px(int t, int x, int y) {
    if (t == 0) return 0;
    if (t >= T8_RAMP && t < T8_RAMP + 16) return (t - T8_RAMP) * 16 + 2 + x * 12 / 7;
    if (t == T8_WHITE) return 0x0F;
    if (t >= T8_PIC && t < T8_PIC + 64) return pic8((t - T8_PIC) % 8 * 8 + x, (t - T8_PIC) / 8 * 8 + y);
    if (t >= T8_GLYPH + 32 && t < T8_GLYPH + 96) { int g = gcell(t - T8_GLYPH, x, y); return g == 15 ? 0x0F : g; }
    if (t >= T8_SWATCH) return ((t - T8_SWATCH) * 4 + (y >> 2) * 2 + (x >> 2)) & 255;
    return 0;
}
static void build_tiles4(u32 dst, int n) {
    u16 *d = (u16 *)dst;
    for (int t = 0; t < n; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x += 4)
                d[t * 16 + y * 2 + x / 4] = (u16)(t4px(t, x, y) | t4px(t, x + 1, y) << 4 | t4px(t, x + 2, y) << 8 | t4px(t, x + 3, y) << 12);
}
static void build_tiles8(u32 dst) {
    u16 *d = (u16 *)dst;
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x += 2)
                d[t * 32 + y * 4 + x / 2] = (u16)(t8px(t, x, y) | t8px(t, x + 1, y) << 8);
}

/* ------------------------------------------------------------------------------------------------- maps */
/* text map entry at tile (tx, ty) of a map of BGxCNT size `size` (0 256x256, 1 512x256, 2 256x512, 3 512x512) */
static void tmap(u16 *m, int size, int tx, int ty, u32 e) {
    int blk = ((tx >> 5) & (size & 1)) + (((ty >> 5) & (size >> 1)) << (size & 1));
    m[blk * 1024 + (ty & 31) * 32 + (tx & 31)] = (u16)e;
}
static u32 me(u32 tile, u32 flip, u32 pal) { return tile | flip << 10 | pal << 12; }
/* a string of glyph tiles: base 0 = 4bpp glyphs, T4_BOX = boxed 4bpp, T8_GLYPH = 8bpp glyphs */
static void tmap_text(u16 *m, int size, int tx, int ty, const char *s, u32 base, u32 pal) {
    for (; *s; s++, tx++) { int c = (u8)*s; if (c >= 'a' && c <= 'z') c -= 32; tmap(m, size, tx, ty, me(base + (u32)c, 0, pal)); }
}
/* an 8x8-tile picture (tiles tile0 .. tile0+63) flipped as a whole */
static void tmap_pic(u16 *m, int size, int tx, int ty, u32 tile0, u32 flip, u32 pal) {
    for (int iy = 0; iy < 8; iy++)
        for (int ix = 0; ix < 8; ix++)
            tmap(m, size, tx + ix, ty + iy, me(tile0 + (u32)((flip & 2 ? 7 - iy : iy) * 8 + (flip & 1 ? 7 - ix : ix)), flip, pal));
}
static u16 *sb(u32 lib, u32 block) { return (u16 *)(lib + block * 2048); }

/* screen blocks of L_A0 (bank A lower 64K): 16-19 T0 BG0, 20-21 T0 BG1, 22-23 T0 BG2, 24 T0 BG3, 25-26 T1,
   27 PATTERN, 28 affine maps, 29 PANEL, 30 PICS, 31 BARS */
#define SB_T0BG0 16
#define SB_T0BG1 20
#define SB_T0BG2 22
#define SB_T0BG3 24
#define SB_T1BG0 25
#define SB_T1BG1 26
#define SB_PATTERN 27
#define SB_AFF   28                /* affine 256x256 at +0, 128x128 at +0x400 */
#define SB_PANEL 29
#define SB_PICS  30
#define SB_BARS  31

static void build_maps_a0(void) {
    u16 *m;
    /* T0 BG0: 512x512 4bpp: 8x8 rooms of 8x8 tiles; the 4bpp picture in the diagonal rooms (flip = room & 3);
       other rooms: a frame, "Rxy" label, the arrow in its four flip states, a pattern or a hole */
    m = sb(L_A0, SB_T0BG0);
    for (int ty = 0; ty < 64; ty++)
        for (int tx = 0; tx < 64; tx++) {
            int rx = tx >> 3, ry = ty >> 3, ix = tx & 7, iy = ty & 7;
            u32 pal = (u32)(rx + ry * 3) & 15, e;
            if (rx == ry) continue;
            if (ix == 0 || iy == 0) e = me(T4_FRAME, 0, pal);
            else if (iy == 2 && ix >= 1 && ix <= 3) e = me(T4_BOX + (u32)(ix == 1 ? 'R' : ix == 2 ? '0' + rx : '0' + ry), 0, pal);
            else if (iy == 5 && ix >= 2 && ix <= 5) e = me(T4_ARROW, (u32)(ix - 2), pal);
            else if ((rx + ry) % 3 == 0) e = me(T4_BLANK, 0, 0);
            else e = me((rx + ry) & 1 ? T4_CHECK : T4_DIAG, 0, pal);
            tmap(m, 3, tx, ty, e);
        }
    for (int k = 0; k < 8; k++) tmap_pic(m, 3, k * 8, k * 8, T4_PIC, (u32)k & 3, (u32)(k * 4) & 15);
    /* T0 BG1: 512x256 4bpp overlay: a red frame on the map edge, a label bar, colour bars, rings; holes elsewhere */
    m = sb(L_A0, SB_T0BG1);
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 64; tx++) {
            u32 e = 0;
            if (ty == 0 || ty == 31 || tx == 0 || tx == 63) e = me(T4_FRAME, 0, 1);
            else if (ty == 20 || ty == 21) e = me(T4_GRAD + (u32)(tx & 7), (u32)(ty == 21 ? 2 : 0), (u32)(tx >> 3) & 15);
            else if ((tx & 15) == 8 && (ty & 7) == 3) e = me(T4_RING, 0, (u32)(tx >> 4) + 4);
            tmap(m, 1, tx, ty, e);
        }
    tmap_text(m, 1, 2, 5, "BG1 4BPP 512X256 LEFT", T4_BOX, 9);
    tmap_text(m, 1, 34, 5, "BG1 RIGHT HALF 256-511", T4_BOX, 12);
    /* T0 BG2: 256x512 8bpp, mostly transparent: two colour-wheel pictures, the palette swatches, a ramp column;
       the palette number bits (used by T1's extended palettes) differ per region */
    m = sb(L_A0, SB_T0BG2);
    for (int i = 0; i < 2048; i++) m[i] = 0;
    tmap_pic(m, 2, 2, 4, T8_PIC, 0, 1);
    tmap_pic(m, 2, 18, 40, T8_PIC, 3, 6);
    for (int iy = 0; iy < 8; iy++) for (int ix = 0; ix < 8; ix++) tmap(m, 2, 13 + ix, 20 + iy, me(T8_SWATCH + (u32)(iy * 8 + ix), 0, 2));
    for (int ty = 0; ty < 64; ty++) tmap(m, 2, 30, ty, me(T8_RAMP + (u32)(ty & 15), 0, (u32)(ty >> 4) + 8));
    tmap_text(m, 2, 1, 1, "BG2 8BPP 256X512", T8_GLYPH, 3);
    tmap_text(m, 2, 1, 33, "BG2 LOWER HALF", T8_GLYPH, 4);
    /* T0 BG3: 256x256 4bpp: coloured 4x4-tile blocks (solid / bricks) with diagonal holes onto the backdrop */
    m = sb(L_A0, SB_T0BG3);
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            u32 pal = (u32)((tx >> 2) + (ty >> 2) * 3) & 15;
            u32 e = (tx + ty) % 7 == 0 ? 0 : me(((tx >> 2) ^ (ty >> 2)) & 1 ? T4_DOTS : T4_BRICK, 0, pal);
            tmap(m, 0, tx, ty, e);
        }
    tmap_text(m, 0, 2, 14, "BG3 256X256", T4_BOX, 0);
    /* T1 BG0: 8bpp: 16 chips (3x3 tiles of the colour wheel's centre), chip p with palette p and its digit */
    m = sb(L_A0, SB_T1BG0);
    for (int i = 0; i < 1024; i++) m[i] = 0;
    for (int p = 0; p < 16; p++) {
        int cx = (p & 3) * 4, cy = 3 + (p >> 2) * 4;
        for (int iy = 0; iy < 3; iy++) for (int ix = 0; ix < 3; ix++)
            tmap(m, 0, cx + ix, cy + iy, me(T8_PIC + (u32)((2 + iy) * 8 + 3 + ix), 0, (u32)p));
        tmap(m, 0, cx + 3, cy, me(T8_GLYPH + (u32)hexd[p], 0, (u32)p));
    }
    tmap_text(m, 0, 0, 19, "BG0 PAL 0-F", T8_GLYPH, 0);
    /* T1 BG1: 8bpp: 16 rows of the 16 bank ramps, row r with palette r */
    m = sb(L_A0, SB_T1BG1);
    for (int i = 0; i < 1024; i++) m[i] = 0;
    for (int r = 0; r < 16; r++) for (int b = 0; b < 16; b++) tmap(m, 0, 16 + b, 3 + r, me(T8_RAMP + (u32)b, 0, (u32)r));
    tmap_text(m, 0, 16, 19, "BG1 ROW = PAL", T8_GLYPH, 0);
    /* PATTERN: calm opaque background */
    m = sb(L_A0, SB_PATTERN);
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++)
            tmap(m, 0, tx, ty, ((tx >> 1) ^ (ty >> 1)) & 1 ? me(T4_SOLID, 0, 15) : me(T4_CHECK, 0, 14));
    tmap_text(m, 0, 10, 12, "PATTERN", T4_BOX, 0);
    /* PANEL: transparent with framed panels, a gradient box, an arrow column */
    m = sb(L_A0, SB_PANEL);
    for (int i = 0; i < 1024; i++) m[i] = 0;
    static const u8 pnl[3][5] = {{2, 4, 13, 8, 9}, {16, 11, 29, 14, 1}, {4, 18, 21, 23, 5}};   /* x0 y0 x1 y1 pal */
    for (int p = 0; p < 3; p++)
        for (int ty = pnl[p][1]; ty <= pnl[p][3]; ty++)
            for (int tx = pnl[p][0]; tx <= pnl[p][2]; tx++) {
                int edge = tx == pnl[p][0] || tx == pnl[p][2] || ty == pnl[p][1] || ty == pnl[p][3];
                tmap(m, 0, tx, ty, edge ? me(T4_FRAME, 0, pnl[p][4]) : p == 2 ? me(T4_GRAD + (u32)(tx & 7), 0, (u32)ty - 13) : me(T4_SOLID, 0, pnl[p][4]));
            }
    tmap_text(m, 0, 4, 6, "PANEL A", T4_BOX, 9);
    tmap_text(m, 0, 18, 12, "PANEL B", T4_BOX, 1);
    for (int ty = 2; ty < 30; ty++) tmap(m, 0, 30, ty, me(T4_ARROW, (u32)ty & 3, 3));
    /* PICS: 8bpp pictures on transparent */
    m = sb(L_A0, SB_PICS);
    for (int i = 0; i < 1024; i++) m[i] = 0;
    tmap_pic(m, 0, 1, 4, T8_PIC, 0, 0);
    tmap_pic(m, 0, 22, 13, T8_PIC, 3, 0);
    for (int iy = 0; iy < 8; iy++) for (int ix = 0; ix < 8; ix++) tmap(m, 0, 12 + ix, 2 + iy, me(T8_SWATCH + (u32)(iy * 8 + ix), 0, 0));
    for (int tx = 0; tx < 16; tx++) tmap(m, 0, 4 + tx, 26, me(T8_RAMP + (u32)tx, 0, 0));
    tmap_text(m, 0, 11, 11, "PICS 8BPP", T8_GLYPH, 0);
    /* BARS: 16 opaque horizontal ramps, bar b = bank b, its digit at the left */
    m = sb(L_A0, SB_BARS);
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++)
            tmap(m, 0, tx, ty, tx == 0 ? me(T4_BOX + (u32)hexd[ty >> 1], 0, (u32)ty >> 1) : me(T4_GRAD + (u32)(tx & 7), 0, (u32)ty >> 1));
    /* affine 8-bit maps (8bpp tiles of char block 1): 256x256 at block 28 and 128x128 at block 28 + 1K */
    u8 *a = (u8 *)(L_A0 + SB_AFF * 2048);
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            int pic = tx >= 12 && tx < 20 && ty >= 12 && ty < 20;
            a[ty * 32 + tx] = pic ? (u8)(T8_PIC + (ty - 12) * 8 + tx - 12) : (tx & 7) == 0 || (ty & 7) == 0 ? (u8)T8_WHITE : (u8)(T8_RAMP + (((tx >> 3) + (ty >> 3) * 4) & 15));
        }
    for (int i = 0; i < 10; i++) a[3 * 32 + 3 + i] = (u8)(T8_GLYPH + "AFFINE 256"[i]);
    a += 0x400;
    for (int ty = 0; ty < 16; ty++)
        for (int tx = 0; tx < 16; tx++)
            a[ty * 16 + tx] = tx >= 4 && tx < 12 && ty >= 4 && ty < 12 ? (u8)(T8_PIC + (ty - 4) * 8 + tx - 4)
                            : tx == 0 || ty == 0 || tx == 15 || ty == 15 ? (u8)T8_WHITE : (u8)(T8_RAMP + ((tx + ty) & 15));
}

/* T2's half of bank A (DISPCNT char base 1 and screen base 1 = +64K): 8bpp tiles (char block 0), affine maps
   512x512 (screen block 8), 256x256 (10), 128x128 (11), the extended 16-bit 512x512 map (12-15), the 1024x1024 floor
   (16-23), 4bpp tiles (char block 3), the text overlay map (28), the sky map (29) and an extended 256x256 map (30) */
#define AF_TILES8 (L_A1AFF + 0x0000)
#define AF_M512   (L_A1AFF + 0x4000)
#define AF_M256   (L_A1AFF + 0x5000)
#define AF_M128   (L_A1AFF + 0x5800)
#define AF_EXT    (L_A1AFF + 0x6000)
#define AF_FLOOR  (L_A1AFF + 0x8000)
#define AF_TILES4 (L_A1AFF + 0xC000)
#define AF_TEXT   (L_A1AFF + 0xE000)
#define AF_SKY    (L_A1AFF + 0xE800)
#define AF_EXT256 (L_A1AFF + 0xF000)
static void build_a1aff(void) {
    build_tiles8(AF_TILES8);
    build_tiles4(AF_TILES4, 256);
    u8 *a = (u8 *)AF_M512;                           /* 512x512: blocks of 8x8 tiles with coordinates */
    for (int ty = 0; ty < 64; ty++)
        for (int tx = 0; tx < 64; tx++) {
            int bx = tx >> 3, by = ty >> 3, ix = tx & 7, iy = ty & 7;
            u8 e;
            if (bx == by && (bx == 3 || bx == 4)) e = (u8)(T8_PIC + iy * 8 + ix);
            else if (ix == 0 || iy == 0) e = T8_WHITE;
            else if (iy == 3 && ix >= 2 && ix <= 4) e = (u8)(T8_GLYPH + (ix == 2 ? 'B' : ix == 3 ? hexd[bx] : hexd[by]));
            else e = (u8)(T8_RAMP + ((bx + by * 2) & 15));
            a[ty * 64 + tx] = e;
        }
    a = (u8 *)AF_M256;                               /* 256x256: concentric squares with transparent rings */
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            int d = imax(iabs(2 * tx - 31), iabs(2 * ty - 31)) / 2;
            a[ty * 32 + tx] = d % 4 == 3 ? 0 : (u8)(T8_RAMP + (d & 15));
        }
    for (int i = 0; i < 7; i++) a[15 * 32 + 12 + i] = (u8)(T8_GLYPH + "ROT 256"[i]);
    a = (u8 *)AF_M128;                               /* 128x128: the picture in a white frame */
    for (int ty = 0; ty < 16; ty++)
        for (int tx = 0; tx < 16; tx++)
            a[ty * 16 + tx] = tx >= 4 && tx < 12 && ty >= 4 && ty < 12 ? (u8)(T8_PIC + (ty - 4) * 8 + tx - 4)
                            : tx == 0 || ty == 0 || tx == 15 || ty == 15 ? (u8)T8_WHITE : (u8)(T8_RAMP + ((tx * 3 + ty) & 15));
    u16 *e = (u16 *)AF_EXT;                          /* extended 512x512: the picture per 8x8 block, flips and palette */
    for (int ty = 0; ty < 64; ty++)
        for (int tx = 0; tx < 64; tx++) {
            int bx = tx >> 3, by = ty >> 3, ix = tx & 7, iy = ty & 7;
            u32 flip = (u32)(bx + by) & 3, pal = (u32)(bx + by * 8) & 15;
            u32 tile = ix == 0 && iy == 0 ? T8_GLYPH + (u32)hexd[pal] : T8_PIC + (u32)((flip & 2 ? 7 - iy : iy) * 8 + (flip & 1 ? 7 - ix : ix));
            e[ty * 64 + tx] = (u16)me(tile, ix == 0 && iy == 0 ? 0 : flip, pal);
        }
    a = (u8 *)AF_FLOOR;                              /* 1024x1024 floor: checker, grid, the picture at the centre */
    for (int ty = 0; ty < 128; ty++)
        for (int tx = 0; tx < 128; tx++) {
            u8 v;
            if (ty == 0) v = (u8)(T8_RAMP + (tx < 8 ? 9 : tx < 16 ? 8 : 7));      /* sky texels for the lines above the horizon */
            else if (tx >= 60 && tx < 68 && ty >= 60 && ty < 68) v = (u8)(T8_PIC + (ty - 60) * 8 + tx - 60);
            else if ((tx & 15) == 0 || (ty & 15) == 0) v = T8_WHITE;
            else v = (u8)(T8_RAMP + ((((tx >> 1) ^ (ty >> 1)) & 1) ? ((tx >> 4) + (ty >> 4)) & 15 : 13));
            a[ty * 128 + tx] = v;
        }
    e = (u16 *)AF_EXT256;                            /* extended 256x256: 4x4 blocks, other flips and palettes */
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++) {
            int bx = tx >> 3, by = ty >> 3, ix = tx & 7, iy = ty & 7;
            u32 flip = (u32)(bx + by + 1) & 3, pal = (u32)(bx * 5 + by * 3 + 2) & 15;
            u32 tile = ix == 0 && iy == 0 ? T8_GLYPH + (u32)hexd[pal] : T8_PIC + (u32)((flip & 2 ? 7 - iy : iy) * 8 + (flip & 1 ? 7 - ix : ix));
            e[ty * 32 + tx] = (u16)me(tile, ix == 0 && iy == 0 ? 0 : flip, pal);
        }
    u16 *m = (u16 *)AF_TEXT;                         /* the text overlay: one boxed caption */
    for (int i = 0; i < 1024; i++) m[i] = 0;
    tmap_text(m, 0, 1, 22, "BG0 VIA DISPCNT BASE 64K", T4_BOX, 2);
    m = (u16 *)AF_SKY;                               /* sky: mountains above map row 8 (screen line 64) + clouds */
    for (int i = 0; i < 1024; i++) m[i] = 0;
    for (int tx = 0; tx < 32; tx++) {
        int top = 5 + (sn(tx * 24) >> 11) + (cs(tx * 56) >> 12);
        for (int ty = top; ty < 8; ty++) tmap(m, 0, tx, ty, me(ty == top ? T4_TOP : T4_SOLID, 0, 13));
    }
    for (int k = 0; k < 6; k++) tmap(m, 0, 3 + k * 5, 1 + (k & 1), me(T4_RING, 0, 0));
}

/* ----------------------------------------------------------------------------------------------- bitmaps */
static u16 pal16[256];                               /* pal_col(i >> 4, i & 15), for the per-pixel loops */
static void num3(char *s, int v) { s[0] = (char)('0' + v / 100); s[1] = (char)('0' + v / 10 % 10); s[2] = (char)('0' + v % 10); }
static void build_bmp8(void) {                       /* 8bpp 256x256: numbered 32x32 blocks, some transparent */
    u8 *d = (u8 *)L_A1BMP8;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int bx = x >> 5, by = y >> 5, lx = x & 31, ly = y & 31;
            d[y * 256 + x] = (u8)(lx == 0 || ly == 0 ? 0x0F : (bx + by) % 5 == 4 ? 0 : ((bx + by * 3) & 15) * 16 + 2 + (lx + ly) * 12 / 62);
        }
    for (int n = 0; n < 64; n++) {
        char s[3] = {hexd[n >> 4], hexd[n & 15], 0};
        plot_text(d, 1, 256, 256, 256, (n & 7) * 32 + 4, (n >> 3) * 32 + 8, s, 2, 0x0F, 0x01);
    }
}
static void build_bmp16(void) {                      /* 16-bit 256x256: a landscape, a sun, a road, a transparent hole */
    u16 *d = (u16 *)L_BMP16;
    for (int y = 0; y < 256; y++) {
        u16 sky = rgb((u32)(8 + (y >> 4)), (u32)(14 + y * 11 / 128), 31);
        for (int x = 0; x < 256; x++) {
            int mh = 112 + (sn(x * 3) >> 9) + (sn(x * 7 + 40) >> 10), sx = x - 200, sy = y - 48;
            u16 c;
            if (sx * sx + sy * sy < 22 * 22) c = rgb(31, 28, 6);
            else if (y < mh) c = sky;
            else if (y < 130) c = rgb((u32)(10 + (x & 7)), 9, 6);
            else if (iabs(x - 128) < (y - 120) * 3 / 2) c = iabs(x - 128) < 2 && (y >> 3) & 1 ? rgb(31, 31, 31) : rgb(12, 12, 13);
            else c = rgb(4, (u32)(14 + ((y >> 3) & 3) * 3), 5);
            if (((x & 63) == 0 || (y & 63) == 0) && (x + y) & 2) c = rgb(31, 31, 31);
            int hx = x - 64, hy = y - 176;
            int hole = hx * hx + hy * hy < 30 * 30 || (x >= 240 && ((y >> 2) & 1));
            d[y * 256 + x] = (u16)(hole ? c : c | 0x8000);
        }
    }
    plot_text(d, 2, 256, 256, 256, 6, 6, "16BPP 256X256", 2, 0xFFFF, 0x8000);
}
static void build_bmp16w(void) {                     /* 16-bit 512x256 panorama with X markings, opaque */
    u16 *d = (u16 *)L_BMP16W;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 512; x++) {
            int h = (x >> 5) & 15;
            u16 c = y < 128 ? pal16[h * 16 + 4 + (y * 10 >> 7)] : pal16[h * 16 + ((((x >> 4) ^ (y >> 4)) & 1) ? 6 : 10)];
            if ((x & 63) == 0) c = rgb(31, 31, 31);
            if (iabs(y - 128 - (sn(x * 2) >> 7)) < 2) c = rgb(31, 31, 0);
            d[y * 512 + x] = (u16)(c | 0x8000);
        }
    for (int x = 0; x < 512; x += 64)
        for (int y = 4; y < 256; y += 64) {
            char s[5] = {'X', 0, 0, 0, 0};
            num3(s + 1, x);
            plot_text(d, 2, 512, 512, 256, x + 3, y, s, 1, 0xFFFF, 0x8000);
        }
}
static void build_stream(void) {                     /* 256x448 16-bit: rows 256..447 repeat 0..191 */
    u16 *d = (u16 *)L_STREAM;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            d[y * 256 + x] = (u16)((x == y || x == ((y + 128) & 255) ? rgb(31, 31, 31) : pal16[(y >> 4) * 16 + 4 + ((x >> 4) & 7)]) | 0x8000);
    for (int y = 0; y < 256; y += 16) {
        char s[8] = {'R', 'O', 'W', ' ', 0, 0, 0, 0};
        num3(s + 4, y);
        plot_text(d, 2, 256, 256, 256, 160, y + 4, s, 1, 0xFFFF, 0x8000);
    }
    for (int i = 0; i < 192 * 256; i++) d[256 * 256 + i] = d[i];
}
static void build_large(void) {                      /* 8bpp 512x1024: 64x64 cells "x,y" (hex), holes */
    u8 *d = (u8 *)L_LARGE;
    for (int y = 0; y < 1024; y++)
        for (int x = 0; x < 512; x++) {
            int cx = x >> 6, cy = y >> 6, lx = x & 63, ly = y & 63, hx = lx - 48, hy = ly - 48;
            d[y * 512 + x] = (u8)(lx == 0 || ly == 0 ? 0x0F : (cx + cy) & 1 && hx * hx + hy * hy < 100 ? 0
                                  : ((cx + cy * 3) & 15) * 16 + 2 + (lx + ly) * 12 / 126);
        }
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 8; cx++) {
            char s[4] = {hexd[cx], ',', hexd[cy], 0};
            plot_text(d, 1, 512, 512, 1024, cx * 64 + 6, cy * 64 + 10, s, 3, 0x0F, 0x01);
        }
}
static void build_small(void) {                      /* 128x128: a 16-bit target (+32K: an 8bpp checker), zeros */
    u16 *d = (u16 *)L_SMALL;
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int dx = x - 64, dy = y - 64, r = (int)isqrt((u32)(dx * dx + dy * dy));
            u16 c = x == 0 || y == 0 || x == 127 || y == 127 ? rgb(31, 31, 31) : r < 60 ? pal16[((r >> 3) & 15) * 16 + 4 + (r & 7)] : rgb(4, 4, 12);
            d[y * 128 + x] = (u16)(dx * dy > 0 && r > 20 && r < 28 ? c : c | 0x8000);    /* two transparent arcs */
        }
    plot_text(d, 2, 128, 128, 128, 22, 58, "128X128", 2, 0xFFFF, 0x8000);
    u8 *e = (u8 *)(L_SMALL + 0x8000);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            e[y * 128 + x] = (u8)(x == 0 || y == 0 ? 0x0F : ((x >> 4) ^ (y >> 4)) & 1 ? 0 : (((x >> 4) + (y >> 4) * 2) & 15) * 16 + 2 + ((x + y) & 15) * 12 / 15);
    plot_text(e, 1, 128, 128, 128, 10, 4, "8BPP 128", 1, 0x0F, 0x01);
    for (u32 *z = (u32 *)(L_SMALL + 0xC000); z < (u32 *)(L_SMALL + 0x20000); z++) *z = 0;
}

/* ------------------------------------------------------------------------------------------------ sprites */
#define NSPR 12
static const u8 spr_w[NSPR] = {8, 16, 32, 64, 16, 32, 32, 64, 8, 8, 16, 32};
static const u8 spr_h[NSPR] = {8, 16, 32, 64, 8, 8, 16, 32, 16, 32, 32, 64};
static const char *const spr_name[NSPR] = {"", "16", "32X32", "64X64", "16", "32X8", "32X16", "64X32", "", "", "16", "32X64"};
/* 1D: 4bpp data from 0x0000, 8bpp from 0x4000, every sprite 256-byte aligned (valid for all four boundaries);
   the title strip at 0x3800; bitmap sprites from 0x8000 */
#define OBJ1D_TITLE 0x3800
#define OBJ1D_BMP   0x8000
static u32 spr_off1d[2][NSPR];                      /* byte offsets */
static u8 sheet_x[2][NSPR], sheet_y[2][NSPR];       /* 2D sheet positions in 4bpp tile slots */
static u8 sprimg[NSPR][64 * 64];                   /* sprite k's pixels (colour 1..15 within the bank), w wide */
static void build_sprimg(void) {
    for (int k = 0; k < NSPR; k++) {
        int w = spr_w[k], h = spr_h[k];
        u8 *p = sprimg[k];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                p[y * w + x] = (u8)(x == 0 || y == 0 || x == w - 1 || y == h - 1 ? 15 : x < 4 && y < 4 ? 2 : x == y ? 14
                                    : ((x >> 2) ^ (y >> 2)) & 1 ? 5 + (k & 3) : 9 + (k & 3));
        plot_text(p, 1, w, w, h, 2, h >= 16 ? h / 2 - 4 : 1, spr_name[k], 1, 15, 1);
    }
}
static void put_spr_tile(u8 *base, int bpp8, int k, int tile_off, int tx, int ty) {   /* one 8x8 tile of sprite k */
    const u8 *s = sprimg[k] + ty * 8 * spr_w[k] + tx * 8;
    for (int y = 0; y < 8; y++, s += spr_w[k])
        for (int x = 0; x < 8; x++) {
            int c = s[x];
            if (bpp8) base[tile_off + y * 8 + x] = (u8)(c ? (k * 5 & 15) * 16 + c : 0);
            else { u8 *p = &base[tile_off + y * 4 + x / 2]; *p = (u8)(x & 1 ? (*p & 0x0F) | c << 4 : (*p & 0xF0) | c); }
        }
}
static void pack_sheet(void) {                      /* first-fit packing of the 24 sprites into the 32x32 slot sheet */
    static u8 occ[32][32];
    static const u8 order[NSPR] = {3, 11, 7, 2, 10, 9, 6, 1, 8, 5, 4, 0};
    for (int bpp8 = 1; bpp8 >= 0; bpp8--)
        for (int n = 0; n < NSPR; n++) {
            int k = order[n], w = (spr_w[k] / 8) << bpp8, h = spr_h[k] / 8, done = 0;
            for (int y = 0; y + h <= 30 && !done; y++)
                for (int x = 0; x + w <= 32 && !done; x += 1 + bpp8) {
                    int ok = 1;
                    for (int j = 0; j < h && ok; j++) for (int i = 0; i < w && ok; i++) if (occ[y + j][x + i]) ok = 0;
                    if (!ok) continue;
                    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) occ[y + j][x + i] = 1;
                    sheet_x[bpp8][k] = (u8)x; sheet_y[bpp8][k] = (u8)y; done = 1;
                }
        }
}
/* 16-bit bitmap sprites into a w-wide image: 32x32 kinds 0..3 (disc, ring, gradient square, bars with a corner
   mark), 64x64 kinds 4 (a framed "F" picture) and 5 (checker with transparent cells) */
static void bmpspr(u16 *d, int stride, int kind, int var) {
    int n = kind < 4 ? 32 : 64;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int dx = 2 * x - 31, dy = 2 * y - 31, r2 = dx * dx + dy * dy;
            u16 c;
            switch (kind) {
            case 0: c = r2 < 30 * 30 ? (u16)(0x8000 | pal16[(1 + var * 3) * 16 + 14 - r2 / 120]) : 0; break;
            case 1: c = r2 < 30 * 30 && r2 > 18 * 18 ? (u16)(0x8000 | rgb((u32)y, 31, (u32)(x + var * 8))) : 0; break;
            case 2: c = (u16)(0x8000 | (x == 0 || y == 0 || x == 31 || y == 31 ? rgb(31, 31, 31) : rgb((u32)x, (u32)y, (u32)(31 - x + var * 4)))); break;
            case 3: c = (u16)(0x8000 | (x < 6 && y < 6 ? rgb(31, 0, 0) : (x >> 2) & 1 ? rgb(31, 31, (u32)var * 8) : rgb(0, 0, 31))); break;
            case 4: c = (u16)(0x8000 | (x == 0 || y == 0 || x == 63 || y == 63 ? rgb(31, 31, 31) : rgb((u32)x >> 1, (u32)y / 3, (u32)(20 + var)))); break;
            default: c = ((x >> 3) ^ (y >> 3)) & 1 ? (u16)(0x8000 | rgb(31, (u32)x >> 1, 0)) : 0; break;
            }
            d[y * stride + x] = c;
        }
    if (kind == 4) plot_text(d, 2, stride, 64, 64, 14, 6, "F", 7, 0xFFFF, 0x8000 | rgb(2, 2, 2));
}
static void build_obj(void) {
    u8 *b;
    build_sprimg();
    /* 1D */
    b = (u8 *)L_OBJ1D;
    for (int i = 0; i < 0x10000; i++) b[i] = 0;
    u32 off[2] = {0x0000, 0x4000};
    for (int bpp8 = 0; bpp8 < 2; bpp8++)
        for (int k = 0; k < NSPR; k++) {
            int tw = spr_w[k] / 8, th = spr_h[k] / 8, ts = 32 << bpp8;
            spr_off1d[bpp8][k] = off[bpp8];
            for (int ty = 0; ty < th; ty++) for (int tx = 0; tx < tw; tx++) put_spr_tile(b, bpp8, k, (int)off[bpp8] + (ty * tw + tx) * ts, tx, ty);
            off[bpp8] = (off[bpp8] + (u32)(tw * th * ts) + 255) & ~255u;
        }
    for (int i = 0; i < 4; i++) bmpspr((u16 *)(b + OBJ1D_BMP + i * 0x800), 32, i, 0);
    for (int i = 0; i < 2; i++) bmpspr((u16 *)(b + OBJ1D_BMP + 0x2000 + i * 0x2000), 64, 4 + i, 0);
    /* 2D sheet (first 32K) and 2D bitmap sprites (128-wide layout, rows 128-255: 16 32x32 cells) */
    b = (u8 *)L_OBJ2D;
    for (int i = 0; i < 0x10000; i++) b[i] = 0;
    pack_sheet();
    for (int bpp8 = 0; bpp8 < 2; bpp8++)
        for (int k = 0; k < NSPR; k++)
            for (int ty = 0; ty < spr_h[k] / 8; ty++)
                for (int tx = 0; tx < spr_w[k] / 8; tx++)
                    put_spr_tile(b, bpp8, k, ((sheet_y[bpp8][k] + ty) * 32 + sheet_x[bpp8][k] + (tx << bpp8)) * 32, tx, ty);
    for (int i = 0; i < 16; i++) bmpspr((u16 *)(b + 0x8000) + (i >> 2) * 32 * 128 + (i & 3) * 32, 128, i & 3, i >> 2);
    /* full-screen image: the 2D sheet, then a 256x192 picture at rows 64..255 of the 256-wide bitmap layout */
    u8 *f = (u8 *)L_OBJFULL;
    for (int i = 0; i < 0x8000; i++) f[i] = b[i];
    u16 *img = (u16 *)(f + 0x8000);
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++) {
            int hx = x - 128, hy = y - 80;
            u16 c = (x & 63) == 0 || (y & 63) == 0 ? rgb(31, 31, 31) : pal16[(x >> 4) * 16 + 3 + y * 11 / 192];
            img[y * 256 + x] = (u16)(hx * hx + hy * hy < 26 * 26 ? c : c | 0x8000);
        }
    for (int c = 0; c < 12; c++) {
        char s[3] = {(char)('0' + c / 10), (char)('0' + c % 10), 0};
        plot_text(img, 2, 256, 256, 192, (c & 3) * 64 + 8, (c >> 2) * 64 + 14, c < 10 ? s + 1 : s, 3, 0xFFFF, 0x8000);
    }
    plot_text(img, 2, 256, 256, 192, 70, 120, "OBJ BITMAP 12X64X64", 1, 0x8000 | rgb(31, 31, 0), 0x8000);
}

/* extended palettes: palette p of slot s = the standard palette with the hue banks rotated by p + 4s (odd slots
   also swap red and blue), so palette numbers and slots show as colour changes */
static void build_extpal(void) {
    u16 *d = (u16 *)L_EXTBG;
    for (u32 s = 0; s < 4; s++)
        for (u32 p = 0; p < 16; p++)
            for (u32 i = 0; i < 256; i++) {
                u16 v = pal_col(((i >> 4) + p + 4 * s) & 15, i & 15);
                if (s & 1) v = (u16)((v & 0x3E0) | (v & 31) << 10 | (v >> 10 & 31));
                d[(s * 16 + p) * 256 + i] = v;
            }
    d = (u16 *)L_EXTOBJ;
    for (u32 p = 0; p < 16; p++)
        for (u32 i = 0; i < 256; i++) d[p * 256 + i] = pal_col(((i >> 4) + p * 3 + 7) & 15, 15 - (i & 15) + (i & 15 ? 1 : 0));
}

/* ------------------------------------------------------------------------ VRAM plans and the copy jobs */
/* A plan names the content of every 16K page of every bank the quarter maps (and of the LCDC banks its capture or
   VRAM display uses), and all of it is copied at every quarter start: no page is skipped because it already holds
   the right data. So no VRAM a renderer can read (DraStic's affine OBJs fetch up to 64K past the sprite's data at
   their edge pixels) holds content from an earlier scene, and the white period has the same length in the cycle ROM
   as in the scene's own ROM: a frame is a function of (scene, t). */
enum { VA, VB, VC, VD, VE, VF, VG, NBANK };
static const u32 bank_lcdc[NBANK] = {0x06800000, 0x06820000, 0x06840000, 0x06860000, 0x06880000, 0x06890000, 0x06894000};
static const u8 bank_pages[NBANK] = {8, 8, 8, 8, 4, 1, 1};   /* A-D 128K, E 64K, F and G 16K */
static u8 plan_cnt[NBANK], plan_pages[NBANK];        /* the VRAMCNT value; bit p = page p planned */
static struct { u32 dst, src; } jobs[48];
static int njobs, job_i, plan_dup;

static void need(int bank, int page, int npages, u32 src) {
    for (int i = 0; i < npages; i++) {
        jobs[njobs].dst = bank_lcdc[bank] + (u32)(page + i) * 0x4000; jobs[njobs].src = src + (u32)i * 0x4000; njobs++;
        plan_dup |= plan_pages[bank] >> (page + i) & 1;     /* a page planned twice costs a white frame for nothing */
        plan_pages[bank] |= (u8)(1u << (page + i));
    }
}
static void plan_begin(void) { for (int b = 0; b < NBANK; b++) { plan_cnt[b] = 0x80; plan_pages[b] = 0; } njobs = job_i = plan_dup = 0; }
static void apply_mapping(void) { for (int b = 0; b < NBANK; b++) VRAMCNT(b) = plan_cnt[b]; }
static void run_jobs(void) {                         /* one frame's share: up to 128K */
    for (int b = 0; b < NBANK; b++) if (plan_pages[b]) VRAMCNT(b) = 0x80;
    for (int n = 0; n < 8 && job_i < njobs; n++, job_i++) dma_copy(jobs[job_i].dst, jobs[job_i].src, 0x4000);
}
#ifdef DS2D_CHECK
static int plan_complete(void) {                     /* every page of every mapped bank planned, each once */
    if (plan_dup || njobs > (int)(sizeof jobs / sizeof jobs[0])) return 0;
    for (int b = 0; b < NBANK; b++)
        if (plan_cnt[b] != 0x80 && plan_pages[b] != (u8)((1u << bank_pages[b]) - 1)) return 0;
    return 1;
}
#endif
#define CNT_ABG(o)  (u8)(0x81 | (o) << 3)
#define CNT_AOBJ    (u8)0x82
#define CNT_BGEXT   (u8)0x84                         /* bank E: BG extended palette slots 0-3 */
#define CNT_OBJEXT  (u8)0x85                         /* bank F: OBJ extended palette */
#define CNT_LCDC    (u8)0x80
/* bank A as BG at 0x06000000: the shared tiles and maps below, `upper` above 64K; a bank as OBJ: all its pages */
static void plan_a(u32 upper) { plan_cnt[VA] = CNT_ABG(0); need(VA, 0, 4, L_A0); need(VA, 4, 4, upper); }
static void plan_obj(int bank, u32 src) { plan_cnt[bank] = CNT_AOBJ; need(bank, 0, bank_pages[bank], src); }

/* ----------------------------------------------------------------------------------------- OAM and titles */
static u16 oam[512];
static void oam_hide(void) {                        /* all OBJs disabled, all 32 affine groups zero */
    for (int i = 0; i < 128; i++) { oam[i * 4] = 0x0200; oam[i * 4 + 1] = 0; oam[i * 4 + 2] = 0; oam[i * 4 + 3] = 0; }
}
static void oam_set(int i, u32 a0, u32 a1, u32 a2) { oam[i * 4] = (u16)a0; oam[i * 4 + 1] = (u16)a1; oam[i * 4 + 2] = (u16)a2; }
static void oam_aff(int g, s32 pa, s32 pb, s32 pc, s32 pd) { oam[g * 16 + 3] = (u16)pa; oam[g * 16 + 7] = (u16)pb; oam[g * 16 + 11] = (u16)pc; oam[g * 16 + 15] = (u16)pd; }
/* rotation a, scale (8.8 texels per pixel) */
static void oam_rot(int g, s32 a, s32 sx, s32 sy) { oam_aff(g, cs(a) * sx >> 12, -(sn(a) * sx) >> 12, sn(a) * sy >> 12, cs(a) * sy >> 12); }
static void oam_commit(void) { dma_copy(0x07000000, (u32)oam, 1024); }
static const u8 spr_shape[NSPR] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2};
/* tile number of sprite k for the tile mapping in `dc` (DISPCNT) */
static u32 spr_tile(int k, int bpp8, u32 dc) {
    if (dc & DC_OBJ1D) return spr_off1d[bpp8][k] >> (5 + ((dc >> 20) & 3));
    return (u32)sheet_y[bpp8][k] * 32 + sheet_x[bpp8][k];
}
static u32 g_dc;                                     /* the DISPCNT the current frame uses (for tile numbers) */
static void spr(int i, int k, int bpp8, s32 x, s32 y, u32 a0, u32 a1, u32 pri, u32 pal) {
    oam_set(i, ((u32)y & 255) | a0 | (bpp8 ? A0_8BPP : 0) | (u32)spr_shape[k] << 14,
            ((u32)x & 511) | a1 | (u32)(k & 3) << 14, spr_tile(k, bpp8, g_dc) | pri << 10 | pal << 12);
}
/* the title: a 256x8 strip of eight 32x8 4bpp sprites (OAM 120-127), rendered into OBJ VRAM when the quarter
   starts; 1D: at 0x3800 + 256 i, 2D: tile row 31 of the sheet */
static u32 title_buf[8][32];
static void title_render(const char *s, int map2d) {
    static u8 strip[8 * 256];
    for (int i = 0; i < 8 * 256; i++) strip[i] = 0;
    plot_text(strip, 1, 256, 255, 8, 1, 0, s, 1, 15, 1);
    for (int i = 0; i < 8; i++) for (int w = 0; w < 32; w++) title_buf[i][w] = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 256; x++)
            if (strip[y * 256 + x]) title_buf[x >> 5][((x >> 3) & 3) * 8 + y] |= (u32)strip[y * 256 + x] << ((x & 7) * 4);
    for (int i = 0; i < 8; i++) {
        u32 dst = map2d ? OBJ_VRAM + (31 * 32 + (u32)i * 4) * 32 : OBJ_VRAM + OBJ1D_TITLE + (u32)i * 256;
        for (int w = 0; w < 32; w++) R32(dst + (u32)w * 4) = title_buf[i][w];
    }
}
static void oam_title(u32 dc) {
    for (int i = 0; i < 8; i++) {
        u32 tile = dc & DC_OBJ1D ? (OBJ1D_TITLE + (u32)i * 256) >> (5 + ((dc >> 20) & 3)) : 31 * 32 + (u32)i * 4;
        oam_set(120 + i, 0 | 1u << 14, (u32)i * 32 | 1u << 14, tile);
    }
}

/* ---------------------------------------------------------------------------------------- BG helpers */
static u16 bgcnt(u32 pri, u32 cb, u32 sbk, u32 size, u32 flags) { return (u16)(pri | cb << 2 | sbk << 8 | size << 14 | flags); }
/* affine BG n: texture point (tx, ty) at screen point (cx, cy), rotation a, scale sx/sy (8.8 texels per pixel) */
static void bg_aff(int n, s32 a, s32 sx, s32 sy, s32 tx, s32 ty, s32 cx, s32 cy) {
    s32 pa = cs(a) * sx >> 12, pb = -(sn(a) * sy) >> 12, pc = sn(a) * sx >> 12, pd = cs(a) * sy >> 12;
    BGPA(n) = (u16)pa; BGPB(n) = (u16)pb; BGPC(n) = (u16)pc; BGPD(n) = (u16)pd;
    BGX(n) = (u32)(tx * 256 - (pa * cx + pb * cy)) & 0x0FFFFFFF;
    BGY(n) = (u32)(ty * 256 - (pc * cx + pd * cy)) & 0x0FFFFFFF;
}
static void bg_ident(int n, s32 x, s32 y) { bg_aff(n, 0, 256, 256, x, y, 0, 0); }
/* WINxH / WINxV: X1 (Y1) in the high byte, X2 (Y2) = right (bottom) + 1 in the low byte */
static u16 winh(s32 x1, s32 x2) { return (u16)(((u32)x1 & 255) << 8 | ((u32)x2 & 255)); }

/* per-line tables for the HBlank DMAs (entry k is written at the hblank of line k, so it holds line k+1's value) */
static u16 htab0[192], htab1[192];
static u32 m7tab[192][4], dctab[192];

/* every frame starts from neutral effect, window and mosaic registers (each scene sets what it uses) */
static void fx_reset(void) {
    WIN0H = WIN1H = WIN0V = WIN1V = 0; WININ = WINOUT = 0;
    MOSAIC = 0; BLDCNT = 0; BLDALPHA = 0; BLDY = 0; MASTER_BRIGHT = 0; DISPCAPCNT = 0;
}
static void reset2d(void) {
    for (int c = 0; c < 3; c++) hdma_off(c);
    DISPCNT = 0; DISPCAPCNT = 0;
    for (int n = 0; n < 4; n++) { BGCNT(n) = 0; BGHOFS(n) = 0; BGVOFS(n) = 0; }
    bg_ident(2, 0, 0); bg_ident(3, 0, 0);
    WIN0H = WIN1H = WIN0V = WIN1V = 0; WININ = WINOUT = 0;
    MOSAIC = 0; BLDCNT = 0; BLDALPHA = 0; BLDY = 0; MASTER_BRIGHT = 0;
    palettes_default();
    oam_hide(); oam_commit();
}

/* ============================================================================================== the scenes */
static u32 qlen;                                     /* frames a quarter */
#define HALF(tq) ((tq) * 2 >= qlen)

/* --- T0: text BGs */
static void p_text(int q) { (void)q; plan_a(L_A1AFF); plan_obj(VB, L_OBJ1D); }
static void s_text(u32 t, int q, u32 tq) {
    static const u8 pri[4][4] = {{1, 0, 2, 3}, {1, 1, 0, 2}, {1, 0, 2, 3}, {0, 0, 0, 0}};
    g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D;
    DISPCNT = g_dc | (q == 3 && (tq & 15) < 4 ? 1u << 7 : 0);      /* q3: forced blank pulses */
    BGCNT(0) = bgcnt(pri[q][0], 0, SB_T0BG0, 3, 0);
    BGCNT(1) = bgcnt(pri[q][1], 0, SB_T0BG1, 1, 0);
    BGCNT(2) = bgcnt(pri[q][2], 1, SB_T0BG2, 2, BG_256);
    BGCNT(3) = bgcnt(pri[q][3], 0, SB_T0BG3, 0, 0);
    BGHOFS(0) = (u16)(t * 3); BGVOFS(0) = (u16)(t * 2);                  /* crosses the 512 edges */
    BGHOFS(1) = (u16)(-(s32)t * 4); BGVOFS(1) = (u16)(sn(t * 2) >> 6);
    BGHOFS(2) = (u16)(cs(t * 3) >> 7); BGVOFS(2) = (u16)(t * 2 + 16);
    BGHOFS(3) = (u16)(t >> 1); BGVOFS(3) = (u16)(-(s32)(t >> 1));
    if (q == 2) {                                    /* per-line BG3 HOFS wave and backdrop gradient */
        for (int l = 0; l < 192; l++) {
            int y = l + 1;
            htab0[l] = (u16)((t >> 1) + (sn(y * 4 + (s32)t * 4) >> 8));
            htab1[l] = rgb((u32)(2 + y / 12 + tri(t + (u32)y / 4, 8)), (u32)(2 + y / 24), (u32)(20 - y / 12));
        }
        BGHOFS(3) = (u16)((t >> 1) + (sn((s32)t * 4) >> 8));
        PAL_BG[0] = rgb((u32)(2 + tri(t, 8)), 2, 20);
        hdma(0, htab0, 0x0400001C, 1, DMA_DST_FIX);
        hdma(1, htab1, 0x05000000, 1, DMA_DST_FIX);
    } else { hdma_off(0); hdma_off(1); PAL_BG[0] = BACKDROP; }
    oam_hide(); oam_title(g_dc); oam_commit();
}

/* --- T1: extended palettes */
static void p_extpal(int q) { (void)q; p_text(0); plan_cnt[VE] = CNT_BGEXT; need(VE, 0, 4, L_EXTBG); }
static void s_extpal(u32 t, int q, u32 tq) {
    int hi = q == 2 || (q == 3 && ((tq >> 4) & 1));
    g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | (q == 1 ? 0 : DC_BGEXT);
    DISPCNT = g_dc;
    BGCNT(0) = bgcnt(0, 1, SB_T1BG0, 0, BG_256 | (hi ? BG_SLOT : 0));
    BGCNT(1) = bgcnt(1, 1, SB_T1BG1, 0, BG_256 | (hi ? BG_SLOT : 0));
    BGCNT(2) = bgcnt(2, 1, SB_T0BG2, 2, BG_256);     /* BG2: always slot 2 */
    BGCNT(3) = bgcnt(3, 0, SB_T0BG3, 0, 0);          /* 4bpp: standard palette */
    BGHOFS(0) = 0; BGVOFS(0) = 0; BGHOFS(1) = 0; BGVOFS(1) = 0;
    BGHOFS(2) = 0; BGVOFS(2) = (u16)(t + 96);
    BGHOFS(3) = (u16)(t >> 2); BGVOFS(3) = 0;
    oam_hide(); oam_title(g_dc); oam_commit();
}

/* --- T2: affine BGs */
static void p_affine(int q) {
    if (q < 3) {
        plan_a(L_A1AFF);
        plan_obj(VB, L_OBJ1D);
        plan_cnt[VE] = CNT_BGEXT; need(VE, 0, 4, L_EXTBG);
    } else {                                          /* mode 6: A..D = the 512K of BG VRAM (the picture); title from G */
        for (int b = VA; b <= VD; b++) { plan_cnt[b] = CNT_ABG(b); need(b, 0, 8, L_LARGE + (u32)b * 0x20000); }
        plan_obj(VG, L_OBJ1D);
    }
}
static void s_affine(u32 t, int q, u32 tq) {
    u32 base = DC_CHARBASE(1) | DC_SCRBASE(1);       /* every text/affine layer reads bank A's upper 64K */
    switch (q) {
    case 0:                                           /* mode 2: BG2 256x256 wrap, rotating; BG3 512x512 clipped, zooming */
        g_dc = DC_ON | DC_MODE(2) | DC_BG(0) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | base;
        BGCNT(0) = bgcnt(0, 3, 28, 0, 0);
        BGCNT(2) = bgcnt(1, 0, 10, 1, BG_WRAP);
        BGCNT(3) = bgcnt(2, 0, 8, 2, 0);
        bg_aff(2, (s32)t, 256 + (sn(t * 2) >> 5), 256 + (sn(t * 2) >> 5), 128, 128, 128, 96);
        bg_aff(3, -(s32)t / 2, 384 + (sn(t * 3) >> 4), 384 + (sn(t * 3) >> 4), 256, 256, 128, 96);
        break;
    case 1: {                                         /* mode 1: BG3 = perspective floor by HBlank DMA, BG2 = mountains */
        g_dc = DC_ON | DC_MODE(1) | DC_BG(0) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | base;
        BGCNT(0) = bgcnt(0, 3, 28, 0, 0);
        BGCNT(2) = bgcnt(1, 3, 29, 0, 0);
        BGCNT(3) = bgcnt(2, 0, 16, 3, BG_WRAP);
        s32 a = (s32)t, cx = 512 * 256 + sn(t) * 40, cy = 512 * 256 + cs(t) * 40;   /* 8.8 camera position */
        BGHOFS(2) = (u16)(t * 2); BGVOFS(2) = 0;
        for (int l = 0; l <= 192; l++) {             /* l = screen line; row l-1 of the table */
            u32 v[4];
            if (l <= 64) {                            /* sky: every pixel the same texel of the sky row */
                v[0] = 0; v[1] = 0; v[2] = (u32)(2 + l * 124 / 65) << 8; v[3] = 3u << 8;
            } else {
                s32 s8 = (40 << 8) / (l - 64), z8 = s8 * 128;          /* texels a pixel; depth */
                s32 pa = cs(a) * s8 >> 12, pc = sn(a) * s8 >> 12;
                s32 x = cx + (sn(a) * (z8 >> 4) >> 8) - pa * 128, y = cy - (cs(a) * (z8 >> 4) >> 8) - pc * 128;
                v[0] = (u32)pa & 0xFFFF; v[1] = (u32)pc & 0xFFFF; v[2] = (u32)x & 0x0FFFFFFF; v[3] = (u32)y & 0x0FFFFFFF;
            }
            if (l == 0) { BGPA(3) = (u16)v[0]; BGPB(3) = 0; BGPC(3) = (u16)v[1]; BGPD(3) = 0; BGX(3) = v[2]; BGY(3) = v[3]; }
            else { m7tab[l - 1][0] = v[0]; m7tab[l - 1][1] = v[1]; m7tab[l - 1][2] = v[2]; m7tab[l - 1][3] = v[3]; }
        }
        hdma(0, m7tab, 0x04000030, 4, DMA_32 | DMA_DST_RELOAD);
        {   /* a mode switch at line 32 (mode 0 without BG3 above, mode 1 below): DISPCNT rewritten every line (ch1) */
            u32 top = (g_dc & ~(7u | DC_BG(3))) | DC_MODE(0);
            for (int l = 0; l < 192; l++) dctab[l] = l + 1 < 32 ? top : g_dc;
            g_dc = top;
            hdma(1, dctab, 0x04000000, 1, DMA_32 | DMA_DST_FIX);
        }
        break; }
    case 2: {                                         /* mode 4 / 5: extended affine BGs */
        int m5 = HALF(tq), extpal = !((tq >> 3) & 1);
        g_dc = DC_ON | DC_MODE(m5 ? 5 : 4) | DC_BG(0) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | base | (extpal ? DC_BGEXT : 0);
        BGCNT(0) = bgcnt(0, 3, 28, 0, 0);
        BGCNT(3) = bgcnt(2, 0, 12, 2, BG_WRAP);       /* 16-bit map, flips and palette numbers, ext slot 3 */
        bg_aff(3, (s32)t, 320 + (sn(t * 2) >> 5), 320, 256, 256, 128, 96);
        if (m5) { BGCNT(2) = bgcnt(1, 0, 30, 1, 0); bg_aff(2, -(s32)t * 2, 200, 200, 128, 128, 96, 96); }     /* 16-bit map 256x256 */
        else { BGCNT(2) = bgcnt(1, 0, 11, 0, (tq >> 4) & 1 ? BG_WRAP : 0); bg_aff(2, 0, 128, 128, 64 - (s32)(tq & 31), 64, 192, 96); }
        break; }
    default: {                                        /* mode 6: the large bitmap */
        int wide = HALF(tq), wrap = (tq >> 3) & 1;
        g_dc = DC_ON | DC_MODE(6) | DC_BG(2) | DC_OBJ | DC_OBJ1D;
        BGCNT(2) = bgcnt(0, 0, 0, wide ? 1 : 0, wrap ? BG_WRAP : 0);
        if (wide) bg_aff(2, (s32)t, 512 + (sn(t * 3) >> 3), 512 + (sn(t * 3) >> 3), 768, 384, 128, 96);
        else bg_aff(2, -(s32)t, 640 + (sn(t * 2) >> 3), 640 + (sn(t * 2) >> 3), 256, 512, 128, 96);
        break; }
    }
    if (q != 1) { hdma_off(0); hdma_off(1); }
    DISPCNT = g_dc;
    oam_hide(); oam_title(g_dc); oam_commit();
}

/* --- T3: bitmap BGs */
static void p_bitmap(int q) {                        /* BG: A 0K, B 128K, C 256K, D 384K; OBJ: G */
    plan_a(L_A1BMP8);
    plan_cnt[VB] = CNT_ABG(1); need(VB, 0, 8, L_BMP16);
    if (q == 1) { plan_cnt[VC] = CNT_ABG(2); need(VC, 0, 8, L_BMP16W); plan_cnt[VD] = CNT_ABG(3); need(VD, 0, 8, L_BMP16W + 0x20000); }
    if (q == 2) { plan_cnt[VC] = CNT_ABG(2); need(VC, 0, 8, L_SMALL); }
    plan_obj(VG, L_OBJ1D);
}
static void s_bitmap(u32 t, int q, u32 tq) {
    u32 obj = DC_OBJ | DC_OBJ1D;
    hdma_off(2);
    switch (q) {
    case 0:                                           /* mode 5: 16-bit 256x256 identity (direct) over a rotating 8bpp */
        g_dc = DC_ON | DC_MODE(5) | DC_BG(0) | DC_BG(2) | DC_BG(3) | obj;
        BGCNT(0) = bgcnt(0, 0, SB_PANEL, 0, 0);
        BGCNT(2) = bgcnt(1, 0, 8, 1, BG_BMP16);
        BGCNT(3) = bgcnt(2, 0, 4, 1, BG_BMP8 | BG_WRAP);
        bg_ident(2, (tq >> 4) & 1, 0);                /* every other 16 frames one pixel off identity: not direct */
        bg_aff(3, (s32)t / 2, 256 + (sn(t) >> 6), 256 + (sn(t) >> 6), 128, 128, 128, 96);
        if (HALF(tq)) {                               /* a window without BG2 and panels blended over the direct layer */
            g_dc |= DC_WIN0;
            WIN0H = winh(96 + (sn(t * 2) >> 7), 200 + (sn(t * 2) >> 7)); WIN0V = winh(60, 150);
            WININ = (u16)(W_BG(0) | W_BG(3) | W_OBJ | W_FX); WINOUT = (u16)(W_BG(0) | W_BG(2) | W_BG(3) | W_OBJ | W_FX);
            BLDCNT = (u16)(T_BG(0) | BL_ALPHA | T2(T_BG(2) | T_BG(3))); BLDALPHA = 0x0808;
        }
        break;
    case 1:                                           /* mode 3: 16-bit 512x256 scrolling with wrap */
        g_dc = DC_ON | DC_MODE(3) | DC_BG(0) | DC_BG(1) | DC_BG(3) | obj;
        BGCNT(0) = bgcnt(0, 0, SB_PANEL, 0, 0);
        BGCNT(1) = bgcnt(3, 0, SB_PATTERN, 0, 0);
        BGCNT(3) = bgcnt(1, 0, 16, 2, BG_BMP16 | BG_WRAP);
        bg_ident(3, (s32)t * 3, sn(t * 2) >> 7);
        break;
    case 2: {                                         /* mode 4 / 5: rotated and zoomed bitmaps, sizes 256 and 128 */
        int small = HALF(tq);
        g_dc = DC_ON | DC_MODE(small ? 5 : 4) | DC_BG(0) | DC_BG(2) | DC_BG(3) | obj;
        BGCNT(0) = bgcnt(0, 0, SB_PANEL, 0, 0);
        if (small) { BGCNT(2) = bgcnt(2, 0, 18, 0, BG_BMP8 | BG_WRAP); bg_aff(2, (s32)t, 128, 128, 64, 64, 64, 96); }   /* C+32K */
        else { BGCNT(2) = bgcnt(2, 1, SB_AFF, 1, BG_WRAP); bg_aff(2, (s32)t, 256, 256, 128, 128, 128, 96); }
        BGCNT(3) = small ? bgcnt(1, 0, 16, 0, BG_BMP16) : bgcnt(1, 0, 8, 1, BG_BMP16);                              /* C / B */
        bg_aff(3, -(s32)t, 160 + tri(t, 64) * 4, 160 + tri(t, 64) * 4, small ? 64 : 128, small ? 64 : 128, 168, 96);
        break; }
    default:
        if (tq * 3 < qlen) {                          /* VRAM display of bank B (LCDC): every line at its hblank */
            VRAMCNT(VB) = CNT_LCDC;
            g_dc = DC_VRAMDISP(1) | DC_BG(0) | obj;
        } else if (tq * 3 < qlen * 2) {               /* main-memory display: a start-mode-4 DMA feeds the FIFO */
            VRAMCNT(VB) = CNT_ABG(1);
            g_dc = (3u << 16) | DC_BG(0) | obj;
            DMASAD(2) = L_STREAM + (t % 256) * 512; DMADAD(2) = 0x04000068;
            DMACNT(2) = DMA_ON | DMA_REPEAT | (4u << 27) | DMA_32 | DMA_DST_FIX | 4;
        } else {                                      /* bitmap row 0 rewritten by HBlank DMA before every line */
            VRAMCNT(VB) = CNT_ABG(1);
            g_dc = DC_ON | DC_MODE(5) | DC_BG(0) | DC_BG(2) | obj;
            BGCNT(0) = bgcnt(0, 0, SB_PANEL, 0, 0);
            BGCNT(2) = bgcnt(1, 0, 8, 1, BG_BMP16);
            BGPA(2) = 0x100; BGPB(2) = 0; BGPC(2) = 0; BGPD(2) = 0;           /* PD = 0: every line reads row 0 */
            BGX(2) = 0; BGY(2) = 0;
            u32 row = t % 256;
            dma_copy(0x06020000, L_STREAM + row * 512, 512);
            hdma(2, (const void *)(L_STREAM + (row + 1) * 512), 0x06020000, 128, DMA_32 | DMA_DST_RELOAD);
        }
        break;
    }
    DISPCNT = g_dc;
    oam_hide(); oam_title(g_dc); oam_commit();
}

/* --- T4: sprites */
static void p_sprites(int q) {
    plan_a(L_A1AFF);
    plan_obj(VB, q < 2 ? L_OBJ1D : L_OBJ2D);
    plan_cnt[VF] = CNT_OBJEXT; need(VF, 0, 1, L_EXTOBJ);
}
/* the 12 sizes in a row at y (4bpp: palette bank k, priority k & 3; 8bpp: flips, priority (k+1) & 3) */
static const u8 row_x[NSPR] = {4, 18, 40, 76, 146, 166, 202, 4, 144, 156, 170, 196};
static void sprite_row(u32 t, int i0, int bpp8, int y) {
    for (int k = 0; k < NSPR; k++) {
        s32 ox = sn(t * 2 + k * 21) >> 9, oy = cs(t * 3 + k * 17) >> 10;
        u32 flip = bpp8 ? (k & 1 ? A1_HFLIP : 0) | (k % 3 == 0 ? A1_VFLIP : 0) : 0;
        spr(i0 + k, k, bpp8, row_x[k] + bpp8 * 8 + ox, y + (k == 7 ? 36 : 0) + (bpp8 ? -oy : oy), 0, flip, (u32)((k + bpp8) & 3), (u32)k);
    }
}
static void s_sprites(u32 t, int q, u32 tq) {
    int map1d = q < 2;
    g_dc = DC_ON | DC_MODE(0) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | (map1d ? DC_OBJ1D | DC_BOUND(q * 2 + (HALF(tq) ? 1 : 0)) : 0)
         | (q == 1 ? DC_OBJEXT : 0) | (q == 1 && ((tq >> 4) & 1) ? DC_HBLANKOBJ : 0);
    DISPCNT = g_dc;
    BGCNT(1) = bgcnt(1, 0, SB_PANEL, 0, 0);
    BGCNT(2) = bgcnt(2, 1, SB_PICS, 0, BG_256);
    BGCNT(3) = bgcnt(3, 0, SB_PATTERN, 0, 0);
    BGHOFS(1) = 0; BGVOFS(1) = 0; BGHOFS(2) = (u16)(t >> 1); BGVOFS(2) = 0; BGHOFS(3) = (u16)(t >> 2); BGVOFS(3) = (u16)(t >> 3);
    oam_hide();
    sprite_row(t, 0, 0, 16);
    if (q != 2) sprite_row(t, 12, 1, 106);
    int i = 24;
    for (int f = 0; f < 4; f++) spr(i++, 2, 0, 216 + (f & 1) * 20, 20 + (f >> 1) * 36, 0, (u32)f << 12, 0, 3);   /* four flips */
    spr(i++, 3, 0, (s32)(t * 2) & 511, 240 + (s32)(t & 31), 0, 0, 0, 9);     /* x across 511 -> 0, y across 255 -> 0 */
    if (q == 1)                                       /* 90 sprites of 32 pixels on lines 150..181, left to right */
        for (int n = 0; n < 90; n++) spr(i++, 2, n & 1, n * 3 - 12 + (s32)(t & 7), 150, 0, 0, (u32)(n & 3), (u32)(n & 15));
    if (q == 3) {                                     /* OAM order against priority, disabled sprites, X/Y wrap */
        for (int n = 0; n < 4; n++) spr(i++, 2, 0, 100 + n * 10, 64 + n * 8, 0, 0, (u32)(3 - n), (u32)(n + 1));
        spr(i++, 2, 0, 160, 64, A0_DBL, 0, 0, 5);     /* bit 9 without bit 8: disabled */
        spr(i++, 3, 1, 480 + (s32)(t & 15), 120, 0, 0, 0, 2);                 /* x 480..495: enters from the left edge */
    }
    oam_title(g_dc);
    oam_commit();
    if (q == 2) {                                     /* multiplexing: OAM 0-11 show the 8bpp row from line 96 on */
        sprite_row(t, 0, 1, 112);                     /* the 4bpp row ends by line 85, the 8bpp row starts at 108 */
#ifdef DS2D_CHECK
        if (VCOUNT < 192) { DISPCNT = 0; for (;;) {} }
#endif
        while (VCOUNT < 96 || VCOUNT >= 192) {}
        dma_copy(0x07000000, (u32)oam, 12 * 8);
    }
}

/* --- T5: affine, bitmap and semi-transparent sprites, the full-screen OBJ bitmap */
static void p_objfx(int q) {
    plan_a(L_A1AFF);
    plan_obj(VB, q < 2 ? L_OBJ1D : q == 2 ? L_OBJ2D : L_OBJFULL);
    plan_cnt[VC] = CNT_ABG(1); need(VC, 0, 8, L_BMP16);
}
static void s_objfx(u32 t, int q, u32 tq) {
    /* mode 3: BG0 PANEL (pri 1), BG1 BARS (pri 3), BG3 = the 16-bit landscape of bank C (pri 2, a transparent hole) */
    g_dc = DC_ON | DC_MODE(3) | DC_BG(0) | DC_BG(1) | DC_BG(3) | DC_OBJ;
    BGCNT(0) = bgcnt(1, 0, SB_PANEL, 0, 0);
    BGCNT(1) = bgcnt(3, 0, SB_BARS, 0, 0);
    BGCNT(3) = bgcnt(2, 0, 8, 1, BG_BMP16);
    BGHOFS(0) = 0; BGVOFS(0) = 0; BGHOFS(1) = (u16)t; BGVOFS(1) = 0;
    bg_ident(3, 0, (s32)(sn(t) >> 8));
    oam_hide();
    int i = 0;
    switch (q) {
    case 0: {                                         /* affine sprites, normal and double size, 8 parameter groups */
        static const u8 ks[4] = {1, 2, 7, 11};
        g_dc |= DC_OBJ1D;
        for (int g = 0; g < 8; g++) {
            s32 sx = 256 + (sn(t * 2 + g * 32) >> 5), sy = g == 7 ? -256 : 256 + (cs(t * 3 + g * 32) >> 5);   /* 7: mirrored */
            oam_rot(g, (s32)t * (g + 1) / 2 + g * 32, sx, sy);
        }
        for (int n = 0; n < 16; n++) {
            int k = ks[n & 3], dbl = n >= 8, w = spr_w[k], h = spr_h[k];
            s32 cx = 24 + (n & 7) * 30, cy = 50 + (n >> 3) * 80;
            spr(i++, k, n & 1, cx - (w >> !dbl), cy - (h >> !dbl), A0_AFF | (dbl ? A0_DBL : 0), (u32)(n & 7) << 9, (n & 3) == 3 ? 2 : 0, (u32)n);
        }
        break; }
    case 1: {                                         /* bitmap sprites, 1D mapping in 128-byte units */
        int noblend = (tq >> 4) & 1, sh = HALF(tq) ? 8 : 7;   /* bitmap 1D boundary 128 or 256 bytes */
        g_dc |= DC_OBJ1D | DC_BMP1D | (sh == 8 ? 1u << 22 : 0);
        BLDCNT = (u16)(noblend ? 0 : T2(T_BG(1) | T_BG(3) | T_BD));   /* bitmap OBJ blend only over 2nd targets */
        BLDALPHA = 0x0808;
        for (int n = 0; n < 8; n++) {
            u32 alpha = (u32)(n * 2 + ((tq >> 3) & 1)) & 15;     /* 0 = never shown */
            oam_set(i++, (u32)(28 + (n >> 2) * 44) | A0_BMP, (u32)(8 + (n & 3) * 40 + (sn(t * 2 + n * 32) >> 9)) | 2u << 14 | (n & 1 ? A1_HFLIP : 0),
                    ((OBJ1D_BMP + (u32)(n & 3) * 0x800) >> sh) | alpha << 12 | 1u << 10);
        }
        oam_set(i++, 112 | A0_BMP, 168 | 3u << 14 | A1_VFLIP, ((OBJ1D_BMP + 0x2000) >> sh) | 15u << 12 | 1u << 10);
        oam_set(i++, 120 | A0_BMP, 96 | 3u << 14, ((OBJ1D_BMP + 0x4000) >> sh) | (u32)tri(t, 15) << 12 | 1u << 10);
        oam_rot(0, (s32)t * 2, 200, 200);
        oam_set(i++, 8 | A0_BMP | A0_AFF | A0_DBL, 176 | 2u << 14, ((OBJ1D_BMP + 0x1000) >> sh) | 12u << 12);
        break; }
    case 2:                                           /* semi-transparent tile sprites (2D) + 2D bitmap sprites (128 wide) */
        BLDCNT = (u16)(T_BG(0) | BL_UP | T2(T_BG(3) | T_BD));    /* brightness selected: OBJ mode 1 still alpha-blends */
        BLDY = 6;
        BLDALPHA = (u16)(tri(t, 16) | (16 - tri(t, 16)) << 8);
        oam_rot(1, (s32)t * 3, 256, 256);
        for (int n = 0; n < 10; n++) {
            int aff = n >= 6;
            spr(i++, 2 + (n & 1), (n >> 1) & 1, 4 + n * 25, 56 + (sn(t * 2 + n * 25) >> 7), A0_SEMI | (aff ? A0_AFF : 0), aff ? 1u << 9 : 0, (u32)(n & 1), (u32)(n + 2));
        }
        for (int n = 0; n < 4; n++) {                 /* 2D bitmap OBJ, 128-dot-wide layout: cell n of rows 128.. */
            u32 cell = (u32)n * 5 & 15;
            oam_set(i++, 130 | A0_BMP, (u32)(20 + n * 56) | 2u << 14, ((16 + (cell >> 2) * 4) * 16 + (cell & 3) * 4) | 13u << 12);
        }
        break;
    default: {                                        /* the full-screen OBJ bitmap: OAM 0-11, 2D bitmap 256 wide */
        int broken = (tq >> 4) & 1;
        g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_OBJ | DC_BMP256;
        BGCNT(0) = bgcnt(1, 0, SB_PANEL, 0, 0);
        BGCNT(1) = bgcnt(3, 0, SB_BARS, 0, 0);
        for (int c = 0; c < 12; c++)
            oam_set(i++, (u32)(c >> 2) * 64 | A0_BMP, (u32)((c & 3) * 64 + (broken && c == 5 ? 1 : 0)) | 3u << 14,
                    (256 + (u32)(c >> 2) * 256 + (u32)(c & 3) * 8) | 15u << 12 | 2u << 10);
        for (int n = 0; n < 4; n++) spr(i++, 2, n & 1, (s32)((t * (n + 1)) & 255), 40 + n * 30, 0, 0, (u32)n, (u32)(n + 3));
        break; }
    }
    DISPCNT = g_dc;
    oam_title(g_dc);
    oam_commit();
}

/* --- T6: windows */
static u16 spot_w[64];
static void p_windows(int q) { (void)q; p_text(0); }
static void s_windows(u32 t, int q, u32 tq) {
    g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | DC_WIN0;
    BGCNT(0) = bgcnt(2, 0, SB_PATTERN, 0, 0);
    BGCNT(1) = bgcnt(0, 0, SB_PANEL, 0, 0);
    BGCNT(2) = bgcnt(1, 1, SB_PICS, 0, BG_256);
    BGCNT(3) = bgcnt(3, 0, SB_BARS, 0, 0);
    BGHOFS(0) = (u16)(t >> 1); BGVOFS(0) = 0; BGHOFS(2) = (u16)(-(s32)t); BGVOFS(2) = 0; BGHOFS(3) = 0; BGVOFS(3) = (u16)(t >> 2);
    BLDCNT = (u16)(BL_DOWN | 0x3F); BLDY = 9;         /* darkening wherever a region enables effects */
    oam_hide();
    int i = 0;
    for (int n = 0; n < 8; n++) spr(i++, 2 + (n & 1), n & 1, 10 + n * 30, 120 + (sn(t * 2 + n * 32) >> 7), 0, 0, 0, (u32)(n + 1));
    s32 x0 = 30 + (sn(t) >> 6), y0 = 30 + (cs(t * 2) >> 7), x1 = x0 + 110, y1 = y0 + 80;
    s32 u0 = 130 + (cs(t) >> 6), v0 = 70 + (sn(t * 3) >> 7);
    hdma_off(0);
    switch (q) {
    case 0:                                           /* WIN0 and WIN1 overlapping, outside */
        g_dc |= DC_WIN1;
        WIN0H = winh(x0, x1); WIN0V = winh(y0, y1);
        WIN1H = winh(u0, u0 + 100); WIN1V = winh(v0, v0 + 90);
        WININ = (u16)((W_BG(3) | W_BG(1) | W_OBJ) | (W_BG(0) | W_BG(2) | W_FX) << 8);
        WINOUT = (u16)(W_BG(0) | W_BG(1) | W_OBJ | W_BG(3));
        break;
    case 1:                                           /* OBJ window (an affine double-size sprite and a 32x64 sprite) with
                                                         WIN0 and WIN1: lines with 1, 2 and all 3 windows */
        g_dc |= DC_OBJWIN | DC_WIN1;
        WIN0H = winh(180, 240); WIN0V = winh(20, 120);
        WIN1H = winh(8 + tri(t, 40), 100 + tri(t, 40)); WIN1V = winh(96, 176);
        WININ = (u16)((W_BG(3) | W_OBJ | W_FX) | (W_BG(1) | W_BG(2) | W_OBJ) << 8);
        WINOUT = (u16)((W_BG(0) | W_BG(1) | W_OBJ) | (W_BG(2) | W_BG(3) | W_OBJ) << 8);
        oam_rot(0, (s32)t * 2, 256 - tri(t, 96), 256 - tri(t, 96));
        spr(i++, 3, 0, 40 + (sn(t) >> 6), 20, A0_WIN | A0_AFF | A0_DBL, 0, 0, 0);
        spr(i++, 11, 1, 180 + (cs(t * 2) >> 7), 90, A0_WIN, 0, 0, 0);
        break;
    case 2: {                                         /* edge cases, 15 frames each */
        int k = (int)(tq / 15) & 3;
        g_dc |= DC_WIN1;
        WININ = (u16)((W_BG(3) | W_OBJ) | (W_BG(2) | W_BG(0) | W_FX) << 8);
        WINOUT = (u16)(W_BG(0) | W_BG(1) | W_OBJ);
        WIN1H = winh(0, 255); WIN1V = winh(150, 192);                 /* full width (X2 = 255), to line 191 */
        if (k == 0) { WIN0H = winh(200, 56); WIN0V = winh(20, 120); }          /* X1 > X2: wraps around */
        else if (k == 1) { WIN0H = winh(40, 216); WIN0V = winh(140, 40); }     /* Y1 > Y2 */
        else if (k == 2) { WIN0H = winh(60, 60); WIN0V = winh(10, 220); WIN1V = winh(100, 230); }   /* X1 = X2, Y2 > 192 */
        else { WIN0H = winh(0, 0); WIN0V = winh(0, 192); WIN1H = winh(128 + tri(t, 64), 120); }     /* X1 = X2 = 0; WIN1 wraps */
        break; }
    default: {                                        /* a round spotlight: WIN0H per line by HBlank DMA */
        s32 cx = 128 + (sn(t * 2) >> 6), cy = 96 + (cs(t * 3) >> 7);
        g_dc |= DC_WIN1;
        WIN0V = winh(0, 192);
        WIN1H = winh(10, 60); WIN1V = winh(150, 180);
        WININ = (u16)((W_BG(0) | W_BG(1) | W_BG(2) | W_BG(3) | W_OBJ) | (W_BG(3) | W_FX) << 8);
        WINOUT = (u16)(W_BG(0) | W_BG(1) | W_BG(2) | W_BG(3) | W_OBJ | W_FX);
        for (int l = 0; l <= 192; l++) {
            s32 dy = l - cy;
            u16 v = 0;
            if (iabs(dy) < 60) { s32 w = spot_w[iabs(dy)]; v = winh(clampi(cx - w, 0, 255), clampi(cx + w, 0, 255)); }
            if (l == 0) WIN0H = v; else htab0[l - 1] = v;
        }
        hdma(0, htab0, 0x04000040, 1, DMA_DST_FIX);
        break; }
    }
    DISPCNT = g_dc;
    oam_title(g_dc);
    oam_commit();
}

/* --- T7: colour effects */
static void p_blend(int q) { (void)q; p_text(0); }
static void s_blend(u32 t, int q, u32 tq) {
    g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_BG(3) | DC_OBJ | DC_OBJ1D;
    DISPCNT = g_dc;
    BGCNT(0) = bgcnt(0, 1, SB_PICS, 0, BG_256);
    BGCNT(1) = bgcnt(1, 0, SB_PANEL, 0, 0);
    BGCNT(3) = bgcnt(3, 0, SB_BARS, 0, 0);
    BGHOFS(0) = (u16)(sn(t) >> 7); BGVOFS(0) = 0; BGHOFS(1) = (u16)(t >> 1); BGVOFS(1) = 0; BGHOFS(3) = 0; BGVOFS(3) = (u16)(t >> 2);
    s32 ev = tri(t, 16);
    MASTER_BRIGHT = 0;
    oam_hide();
    int i = 0;
    switch (q) {
    case 0:                                           /* BG0/BG1 over BG3/backdrop; then EVA+EVB > 16 and EVA > 16 */
        BLDCNT = (u16)(T_BG(0) | T_BG(1) | BL_ALPHA | T2(T_BG(3) | T_BD));
        BLDALPHA = HALF(tq) ? (u16)(12 | (u32)(4 + (tq & 15)) << 8) : (u16)(ev | (16 - ev) << 8);
        if (HALF(tq) && ((tq >> 3) & 1)) BLDALPHA = (u16)(31 | 3u << 8);
        break;
    case 1:                                           /* OBJ (normal and semi-transparent) over BG3; backdrop target */
        BLDCNT = (u16)(T_OBJ | T_BD | BL_ALPHA | T2(T_BG(3) | T_BD));
        BLDALPHA = (u16)(ev | (16 - ev) << 8);
        for (int n = 0; n < 10; n++) spr(i++, 2 + (n % 3 == 0), n & 1, 6 + n * 24, 50 + (n & 1) * 60 + (sn(t * 2 + n * 20) >> 8), n >= 5 ? A0_SEMI : 0, 0, 0, (u32)n + 1);
        break;
    case 2:                                           /* brightness up, then down; EVY sweeps past 16 */
        BLDCNT = (u16)(T_BG(0) | T_BG(3) | T_OBJ | T_BD | (HALF(tq) ? BL_DOWN : BL_UP));
        BLDY = (u16)(tri(tq, 15) * 4 / 3);
        for (int n = 0; n < 6; n++) spr(i++, 3, n & 1, 4 + n * 42, 90, 0, 0, 0, (u32)n + 4);
        break;
    default:                                          /* master brightness fades + alpha + palette cycling */
        BLDCNT = (u16)(T_BG(0) | BL_ALPHA | T2(T_BG(3)));
        BLDALPHA = 0x0A06;
        MASTER_BRIGHT = (u16)((u32)(HALF(tq) ? (tq - qlen / 2) : tq) & 31) | (HALF(tq) ? 2u : 1u) << 14;
        for (int c = 2; c < 15; c++) PAL_BG[7 * 16 + c] = pal_col(7 + (((u32)c + t) / 13 & 1) * 4, (u32)(2 + ((u32)c + t) % 13));
        break;
    }
    oam_title(g_dc);
    oam_commit();
}

/* --- T8: mosaic */
static void p_mosaic(int q) { (void)q; p_text(0); plan_cnt[VC] = CNT_ABG(1); need(VC, 0, 8, L_BMP16); }
static void s_mosaic(u32 t, int q, u32 tq) {
    u32 m = (t >> 2) & 15;
    g_dc = DC_ON | DC_MODE(0) | DC_BG(0) | DC_BG(1) | DC_BG(2) | DC_OBJ | DC_OBJ1D;
    BGCNT(0) = bgcnt(2, 0, SB_PATTERN, 0, BG_MOS);
    BGCNT(1) = bgcnt(0, 0, SB_PANEL, 0, BG_MOS);
    BGCNT(2) = bgcnt(1, 1, SB_PICS, 0, BG_256);      /* reference: no mosaic */
    BGHOFS(0) = (u16)(t >> 1); BGVOFS(0) = (u16)(t >> 2); BGHOFS(1) = 0; BGVOFS(1) = 0; BGHOFS(2) = (u16)(sn(t) >> 7); BGVOFS(2) = 0;
    MOSAIC = (u16)(m | (15 - m) << 4 | m << 8 | m << 12);
    oam_hide();
    int i = 0;
    hdma_off(0);
    switch (q) {
    case 0: break;
    case 1:
        if (!HALF(tq)) {                              /* mode 2: affine BG with mosaic (left, WIN0) next to the same without */
            g_dc = DC_ON | DC_MODE(2) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | DC_OBJ1D | DC_WIN0;
            BGCNT(2) = bgcnt(1, 1, SB_AFF, 1, BG_MOS | BG_WRAP);
            BGCNT(3) = bgcnt(2, 1, SB_AFF, 1, BG_WRAP);
            bg_aff(2, (s32)t, 200, 200, 128, 128, 64, 96);
            bg_aff(3, (s32)t, 200, 200, 128, 128, 192, 96);
            WIN0H = winh(0, 128); WIN0V = winh(0, 192);
            WININ = (u16)(W_BG(1) | W_BG(2) | W_OBJ); WINOUT = (u16)(W_BG(1) | W_BG(3) | W_OBJ);
        } else {                                      /* mode 5: 16-bit bitmap with mosaic */
            g_dc = DC_ON | DC_MODE(5) | DC_BG(1) | DC_BG(3) | DC_OBJ | DC_OBJ1D;
            BGCNT(3) = bgcnt(2, 0, 8, 1, BG_BMP16 | BG_MOS | BG_WRAP);
            bg_aff(3, (s32)(sn(t) >> 9), 256, 256, 128, 128, 128, 96);
        }
        break;
    case 2:                                           /* OBJ mosaic: normal and affine sprites, with and without */
        oam_rot(0, (s32)t, 256, 256);
        for (int n = 0; n < 10; n++) {
            int aff = n >= 7;
            spr(i++, n & 1 ? 3 : 2, n & 1, 4 + n * 24, 40 + (n & 3) * 30, (n % 3 ? A0_MOS : 0) | (aff ? A0_AFF : 0), 0, 0, (u32)n + 1);
        }
        break;
    default:                                          /* MOSAIC per line: bands of 16 lines */
        for (int l = 0; l <= 192; l++) {
            u32 b = (u32)(l >> 4), s = (b + (t >> 3)) & 15;
            u16 v = (u16)(s | (b & 7) << 4 | s << 8 | (b & 3) << 12);
            if (l == 0) MOSAIC = v; else htab0[l - 1] = v;
        }
        hdma(0, htab0, 0x0400004C, 1, DMA_DST_FIX);
        for (int n = 0; n < 6; n++) spr(i++, 3, n & 1, 8 + n * 40, 70 + (sn(t + n * 40) >> 8), A0_MOS, 0, 0, (u32)n + 2);
        break;
    }
    DISPCNT = g_dc;
    oam_title(g_dc);
    oam_commit();
}

/* --- T9: the 3D layer, blending, windows, capture */
static void gx_setup(u32 clear_alpha) {
    DISP3DCNT = 1u << 3;                              /* alpha blending */
    CLEAR_COLOR = (u32)rgb(2, 2, 6) | clear_alpha << 16 | 63u << 24;
    CLEAR_DEPTH = 0x7FFF;
    VIEWPORT = 0 | (0 << 8) | (255u << 16) | (191u << 24);
    MTX_MODE = 0; MTX_IDENT = 0;
    for (int k = 0; k < 16; k++) MTX_LOAD44 = (u32)proj[k];
    MTX_MODE = 1; MTX_IDENT = 0; MTX_MODE = 3; MTX_IDENT = 0; MTX_MODE = 2; MTX_IDENT = 0;
}
static void gx_quad(s32 a, s32 x, s32 y, s32 z, s32 size, u32 alpha, u32 id, u16 c0, u16 c1) {
    MTX_MODE = 2; MTX_IDENT = 0;
    MTX_TRANS = (u32)x; MTX_TRANS = (u32)y; MTX_TRANS = (u32)z;
    s32 c = cs(a), s = sn(a);
    MTX_MULT33 = (u32)c; MTX_MULT33 = (u32)s; MTX_MULT33 = 0; MTX_MULT33 = (u32)-s; MTX_MULT33 = (u32)c; MTX_MULT33 = 0;
    MTX_MULT33 = 0; MTX_MULT33 = 0; MTX_MULT33 = FX;
    POLY_ATTR = 0xC0 | alpha << 16 | id << 24; TEX_PARAM = 0;
    BEGIN_VTXS = 1;
    GX_COLOR = c0; GX_VTX16 = ((u32)-size & 0xFFFF) | (u32)size << 16; GX_VTX16 = 0;
    GX_COLOR = c1; GX_VTX16 = ((u32)-size & 0xFFFF) | ((u32)-size << 16); GX_VTX16 = 0;
    GX_COLOR = c0; GX_VTX16 = ((u32)size & 0xFFFF) | ((u32)-size << 16); GX_VTX16 = 0;
    GX_COLOR = c1; GX_VTX16 = ((u32)size & 0xFFFF) | (u32)size << 16; GX_VTX16 = 0;
    END_VTXS = 0;
}
static void gx_scene(u32 t) {
    gx_quad((s32)t, 0, 0, -12000, 3600, 31, 1, rgb(31, 18, 4), rgb(4, 18, 31));
    gx_quad(-(s32)t * 2, sn(t) >> 1, cs(t) >> 1, -9000, 2200, 16, 2, rgb(31, 31, 31), rgb(0, 31, 8));
    gx_quad((s32)t * 3, -2600, 1600, -15000, 1800, 31, 3, rgb(31, 0, 31), rgb(31, 31, 0));
    SWAP_BUF = 0;
}
static void p_3d(int q) {
    plan_a(L_A1AFF);
    plan_obj(VB, q < 2 ? L_OBJ1D : L_OBJFULL);
    if (q == 2) need(VC, 0, 8, L_BMP16);              /* the feedback (C/D) and the trails (D) start from the landscape */
    if (q >= 2) need(VD, 0, 8, L_BMP16);
}
static void s_3d(u32 t, int q, u32 tq) {
    int half = HALF(tq), i = 0;
    u32 objmap = q >= 2 ? 0 : DC_OBJ1D;              /* q2-q3: the 2D sheet of L_OBJFULL */
    g_dc = DC_ON | DC_MODE(0) | DC_3D | DC_BG(0) | DC_BG(1) | DC_BG(2) | DC_BG(3) | DC_OBJ | objmap;
    BGCNT(0) = bgcnt(1, 0, 0, 0, 0);                  /* the 3D layer: priority 1 */
    BGCNT(1) = bgcnt(0, 0, SB_PANEL, 0, 0);
    BGCNT(2) = bgcnt(2, 1, SB_PICS, 0, BG_256);
    BGCNT(3) = bgcnt(3, 0, SB_BARS, 0, 0);
    BGHOFS(0) = 0; BGHOFS(1) = 0; BGVOFS(1) = 0; BGHOFS(2) = (u16)(t >> 1); BGVOFS(2) = 0; BGHOFS(3) = 0; BGVOFS(3) = (u16)(t >> 2);
    BLDCNT = 0; WININ = WINOUT = 0; DISPCAPCNT = 0;
    gx_setup(0);
    oam_hide();
    switch (q) {
    case 0:                                           /* priorities; BG0HOFS shifts the 3D layer */
        if (half) BGHOFS(0) = (u16)(sn(t * 4) >> 7);
        break;
    case 1: {                                         /* 3D blending with 2nd targets, brightness, windows */
        s32 x0 = 40 + (sn(t) >> 6);
        g_dc |= DC_WIN0 | DC_WIN1;
        WIN0H = winh(x0, x0 + 70); WIN0V = winh(30, 110);
        WIN1H = winh(150, 230); WIN1V = winh(60, 170);
        WININ = (u16)((W_BG(1) | W_BG(2) | W_BG(3) | W_OBJ | W_FX) | (W_BG(0) | W_BG(1) | W_BG(2) | W_BG(3) | W_OBJ) << 8);
        WINOUT = (u16)(W_BG(0) | W_BG(1) | W_BG(2) | W_BG(3) | W_OBJ | W_FX);
        if (!half) { BLDCNT = (u16)(T_BG(0) | T_OBJ | BL_ALPHA | T2(T_BG(2) | T_BG(3) | T_BD)); BLDALPHA = 0x060A; }
        else { BLDCNT = (u16)(T_BG(0) | BL_UP | T2(T_BG(3))); BLDY = (u16)tri(t, 12); }
        break; }
    case 2:
        g_dc &= ~DC_BG(2);
        if (!half) {                                  /* feedback: capture into C / D, the other one shown as BG3 */
            int odd = t & 1;
            VRAMCNT(VC) = odd ? CNT_ABG(1) : CNT_LCDC;
            VRAMCNT(VD) = odd ? CNT_LCDC : CNT_ABG(1);
            g_dc = (g_dc & ~7u) | DC_MODE(5);
            BGCNT(3) = bgcnt(3, 0, 8, 1, BG_BMP16);   /* 16-bit 256x256 at identity: DraStic's direct layer */
            bg_ident(3, 0, 0);
            BLDCNT = (u16)(T_BG(3) | BL_DOWN | T2(T_BG(3))); BLDY = 1;
            DISPCAPCNT = 16u | (odd ? 3u : 2u) << 16 | 3u << 20 | 1u << 31;
        } else {                                      /* a captured frame shown as the full-screen OBJ bitmap */
            int cap = tq == qlen / 2 || tq == qlen / 2 + 16;  /* the screen, then the 3D layer alone */
            g_dc |= DC_BMP256;
            VRAMCNT(VC) = CNT_LCDC; VRAMCNT(VD) = CNT_LCDC;
            if (cap) {                                /* B as LCDC: no sprites this frame; capture into B + 32K */
                VRAMCNT(VB) = CNT_LCDC;
                DISPCAPCNT = 16u | 1u << 16 | 1u << 18 | 3u << 20 | (tq == qlen / 2 ? 0 : 1u << 24) | 1u << 31;
            } else {                                  /* OAM 0-11: the image (priority 3), the 3D in front of it */
                VRAMCNT(VB) = CNT_AOBJ;
                for (int c = 0; c < 12; c++)
                    oam_set(i++, (u32)(c >> 2) * 64 | A0_BMP, (u32)(c & 3) * 64 | 3u << 14, (256 + (u32)(c >> 2) * 256 + (u32)(c & 3) * 8) | 15u << 12 | 3u << 10);
                BGCNT(0) = bgcnt(0, 0, 0, 0, 0);
            }
        }
        break;
    default:                                          /* VRAM display of D with a blended capture into D: trails */
        VRAMCNT(VD) = CNT_LCDC;
        g_dc = DC_VRAMDISP(3) | DC_3D | DC_BG(0) | DC_BG(1) | DC_BG(3) | DC_OBJ | objmap;
        if (!half) DISPCAPCNT = 8u | 8u << 8 | 3u << 16 | 3u << 20 | 1u << 24 | 2u << 29 | 1u << 31;   /* 3D + VRAM */
        else DISPCAPCNT = 4u | 12u << 8 | 3u << 16 | 2u << 20 | 1u << 26 | ((tq >> 3) & 1 ? 1u : 2u) << 29 | 1u << 31;
                                                      /* screen + VRAM +32K, 256x128; every other 8 frames VRAM +32K alone */
        break;
    }
    for (int n = 0; n < 6; n++) spr(i++, 2, n & 1, 10 + n * 40, 140 + (sn(t * 2 + n * 40) >> 7), 0, 0, (u32)(n & 3), (u32)n + 4);
    DISPCNT = g_dc;
    oam_title(g_dc);
    oam_commit();
    gx_scene(t);
}

/* ------------------------------------------------------------------------------------------- dispatch */
static void (*const plans[NSCENES])(int) = {p_text, p_extpal, p_affine, p_bitmap, p_sprites, p_objfx, p_windows, p_blend, p_mosaic, p_3d};
static void (*const frames[NSCENES])(u32, int, u32) = {s_text, s_extpal, s_affine, s_bitmap, s_sprites, s_objfx, s_windows, s_blend, s_mosaic, s_3d};
static const char *const titles[NSCENES][4] = {
    {"T0 TEXT BG Q0 PRI 1,0,2,3 SCROLL", "T0 TEXT BG Q1 PRI 1,1,0,2 TIES", "T0 TEXT BG Q2 HDMA HOFS+BACKDROP", "T0 TEXT BG Q3 ALL PRI 0"},
    {"T1 EXT PAL Q0 SLOT 0/1", "T1 EXT PAL Q1 EXT PAL OFF", "T1 EXT PAL Q2 SLOT 2/3", "T1 EXT PAL Q3 SLOT TOGGLE"},
    {"T2 AFFINE Q0 MODE 2 WRAP/CLIP", "T2 AFFINE Q1 MODE 1 HDMA FLOOR", "T2 AFFINE Q2 MODE 4/5 EXTENDED", "T2 AFFINE Q3 MODE 6 LARGE BMP"},
    {"T3 BITMAP Q0 MODE 5 DIRECT+8BPP", "T3 BITMAP Q1 MODE 3 512X256", "T3 BITMAP Q2 MODE 4/5 AFFINE", "T3 BITMAP Q3 VRAM/MAIN MEM/HDMA ROW"},
    {"T4 OBJ Q0 1D/32,64 SIZES FLIPS", "T4 OBJ Q1 1D/128,256 EXTPAL CROWD", "T4 OBJ Q2 2D MAP MULTIPLEX", "T4 OBJ Q3 2D OVERLAP WRAP"},
    {"T5 OBJ FX Q0 AFFINE DOUBLE", "T5 OBJ FX Q1 BITMAP ALPHA", "T5 OBJ FX Q2 SEMI-TRANSPARENT", "T5 OBJ FX Q3 FULL-SCREEN BITMAP"},
    {"T6 WINDOW Q0 WIN0 WIN1 OUT", "T6 WINDOW Q1 OBJ WIN+WIN0+WIN1", "T6 WINDOW Q2 EDGE CASES", "T6 WINDOW Q3 HDMA SPOTLIGHT"},
    {"T7 EFFECT Q0 ALPHA BG/BG", "T7 EFFECT Q1 ALPHA OBJ/BD", "T7 EFFECT Q2 BRIGHT UP/DOWN", "T7 EFFECT Q3 MASTER BRIGHT"},
    {"T8 MOSAIC Q0 TEXT BG", "T8 MOSAIC Q1 AFFINE/BITMAP", "T8 MOSAIC Q2 OBJ", "T8 MOSAIC Q3 HDMA PER LINE"},
    {"T9 3D Q0 LAYER PRI HOFS", "T9 3D Q1 BLEND WINDOWS", "T9 3D Q2 CAPTURE FEEDBACK/OBJ", "T9 3D Q3 VRAM DISPLAY TRAILS"}};

int main(void) {
    POWCNT1 = 0x820F;                                 /* LCDs, 2D A+B, 3D, A on top */
    DISPCNT = 0; DISPCNT_B = 0;
    for (int b = 0; b < NBANK; b++) VRAMCNT(b) = CNT_LCDC;
    palettes_default();
    for (u32 i = 0; i < 256; i++) pal16[i] = pal_col(i >> 4, i & 15);
    /* the library */
    build_pics();
    build_tiles4(L_A0, 256);
    build_tiles8(L_A0 + 0x4000);
    build_maps_a0();
    build_a1aff();
    build_bmp8();
    build_bmp16();
    build_bmp16w();
    build_stream();
    build_large();
    build_small();
    build_obj();
    build_extpal();
    for (int l = 0; l < 64; l++) spot_w[l] = (u16)isqrt((u32)(60 * 60 - l * l));
    gx_setup(0);

    qlen = cfg.mode == 1 ? cfg.ramp / 4 : 60;
    if (!qlen) qlen = 1;
    u32 frame = 0;
    int cur_scene = -1, cur_q = -1, ready = 0;
    for (;;) {
        wait_vblank();
        u32 scene, t;
        if (cfg.mode == 1) { scene = (frame / cfg.ramp) % NSCENES; t = frame % cfg.ramp; }
        else { scene = cfg.level % NSCENES; t = frame; }
        int q = (int)((t / qlen) & 3);
        u32 tq = t % qlen;
        if ((int)scene != cur_scene || q != cur_q) {     /* a quarter starts from reset registers, palettes and OAM */
            cur_scene = (int)scene; cur_q = q;
            reset2d();
            plan_begin();
            plans[scene](q);
#ifdef DS2D_CHECK
            if (!plan_complete()) { DISPCNT = 0; for (;;) {} }
#endif
            ready = 0;
        }
        if (!ready) {
            if (job_i < njobs) {                      /* display off (white) while VRAM is filled */
                DISPCNT = 0; run_jobs();
                if (scene == 9) { gx_setup(0); gx_scene(t); }   /* so the first frame shown has the 3D of frame t-1 */
#ifdef DS2D_CHECK
                if (VCOUNT < 192) { for (;;) {} }
#endif
                frame++; continue;
            }
            apply_mapping();
            title_render(titles[scene][q], (scene == 4 || scene == 5 || scene == 9) && q >= 2);
            ready = 1;
        }
        fx_reset();
        frames[scene](t, q, tq);
#ifdef DS2D_CHECK                                     /* a check build: halt (white) if the vblank work spilled into the frame */
        if (VCOUNT < 192 && !(scene == 4 && q == 2)) { DISPCNT = 0; for (;;) {} }
#endif
        frame++;
    }
}
