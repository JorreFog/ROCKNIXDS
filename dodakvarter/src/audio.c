// audio.c: every sound is synthesised at start (no files): noise, oscillators, filters and envelopes. A mixer
// runs on the platform's audio thread; the game sends it commands through a lock-free ring. Music is a small
// sequencer: the title plays "Vem kan segla förutan vind?" (a traditional Swedish folk song) as a music box; the
// Mystery Box and the game over have little pieces of their own. Under the play runs the night outside, made as it
// plays: autumn rain, winter wind, or a midsummer night's crickets and birds.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define RATE 48000
#define NVOICES 28

typedef struct { int16_t *pcm; int n; } Sound;
static Sound snd[SFX_COUNT];

typedef struct { const Sound *s; float pos, rate, vol, pan; int active; } Voice;
static Voice voices[NVOICES];

/* commands from the game thread */
typedef struct { int id; float vol, pan, rate; } Cmd;
static Cmd ring[256];
static volatile unsigned rhead, rtail;
static int master = 80, want_music = MUS_NONE, want_amb = AMB_NONE;   /* set by the game, read by the mixer: atomics */
static int ready;

/* ---------------------------------------------------------------- synthesis helpers */
static uint32_t nstate = 12345;
static float noise(void) { nstate = nstate * 1664525u + 1013904223u; return ((nstate >> 9) & 0xFFFF) / 32768.0f - 1.0f; }
typedef struct { float y; } LP;
static float lp(LP *f, float x, float cutoff) { float a = 1 - expf(-2 * PI_F * cutoff / RATE); f->y += a * (x - f->y); return f->y; }
typedef struct { float lo, bp; } SVF;                           /* state variable filter: band pass */
static float bandpass(SVF *f, float x, float freq, float q) {
    float k = 2 * sinf(PI_F * MIN(freq, RATE / 6.0f) / RATE);
    float hi = x - f->lo - q * f->bp;
    f->bp += k * hi; f->lo += k * f->bp;
    return f->bp;
}
static float *buf_new(float secs, int *n) { *n = (int)(secs * RATE); return calloc((size_t)*n, sizeof(float)); }
static void finish(int id, float *b, int n, float gain) {
    float peak = 0.0001f;
    for (int i = 0; i < n; i++) peak = MAX(peak, fabsf(b[i]));
    float k = gain / peak;
    snd[id].pcm = malloc((size_t)n * 2); snd[id].n = n;
    for (int i = 0; i < n; i++) {
        float v = b[i] * k;
        if (i > n - 64) v *= (n - i) / 64.0f;                   /* no click at the end */
        snd[id].pcm[i] = (int16_t)(CLAMP(v, -1, 1) * 32000);
    }
    free(b);
}
static float env(float t, float a, float d) { return t < a ? t / a : expf(-(t - a) / d); }

/* a gunshot: a crack of noise, a body, a low thump */
static void gun(int id, float len, float crack_cut, float body_cut, float thump_hz, float tail) {
    int n; float *b = buf_new(len, &n);
    LP f1 = { 0 }, f2 = { 0 };
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE, x = noise();
        float crack = lp(&f1, x, crack_cut) * env(t, 0.0005f, 0.012f);
        float body = lp(&f2, x, body_cut) * env(t, 0.001f, tail);
        ph += 2 * PI_F * thump_hz * (1 - t * 2) / RATE;
        float thump = sinf(ph) * env(t, 0.001f, 0.04f);
        b[i] = crack * 1.2f + body * 0.9f + thump * 0.8f;
    }
    finish(id, b, n, 0.9f);
}

static void groan(int id, float len, float f0, float f1, float seed) {
    int n; float *b = buf_new(len, &n);
    SVF a = { 0 }, c = { 0 }; LP l = { 0 };
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE, k = t / len;
        float f = f0 + (f1 - f0) * k + sinf(t * 7 + seed) * 6 + sinf(t * 23 + seed) * 3;
        ph += f / RATE; ph -= floorf(ph);
        float saw = ph * 2 - 1 + noise() * 0.25f;
        float v = bandpass(&a, saw, 550 + 150 * sinf(t * 3 + seed), 0.35f) + 0.6f * bandpass(&c, saw, 1150, 0.4f);
        v = lp(&l, v, 2200);
        float e = MIN(1.0f, t / 0.12f) * (1 - k) * (0.7f + 0.3f * sinf(t * 11 + seed));
        b[i] = v * e;
    }
    finish(id, b, n, 0.8f);
}

static void tone_seq(int id, const float *freqs, int nf, float note, float decay, int bell) {
    int n; float *b = buf_new(note * nf + decay * 3, &n);
    for (int k = 0; k < nf; k++) {
        if (freqs[k] <= 0) continue;
        int s0 = (int)(k * note * RATE);
        for (int i = s0; i < n; i++) {
            float t = (float)(i - s0) / RATE, e = expf(-t / decay);
            if (e < 0.001f) break;
            float w = 2 * PI_F * freqs[k] * t;
            float v = bell ? sinf(w) + 0.5f * sinf(w * 2.01f) * expf(-t / (decay * 0.3f)) + 0.25f * sinf(w * 3.98f) * expf(-t / (decay * 0.15f))
                           : (sinf(w) > 0 ? 0.6f : -0.6f) + 0.3f * sinf(w);
            b[i] += v * e * MIN(1.0f, t * 400);
        }
    }
    finish(id, b, n, 0.7f);
}

static float midi(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

/* the perk machines' jingles: a little tune each (notes and sixteenths; 1 rests), on a toy organ */
static const signed char JINGLES[PK_COUNT][32] = {
    { 67,2, 72,2, 72,2, 76,2, 79,4, 76,2, 72,2, 74,2, 77,2, 76,2, 74,2, 72,6 },                     /* Julmust: a march */
    { 72,1, 74,1, 76,1, 77,1, 79,1, 77,1, 76,1, 74,1, 76,1, 77,1, 79,1, 81,1, 83,2, 84,4 },          /* Snabbkaffe: in a hurry */
    { 64,2, 64,2, 1,2, 64,2, 67,2, 1,2, 64,2, 62,2, 60,4, 59,4 },                                   /* Salmiak: rat-a-tat */
    { 65,4, 69,2, 72,2, 69,4, 70,2, 69,2, 67,4, 65,8 },                                             /* Kanelbulle: a waltz */
    { 72,3, 74,1, 76,3, 72,1, 79,3, 76,1, 77,2, 76,2, 74,2, 72,6 },                                 /* Blåbärssoppa: skipping */
    { 69,1, 72,1, 76,1, 81,1, 76,1, 72,1, 69,1, 72,1, 76,1, 81,1, 84,2, 81,2, 1,1, 81,4 },          /* Lingondricka: sparks */
    { 67,2, 67,1, 67,1, 72,4, 67,2, 72,2, 76,4, 79,6 },                                             /* Kaviar: a fanfare */
};
static void jingle(int k) {
    const signed char *j = JINGLES[k];
    const float six = 0.09f, hold = expf(-2.5f / RATE), fall = expf(-25.0f / RATE);
    int len = 0; for (int i = 0; j[i]; i += 2) len += j[i + 1];
    int n; float *b = buf_new(len * six + 0.5f, &n);
    float t0 = 0;
    for (int i = 0; j[i]; i += 2) {
        float d = j[i + 1] * six;
        if (j[i] > 1) {
            float f = midi(j[i]) / RATE, ph = 0, e = 1;
            int s0 = (int)(t0 * RATE), s1 = MIN(n, (int)((t0 + d + 0.3f) * RATE)), sd = s0 + (int)(d * RATE);
            for (int s = s0; s < s1; s++) {                 /* rings while it's held, then falls away */
                float a = MIN(1.0f, (float)(s - s0) / (0.006f * RATE));
                e *= s < sd ? hold : fall;
                ph += f; ph -= floorf(ph);
                float w = 2 * PI_F * ph, sw = sinf(w);
                b[s] += (sw * 0.55f + 0.4f * sw * cosf(w) + (ph < 0.5f ? 0.12f : -0.12f)) * a * e;   /* (sin 2w = 2 sin w cos w) */
            }
        }
        t0 += d;
    }
    finish(SFX_JINGLE + k, b, n, 0.5f);
}

static void synth_all(void) {
    int n; float *b;
    for (int k = 0; k < PK_COUNT; k++) jingle(k);
    gun(SFX_PISTOL, 0.22f, 7000, 1800, 120, 0.05f);
    gun(SFX_SMG, 0.14f, 8000, 2200, 140, 0.03f);
    gun(SFX_RIFLE, 0.28f, 6000, 1500, 100, 0.07f);
    gun(SFX_SHOTGUN, 0.45f, 4500, 900, 80, 0.14f);
    gun(SFX_SNIPER, 0.7f, 9000, 1200, 70, 0.22f);
    gun(SFX_LMG, 0.2f, 6500, 1400, 90, 0.05f);
    /* rocket: a rising whoosh */
    b = buf_new(0.55f, &n); { SVF f = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = bandpass(&f, noise(), 300 + t * 2400, 0.5f) * env(t, 0.02f, 0.25f) + noise() * 0.3f * env(t, 0.0005f, 0.01f); } finish(SFX_ROCKET, b, n, 0.8f); }
    /* ray gun: pew */
    b = buf_new(0.25f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float f = 1400 * expf(-t * 9) + 250 + 40 * sinf(t * 90); ph += f / RATE; ph -= floorf(ph); b[i] = (ph < 0.5f ? 0.7f : -0.7f) * env(t, 0.002f, 0.08f); } finish(SFX_RAY, b, n, 0.6f); }
    /* zap: buzz and crackle */
    b = buf_new(0.4f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 120.0f / RATE; ph -= floorf(ph); float crack = (noise() > 0.92f ? noise() : 0); b[i] = ((ph < 0.5f ? 0.4f : -0.4f) + crack * 1.2f + noise() * 0.2f) * env(t, 0.002f, 0.12f); } finish(SFX_ZAP, b, n, 0.7f); }
    /* frost: a hiss */
    b = buf_new(0.18f, &n); { LP l = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE, x = noise(); b[i] = (x - lp(&l, x, 3000)) * env(t, 0.01f, 0.06f); } finish(SFX_FROST, b, n, 0.5f); }
    /* explosion */
    b = buf_new(1.4f, &n); { LP l = { 0 }, l2 = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float x = noise(); ph += 2 * PI_F * (55 - t * 20) / RATE; b[i] = lp(&l, x, 900 * expf(-t * 1.5f) + 150) * env(t, 0.002f, 0.35f) * 1.4f + sinf(ph) * env(t, 0.002f, 0.18f) + lp(&l2, x, 5000) * env(t, 0.0005f, 0.02f); } finish(SFX_EXPLODE, b, n, 1.0f); }
    b = buf_new(2.2f, &n); { LP l = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (40 - t * 8) / RATE; b[i] = lp(&l, noise(), 600 * expf(-t) + 100) * env(t, 0.01f, 0.7f) * 1.3f + sinf(ph) * env(t, 0.01f, 0.5f); } finish(SFX_KABOOM, b, n, 1.0f); }
    /* thunder: a crack, then the rumble rolling, swelling and dying away */
    b = buf_new(3.6f, &n); { LP l1 = { 0 }, l2 = { 0 }, l3 = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE;
        float roll = 0.55f + 0.45f * sinf(t * 7.3f) * sinf(t * 2.9f + 1);
        float rumble = lp(&l2, lp(&l1, noise(), 160), 110) * 3.2f * MIN(1.0f, t / 0.25f) * expf(-t / 1.3f) * roll;
        float crack = lp(&l3, noise(), 2400) * env(t, 0.003f, 0.06f) * 0.8f;
        b[i] = rumble + crack; } finish(SFX_THUNDER, b, n, 0.9f); }
    /* the bosses: a roar, Draugen's war horn, a swing, a slam, Näcken's fiddle, a splash, the lindworm's hiss */
    b = buf_new(1.7f, &n); { SVF a = { 0 }, a2 = { 0 }; float ph = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / RATE, f = 70 + 40 * sinf(PI_F * MIN(1.0f, t / 1.4f));
            ph += f / RATE; ph -= floorf(ph);
            float saw = ph * 2 - 1, x = saw * 0.7f + noise() * 0.5f, growl = 0.65f + 0.35f * sinf(2 * PI_F * 28 * t);
            b[i] = (bandpass(&a, x, 520 + 200 * sinf(t * 3), 0.5f) * 1.2f + bandpass(&a2, x, 1100, 0.6f) * 0.5f + saw * 0.25f) * growl * MIN(1.0f, t / 0.12f) * MAX(0.0f, 1 - t / 1.7f);
        }
        finish(SFX_ROAR, b, n, 0.9f); }
    b = buf_new(1.9f, &n); { LP l = { 0 }; float ph = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / RATE, f = 98 * (1 - 0.06f * expf(-t * 12)) * (1 + 0.004f * sinf(2 * PI_F * 5 * t));
            ph += f / RATE; ph -= floorf(ph);
            float e = MIN(1.0f, t / 0.25f) * (t > 1.5f ? MAX(0.0f, 1 - (t - 1.5f) / 0.4f) : 1), v = 0;
            for (int h = 1; h <= 8; h++) v += sinf(2 * PI_F * ph * h) / h * (h <= 2 + (int)(e * 6) ? 1.0f : 0.2f);
            b[i] = lp(&l, v, 900 + 1500 * e) * e;
        }
        finish(SFX_HORN, b, n, 0.8f); }
    b = buf_new(0.4f, &n); { SVF a = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE, s = sinf(PI_F * t / 0.4f); b[i] = bandpass(&a, noise(), 2600 - t * 5000, 0.7f) * s * s; } finish(SFX_SWOOSH, b, n, 0.7f); }
    b = buf_new(0.9f, &n); { LP l = { 0 }; float ph = 0;
        for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (60 - t * 30) / RATE; b[i] = sinf(ph) * env(t, 0.002f, 0.22f) * 1.2f + lp(&l, noise(), 1200 * expf(-t * 3) + 200) * env(t, 0.001f, 0.12f) * 1.4f; }
        finish(SFX_SLAM, b, n, 1.0f); }
    b = buf_new(1.9f, &n); { SVF a = { 0 }, a2 = { 0 }; float ph = 0; static const int notes[6] = { 69, 72, 76, 74, 72, 71 };
        for (int i = 0; i < n; i++) {
            float t = (float)i / RATE; int k = MIN(5, (int)(t / 0.3f)); float tn = t - k * 0.3f;
            float f = midi(notes[k]) * (1 + 0.012f * sinf(2 * PI_F * 6 * t) * MIN(1.0f, tn * 4));
            ph += f / RATE; ph -= floorf(ph);
            float saw = ph * 2 - 1 + noise() * 0.06f;
            float e = MIN(1.0f, tn / 0.05f) * (0.8f + 0.2f * sinf(PI_F * tn / 0.3f)) * (t > 1.6f ? MAX(0.0f, 1 - (t - 1.6f) / 0.3f) : 1);
            b[i] = (bandpass(&a, saw, 1100, 0.5f) + bandpass(&a2, saw, 2600, 0.7f) * 0.5f + saw * 0.15f) * e;
        }
        finish(SFX_FIDDLE, b, n, 0.7f); }
    b = buf_new(0.7f, &n); { SVF a = { 0 };
        for (int i = 0; i < n; i++) { float t = (float)i / RATE; float x = bandpass(&a, noise(), 1800 - t * 1600, 0.4f) * env(t, 0.003f, 0.18f); float blip = sinf(2 * PI_F * (700 + 400 * sinf(t * 37)) * t) * env(fmodf(t, 0.09f), 0.002f, 0.02f) * (t > 0.1f ? 0.3f : 0); b[i] = x * 1.3f + blip; }
        finish(SFX_SPLASH, b, n, 0.8f); }
    b = buf_new(1.0f, &n); { LP l = { 0 };
        for (int i = 0; i < n; i++) { float t = (float)i / RATE, x = noise(), hp = x - lp(&l, x, 3500); b[i] = hp * sinf(PI_F * t) * (0.75f + 0.25f * sinf(2 * PI_F * 40 * t)); }
        finish(SFX_HISS, b, n, 0.7f); }
    /* (original) a boss felled: brass in D, three quick notes, a leap up, and the D an octave above, held */
    b = buf_new(2.9f, &n); { LP l = { 0 }; float ph = 0, ph2 = 0, t0 = 0;
        static const float fan[7][2] = { { 62, .13f }, { 62, .13f }, { 62, .13f }, { 69, .45f }, { 67, .16f }, { 69, .16f }, { 74, 1.0f } };
        for (int k = 0; k < 7; k++) {
            float f = midi((int)fan[k][0]) / RATE, d = fan[k][1];
            int s0 = (int)(t0 * RATE), s1 = MIN(n, (int)((t0 + d + (k == 6 ? 0.6f : 0)) * RATE));
            for (int s = s0; s < s1; s++) {
                float tn = (float)(s - s0) / RATE;
                float e = MIN(1.0f, tn / 0.02f) * (k == 6 ? expf(-tn * 1.8f) : tn < d - 0.03f ? 1.0f : MAX(0.0f, (d - tn) / 0.03f));
                ph += f; ph -= floorf(ph); ph2 += f * 0.5f; ph2 -= floorf(ph2);
                b[s] += lp(&l, (ph * 2 - 1) * 0.6f + (ph2 * 2 - 1) * 0.35f, 700 + 2600 * e) * e;
            }
            t0 += d;
        }
        finish(SFX_FANFARE, b, n, 0.8f); }
    /* reload: click, slide, click */
    b = buf_new(0.5f, &n); { LP l = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float x = noise(); float c1 = env(t, 0.0005f, 0.006f), c2 = t > 0.18f ? env(t - 0.18f, 0.02f, 0.05f) * 0.4f : 0, c3 = t > 0.36f ? env(t - 0.36f, 0.0005f, 0.008f) : 0; b[i] = (x - lp(&l, x, 1500)) * (c1 + c3) + x * c2 * 0.5f; } finish(SFX_RELOAD, b, n, 0.6f); }
    b = buf_new(0.05f, &n); for (int i = 0; i < n; i++) b[i] = noise() * env((float)i / RATE, 0.0005f, 0.004f); finish(SFX_EMPTY, b, n, 0.5f);
    b = buf_new(0.06f, &n); for (int i = 0; i < n; i++) b[i] = noise() * env((float)i / RATE, 0.0005f, 0.006f) + sinf(i * 0.3f) * env((float)i / RATE, 0.0005f, 0.01f); finish(SFX_SWAP, b, n, 0.5f);
    /* knife swoosh */
    b = buf_new(0.18f, &n); { SVF f = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = bandpass(&f, noise(), 800 + t * 9000, 0.6f) * sinf(PI_F * t / 0.18f); } finish(SFX_KNIFE, b, n, 0.5f); }
    /* hit: a wet thud */
    b = buf_new(0.12f, &n); { LP l = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (180 - t * 900) / RATE; b[i] = (lp(&l, noise(), 1200) + sinf(ph) * 0.6f) * env(t, 0.001f, 0.03f); } finish(SFX_HIT, b, n, 0.6f); }
    b = buf_new(0.3f, &n); { LP l = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (110 - t * 120) / RATE; b[i] = (lp(&l, noise(), 700) * 1.3f + sinf(ph)) * env(t, 0.002f, 0.07f); } finish(SFX_SPLAT, b, n, 0.7f); }
    groan(SFX_GROAN1, 1.1f, 120, 85, 0.3f);
    groan(SFX_GROAN2, 0.8f, 95, 130, 1.7f);
    groan(SFX_GROAN3, 1.3f, 140, 70, 4.1f);
    groan(SFX_ZATTACK, 0.45f, 160, 90, 2.2f);
    /* the player hurt: a short grunt */
    b = buf_new(0.25f, &n); { SVF a = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += (210 - t * 300) / RATE; ph -= floorf(ph); b[i] = bandpass(&a, ph * 2 - 1, 700, 0.4f) * env(t, 0.01f, 0.07f); } finish(SFX_HURT, b, n, 0.7f); }
    /* a wolf's howl */
    b = buf_new(1.3f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float f = 420 + 300 * sinf(PI_F * MIN(1.0f, t / 1.1f)) + 8 * sinf(t * 40); ph += 2 * PI_F * f / RATE; b[i] = (sinf(ph) + 0.2f * sinf(ph * 2) + noise() * 0.05f) * MIN(1.0f, t / 0.15f) * MAX(0.0f, 1 - t / 1.3f); } finish(SFX_WOLF, b, n, 0.6f); }
    /* the moose: a deep bellow */
    b = buf_new(1.5f, &n); { SVF a = { 0 }; float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float f = 85 + 30 * sinf(PI_F * t / 1.5f) + 5 * sinf(t * 31); ph += f / RATE; ph -= floorf(ph); float v = ph * 2 - 1 + noise() * 0.4f; b[i] = (bandpass(&a, v, 420, 0.3f) + v * 0.3f) * MIN(1.0f, t / 0.1f) * MAX(0.0f, 1 - t / 1.5f) * (0.8f + 0.2f * sinf(t * 17)); } finish(SFX_MOOSE, b, n, 0.9f); }
    /* boards: crack, and the hammer */
    b = buf_new(0.25f, &n); { SVF a = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = (bandpass(&a, noise(), 320, 0.15f) * 2 + noise() * env(t, 0.0005f, 0.01f)) * env(t, 0.001f, 0.06f); } finish(SFX_BOARD_BREAK, b, n, 0.7f); }
    b = buf_new(0.3f, &n); for (int i = 0; i < n; i++) { float t = (float)i / RATE, t2 = t - 0.14f; b[i] = sinf(2 * PI_F * 230 * t) * env(t, 0.0005f, 0.03f) + (t2 > 0 ? sinf(2 * PI_F * 250 * t2) * env(t2, 0.0005f, 0.03f) : 0) + noise() * (env(t, 0.0005f, 0.004f) + (t2 > 0 ? env(t2, 0.0005f, 0.004f) : 0)); } finish(SFX_BOARD_FIX, b, n, 0.6f);
    /* ka-ching */
    { float f[] = { 2093, 2637, 3136 }; tone_seq(SFX_BUY, f, 3, 0.05f, 0.25f, 1); }
    b = buf_new(0.25f, &n); for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = (sinf(2 * PI_F * 110 * t) > 0 ? 0.5f : -0.5f) * env(t, 0.005f, 0.12f); } finish(SFX_DENY, b, n, 0.5f);
    /* the box's music-box jingle */
    { float f[] = { midi(76), midi(79), midi(83), midi(88), midi(86), midi(83), midi(79), midi(83), midi(88) }; tone_seq(SFX_BOX, f, 9, 0.12f, 0.5f, 1); }
    /* the Dalahäst's whinny */
    b = buf_new(1.0f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float f = 700 + 250 * sinf(t * 38) * MAX(0.0f, 1 - t) + 200 * (1 - t); ph += 2 * PI_F * f / RATE; b[i] = (sinf(ph) + 0.4f * sinf(ph * 2.02f)) * MIN(1.0f, t / 0.05f) * MAX(0.0f, 1 - t) ; } finish(SFX_HORSE, b, n, 0.6f); }
    { float f[] = { midi(72), midi(76), midi(79), midi(84) }; tone_seq(SFX_POWERUP_SPAWN, f, 4, 0.06f, 0.3f, 1); }
    { float f[] = { midi(79), midi(84), midi(88), midi(91), midi(96) }; tone_seq(SFX_POWERUP, f, 5, 0.05f, 0.35f, 0); }
    { float f[] = { midi(60), midi(64), midi(67), midi(72) }; tone_seq(SFX_PERK, f, 4, 0.09f, 0.3f, 1); }
    /* round start: drums and a dark chord; round end: a falling one */
    b = buf_new(2.6f, &n); { float ph[4] = { 0 }; static const int ch[4] = { 45, 52, 57, 60 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float v = 0; for (int k = 0; k < 4; k++) { ph[k] += midi(ch[k]) / RATE; ph[k] -= floorf(ph[k]); v += (ph[k] * 2 - 1) * 0.25f; } float drum = 0; for (int d = 0; d < 3; d++) { float td = t - d * 0.35f; if (td > 0) drum += sinf(2 * PI_F * (70 - td * 60) * td) * env(td, 0.002f, 0.12f); } b[i] = v * MIN(1.0f, t / 0.6f) * MAX(0.0f, 1 - t / 2.6f) * 0.7f + drum; } finish(SFX_ROUND_START, b, n, 0.8f); }
    b = buf_new(2.4f, &n); { float ph[3] = { 0 }; static const int ch[3] = { 57, 60, 64 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; float v = 0; for (int k = 0; k < 3; k++) { ph[k] += midi(ch[k]) * (1 - t * 0.08f) / RATE; ph[k] -= floorf(ph[k]); v += sinf(2 * PI_F * ph[k]) * 0.3f; } b[i] = v * MIN(1.0f, t / 0.1f) * MAX(0.0f, 1 - t / 2.4f); } finish(SFX_ROUND_END, b, n, 0.6f); }
    { float f[] = { midi(64), midi(60), midi(57), midi(52), midi(45) }; tone_seq(SFX_GAMEOVER, f, 5, 0.32f, 0.8f, 0); }
    { float f[] = { 1200 }; tone_seq(SFX_PICKUP, f, 1, 0.05f, 0.06f, 0); }
    { float f[] = { 900 }; tone_seq(SFX_MENU_MOVE, f, 1, 0.03f, 0.03f, 0); }
    { float f[] = { midi(84), midi(91) }; tone_seq(SFX_MENU_OK, f, 2, 0.05f, 0.08f, 0); }
    { float f[] = { midi(79), midi(72) }; tone_seq(SFX_MENU_BACK, f, 2, 0.05f, 0.08f, 0); }
    { float f[] = { 2400 }; tone_seq(SFX_BEEP, f, 1, 0.02f, 0.02f, 0); }
    /* Smedjan: hammer on the anvil */
    b = buf_new(1.2f, &n); for (int i = 0; i < n; i++) { float t = (float)i / RATE; float v = 0; for (int k = 0; k < 3; k++) { float tk = t - k * 0.3f; if (tk > 0) v += (sinf(2 * PI_F * 1800 * tk) * 0.6f + sinf(2 * PI_F * 2750 * tk) * 0.4f + noise() * 0.5f * env(tk, 0.0005f, 0.003f)) * env(tk, 0.0005f, 0.12f); } b[i] = v; } finish(SFX_PAP, b, n, 0.7f);
    /* the power: a heavy switch, then a hum coming up */
    b = buf_new(2.0f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (50 + 10 * MIN(1.0f, t)) / RATE; b[i] = noise() * env(t, 0.0005f, 0.03f) + sinf(ph) * MIN(1.0f, t / 0.8f) * MAX(0.0f, 1 - t / 2) * 0.6f + sinf(ph * 2) * 0.2f * MIN(1.0f, t); } finish(SFX_POWER, b, n, 0.8f); }
    b = buf_new(0.9f, &n); { LP l = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = lp(&l, noise(), 1400) * env(t, 0.005f, 0.2f) * (1 + 0.5f * (noise() > 0.8f)); } finish(SFX_DOOR, b, n, 0.8f); }
    b = buf_new(0.2f, &n); { SVF f = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = bandpass(&f, noise(), 1500, 0.5f) * sinf(PI_F * t / 0.2f); } finish(SFX_THROW, b, n, 0.4f); }
    b = buf_new(0.45f, &n); { float ph = 0; for (int i = 0; i < n; i++) { float t = (float)i / RATE; ph += 2 * PI_F * (180 + 60 * sinf(t * 25)) / RATE; b[i] = sinf(ph) * env(t, 0.01f, 0.12f) * (0.6f + 0.4f * sinf(t * 25)); } finish(SFX_GULP, b, n, 0.6f); }
    b = buf_new(0.08f, &n); { LP l = { 0 }; for (int i = 0; i < n; i++) { float t = (float)i / RATE; b[i] = lp(&l, noise(), 400) * env(t, 0.002f, 0.02f); } finish(SFX_STEP, b, n, 0.5f); }
}

/* ---------------------------------------------------------------- music */
/* "Vem kan segla förutan vind?" (traditional), in A minor: pitch, length in eighths */
static const int16_t TUNE[][2] = {
    {69,2},{69,2},{72,2},{71,2},{69,4},{64,4}, {69,2},{69,2},{72,2},{71,2},{69,8},
    {69,2},{71,2},{72,2},{74,2},{76,4},{72,4}, {74,2},{72,2},{71,2},{68,2},{69,8},
    {76,2},{76,2},{77,2},{76,2},{74,4},{72,4}, {74,2},{72,2},{71,2},{68,2},{69,8},
};
/* the box and the game over: notes at times, on a few voices. Music box: a struck tine that rings down (a little
   inharmonic overtone); organ: a soft reed swell that holds for the note's length */
typedef struct { float t, len; int8_t note; } Ev;
static const Ev BOX_SONG[] = {           /* (original) an A minor music box figure, round and round while it spins */
    {0.00f,.5f,81},{0.18f,.5f,88},{0.36f,.5f,84},{0.54f,.5f,88},{0.72f,.5f,83},{0.90f,.5f,88},{1.08f,.5f,80},{1.26f,.5f,88},
    {1.44f,.5f,81},{1.62f,.5f,88},{1.80f,.5f,84},{1.98f,.5f,88},{2.16f,.5f,86},{2.34f,.5f,84},{2.52f,.5f,83},{2.70f,.9f,80},
    {2.88f,.5f,76},{3.06f,.5f,80},{3.24f,1.2f,81},
};
static const Ev BOSS_SONG[] = {          /* (original) a boss is up: D minor, the bass driving in octaves, a falling line */
    {0.0f,.3f,38},{0.0f,1.2f,62},{0.0f,1.2f,69},{0.3f,.3f,50},{0.6f,.3f,38},{0.9f,.3f,50},{1.2f,.3f,38},{1.2f,.6f,65},
    {1.5f,.3f,50},{1.8f,.3f,38},{1.8f,.6f,64},{2.1f,.3f,50},{2.4f,.3f,34},{2.4f,1.2f,62},{2.4f,1.2f,65},{2.7f,.3f,46},
    {3.0f,.3f,34},{3.3f,.3f,46},{3.6f,.3f,36},{3.6f,1.2f,61},{3.6f,1.2f,64},{3.9f,.3f,48},{4.2f,.3f,37},{4.5f,.3f,49},
};
static const Ev OVER_SONG[] = {          /* (original) a slow chorale: Am, F, Dm, E, Am, the tune falling to A */
    {0.0f,1.5f,57},{0.0f,1.5f,60},{0.0f,1.5f,64},{0.0f,1.5f,76},
    {1.5f,1.5f,53},{1.5f,1.5f,57},{1.5f,1.5f,60},{1.5f,1.5f,72},
    {3.0f,1.5f,50},{3.0f,1.5f,53},{3.0f,1.5f,57},{3.0f,1.5f,74},
    {4.5f,1.5f,52},{4.5f,1.5f,56},{4.5f,1.5f,59},{4.5f,1.5f,71},
    {6.0f,3.4f,45},{6.0f,3.4f,57},{6.0f,3.4f,60},{6.0f,3.4f,64},{6.0f,3.4f,69},
};
/* the tomtar's song: "Broder Jakob" (traditional), in A minor as Mahler had it, slow, a canon of three voices */
static const int8_t JAKOB[][2] = {        /* MIDI note, length in eighths */
    {69,2},{71,2},{72,2},{69,2}, {69,2},{71,2},{72,2},{69,2}, {72,2},{74,2},{76,4}, {72,2},{74,2},{76,4},
    {76,1},{77,1},{76,1},{74,1},{72,2},{69,2}, {76,1},{77,1},{76,1},{74,1},{72,2},{69,2}, {69,2},{64,2},{69,4}, {69,2},{64,2},{69,4},
};
static Ev SONG[3 * ARRAY_LEN(JAKOB) * 2]; static int nsong;
static void song_build(void) {
    const float eighth = 0.36f;
    for (int v = 0; v < 3; v++) {                         /* each voice twice through, two bars after the last */
        float t = v * 16 * eighth;
        for (int rep = 0; rep < 2; rep++)
            for (int i = 0; i < (int)ARRAY_LEN(JAKOB); i++) {
                SONG[nsong++] = (Ev){ t, JAKOB[i][1] * eighth * 1.1f, (int8_t)(JAKOB[i][0] - (v == 1 ? 12 : 0)) };
                t += JAKOB[i][1] * eighth;
            }
    }
    for (int i = 1; i < nsong; i++) { Ev e = SONG[i]; int k = i; while (k > 0 && SONG[k - 1].t > e.t) { SONG[k] = SONG[k - 1]; k--; } SONG[k] = e; }
}
typedef struct { float ph, f, env, t, len, vel; int on; } MV;
#define NMV 12
static MV mv[NMV];
static double song_t; static int song_ev;   /* (a double: a float counting seconds in 1/48000ths drifts after half a minute) */
static int tune_over;                        /* a tune that doesn't repeat has rung out: set by the mixer, read by the game */
static void song_tick(int16_t *out, int frames, const Ev *ev, int n, float period, int organ, float gain, int vol) {
    for (int i = 0; i < frames; i++) {
        while (song_ev < n && ev[song_ev].t <= song_t) {    /* notes starting now take a free voice */
            MV *v = &mv[0];
            for (int k = 0; k < NMV; k++) if (!mv[k].on) { v = &mv[k]; break; }
            v->on = 1; v->f = midi(ev[song_ev].note); v->t = 0; v->len = ev[song_ev].len; v->env = organ ? 0 : 1; v->ph = 0;
            v->vel = organ && ev[song_ev].note >= 69 ? 0.9f : 0.6f;   /* (the tune over the chords) */
            song_ev++;
        }
        float s = 0;
        for (int k = 0; k < NMV; k++) {
            MV *v = &mv[k];
            if (!v->on) continue;
            v->t += 1.0f / RATE;
            if (organ) {                                    /* swell in, hold, let go */
                float target = v->t < v->len ? 1.0f : 0.0f;
                v->env += (target - v->env) * (target > v->env ? 0.0006f : 0.00012f);
                if (v->t > v->len && v->env < 0.001f) { v->on = 0; continue; }
                float vib = 1 + 0.003f * sinf(2 * PI_F * 5.2f * v->t);
                v->ph += v->f * vib / RATE; v->ph -= floorf(v->ph);
                float w = 2 * PI_F * v->ph;
                s += (sinf(w) + 0.5f * sinf(2 * w) + 0.25f * sinf(3 * w) + 0.1f * sinf(4 * w)) * v->env * v->vel * 0.09f;
            } else {                                        /* struck: rings down */
                v->env *= 0.99985f;
                if (v->env < 0.002f) { v->on = 0; continue; }
                v->ph += v->f / RATE; v->ph -= floorf(v->ph);
                float w = 2 * PI_F * v->ph;
                s += (sinf(w) + 0.25f * sinf(2 * w) * v->env + 0.1f * sinf(5.4f * w) * v->env * v->env) * v->env * v->vel * 0.12f;
            }
        }
        song_t += 1.0 / RATE;
        if (period > 0 && song_t >= period) { song_t -= period; song_ev = 0; }
        int o = (int)(s * gain * 32767 * vol / 100);
        out[i * 2] = (int16_t)CLAMP(out[i * 2] + o, -32768, 32767);
        out[i * 2 + 1] = (int16_t)CLAMP(out[i * 2 + 1] + o, -32768, 32767);
    }
    if (period <= 0 && n > 0 && song_ev >= n) {
        int ringing = 0; for (int k = 0; k < NMV; k++) ringing |= mv[k].on;
        if (!ringing) __atomic_store_n(&tune_over, 1, __ATOMIC_RELAXED);
    }
}

static int mus, mus_note, mus_left;          /* samples left of the current note */
static float mus_ph, mus_env, mus_freq, mus_bass_ph, mus_bass_f;
static int mus_beat;

static void music_tick(int16_t *out, int frames) {
    int want = __atomic_load_n(&want_music, __ATOMIC_RELAXED), vol = __atomic_load_n(&master, __ATOMIC_RELAXED);
    if (mus != want) {
        mus = want; mus_note = 0; mus_left = 0; mus_env = 0; __atomic_store_n(&tune_over, 0, __ATOMIC_RELAXED);
        song_t = 0; song_ev = 0; for (int k = 0; k < NMV; k++) if (want != MUS_NONE) mv[k].on = 0;   /* (stopping lets notes ring out) */
    }
    if (mus == MUS_BOX) { song_tick(out, frames, BOX_SONG, ARRAY_LEN(BOX_SONG), 3.6f, 0, 2.0f, vol); return; }
    if (mus == MUS_BOSS) { song_tick(out, frames, BOSS_SONG, ARRAY_LEN(BOSS_SONG), 4.8f, 0, 1.5f, vol); return; }
    if (mus == MUS_GAMEOVER) { song_tick(out, frames, OVER_SONG, ARRAY_LEN(OVER_SONG), 0, 1, 1.0f, vol); return; }
    if (mus == MUS_SONG) { song_tick(out, frames, SONG, nsong, 0, 0, 1.4f, vol); return; }
    if (mus == MUS_NONE) { song_tick(out, frames, 0, 0, 0, 0, 0.8f, vol); return; }    /* the last notes ring out */
    if (mus != MUS_TITLE) return;
    const int eighth = RATE * 3 / 10 / 2;      /* slow and sad: 100 bpm in quarters */
    for (int i = 0; i < frames; i++) {
        if (mus_left <= 0) {
            int nn = ARRAY_LEN(TUNE);
            if (mus_note >= nn) mus_note = 0;
            mus_freq = midi(TUNE[mus_note][0]);
            mus_left = TUNE[mus_note][1] * eighth;
            mus_env = 1;
            if (mus_beat++ % 2 == 0) mus_bass_f = midi(TUNE[mus_note][0] - 24);
            mus_note++;
        }
        mus_left--;
        mus_env *= 0.99992f;
        mus_ph += mus_freq / RATE; mus_ph -= floorf(mus_ph);
        mus_bass_ph += mus_bass_f / RATE; mus_bass_ph -= floorf(mus_bass_ph);
        float w = 2 * PI_F * mus_ph;
        float v = (sinf(w) + 0.35f * sinf(w * 2) * mus_env + 0.15f * sinf(w * 3) * mus_env * mus_env) * mus_env * 0.16f;
        v += sinf(2 * PI_F * mus_bass_ph) * 0.07f;
        int s = (int)(v * 32767 * vol / 100);
        out[i * 2] = (int16_t)CLAMP(out[i * 2] + s, -32768, 32767);
        out[i * 2 + 1] = (int16_t)CLAMP(out[i * 2 + 1] + s, -32768, 32767);
    }
}

/* ---------------------------------------------------------------- the night outside (made as it plays) */
static float amb_gain;                   /* fades between kinds */
static int amb_kind;
static uint32_t astate = 777;
static float anoise(void) { astate = astate * 1664525u + 1013904223u; return ((astate >> 9) & 0xFFFF) / 32768.0f - 1.0f; }
static float a_lp[4], a_drop[2], a_bird_t = 3, a_bird_f, a_bird_len, a_bird_ph, a_bird_age;
static float a_gust[2], a_chirp[3] = { 0, 0.21f, 0.42f }, a_tone[3];   /* phases kept small: a float counting the
                                                                          seconds in 1/48000ths stops after minutes */
static int a_bird_n;
static void ambience_tick(int16_t *out, int frames, int vol) {
    int want = __atomic_load_n(&want_amb, __ATOMIC_RELAXED);
    for (int i = 0; i < frames; i++) {
        /* fade out, change, fade in */
        if (want != amb_kind) { amb_gain -= 1.0f / (RATE * 1.2f); if (amb_gain <= 0) { amb_gain = 0; amb_kind = want; } }
        else if (amb_gain < 1) amb_gain = MIN(1.0f, amb_gain + 1.0f / (RATE * 2.5f));
        if (amb_kind == AMB_NONE || amb_gain <= 0) continue;
        float l = 0, r = 0;
        if (amb_kind == AMB_RAIN) {                         /* a wash of noise between 600 Hz and 5 kHz, and drops */
            float nl = anoise(), nr = anoise();
            a_lp[0] += 0.48f * (nl - a_lp[0]); a_lp[1] += 0.075f * (nl - a_lp[1]);
            a_lp[2] += 0.48f * (nr - a_lp[2]); a_lp[3] += 0.075f * (nr - a_lp[3]);
            l = (a_lp[0] - a_lp[1]) * 0.06f; r = (a_lp[2] - a_lp[3]) * 0.06f;
            for (int c = 0; c < 2; c++) {
                if ((astate & 1023) < 5) a_drop[c] += 0.4f + 0.6f * ((astate >> 12) & 255) / 255.0f;
                a_drop[c] *= 0.993f;
            }
            l += (a_lp[0] - a_lp[1]) * a_drop[0] * 0.12f; r += (a_lp[2] - a_lp[3]) * a_drop[1] * 0.12f;
        } else if (amb_kind == AMB_WIND) {                  /* gusts: low noise with a cutoff that breathes */
            a_gust[0] += 0.071f / RATE; a_gust[0] -= floorf(a_gust[0]);
            a_gust[1] += 0.19f / RATE; a_gust[1] -= floorf(a_gust[1]);
            float g = 0.55f + 0.3f * sinf(2 * PI_F * a_gust[0]) + 0.15f * sinf(2 * PI_F * a_gust[1] + 1.3f);
            float k = (180 + 520 * g) / RATE * 2 * PI_F;
            a_lp[0] += k * (anoise() - a_lp[0]); a_lp[1] += k * (a_lp[0] - a_lp[1]);
            a_lp[2] += k * (anoise() - a_lp[2]); a_lp[3] += k * (a_lp[2] - a_lp[3]);
            l = a_lp[1] * g * 0.55f; r = a_lp[3] * g * 0.55f;
        } else if (amb_kind == AMB_SUMMER) {                /* crickets, a bird now and then, a breath of air */
            for (int c = 0; c < 3; c++) {
                static const float per[3] = { 0.61f, 0.73f, 0.89f }, fr[3] = { 4400, 4750, 4150 }, pan[3] = { -0.7f, 0.5f, 0.1f };
                a_chirp[c] += 1.0f / RATE; if (a_chirp[c] >= per[c]) a_chirp[c] -= per[c];
                a_tone[c] += fr[c] / RATE; a_tone[c] -= floorf(a_tone[c]);
                float ph = a_chirp[c];
                float on = ph < 0.09f && fmodf(ph, 0.03f) < 0.016f ? 1.0f : 0.0f;
                float v = on * sinf(2 * PI_F * a_tone[c]) * 0.03f;
                l += v * (1 - pan[c]) * 0.5f; r += v * (1 + pan[c]) * 0.5f;
            }
            a_bird_t -= 1.0f / RATE;
            if (a_bird_t <= 0 && a_bird_n <= 0) { a_bird_n = 3 + (int)((astate >> 8) % 4); a_bird_age = 0; a_bird_len = 0; }
            if (a_bird_n > 0) {                             /* a phrase of falling whistles */
                if (a_bird_age >= a_bird_len) {
                    a_bird_n--; a_bird_age = 0; a_bird_len = 0.07f + ((astate >> 10) & 63) / 600.0f;
                    a_bird_f = 2600 + ((astate >> 4) % 1600);
                    if (a_bird_n <= 0) a_bird_t = 3.5f + ((astate >> 6) % 600) / 100.0f;
                }
                a_bird_age += 1.0f / RATE;
                float k = a_bird_age / a_bird_len, f = a_bird_f * (1 - 0.25f * k) * (1 + 0.02f * sinf(2 * PI_F * 38 * a_bird_age));
                a_bird_ph += f / RATE; a_bird_ph -= floorf(a_bird_ph);
                float v = sinf(2 * PI_F * a_bird_ph) * sinf(PI_F * MIN(1.0f, k)) * 0.07f;
                l += v * 0.35f; r += v * 0.65f;
            }
            a_lp[0] += 0.05f * (anoise() - a_lp[0]);
            l += a_lp[0] * 0.05f; r += a_lp[0] * 0.05f;
        }
        int sl = (int)(l * amb_gain * 32767 * vol / 100), sr = (int)(r * amb_gain * 32767 * vol / 100);
        out[i * 2] = (int16_t)CLAMP(out[i * 2] + sl, -32768, 32767);
        out[i * 2 + 1] = (int16_t)CLAMP(out[i * 2 + 1] + sr, -32768, 32767);
    }
}

/* ---------------------------------------------------------------- the mixer (audio thread) */
static void mix(int16_t *out, int frames) {
    memset(out, 0, (size_t)frames * 4);
    unsigned head = __atomic_load_n(&rhead, __ATOMIC_ACQUIRE);  /* (the commands up to here are written) */
    while (rtail != head) {
        Cmd c = ring[rtail & 255];
        __atomic_store_n(&rtail, rtail + 1, __ATOMIC_RELEASE);
        if (c.id < 0 || c.id >= SFX_COUNT || !snd[c.id].pcm) continue;
        Voice *v = 0;
        for (int i = 0; i < NVOICES; i++) if (!voices[i].active) { v = &voices[i]; break; }
        if (!v) {                                             /* steal the quietest */
            v = &voices[0];
            for (int i = 1; i < NVOICES; i++) if (voices[i].vol < v->vol) v = &voices[i];
        }
        v->s = &snd[c.id]; v->pos = 0; v->rate = c.rate; v->vol = c.vol; v->pan = c.pan; v->active = 1;
    }
    float m = __atomic_load_n(&master, __ATOMIC_RELAXED) / 100.0f;
    for (int k = 0; k < NVOICES; k++) {
        Voice *v = &voices[k];
        if (!v->active) continue;
        float lv = v->vol * m * (v->pan > 0 ? 1 - v->pan : 1), rv = v->vol * m * (v->pan < 0 ? 1 + v->pan : 1);
        for (int i = 0; i < frames; i++) {
            int p = (int)v->pos;
            if (p >= v->s->n - 1) { v->active = 0; break; }
            float f = v->pos - p, s = v->s->pcm[p] * (1 - f) + v->s->pcm[p + 1] * f;
            int l = out[i * 2] + (int)(s * lv), r = out[i * 2 + 1] + (int)(s * rv);
            out[i * 2] = (int16_t)CLAMP(l, -32768, 32767); out[i * 2 + 1] = (int16_t)CLAMP(r, -32768, 32767);
            v->pos += v->rate;
        }
    }
    music_tick(out, frames);
    ambience_tick(out, frames, __atomic_load_n(&master, __ATOMIC_RELAXED));
}

void audio_init(void) {
    synth_all(); song_build();
    ready = plat_audio_start(RATE, mix) == 0;
    plat_log("audio: %s", ready ? "on" : "off");
}
/* tests/sounds.c: the mixer without a sound card, run by the caller */
void audio_offline(void) { synth_all(); song_build(); ready = 1; }
void audio_render(int16_t *out, int frames) { mix(out, frames); }
void audio_set_volume(int v) { __atomic_store_n(&master, CLAMP(v, 0, 100), __ATOMIC_RELAXED); }

int sfx_asked[SFX_COUNT];
void sfx(int id, float vol, float pan) {
    if (id >= 0 && id < SFX_COUNT) sfx_asked[id]++;
    if (!ready || master == 0) return;
    unsigned h = rhead;
    if (h - __atomic_load_n(&rtail, __ATOMIC_ACQUIRE) >= 255) return;   /* full (the mixer is behind) */
    Cmd *c = &ring[h & 255];
    c->id = id; c->vol = vol; c->pan = clampf(pan, -1, 1);
    c->rate = (id == SFX_HIT || id == SFX_SPLAT || (id >= SFX_GROAN1 && id <= SFX_ZATTACK) || id == SFX_STEP) ? 0.9f + (float)(rand() % 200) / 1000.0f : 1.0f;
    __atomic_store_n(&rhead, h + 1, __ATOMIC_RELEASE);
}
/* a sound in the world: quieter with distance, panned by where it is on screen */
void sfx_at(int id, float x, float y, float vol) {
    float dx = x - G->p.x, dy = y - G->p.y, d = sqrtf(dx * dx + dy * dy);
    float k = 1.0f - d / 360.0f;
    if (k <= 0.02f) return;
    sfx(id, vol * k, clampf(dx / 200.0f, -0.8f, 0.8f));
}
void audio_ambience(int kind) { __atomic_store_n(&want_amb, kind, __ATOMIC_RELAXED); }
/* what is playing: MUS_NONE also once a tune that doesn't repeat (a found song, the game-over chorale) has rung out */
int music_now(void) { return __atomic_load_n(&tune_over, __ATOMIC_RELAXED) ? MUS_NONE : __atomic_load_n(&want_music, __ATOMIC_RELAXED); }
void music_play(int track) { __atomic_store_n(&tune_over, 0, __ATOMIC_RELAXED); __atomic_store_n(&want_music, S.music ? track : MUS_NONE, __ATOMIC_RELAXED); }

/* tests: render the mixer's output without a device */
void audio_render_test(int16_t *out, int frames);
void audio_render_test(int16_t *out, int frames) { if (!snd[0].pcm) synth_all(); ready = 1; mix(out, frames); }
