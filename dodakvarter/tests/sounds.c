// sounds: every sound effect, the music and the ambience, rendered by the game's own mixer into WAV files, to listen
// to on a computer (and for tests/run.sh to check that nothing is silent or clipped).
//   sounds <dir>      writes <dir>/sfx-*.wav, music-*.wav, ambience-*.wav and prints their levels
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define RATE 48000
static int16_t *buf; static int cap;

static void wav(const char *path, const int16_t *pcm, int frames) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); exit(1); }
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, fmt = 16, rate = RATE, bps = RATE * 4;
    uint16_t pcm_fmt = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm_fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 4, (size_t)frames, f);
    fclose(f);
}
static int fails;
/* renders `secs` of the mixer into a file; prints the level, and checks it's neither silent nor clipped */
static void take(const char *dir, const char *name, float secs, float skip) {
    int frames = (int)(secs * RATE), sk = (int)(skip * RATE);
    if (frames + sk > cap) { cap = frames + sk; buf = realloc(buf, (size_t)cap * 4); }
    for (int i = 0; i < sk + frames; i += 512) audio_render(buf + (size_t)i * 2, MIN(512, sk + frames - i));
    double sum = 0; int peak = 0, clip = 0;
    for (int i = sk * 2; i < (sk + frames) * 2; i++) { int v = buf[i]; sum += (double)v * v; peak = MAX(peak, abs(v)); clip += abs(v) >= 32767; }
    double rms = sqrt(sum / (frames * 2.0));
    char p[512]; snprintf(p, sizeof p, "%s/%s.wav", dir, name);
    wav(p, buf + (size_t)sk * 2, frames);
    printf("%-22s %5.1f s  rms %6.0f  peak %5d%s\n", name, secs, rms, peak, clip > 8 ? "  CLIPPED" : rms < 30 ? "  SILENT" : "");
    if (clip > 8 || rms < 30) fails++;
}
static void quiet(void) { for (int i = 0; i < 40; i++) audio_render(buf, 512); }   /* let everything ring out */

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build/sounds";
    mkdir(dir, 0755);
    static Game g; G = &g;
    S.music = 1;
    audio_offline();
    cap = RATE * 30; buf = malloc((size_t)cap * 4);
    static const char *names[SFX_COUNT] = { "pistol", "smg", "rifle", "shotgun", "sniper", "lmg", "rocket", "ray", "zap", "frost",
        "explode", "reload", "empty", "knife", "hit", "groan1", "groan2", "groan3", "zattack", "hurt", "wolf", "moose",
        "board-break", "board-fix", "buy", "deny", "box", "horse", "powerup-spawn", "powerup", "round-start", "round-end",
        "gameover", "swap", "pickup", "menu-move", "menu-ok", "menu-back", "perk", "pap", "power", "door", "splat", "throw",
        "beep", "gulp", "step", "kaboom", "thunder", "boss-roar", "boss-horn", "boss-swoosh", "boss-slam", "boss-fiddle",
        "boss-splash", "boss-hiss", "jingle-julmust", "jingle-snabbkaffe", "jingle-salmiak",
        "jingle-kanelbulle", "jingle-blabarssoppa", "jingle-lingondricka", "jingle-kaviar" };
    for (int i = 0; i < SFX_COUNT; i++) {
        char n[48]; snprintf(n, sizeof n, "sfx-%s", names[i] ? names[i] : "?");
        quiet(); sfx(i, 1, 0); take(dir, n, i == SFX_ROUND_START || i == SFX_POWER || i == SFX_ROUND_END ? 2.8f : i == SFX_THUNDER || i >= SFX_JINGLE ? 3.6f : i >= SFX_ROAR ? 2.0f : 1.4f, 0);
    }
    quiet(); music_play(MUS_TITLE); take(dir, "music-title", 24, 0);
    music_play(MUS_NONE); quiet(); music_play(MUS_BOX); take(dir, "music-box", 3.6f, 0);
    music_play(MUS_NONE); quiet(); music_play(MUS_GAMEOVER); take(dir, "music-gameover", 10, 0);
    music_play(MUS_NONE); quiet(); music_play(MUS_SONG); take(dir, "music-tomtar", 30, 0);
    music_play(MUS_NONE); quiet(); music_play(MUS_BOSS); take(dir, "music-boss", 9.6f, 0);
    music_play(MUS_NONE); quiet();
    static const char *amb[] = { 0, "ambience-autumn-rain", "ambience-winter-wind", "ambience-midsummer-night" };
    for (int k = AMB_RAIN; k <= AMB_SUMMER; k++) { audio_ambience(k); take(dir, amb[k], 12, 4); audio_ambience(AMB_NONE); take(dir, "ambience-fade", 1.5f, 0); }
    /* a long night: after ten minutes the crickets chirp as high and the wind gusts as much as at first (phases, not
       a float counting the seconds, which stops) */
    for (int k = AMB_WIND; k <= AMB_SUMMER; k += AMB_SUMMER - AMB_WIND) {
        audio_ambience(k);
        double hf[2] = { 0 }, lv[2] = { 0 }, lv2[2] = { 0 };   /* high frequencies; the level of each half second */
        for (int t = 0; t < 1320; t++) {                    /* half seconds */
            audio_render(buf, RATE / 2);
            int at = t >= 20 && t < 80 ? 0 : t >= 1260 ? 1 : -1;
            if (at < 0) continue;
            double e = 0;
            for (int i = 1; i < RATE / 2; i++) { double d = buf[i * 2] - buf[i * 2 - 2]; hf[at] += d * d; e += (double)buf[i * 2] * buf[i * 2]; }
            double r = sqrt(e / (RATE / 2)); lv[at] += r; lv2[at] += r * r;
        }
        double gust[2];                                     /* how much the level moves: deviation over mean */
        for (int a = 0; a < 2; a++) { double m = lv[a] / 60; gust[a] = sqrt(fmax(0, lv2[a] / 60 - m * m)) / (m + 1); }
        double ratio = hf[1] / (hf[0] + 1);
        printf("%-22s after ten minutes: highs %.2f of the first, gusts %.2f against %.2f\n", k == AMB_WIND ? "a long windy night" : "a long summer night", ratio, gust[1], gust[0]);
        if (ratio < 0.5 || ratio > 2 || (k == AMB_WIND && gust[1] < gust[0] * 0.5)) { printf("  the ambience changed over ten minutes\n"); fails++; }
        audio_ambience(AMB_NONE); quiet(); quiet(); quiet();
    }
    printf("sounds: %d silent or clipped\n", fails);
    return fails ? 1 : 0;
}
