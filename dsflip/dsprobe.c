// libdsprobe.so: times DraStic's SDL2 video calls (plan §7). Chains via RTLD_NEXT, so load it
// FIRST in LD_PRELOAD and its timings include drastouch's shader pass.
//   DSPROBE_LOG=path   log file (default /storage/dsflip/logs/probe.log)
//   DSPROBE_NULL=1     skip UpdateTexture/RenderClear/RenderCopy*/RenderPresent entirely:
//                      the emulator runs with no display cost (upper bound for a zero-cost presenter)
// Build: clang -target aarch64-linux-gnu -fuse-ld=lld -shared -fPIC -nostdlib -O2 -o libdsprobe.so dsprobe.c
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int Uint32;
struct timespec { long tv_sec; long tv_nsec; };
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { const char *name; Uint32 flags, num_texture_formats, texture_formats[16];
                 int max_texture_width, max_texture_height; } SDL_RendererInfo;

#define RTLD_NEXT ((void *)-1l)
extern void *dlsym(void *, const char *);
extern int clock_gettime(int, struct timespec *);
extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern char *getenv(const char *);
extern int getpid(void);
extern long syscall(long, ...);
extern void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
extern void *memcpy(void *, const void *, size_t);

static int lg = 2, nul;
static long t_win, t_pres_last;
static long c_upd, c_lock, c_copy, c_clear, c_pres, c_other;   /* us accumulated in window */
static int n_pres, n_upd, n_lock, n_copy, n_clear, n_swap, n_mouse, n_tot_upd, n_tot_copy;
#define NIV 600
static long iv[NIV]; static int niv;

static long now_us(void) {
    struct timespec ts; clock_gettime(1, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}
static int tid(void) { return (int)syscall(178); }
#define LOG(...) do { char b_[512]; int n_ = snprintf(b_, sizeof b_, __VA_ARGS__); write(lg, b_, n_ > 511 ? 511 : n_); } while (0)
static void rect(char *b, const SDL_Rect *r) {
    if (r) snprintf(b, 40, "%d,%d %dx%d", r->x, r->y, r->w, r->h); else snprintf(b, 40, "full");
}

__attribute__((constructor)) static void init(void) {
    const char *p = getenv("DSPROBE_LOG");
    int fd = open(p ? p : "/storage/dsflip/logs/probe.log", 01 | 0100 | 01000 | 02000, 0644);
    if (fd >= 0) lg = fd;
    p = getenv("DSPROBE_NULL"); nul = p && *p == '1';
    LOG("[probe] loaded pid=%d null=%d\n", getpid(), nul);
}

#define REAL(ret, fn, ...) static ret (*real)(__VA_ARGS__); if (!real) real = dlsym(RTLD_NEXT, #fn)

extern const char *SDL_GetCurrentVideoDriver(void);
extern const char *SDL_GetPixelFormatName(Uint32);
extern int SDL_GetRendererInfo(void *, SDL_RendererInfo *);

void *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    REAL(void *, SDL_CreateWindow, const char *, int, int, int, int, Uint32);
    void *win = real(title, x, y, w, h, flags);
    const char *d = SDL_GetCurrentVideoDriver();
    LOG("[win] CreateWindow '%s' %d,%d %dx%d flags=0x%x driver=%s tid=%d\n", title ? title : "", x, y, w, h, flags, d ? d : "?", tid());
    return win;
}
void SDL_SetWindowSize(void *win, int w, int h) {
    REAL(void, SDL_SetWindowSize, void *, int, int);
    LOG("[win] SetWindowSize %dx%d\n", w, h);
    real(win, w, h);
}
void *SDL_CreateRenderer(void *win, int index, Uint32 flags) {
    REAL(void *, SDL_CreateRenderer, void *, int, Uint32);
    void *r = real(win, index, flags);
    SDL_RendererInfo info;
    if (r && SDL_GetRendererInfo(r, &info) == 0)
        LOG("[ren] CreateRenderer name=%s flags=0x%x requested=0x%x\n", info.name, info.flags, flags);
    return r;
}
void *SDL_CreateTexture(void *r, Uint32 fmt, int access, int w, int h) {
    REAL(void *, SDL_CreateTexture, void *, Uint32, int, int, int);
    void *t = real(r, fmt, access, w, h);
    LOG("[tex] CreateTexture %p fmt=%s access=%d %dx%d\n", t, SDL_GetPixelFormatName(fmt), access, w, h);
    return t;
}
int SDL_SetRenderTarget(void *r, void *t) {
    REAL(int, SDL_SetRenderTarget, void *, void *);
    static int n; if (n++ < 12) LOG("[tgt] SetRenderTarget %p\n", t);
    long t0 = now_us(); int ret = real(r, t); c_other += now_us() - t0; return ret;
}
int SDL_RenderSetLogicalSize(void *r, int w, int h) {
    REAL(int, SDL_RenderSetLogicalSize, void *, int, int);
    LOG("[ren] SetLogicalSize %dx%d\n", w, h);
    return real(r, w, h);
}
int SDL_UpdateTexture(void *t, const SDL_Rect *rc, const void *px, int pitch) {
    REAL(int, SDL_UpdateTexture, void *, const SDL_Rect *, const void *, int);
    if (n_tot_upd++ < 12) { char b[40]; rect(b, rc); LOG("[upd] tex=%p rect=%s pitch=%d src=%p tid=%d\n", t, b, pitch, px, tid()); }
    n_upd++;
    if (nul) return 0;
    long t0 = now_us(); int ret = real(t, rc, px, pitch); c_upd += now_us() - t0; return ret;
}
extern int SDL_QueryTexture(void *, Uint32 *, int *, int *, int *);
static unsigned int nullbuf[1024 * 512];      /* null mode: DraStic writes here, nothing is uploaded */
int SDL_LockTexture(void *t, const SDL_Rect *rc, void **px, int *pitch) {
    REAL(int, SDL_LockTexture, void *, const SDL_Rect *, void **, int *);
    if (nul) { int w = 512; SDL_QueryTexture(t, 0, 0, &w, 0); *px = nullbuf; *pitch = w * 4; n_lock++; return 0; }
    long t0 = now_us(); int ret = real(t, rc, px, pitch); c_lock += now_us() - t0;
    static int n; if (n++ < 12) { char b[40]; rect(b, rc); LOG("[lock] tex=%p rect=%s pitch=%d\n", t, b, pitch ? *pitch : -1); }
    n_lock++; return ret;
}
void SDL_UnlockTexture(void *t) {
    REAL(void, SDL_UnlockTexture, void *);
    if (nul) return;
    long t0 = now_us(); real(t); c_lock += now_us() - t0;
}
int SDL_RenderClear(void *r) {
    REAL(int, SDL_RenderClear, void *);
    n_clear++;
    if (nul) return 0;
    long t0 = now_us(); int ret = real(r); c_clear += now_us() - t0; return ret;
}
int SDL_RenderCopy(void *r, void *t, const SDL_Rect *s, const SDL_Rect *d) {
    REAL(int, SDL_RenderCopy, void *, void *, const SDL_Rect *, const SDL_Rect *);
    if (n_tot_copy++ < 16) { char sb[40], db[40]; rect(sb, s); rect(db, d); LOG("[copy] tex=%p src=%s dst=%s\n", t, sb, db); }
    n_copy++;
    if (nul) return 0;
    long t0 = now_us(); int ret = real(r, t, s, d); c_copy += now_us() - t0; return ret;
}
int SDL_RenderCopyEx(void *r, void *t, const SDL_Rect *s, const SDL_Rect *d, double a, const void *c, int f) {
    REAL(int, SDL_RenderCopyEx, void *, void *, const SDL_Rect *, const SDL_Rect *, double, const void *, int);
    if (n_tot_copy++ < 16) { char sb[40], db[40]; rect(sb, s); rect(db, d); LOG("[copyex] tex=%p src=%s dst=%s angle=%d flip=%d\n", t, sb, db, (int)a, f); }
    n_copy++;
    if (nul) return 0;
    long t0 = now_us(); int ret = real(r, t, s, d, a, c, f); c_copy += now_us() - t0; return ret;
}
void SDL_GL_SwapWindow(void *w) {
    REAL(void, SDL_GL_SwapWindow, void *);
    if (n_swap++ < 12) LOG("[gl] SDL_GL_SwapWindow tid=%d\n", tid());
    real(w);
}
Uint32 SDL_GetMouseState(int *x, int *y) {
    REAL(Uint32, SDL_GetMouseState, int *, int *);
    n_mouse++; return real(x, y);
}

static int cmpl(const void *a, const void *b) { long x = *(const long *)a, y = *(const long *)b; return (x > y) - (x < y); }

void SDL_RenderPresent(void *r) {
    REAL(void, SDL_RenderPresent, void *);
    long t0 = now_us();
    if (!nul) real(r);
    long t1 = now_us();
    c_pres += t1 - t0;
    if (t_pres_last && niv < NIV) iv[niv++] = t1 - t_pres_last;
    t_pres_last = t1;
    n_pres++;
    if (!t_win) t_win = t1;
    long el = t1 - t_win;
    if (el >= 1000000) {             /* one line per second */
        long s[NIV]; int k = niv; memcpy(s, iv, k * sizeof s[0]); qsort(s, k, sizeof s[0], cmpl);
        int np = n_pres ? n_pres : 1;
        LOG("[sec] pres/s=%.1f upd/s=%d copy/s=%d | per present us: upd=%ld lock=%ld clear=%ld copy=%ld present=%ld other=%ld | iv p50=%ld p99=%ld max=%ld | mouse=%d tid=%d\n",
            n_pres * 1e6 / el, n_upd, n_copy, c_upd / np, c_lock / np, c_clear / np, c_copy / np, c_pres / np, c_other / np,
            k ? s[k / 2] : 0, k ? s[(k * 99) / 100] : 0, k ? s[k - 1] : 0, n_mouse, tid());
        c_upd = c_lock = c_copy = c_clear = c_pres = c_other = 0;
        n_pres = n_upd = n_lock = n_copy = n_clear = n_mouse = 0; niv = 0; t_win = t1;
    }
}

/* key/mouse event log (first 40), to debug input reaching DraStic */
int SDL_PollEvent(void *e) {
    REAL(int, SDL_PollEvent, void *);
    int r = real(e);
    static int n;
    if (r && e && n < 40) {
        Uint32 ty = *(Uint32 *)e;
        if (ty == 0x300 || ty == 0x301) { n++; LOG("[ev] key %s scancode=%d sym=%d win=%u\n", ty == 0x300 ? "down" : "up", ((int *)e)[4], ((int *)e)[5], ((Uint32 *)e)[2]); }
        else if (ty == 0x200 || ty == 0x202) { n++; LOG("[ev] type=0x%x\n", ty); }
    }
    return r;
}
