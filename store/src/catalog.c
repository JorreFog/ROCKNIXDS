// catalog.c: what the package manager knows, read with `rocknixds-store list` (local, quick: the catalog and the newest
// versions as the last refresh found them) and the pictures that refresh converted for this app (RNDSIMG1 files:
// width and height, then ARGB words, all little endian; the app has no PNG decoder).
#define _GNU_SOURCE
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a field of a tab separated line, its escapes (\n, \\) undone */
static char *field(char **p) {
    char *s = *p, *e = strchr(s, '\t');
    if (e) { *e = 0; *p = e + 1; } else *p = s + strlen(s);
    char *o = s;
    for (char *i = s; *i; i++) {
        if (*i == '\\' && i[1] == 'n') { *o++ = '\n'; i++; }
        else if (*i == '\\' && i[1] == '\\') { *o++ = '\\'; i++; }
        else *o++ = *i;
    }
    *o = 0;
    return s;
}

static void cpy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

static int load_raw(Img *im, const char *path) {
    memset(im, 0, sizeof *im);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RNDSIMG1", 8)) { fclose(f); return -1; }
    int w = h[8] | h[9] << 8, ht = h[10] | h[11] << 8;
    if (w <= 0 || ht <= 0 || w > 1024 || ht > 1024) { fclose(f); return -1; }
    img_alloc(im, w, ht);
    unsigned char *b = malloc((size_t)w * ht * 4);
    size_t got = b ? fread(b, 4, (size_t)w * ht, f) : 0;
    fclose(f);
    if (got != (size_t)w * ht) { free(b); free(im->px); memset(im, 0, sizeof *im); return -1; }
    for (size_t i = 0; i < got; i++)
        im->px[i] = (uint32_t)b[i * 4] | (uint32_t)b[i * 4 + 1] << 8 | (uint32_t)b[i * 4 + 2] << 16 | (uint32_t)b[i * 4 + 3] << 24;
    free(b);
    return 0;
}

static const char *media_dir(void) {
    static char d[512];
    const char *e = getenv("RNDS_STORE_DATA");
    snprintf(d, sizeof d, "%s/media", e && *e ? e : plat_data_dir());
    return d;
}

static int state_of(const char *s) {
    if (!strcmp(s, "install")) return ST_INSTALL;
    if (!strcmp(s, "update")) return ST_UPDATE;
    if (!strcmp(s, "installed")) return ST_INSTALLED;
    if (!strcmp(s, "unsupported")) return ST_UNSUPPORTED;
    if (!strncmp(s, "needs-", 6)) return ST_NEEDS;
    return ST_UNAVAILABLE;
}

void catalog_free(Catalog *c) {
    for (int i = 0; i < c->n; i++) { free(c->apps[i].icon.px); free(c->apps[i].shot.px); }
    memset(c, 0, sizeof *c);
}

void catalog_load(Catalog *c) {
    catalog_free(c);
    char cmd[700]; snprintf(cmd, sizeof cmd, "'%s' list 2>/dev/null", store_cli());
    FILE *f = popen(cmd, "r");
    if (!f) return;
    static char l[4096];
    while (fgets(l, sizeof l, f)) {
        size_t n = strlen(l); while (n && (l[n - 1] == '\n' || l[n - 1] == '\r')) l[--n] = 0;
        char *p = l, *tag = field(&p);
        if (!strcmp(tag, "CATALOG")) { field(&p); c->refreshed = atol(field(&p)); }
        else if (!strcmp(tag, "APP") && c->n < MAX_APPS) {
            App *a = &c->apps[c->n++];
            cpy(a->id, sizeof a->id, field(&p)); cpy(a->kind, sizeof a->kind, field(&p));
            cpy(a->name, sizeof a->name, field(&p)); cpy(a->dev, sizeof a->dev, field(&p));
            cpy(a->have, sizeof a->have, field(&p)); cpy(a->latest, sizeof a->latest, field(&p));
            cpy(a->state_s, sizeof a->state_s, field(&p)); a->size = atol(field(&p));
            cpy(a->summary, sizeof a->summary, field(&p));
            const char *acc = field(&p); a->accent = acc[0] == '#' ? (uint32_t)strtoul(acc + 1, 0, 16) : 0x2eae62;
            cpy(a->genre, sizeof a->genre, field(&p)); cpy(a->players, sizeof a->players, field(&p));
            a->state = state_of(a->state_s);
            char path[700];
            snprintf(path, sizeof path, "%s/%s/icon.raw", media_dir(), a->id); load_raw(&a->icon, path);
            snprintf(path, sizeof path, "%s/%s/shot.raw", media_dir(), a->id); load_raw(&a->shot, path);
        } else if (!strcmp(tag, "DESC")) {
            const char *id = field(&p);
            for (int i = 0; i < c->n; i++) if (!strcmp(c->apps[i].id, id)) cpy(c->apps[i].desc, sizeof c->apps[i].desc, field(&p));
        } else if (!strcmp(tag, "NOTES")) {
            const char *id = field(&p);
            for (int i = 0; i < c->n; i++) if (!strcmp(c->apps[i].id, id)) cpy(c->apps[i].notes, sizeof c->apps[i].notes, field(&p));
        }
    }
    pclose(f);
}

int app_tab_has(const App *a, int tab) {
    if (tab == TAB_UPDATES) return a->state == ST_UPDATE;
    if (tab == TAB_INSTALLED) return a->have[0] != 0;
    if (tab == TAB_GAMES) return !strcmp(a->kind, "game");
    return strcmp(a->kind, "game") != 0;
}

int updates_count(const Catalog *c) {
    int n = 0;
    for (int i = 0; i < c->n; i++) n += c->apps[i].state == ST_UPDATE;
    return n;
}
