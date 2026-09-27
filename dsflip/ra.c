// RetroAchievements for DraStic, inside libdsflip (rcheevos rc_client).
//
// - Credentials come from ROCKNIX's own settings (ES > RetroAchievements writes them to system.cfg:
//   global.retroachievements=1, .username, .password). After the first password login only RA's
//   login token is kept, in /storage/.config/drastic/dsflip/ra.token (mode 600).
// - The ROM is identified by rcheevos' own NDS hash of the file DraStic was started with.
// - DS main RAM (RA addresses 0x000000-0x3FFFFF) is found inside DraStic's process: the DS keeps a
//   copy of the cartridge header at 0x027FFE00, i.e. at offset 0x3FFE00 of main RAM, so we look for the
//   ROM's header in DraStic's writable memory at that offset from a 4 MB span.
// - HTTP goes through libcurl (dlopen'd, so libdsflip doesn't depend on it) on a worker thread per request.
// - Softcore only: DraStic's savestates/fast-forward/cheats can't be blocked from outside, which hardcore requires.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#ifndef DSFLIP_VERSION
#define DSFLIP_VERSION "dev"
#endif

void dsflip_toast(const char *line1, const char *line2, uint32_t accent, int ms);
void ui_popup(const char *l1, const char *l2, const char *badge_png, uint32_t accent, int ms);
void ui_progress(const char *text, const char *badge_png);
void dsflip_log(const char *fmt, ...);

#define SYSCFG "/storage/.config/system/configs/system.cfg"
#define TOKEN_FILE "/storage/.config/drastic/dsflip/ra.token"
#define BADGE_DIR "/storage/.config/drastic/dsflip/badges"     /* achievement badges and game icons, fetched once */
#define DS_RAM_SIZE 0x400000u
#define HDR_OFF 0x3FFE00u

static rc_client_t *rc;
static volatile uint8_t *ram;           /* DS main RAM inside DraStic, once found */
/* DTCM (the ARM9's 16 KB data TCM; RA addresses 0x1000000-0x1003FFF). DraStic backs the whole DS address space with
 * one shared-memory file (/dev/shm/drastic_mapped_memory.dat, kept open, unlinked) mapped view by view at a fixed
 * base: main RAM at file offset 0, ITCM at 0x400000, DTCM at 0x410000, mapped wherever the game's CP15 puts it
 * (HeartGold: 0x027E0000, verified byte-identical). We map our own read-only view of that 16 KB from DraStic's fd,
 * so it stays valid if the game moves DTCM (DraStic remaps its view then). */
#define DTCM_ADDR 0x1000000u
#define DTCM_SIZE 0x4000u
#define DTCM_FILE_OFF 0x410000
static volatile uint8_t *dtcm;
static volatile int game_loaded, scanning, logged_in, load_started;
static char rom_path[1024], user[128], pass[256];
static uint8_t rom_hdr[0x160];
static int frames;

int audio_sfx_load(const char *path);   /* audio.c */
void audio_sfx_play(void);

/* ---------- settings ---------- */
static int cfg_get(const char *key, char *out, size_t n) {
    FILE *f = fopen(SYSCFG, "r"); if (!f) return 0;
    char line[512]; size_t kl = strlen(key); int found = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            char *v = line + kl + 1; v[strcspn(v, "\r\n")] = 0;
            snprintf(out, n, "%s", v); found = 1;
        }
    fclose(f);
    return found;
}

/* ---------- HTTP via libcurl (dlopen) ---------- */
static void *curl_lib;
static void *(*c_init)(void); static int (*c_setopt)(void *, int, ...); static int (*c_perform)(void *);
static int (*c_getinfo)(void *, int, ...); static void (*c_cleanup)(void *);
static void *(*c_slist_append)(void *, const char *); static void (*c_slist_free)(void *);
enum { CURLOPT_WRITEDATA = 10001, CURLOPT_URL = 10002, CURLOPT_POSTFIELDS = 10015, CURLOPT_USERAGENT = 10018,
       CURLOPT_HTTPHEADER = 10023, CURLOPT_WRITEFUNCTION = 20011, CURLOPT_TIMEOUT = 13, CURLOPT_NOSIGNAL = 99,
       CURLOPT_FOLLOWLOCATION = 52, CURLINFO_RESPONSE_CODE = 0x200002 };

static int curl_load(void) {
    if (curl_lib) return 1;
    curl_lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!curl_lib) return 0;
    c_init = dlsym(curl_lib, "curl_easy_init"); c_setopt = dlsym(curl_lib, "curl_easy_setopt");
    c_perform = dlsym(curl_lib, "curl_easy_perform"); c_getinfo = dlsym(curl_lib, "curl_easy_getinfo");
    c_cleanup = dlsym(curl_lib, "curl_easy_cleanup"); c_slist_append = dlsym(curl_lib, "curl_slist_append");
    c_slist_free = dlsym(curl_lib, "curl_slist_free_all");
    return c_init && c_setopt && c_perform && c_getinfo && c_cleanup && c_slist_append && c_slist_free;
}

typedef struct { char *data; size_t len; } membuf;
static size_t on_body(char *p, size_t sz, size_t n, void *u) {
    membuf *m = u; size_t add = sz * n;
    char *d = realloc(m->data, m->len + add + 1); if (!d) return 0;
    memcpy(d + m->len, p, add); m->len += add; d[m->len] = 0; m->data = d;
    return add;
}

typedef struct { char *url, *post, *ctype; rc_client_server_callback_t cb; void *cbdata; } http_req;
static char user_agent[256];

static void *http_thread(void *a) {
    http_req *r = a;
    membuf body = { 0, 0 }; long status = 0;
    void *h = c_init();
    void *hdrs = 0;
    if (h) {
        c_setopt(h, CURLOPT_URL, r->url);
        c_setopt(h, CURLOPT_USERAGENT, user_agent);
        c_setopt(h, CURLOPT_NOSIGNAL, 1L);
        c_setopt(h, CURLOPT_TIMEOUT, 30L);
        c_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        c_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
        c_setopt(h, CURLOPT_WRITEDATA, &body);
        if (r->post) {
            c_setopt(h, CURLOPT_POSTFIELDS, r->post);
            if (r->ctype) {
                char ct[128]; snprintf(ct, sizeof ct, "Content-Type: %s", r->ctype);
                hdrs = c_slist_append(0, ct); c_setopt(h, CURLOPT_HTTPHEADER, hdrs);
            }
        }
        if (c_perform(h) == 0) c_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        c_cleanup(h);
        if (hdrs) c_slist_free(hdrs);
    }
    rc_api_server_response_t resp;
    resp.body = body.data ? body.data : ""; resp.body_length = body.len;
    resp.http_status_code = status ? (int)status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    r->cb(&resp, r->cbdata);
    free(body.data); free(r->url); free(r->post); free(r->ctype); free(r);
    return 0;
}

static void server_call(const rc_api_request_t *req, rc_client_server_callback_t cb, void *cbdata, rc_client_t *c) {
    (void)c;
    if (!curl_load()) {
        rc_api_server_response_t resp = { "", 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR };
        cb(&resp, cbdata); return;
    }
    http_req *r = calloc(1, sizeof *r);
    r->url = strdup(req->url); r->post = req->post_data ? strdup(req->post_data) : 0;
    r->ctype = req->content_type ? strdup(req->content_type) : 0; r->cb = cb; r->cbdata = cbdata;
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, http_thread, r)) { http_thread(r); }
    pthread_attr_destroy(&at);
}

/* ---------- memory ---------- */
static void dtcm_map(void) {
    if (dtcm) return;
    DIR *d = opendir("/proc/self/fd"); if (!d) return;
    struct dirent *e; int fd = -1;
    while ((e = readdir(d)) && fd < 0) {
        char l[64], t[256]; snprintf(l, sizeof l, "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(l, t, sizeof t - 1); if (n <= 0) continue; t[n] = 0;
        if (!strncmp(t, "/dev/shm/drastic_mapped_memory.dat", 34)) fd = atoi(e->d_name);
    }
    closedir(d);
    if (fd < 0) { dsflip_log("[ra] DTCM: DraStic's shared memory file not open, DTCM achievements unavailable\n"); return; }
    void *p = mmap(0, DTCM_SIZE, PROT_READ, MAP_SHARED, fd, DTCM_FILE_OFF);
    if (p == MAP_FAILED) { dsflip_log("[ra] DTCM: mmap failed: %s\n", strerror(errno)); return; }
    dtcm = p;
    dsflip_log("[ra] DTCM mapped (fd %d, offset %#x)\n", fd, DTCM_FILE_OFF);
}

static uint32_t read_memory(uint32_t addr, uint8_t *buf, uint32_t n, rc_client_t *c) {
    (void)c;
    if (addr >= DTCM_ADDR && addr < DTCM_ADDR + DTCM_SIZE) {
        const volatile uint8_t *t = dtcm;
        if (!t) return 0;
        if (addr + n > DTCM_ADDR + DTCM_SIZE) n = DTCM_ADDR + DTCM_SIZE - addr;
        memcpy(buf, (const void *)(t + (addr - DTCM_ADDR)), n);
        return n;
    }
    const volatile uint8_t *m = ram;
    if (!m || addr >= DS_RAM_SIZE) return 0;
    if (addr + n > DS_RAM_SIZE) n = DS_RAM_SIZE - addr;
    memcpy(buf, (const void *)(m + addr), n);
    return n;
}

/* find DS main RAM: the ROM header at offset 0x3FFE00 of a 4 MB span inside one writable mapping.
 * Read through /proc/self/mem, never by dereferencing: some mappings fault (SIGBUS) when touched.
 * DraStic maps the DS address space at a fixed base and mirrors main RAM several times; prefer a
 * candidate that starts an exactly-4 MB mapping (the canonical 0x02000000 view). */
static void *scan_thread(void *a) {
    (void)a;
    FILE *f = fopen("/proc/self/maps", "r");
    int mem = open("/proc/self/mem", O_RDONLY);
    static uint8_t chunk[(1 << 20) + 0x160];
    char line[512]; int cands = 0; const uint8_t *best = 0; int best_exact = 0;
    while (f && mem >= 0 && fgets(line, sizeof line, f)) {
        unsigned long lo, hi; char perms[8]; int pathoff = 0;
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %n", &lo, &hi, perms, &pathoff) < 3 || perms[0] != 'r' || perms[1] != 'w') continue;
        if (hi - lo < DS_RAM_SIZE) continue;
        /* device mappings (GPU/DRM buffers) fault when read; DraStic's own RAM is a shared /dev/zero mapping */
        if (pathoff && !strncmp(line + pathoff, "/dev/", 5) && strncmp(line + pathoff, "/dev/zero", 9) &&
            strncmp(line + pathoff, "/dev/shm", 8)) continue;
        int exact = hi - lo == DS_RAM_SIZE;

        for (unsigned long off = lo + HDR_OFF; off < hi; off += 1 << 20) {
            size_t want = hi - off < sizeof chunk ? hi - off : sizeof chunk;
            ssize_t got = pread(mem, chunk, want, (off_t)off);
            if (got < (ssize_t)sizeof rom_hdr) break;                        /* unreadable: skip the mapping */
            for (const uint8_t *p = chunk; (p = memmem(p, (size_t)got - (p - chunk), rom_hdr, 0x40)) != 0; p++) {
                unsigned long at = off + (unsigned long)(p - chunk);
                if ((size_t)(p - chunk) + sizeof rom_hdr > (size_t)got || memcmp(p, rom_hdr, sizeof rom_hdr)) continue;
                if (at < lo + HDR_OFF || at - HDR_OFF + DS_RAM_SIZE > hi) continue;
                cands++;
                dsflip_log("[ra] DS RAM candidate at %#lx (mapping %lx-%lx%s)\n", at - HDR_OFF, lo, hi, exact ? ", exactly 4 MB" : "");
                if (!best || (exact && !best_exact && at - HDR_OFF == lo)) { best = (const uint8_t *)(at - HDR_OFF); best_exact = exact && at - HDR_OFF == lo; }
            }
        }
    }
    if (f) fclose(f);
    if (mem >= 0) close(mem);
    dtcm_map();                                           /* before ram is published: the set loads after that */
    if (best) ram = (volatile uint8_t *)best;
    dsflip_log("[ra] RAM scan: %d candidate(s)%s%p\n", cands, best ? ", using " : ", will retry", (void *)best);
    scanning = 0;
    return 0;
}

/* ---------- images: badges and game icons, cached as files ---------- */
static int fetch_file(const char *url, const char *path) {         /* 1 if path exists afterwards */
    if (access(path, R_OK) == 0) return 1;
    if (!*url || !curl_load()) return 0;
    membuf body = { 0, 0 }; long status = 0;
    void *h = c_init(); if (!h) return 0;
    c_setopt(h, CURLOPT_URL, url); c_setopt(h, CURLOPT_USERAGENT, user_agent); c_setopt(h, CURLOPT_NOSIGNAL, 1L);
    c_setopt(h, CURLOPT_TIMEOUT, 10L); c_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    c_setopt(h, CURLOPT_WRITEFUNCTION, on_body); c_setopt(h, CURLOPT_WRITEDATA, &body);
    if (c_perform(h) == 0) c_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    c_cleanup(h);
    int ok = 0;
    if (status == 200 && body.len > 8 && !memcmp(body.data, "\x89PNG", 4)) {
        char tmp[600]; snprintf(tmp, sizeof tmp, "%s.part", path);
        FILE *f = fopen(tmp, "wb");
        if (f) { ok = fwrite(body.data, 1, body.len, f) == body.len; if (fclose(f)) ok = 0; }
        if (ok) ok = rename(tmp, path) == 0; else unlink(tmp);
    }
    free(body.data);
    return ok;
}
static void badge_file(const rc_client_achievement_t *a, char *path, size_t n) { snprintf(path, n, BADGE_DIR "/%s.png", a->badge_name); }
static int badge_fetch(const rc_client_achievement_t *a, char *path, size_t n) {
    char url[512] = "";
    badge_file(a, path, n);
    rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof url);
    return fetch_file(url, path);
}
static void detach(void *(*fn)(void *), void *arg) {
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, fn, arg)) fn(arg);
    pthread_attr_destroy(&at);
}
/* after the game loads: its icon for the "loaded" pop-up, then every badge, so an unlock shows its badge at once */
typedef struct { char title[128], line[160], icon_url[512]; } load_msg;
static void *prefetch_thread(void *a) {
    load_msg *m = a;
    mkdir(BADGE_DIR, 0755);
    char icon[600]; const rc_client_game_t *g = rc_client_get_game_info(rc);
    snprintf(icon, sizeof icon, BADGE_DIR "/game-%s.png", g && g->badge_name ? g->badge_name : "0");
    int have_icon = fetch_file(m->icon_url, icon);
    ui_popup(m->title, m->line, have_icon ? icon : 0, 0x6ab0ff, 5000);
    free(m);
    rc_client_achievement_list_t *l = rc_client_create_achievement_list(rc, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    int got = 0, n = 0;
    for (uint32_t b = 0; l && b < l->num_buckets; b++)
        for (uint32_t i = 0; i < l->buckets[b].num_achievements; i++) {
            char path[600]; n++; got += badge_fetch(l->buckets[b].achievements[i], path, sizeof path);
        }
    if (l) rc_client_destroy_achievement_list(l);
    dsflip_log("[ra] badges: %d of %d cached in " BADGE_DIR "\n", got, n);
    if (getenv("DSFLIP_UI_DEMO")) {                  /* test hook: what an unlock and a progress update look like */
        rc_client_achievement_list_t *d = rc_client_create_achievement_list(rc, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
        if (d && d->num_buckets && d->buckets[0].num_achievements) {
            const rc_client_achievement_t *a = d->buckets[0].achievements[0];
            char path[600], l1[64]; badge_file(a, path, sizeof path);
            snprintf(l1, sizeof l1, "Achievement unlocked \xc2\xb7 %u points", a->points);
            sleep(6); audio_sfx_play(); ui_popup(l1, a->title, path, 0xffd84a, 5000);
            sleep(7); ui_progress("3/5", path);
            sleep(5); ui_progress(0, 0);
        }
        if (d) rc_client_destroy_achievement_list(d);
    }
    return 0;
}
typedef struct { char l1[128], l2[160], badge[600]; const rc_client_achievement_t *a; } unlock_msg;
static void *unlock_thread(void *a) {                /* the badge is normally cached already; fetch it if not */
    unlock_msg *m = a;
    int have = badge_fetch(m->a, m->badge, sizeof m->badge);
    ui_popup(m->l1, m->l2, have ? m->badge : 0, 0xffd84a, 5000);
    free(m);
    return 0;
}

/* ---------- events, login, game load ---------- */
static void on_event(const rc_client_event_t *e, rc_client_t *c) {
    (void)c;
    char l2[160];
    switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        unlock_msg *m = calloc(1, sizeof *m);
        dsflip_log("[ra] unlocked: %s (%u)\n", e->achievement->title, e->achievement->points);
        if (!m) break;
        snprintf(m->l1, sizeof m->l1, "Achievement unlocked \xc2\xb7 %u points", e->achievement->points);
        snprintf(m->l2, sizeof m->l2, "%s", e->achievement->title);
        m->a = e->achievement;
        audio_sfx_play();
        detach(unlock_thread, m);
        break;
    }
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE: {
        char path[600]; badge_file(e->achievement, path, sizeof path);
        ui_progress(e->achievement->measured_progress, access(path, R_OK) == 0 ? path : 0);
        break;
    }
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        ui_progress(0, 0);
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        dsflip_toast("All achievements unlocked!", rc_client_get_game_info(rc)->title, 0xffd84a, 6000);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        snprintf(l2, sizeof l2, "%s", e->leaderboard->title);
        dsflip_toast("Leaderboard submitted", l2, 0x6ab0ff, 3000);
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        dsflip_log("[ra] server error: %s\n", e->server_error->error_message);
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        dsflip_toast("RetroAchievements", "offline: unlocks will be sent later", 0xff7a4a, 3000);
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        dsflip_toast("RetroAchievements", "back online, unlocks sent", 0x6ad07a, 3000);
        break;
    }
}

static void on_load(int result, const char *err, rc_client_t *c, void *u) {
    (void)u;
    if (result != RC_OK) {
        dsflip_log("[ra] game load failed: %s\n", err ? err : "?");
        if (result == RC_NO_GAME_LOADED) dsflip_toast("RetroAchievements", "this ROM isn't recognised", 0xff7a4a, 4000);
        else dsflip_toast("RetroAchievements", err ? err : "couldn't load the game", 0xff7a4a, 4000);
        return;
    }
    const rc_client_game_t *g = rc_client_get_game_info(c);
    rc_client_user_game_summary_t s; rc_client_get_user_game_summary(c, &s);
    char l2[160];
    if (s.num_core_achievements)
        snprintf(l2, sizeof l2, "%u of %u unlocked (softcore)", s.num_unlocked_achievements, s.num_core_achievements);
    else snprintf(l2, sizeof l2, "no achievements for this game yet");
    dsflip_log("[ra] game %u '%s': %s\n", g->id, g->title, l2);
    game_loaded = 1;
    load_msg *m = calloc(1, sizeof *m);
    if (!m) { dsflip_toast(g->title, l2, 0x6ab0ff, 5000); return; }
    snprintf(m->title, sizeof m->title, "%s", g->title); snprintf(m->line, sizeof m->line, "%s", l2);
    rc_client_game_get_image_url(g, m->icon_url, sizeof m->icon_url);
    detach(prefetch_thread, m);
}

static void save_token(void) {
    const rc_client_user_t *u = rc_client_get_user_info(rc);
    if (!u || !u->token) return;
    FILE *f = fopen(TOKEN_FILE, "w"); if (!f) return;
    fprintf(f, "%s\n%s\n", u->username, u->token); fclose(f);
    chmod(TOKEN_FILE, 0600);
}

static void on_login_password(int result, const char *err, rc_client_t *c, void *u);
static void start_load(void) {
    rc_client_begin_identify_and_load_game(rc, RC_CONSOLE_NINTENDO_DS, rom_path, 0, 0, on_load, 0);
}
static void on_login_token(int result, const char *err, rc_client_t *c, void *u) {
    (void)c; (void)u;
    if (result == RC_OK) { dsflip_log("[ra] logged in with token as %s\n", user); logged_in = 1; return; }
    dsflip_log("[ra] token login failed (%s)%s\n", err ? err : "?", *pass ? ", trying the password" : "");
    unlink(TOKEN_FILE);
    if (*pass) rc_client_begin_login_with_password(rc, user, pass, on_login_password, 0);
    else dsflip_toast("RetroAchievements", "login expired: re-enter your password in ES", 0xff7a4a, 5000);
}
static void on_login_password(int result, const char *err, rc_client_t *c, void *u) {
    (void)c; (void)u;
    if (result != RC_OK) {
        dsflip_log("[ra] login failed: %s\n", err ? err : "?");
        dsflip_toast("RetroAchievements login failed", err ? err : "check user/password in ES", 0xff7a4a, 5000);
        return;
    }
    dsflip_log("[ra] logged in as %s\n", user);
    save_token();
    logged_in = 1;
}

/* ---------- unlock sound ----------
 * The one chosen in ES > RetroAchievements > Unlock sound (system.cfg retroachievements.sound; "none" or unset: no
 * sound), looked up like ROCKNIX's setsettings.sh does for RetroArch: this game, then the nds system, then global.
 * The files are ES's list: /storage/roms/music/retroachievements/<name>.ogg, then /usr/share/libretro/sounds. */
static char sound_path[600];
static void *sound_load_thread(void *a) { (void)a; audio_sfx_load(sound_path); return 0; }
static void sound_setup(void) {
    char key[700], name[128] = "";
    const char *base = strrchr(rom_path, '/'); base = base ? base + 1 : rom_path;
    snprintf(key, sizeof key, "nds[\"%s\"].retroachievements.sound", base);
    if (!cfg_get(key, name, sizeof name) && !cfg_get("nds.retroachievements.sound", name, sizeof name))
        cfg_get("global.retroachievements.sound", name, sizeof name);
    if (!*name || !strcmp(name, "none") || strchr(name, '/')) { dsflip_log("[ra] unlock sound: none\n"); return; }
    const char *dirs[] = { "/storage/roms/music/retroachievements", "/usr/share/libretro/sounds" };
    for (int i = 0; i < 2; i++) {
        snprintf(sound_path, sizeof sound_path, "%s/%s.ogg", dirs[i], name);
        if (access(sound_path, R_OK) == 0) { detach(sound_load_thread, 0); return; }
    }
    dsflip_log("[ra] unlock sound '%s' not found\n", name);
}

static void on_rc_log(const char *msg, const rc_client_t *c) { (void)c; dsflip_log("[rc] %s\n", msg); }

/* called once, from the first SDL_RenderPresent (DraStic's main thread) */
static void ra_start(void) {
    char v[16] = "";
    cfg_get("global.retroachievements", v, sizeof v);
    cfg_get("global.retroachievements.username", user, sizeof user);
    cfg_get("global.retroachievements.password", pass, sizeof pass);
    const char *test = getenv("DSFLIP_RA_TEST");
    /* ROM = DraStic's last argument */
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (f) {
        char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        char *last = buf; for (char *p = buf; p < buf + n; p += strlen(p) + 1) if (*p) last = p;
        snprintf(rom_path, sizeof rom_path, "%s", last);
    }
    FILE *r = fopen(rom_path, "rb");
    if (!r || fread(rom_hdr, 1, sizeof rom_hdr, r) != sizeof rom_hdr) { if (r) fclose(r); dsflip_log("[ra] can't read ROM %s\n", rom_path); return; }
    fclose(r);
    if (test) {       /* offline self-test: hash + RAM discovery only */
        char hash[33] = "";
        if (!rc_hash_generate_from_file(hash, RC_CONSOLE_NINTENDO_DS, rom_path)) hash[0] = 0;
        dsflip_log("[ra] test mode: %s hash %s\n", rom_path, hash);
        dsflip_toast("RetroAchievements test", hash, 0x6ab0ff, 8000);
        rc = rc_client_create(read_memory, server_call);
        return;
    }
    if (strcmp(v, "1") || !*user) { dsflip_log("[ra] RetroAchievements off (ES: RetroAchievements settings)\n"); return; }

    sound_setup();
    rc = rc_client_create(read_memory, server_call);
    rc_client_enable_logging(rc, RC_CLIENT_LOG_LEVEL_WARN, on_rc_log);
    rc_client_set_event_handler(rc, on_event);
    rc_client_set_hardcore_enabled(rc, 0);
    char clause[128] = ""; rc_client_get_user_agent_clause(rc, clause, sizeof clause);
    snprintf(user_agent, sizeof user_agent, "dsflip/%s (ROCKNIX; RG DS) %s", DSFLIP_VERSION, clause);

    char tu[128] = "", tok[256] = "";
    FILE *tf = fopen(TOKEN_FILE, "r");
    if (tf) {
        if (fgets(tu, sizeof tu, tf) && fgets(tok, sizeof tok, tf)) { tu[strcspn(tu, "\r\n")] = 0; tok[strcspn(tok, "\r\n")] = 0; }
        fclose(tf);
    }
    if (*tok && !strcmp(tu, user)) rc_client_begin_login_with_token(rc, user, tok, on_login_token, 0);
    else if (*pass) rc_client_begin_login_with_password(rc, user, pass, on_login_password, 0);
    else dsflip_toast("RetroAchievements", "no password set in ES", 0xff7a4a, 4000);
}

/* called from every SDL_RenderPresent */
void ra_frame(void) {
    frames++;
    if (frames == 1) ra_start();
    if (!rc) return;
    /* the cart header is copied into RAM at boot: look once the game is running, retry every ~2 s */
    if (!ram && !scanning && frames >= 90 && frames % 120 == 90) {
        scanning = 1;
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, scan_thread, 0)) scanning = 0;
        pthread_attr_destroy(&at);
    }
    /* load the game only once DS RAM is found, so rc_client validates the achievement set against real memory
     * (loading before the async scan finished disabled every achievement with "Invalid address"). Fall back to
     * loading anyway after ~15 s so a game whose RAM we never find at least identifies. */
    if (logged_in && !load_started && (ram || frames > 900)) {
        load_started = 1;
        if (!ram) dsflip_log("[ra] RAM not found after 15 s; loading anyway (achievements may not work)\n");
        start_load();
    }
    /* RAM moved (reset / new game)? */
    if (ram && frames % 600 == 0 && memcmp((const void *)(ram + HDR_OFF), rom_hdr, 0x40)) {
        dsflip_log("[ra] header no longer at the RAM location, rescanning\n");
        ram = 0;
    }
    if (getenv("DSFLIP_RA_TEST") && ram && frames % 120 == 0) {
        uint32_t sum = 0; for (uint32_t i = 0; i < DS_RAM_SIZE; i += 64) sum = sum * 31 + ram[i];
        dsflip_log("[ra] test: RAM checksum %08x at frame %d\n", sum, frames);
        uint8_t t[DTCM_SIZE]; uint32_t got = read_memory(DTCM_ADDR, t, DTCM_SIZE, rc), nz = 0;
        for (uint32_t i = 0; i < got; i++) nz += t[i] != 0;
        dsflip_log("[ra] test: DTCM read %u bytes, %u non-zero, top word %02x%02x%02x%02x\n", got, nz,
                   t[DTCM_SIZE - 5], t[DTCM_SIZE - 6], t[DTCM_SIZE - 7], t[DTCM_SIZE - 8]);
    }
    if (game_loaded && ram) rc_client_do_frame(rc);
    else rc_client_idle(rc);
}
