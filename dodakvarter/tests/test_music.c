// test_music: the songs (music.c) decode and play where they should: the title's, the play's under a run, the boss's,
// and game over's, which ends (music_now() goes to MUS_NONE); the play's picks up where it was after the boss's.
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
void audio_offline(void); void audio_render(int16_t *out, int frames);
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double rms(float secs) {
    int n = (int)(secs * 48000); int16_t *b = malloc((size_t)n * 4); double ss = 0;
    for (int i = 0; i < n; i += 480) audio_render(b + i * 2, n - i < 480 ? n - i : 480);
    for (int i = 0; i < n * 2; i++) ss += (double)b[i] * b[i];
    free(b); return sqrt(ss / (n * 2));
}
int main(void) {
    G = calloc(1, sizeof *G); S.music = 1;
    audio_offline(); audio_set_volume(80);
    music_play(MUS_TITLE); double t = rms(3);
    audio_in_run(1); music_play(MUS_NONE); rms(1); double p = rms(3);
    music_play(MUS_BOSS); rms(1); double b = rms(3);
    audio_in_run(0); music_play(MUS_NONE); rms(1.5); double quiet = rms(2);
    music_play(MUS_GAMEOVER); double o = rms(3);
    CHECK(t > 300 && p > 300 && b > 300 && o > 300, "a song is silent: title %.0f, play %.0f, boss %.0f, game over %.0f", t, p, b, o);
    CHECK(quiet < 200, "the play's song went on outside a run (%.0f)", quiet);
    int left = 60 * 5;                                   /* game over's plays once: about three minutes */
    while (music_now() == MUS_GAMEOVER && left-- > 0) rms(1);
    CHECK(music_now() == MUS_NONE, "game over's song didn't end");
    double after = rms(2);
    CHECK(after < 200, "game over's song started again (%.0f)", after);
    printf("music: %d failures (title %.0f, play %.0f, boss %.0f, game over %.0f)\n", fails, t, p, b, o);
    return fails ? 1 : 0;
}
