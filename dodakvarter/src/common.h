// common.h: small helpers shared by every file (types, math, the random number generators).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SGN(x) ((x) > 0 ? 1 : (x) < 0 ? -1 : 0)
#define PI_F 3.14159265f

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float len2(float x, float y) { return sqrtf(x * x + y * y); }
static inline float dist2f(float ax, float ay, float bx, float by) { float dx = bx - ax, dy = by - ay; return dx * dx + dy * dy; }
static inline int idiv_floor(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
/* the angle difference a - b wrapped to [-pi, pi] */
static inline float angdiff(float a, float b) { float d = fmodf(a - b + 3 * PI_F, 2 * PI_F); if (d < 0) d += 2 * PI_F; return d - PI_F; }

/* PCG32: one per purpose (map, gameplay, effects) so cosmetic randomness never changes a seeded run */
typedef struct { uint64_t s, inc; } Rng;
static inline uint32_t rng_u32(Rng *r) {
    uint64_t old = r->s;
    r->s = old * 6364136223846793005ULL + r->inc;
    uint32_t x = (uint32_t)(((old >> 18) ^ old) >> 27), rot = (uint32_t)(old >> 59);
    return (x >> rot) | (x << ((-rot) & 31));
}
static inline void rng_seed(Rng *r, uint64_t seed, uint64_t stream) {
    r->s = 0; r->inc = (stream << 1) | 1; rng_u32(r); r->s += seed; rng_u32(r);
}
static inline int rng_int(Rng *r, int n) { return n <= 1 ? 0 : (int)(((uint64_t)rng_u32(r) * (uint32_t)n) >> 32); }  /* [0, n) */
static inline int rng_range(Rng *r, int lo, int hi) { return lo + rng_int(r, hi - lo + 1); }                       /* [lo, hi] */
static inline float rng_float(Rng *r) { return (rng_u32(r) >> 8) * (1.0f / 16777216.0f); }                       /* [0, 1) */
static inline float rng_rangef(Rng *r, float lo, float hi) { return lo + (hi - lo) * rng_float(r); }
static inline int rng_chance(Rng *r, float p) { return rng_float(r) < p; }

/* a stable hash of a few integers (procedural textures: same tile, same pixels) */
static inline uint32_t hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
