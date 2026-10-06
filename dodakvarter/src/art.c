// art.c: text sprites to images, with palette swaps and a cache. Entries are allocated one by one: callers keep
// the pointers for the whole run.
#include "art.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { char name[32]; uint32_t col[128]; uint8_t has[128]; } Pal;
typedef struct { const ArtSprite *s; const Pal *p; Img im; } Cached;

static Pal **pals; static int npals, cap_pals;
static Cached **cache; static int ncache, cap_cache;

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }

static void parse_into(Pal *p, const char *enc) {
    for (const char *q = enc; q[0] && q[1]; q += 7) {
        unsigned char ch = (unsigned char)q[0];
        uint32_t v = 0;
        for (int i = 1; i <= 6 && q[i]; i++) v = v << 4 | (uint32_t)hexv(q[i]);
        if (ch < 128) { p->col[ch] = v; p->has[ch] = 1; }
    }
}
static Pal *new_pal(void) {
    if (npals == cap_pals) { cap_pals = cap_pals ? cap_pals * 2 : 64; pals = realloc(pals, (size_t)cap_pals * sizeof *pals); }
    return pals[npals++] = calloc(1, sizeof(Pal));
}
static Pal *find_pal(const char *name) {
    if (!pals)
        for (int i = 0; i < ART_NPALETTES; i++) {
            Pal *p = new_pal();
            snprintf(p->name, sizeof p->name, "%s", ART_PALETTES[i].name);
            parse_into(p, ART_PALETTES[i].colors);
        }
    for (int i = 0; i < npals; i++) if (!strcmp(pals[i]->name, name)) return pals[i];
    return 0;
}
/* overrides: "c=rrggbb" pairs as in art_data's encoding, i.e. "crrggbbdrrggbb..." */
const char *art_palette_add(const char *name, const char *base, const char *overrides) {
    Pal *e = find_pal(name);
    if (e) return e->name;
    Pal *b = find_pal(base), *p = new_pal();
    if (b) *p = *b;
    snprintf(p->name, sizeof p->name, "%s", name);
    if (overrides) parse_into(p, overrides);
    return p->name;
}
uint32_t art_color(const char *palette, char ch) {
    Pal *p = find_pal(palette);
    return p && (unsigned char)ch < 128 ? p->col[(unsigned char)ch] : 0xFF00FF;
}

static const ArtSprite *find_sprite(const char *name) {
    for (int i = 0; i < ART_NSPRITES; i++) if (!strcmp(ART_SPRITES[i].name, name)) return &ART_SPRITES[i];
    return 0;
}
int art_exists(const char *name) { return find_sprite(name) != 0; }

const Img *art_pal(const char *name, const char *palette) {
    const ArtSprite *s = find_sprite(name);
    if (!s) { fprintf(stderr, "art: no sprite %s\n", name); return 0; }
    const Pal *own = find_pal(s->palette), *p = palette ? find_pal(palette) : own;
    if (!p) p = own;
    for (int i = 0; i < ncache; i++) if (cache[i]->s == s && cache[i]->p == p) return &cache[i]->im;
    if (ncache == cap_cache) { cap_cache = cap_cache ? cap_cache * 2 : 256; cache = realloc(cache, (size_t)cap_cache * sizeof *cache); }
    Cached *c = cache[ncache++] = calloc(1, sizeof(Cached));
    c->s = s; c->p = p;
    img_alloc(&c->im, s->w, s->h);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            unsigned char ch = (unsigned char)s->rows[y][x];
            if (ch == '.' || ch >= 128) continue;
            uint32_t v = p->has[ch] ? p->col[ch] : own && own->has[ch] ? own->col[ch] : 0xFF00FF;
            c->im.px[y * s->w + x] = 0xFF000000u | v;
        }
    return &c->im;
}
const Img *art(const char *name) { return art_pal(name, 0); }
