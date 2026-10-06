// mapview: generate a town and write it to a PNG (the whole map with its standing props and machines).
//   mapview <seed> <season 0-2> <out.png> [power]
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>




static int cmp_y(const void *a, const void *b) { return ((const Prop *)a)->y - ((const Prop *)b)->y; }

int main(int argc, char **argv) {
    uint64_t seed = argc > 1 ? strtoull(argv[1], 0, 10) : 1;
    int season = argc > 2 ? atoi(argv[2]) : 0;
    const char *out = argc > 3 ? argv[3] : "map.png";
    G = calloc(1, sizeof *G);
    G->season = season; G->seed = seed;
    rng_seed(&G->fx, seed, 3);
    map_generate(seed, season);
    G->power_on = argc > 4;
    world_paint();
    Surf s; s.w = G->ww; s.h = G->wh; s.pitch = G->ww; s.px = G->world; surf_noclip(&s);
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type == IT_WALLBUY || it->type == IT_WINDOW || it->type == IT_BARRIER) inter_draw(&s, it, it->tx * TS, it->ty * TS);
    }
    Prop *sorted = malloc(sizeof(Prop) * (size_t)G->nprops); memcpy(sorted, G->props, sizeof(Prop) * (size_t)G->nprops);
    qsort(sorted, (size_t)G->nprops, sizeof(Prop), cmp_y);
    for (int i = 0; i < G->nprops; i++) if (prop_is_tall(sorted[i].kind)) prop_draw(&s, &sorted[i], sorted[i].x, sorted[i].y);
    for (int i = 0; i < G->nit; i++) {
        Inter *it = &G->it[i];
        if (it->type == IT_PERK || it->type == IT_PAP || it->type == IT_POWER || it->type == IT_BOX) inter_draw(&s, it, it->tx * TS, it->ty * TS);
    }
    for (int i = 0; i < G->nspawns; i++) rect_line(&s, (int)G->spawns[i].x - 3, (int)G->spawns[i].y - 3, 7, 7, 0xff00ff);
    for (int z = 0; z < G->nzones; z++) {
        Zone *zn = &G->zones[z];
        text_ol(&s, FONT_NORMAL, zn->x * TS + 20, zn->y * TS + 20, z == G->start_zone ? 0x40ff40 : 0xffffff, 0x000000, zn->name);
        char b[32]; snprintf(b, sizeof b, "d%d", zn->dist);
        text_ol(&s, FONT_NORMAL, zn->x * TS + 20, zn->y * TS + 32, 0xffff80, 0x000000, b);
    }
    png_write(out, G->world, G->ww, G->wh, G->ww);
    printf("%s: %dx%d tiles, %d zones, %d buildings, %d props, %d interactables, %d spawns, town %s\n", out, G->w, G->h,
           G->nzones, G->nb, G->nprops, G->nit, G->nspawns, G->town);
    return 0;
}
