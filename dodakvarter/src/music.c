// music.c: the game's songs (0.3), MP3s in music/ built into the program (so an update that brings a new program
// brings its songs too): the menu's, one under the play, the bosses', and game over's. The mixer (audio.c) asks for
// a block of samples; each song has a player that decodes as it goes (minimp3, src/third_party) and a volume that
// glides to where it should be, so songs cross-fade, and the one under the play picks up where it left off after a
// boss, the box's tune or a found song. Game over's plays once; the rest go round. Without them (MUS_* the songs
// don't cover: the box, the gnomes' song), the synthesized tunes in audio.c play as before.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "third_party/minimp3.h"
#include "game.h"
#include <string.h>

#ifndef DK_MUSIC
#define DK_MUSIC "music"
#endif
#define SONG(sym, file) \
    __asm__(".section .rodata\n.balign 16\n.global " #sym "_mp3\n" #sym "_mp3:\n.incbin \"" DK_MUSIC "/" file "\"\n" \
            ".global " #sym "_mp3_end\n" #sym "_mp3_end:\n.byte 0\n.previous\n"); \
    extern const unsigned char sym##_mp3[], sym##_mp3_end[];
SONG(menu, "menu.mp3")
SONG(gameplay, "gameplay.mp3")
SONG(boss, "boss.mp3")
SONG(gameover, "gameover.mp3")

enum { TR_MENU, TR_PLAY, TR_BOSS, TR_OVER, TR_COUNT };
typedef struct {
    const unsigned char *data; size_t size, pos;   /* the file, and how far into it the decoder is */
    mp3dec_t dec;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]; int n, at, ch, hz;   /* a decoded frame, samples (per channel) used */
    double frac;                                   /* (resampling when a song isn't 48 kHz) */
    float gain;                                    /* glides toward 1 (chosen) or 0 */
    int loop, done, rewind;                        /* goes round; played to the end (once); start over when chosen */
} Song;
static Song pl[TR_COUNT];
static int ready;

void music_mp3_init(void) {
    const unsigned char *d[TR_COUNT] = { menu_mp3, gameplay_mp3, boss_mp3, gameover_mp3 };
    const unsigned char *e[TR_COUNT] = { menu_mp3_end, gameplay_mp3_end, boss_mp3_end, gameover_mp3_end };
    for (int i = 0; i < TR_COUNT; i++) {
        memset(&pl[i], 0, sizeof pl[i]);
        pl[i].data = d[i]; pl[i].size = (size_t)(e[i] - d[i]);
        pl[i].loop = i != TR_OVER; pl[i].rewind = 1;
        mp3dec_init(&pl[i].dec);
    }
    ready = 1;
}

static int next_frame(Song *p) {                 /* decode the next frame; 0 at the end of the file */
    for (int tries = 0; tries < 8; tries++) {
        if (p->pos >= p->size) return 0;
        mp3dec_frame_info_t info;
        int n = mp3dec_decode_frame(&p->dec, p->data + p->pos, (int)MIN(p->size - p->pos, (size_t)16384), p->pcm, &info);
        if (info.frame_bytes <= 0) { p->pos = p->size; return 0; }
        p->pos += (size_t)info.frame_bytes;
        if (n > 0) { p->n = n; p->at = 0; p->ch = info.channels; p->hz = info.hz; return 1; }
    }
    return 0;
}
static void restart(Song *p) { p->pos = 0; p->n = p->at = 0; p->frac = 0; p->done = 0; mp3dec_init(&p->dec); }

/* one stereo sample of the song, or 0 when it has ended */
static int sample(Song *p, int *l, int *r) {
    while (p->at >= p->n) {
        if (!next_frame(p)) {
            if (!p->loop) { p->done = 1; return 0; }
            restart(p);
            if (!next_frame(p)) { p->done = 1; return 0; }
        }
    }
    int i = p->at;
    *l = p->pcm[i * p->ch]; *r = p->pcm[i * p->ch + (p->ch > 1)];
    p->frac += (double)p->hz / 48000.0;
    while (p->frac >= 1.0) { p->frac -= 1.0; p->at++; }
    return 1;
}

/* the mixer's music for a block: which song (from what the game asks for, and whether a run is on); 1 when a song
   covers what's asked (the synthesized tune stays quiet), and *over when game over's song has played through */
int music_mp3_mix(int16_t *out, int frames, int want, int in_run, int vol, int *over) {
    if (!ready) return 0;
    int pick = want == MUS_TITLE ? TR_MENU : want == MUS_BOSS ? TR_BOSS : want == MUS_GAMEOVER ? TR_OVER
             : want == MUS_NONE && in_run ? TR_PLAY : -1;
    if (pick >= 0 && pl[pick].rewind && pl[pick].gain <= 0.001f) {    /* chosen from silence: from the start */
        restart(&pl[pick]);
        pl[pick].rewind = 0;                        /* (again only once it's been let go; the play's song never) */
    }
    float step = 1.0f / (0.6f * 48000);             /* fades of 0.6 s */
    float k = 0.55f * vol / 100.0f;                 /* (under the sounds) */
    for (int t = 0; t < TR_COUNT; t++) {
        Song *p = &pl[t];
        float target = t == pick && !p->done ? 1.0f : 0.0f;
        if (p->gain <= 0 && target <= 0) { if (t != TR_PLAY && t != pick) p->rewind = 1; continue; }
        for (int i = 0; i < frames; i++) {
            if (p->gain < target) p->gain = MIN(target, p->gain + step);
            else if (p->gain > target) p->gain = MAX(target, p->gain - step);
            int l, r;
            if (!sample(p, &l, &r)) { p->gain = 0; break; }
            float g = p->gain * p->gain * k;
            out[i * 2] = (int16_t)CLAMP(out[i * 2] + (int)(l * g), -32768, 32767);
            out[i * 2 + 1] = (int16_t)CLAMP(out[i * 2 + 1] + (int)(r * g), -32768, 32767);
        }
    }
    if (pick == TR_OVER && pl[TR_OVER].done) *over = 1;
    return pick >= 0;
}
