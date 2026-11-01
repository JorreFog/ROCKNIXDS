/* catalog_parse_test.c: load one `rocknixds-store list` (RNDS_STORE_CLI) and print the Catalog.
 * The package manager's tests cover what it prints; this is the app that reads it (store/src/catalog.c):
 * tab fields, \\n and \\ escapes, which tab an app belongs on, and the raw pictures. */
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *store_cli(void) {
    const char *e = getenv("RNDS_STORE_CLI");
    return e && *e ? e : "false";
}

const char *plat_data_dir(void) { return "/tmp"; }

void img_alloc(Img *im, int w, int h) {
    im->w = w;
    im->h = h;
    im->px = calloc((size_t)w * h, 4);
}

static void esc(const char *s) {
    for (; *s; s++) {
        if (*s == '\\') fputs("\\\\", stdout);
        else if (*s == '\n') fputs("\\n", stdout);
        else if (*s == '\t') fputs("\\t", stdout);
        else fputc(*s, stdout);
    }
}

static const char *state_name(int s) {
    switch (s) {
    case ST_INSTALL: return "install";
    case ST_UPDATE: return "update";
    case ST_INSTALLED: return "installed";
    case ST_UNAVAILABLE: return "unavailable";
    case ST_UNSUPPORTED: return "unsupported";
    case ST_NEEDS: return "needs";
    default: return "?";
    }
}

int main(void) {
    Catalog c;
    memset(&c, 0, sizeof c);
    catalog_load(&c);
    printf("n %d\nrefreshed %ld\nupdates %d\n", c.n, c.refreshed, updates_count(&c));
    for (int i = 0; i < c.n; i++) {
        App *a = &c.apps[i];
        printf("app %d\n", i);
        printf("id "); esc(a->id); printf("\n");
        printf("kind "); esc(a->kind); printf("\n");
        printf("state %s\nstate_s ", state_name(a->state)); esc(a->state_s); printf("\n");
        printf("have "); esc(a->have); printf("\n");
        printf("latest "); esc(a->latest); printf("\n");
        printf("players "); esc(a->players); printf("\n");
        printf("accent %x\nsize %ld\n", a->accent, a->size);
        printf("summary_len %zu\nsummary ", strlen(a->summary)); esc(a->summary); printf("\n");
        printf("desc "); esc(a->desc); printf("\n");
        printf("notes "); esc(a->notes); printf("\n");
        printf("icon %d %d %x\n", a->icon.w, a->icon.h, a->icon.px && a->icon.w ? a->icon.px[0] : 0);
        printf("shot %d %d %x\n", a->shot.w, a->shot.h, a->shot.px && a->shot.w ? a->shot.px[0] : 0);
        printf("tab_games %d\ntab_apps %d\ntab_installed %d\ntab_updates %d\n",
               app_tab_has(a, TAB_GAMES), app_tab_has(a, TAB_APPS),
               app_tab_has(a, TAB_INSTALLED), app_tab_has(a, TAB_UPDATES));
    }
    catalog_free(&c);
    return 0;
}
