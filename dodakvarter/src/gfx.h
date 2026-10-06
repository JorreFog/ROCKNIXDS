// gfx.h: the software renderer. Everything is drawn into 32-bit surfaces at the game's logical resolution
// (320x240 on the RG DS, 341x256 on the RG DS Plus); the platform layer scales them to the panels.
#pragma once
#include "common.h"

/* 0x00RRGGBB in a surface (XRGB8888, the panels' format); images carry alpha in the top byte */
typedef struct { int w, h, pitch; uint32_t *px; int cx0, cy0, cx1, cy1; } Surf;   /* clip: [cx0, cx1) x [cy0, cy1) */
typedef struct { int w, h; uint32_t *px; } Img;

#define RGB(r, g, b) ((uint32_t)(((r) & 255) << 16 | ((g) & 255) << 8 | ((b) & 255)))
#define CR(c) (((c) >> 16) & 255)
#define CG(c) (((c) >> 8) & 255)
#define CB(c) ((c) & 255)
#define CA(c) ((c) >> 24)

enum { FLIP_X = 1, FLIP_Y = 2 };
enum { FONT_SMALL, FONT_NORMAL };                 /* 3x5 caps/digits, 5x7 with lowercase and descenders */

void surf_alloc(Surf *s, int w, int h);
void surf_free(Surf *s);
void surf_clip(Surf *s, int x, int y, int w, int h);
void surf_noclip(Surf *s);
void img_alloc(Img *im, int w, int h);

uint32_t col_mix(uint32_t a, uint32_t b, int t);          /* t 0..256: a -> b */
uint32_t col_mul(uint32_t c, int r, int g, int b);        /* per channel, 256 = 1.0 */
uint32_t col_scale(uint32_t c, int k);                    /* 256 = 1.0, saturating */
uint32_t col_add(uint32_t a, uint32_t b);                 /* saturating add */

void fill(Surf *s, uint32_t c);
void pset(Surf *s, int x, int y, uint32_t c);
void pblend(Surf *s, int x, int y, uint32_t c, int a);    /* a 0..255 */
void padd(Surf *s, int x, int y, uint32_t c);
void rectf(Surf *s, int x, int y, int w, int h, uint32_t c);
void rect_blend(Surf *s, int x, int y, int w, int h, uint32_t c, int a);
void rect_dither(Surf *s, int x, int y, int w, int h, uint32_t c, int level);   /* level 1..3: 25/50/75% checker */
void rect_line(Surf *s, int x, int y, int w, int h, uint32_t c);
void hline(Surf *s, int x0, int x1, int y, uint32_t c);
void vline(Surf *s, int x, int y0, int y1, uint32_t c);
void line(Surf *s, int x0, int y0, int x1, int y1, uint32_t c);
void line_blend(Surf *s, int x0, int y0, int x1, int y1, uint32_t c, int a);
void circlef(Surf *s, int cx, int cy, int r, uint32_t c);
void circle_blend(Surf *s, int cx, int cy, int r, uint32_t c, int a);
void circle(Surf *s, int cx, int cy, int r, uint32_t c);
void ellipse_blend(Surf *s, int cx, int cy, int rx, int ry, uint32_t c, int a);
void bevel(Surf *s, int x, int y, int w, int h, uint32_t face, uint32_t hi, uint32_t lo);   /* a raised panel */

void blit(Surf *s, const Img *im, int x, int y, int flags);
/* tint toward `tint` by amt 0..256 (hit flashes, freeze), alpha 0..255 overall */
void blit_ex(Surf *s, const Img *im, int x, int y, int flags, uint32_t tint, int amt, int alpha);
void blit_region(Surf *s, const Img *im, int sx, int sy, int sw, int sh, int dx, int dy, int flags);
void blit_scaled(Surf *s, const Img *im, int x, int y, int k);         /* integer nearest scale */
void blit_silhouette(Surf *s, const Img *im, int x, int y, int flags, uint32_t c, int a);
void blit_rot(Surf *s, const Img *im, float cx, float cy, float ang, int flags);   /* rotated about its center */
void copy_rect(Surf *dst, const uint32_t *src, int spitch, int sw, int sh, int sx, int sy, int w, int h, int dx, int dy);

int text_w(int font, const char *s);
int text_h(int font);
int text(Surf *s, int font, int x, int y, uint32_t c, const char *str);               /* returns the end x */
int text_sh(Surf *s, int font, int x, int y, uint32_t c, uint32_t shadow, const char *str);
int text_ol(Surf *s, int font, int x, int y, uint32_t c, uint32_t outline, const char *str);
int text_big(Surf *s, int x, int y, int k, uint32_t c, uint32_t outline, const char *str);   /* FONT_NORMAL x k */
int textf(Surf *s, int font, int x, int y, uint32_t c, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
void text_center(Surf *s, int font, int cx, int y, uint32_t c, uint32_t shadow, const char *str);
int text_wrap(Surf *s, int font, int x, int y, int w, uint32_t c, const char *str);      /* returns the height */
/* thousands separated with a thin space the Swedish way: 12 345 */
void fmt_num(char *out, int n);
