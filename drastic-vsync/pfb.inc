// ---- diagnostics: wp_presentation feedback (ground truth of when frames hit the panel) ----
// Only used with DVSYNC_LOG=1. Proxies live on a private queue; SDL's own event reading
// moves our events into it, and we dispatch it from the present hook.
struct wl_message { const char *name; const char *signature; const void **types; };
struct wl_interface { const char *name; int version; int method_count; const struct wl_message *methods; int event_count; const struct wl_message *events; };
// SDL dlopen()s libwayland-client, so nothing here may be a load-time reference.
extern void *dlopen(const char *, int);
static const struct wl_interface *wl_registry_interface_p, *wl_surface_interface_p, *wl_output_interface_p;
static void *(*wl_proxy_marshal_flags)(void *proxy, uint32_t opcode, const struct wl_interface *iface, uint32_t version, uint32_t flags, ...);
static void *(*wl_display_create_queue)(void *display);
static void *(*wl_proxy_create_wrapper)(void *proxy);
static void (*wl_proxy_set_queue)(void *proxy, void *queue);
static int (*wl_proxy_add_listener)(void *proxy, void (**impl)(void), void *data);
static int (*wl_display_roundtrip_queue)(void *display, void *queue);
static int (*wl_display_dispatch_queue_pending)(void *display, void *queue);

extern const struct wl_interface wp_presentation_feedback_interface;
static const void *pfb_types[] = { 0, 0, 0, 0, 0, 0, 0, 0 /* wl_surface */, &wp_presentation_feedback_interface, 0 /* wl_output */ };
static const struct wl_message wp_presentation_requests[] = { { "destroy", "", pfb_types }, { "feedback", "on", pfb_types + 7 } };
static const struct wl_message wp_presentation_events[] = { { "clock_id", "u", pfb_types } };
const struct wl_interface wp_presentation_interface = { "wp_presentation", 1, 2, wp_presentation_requests, 1, wp_presentation_events };
static const struct wl_message wp_feedback_events[] = { { "sync_output", "o", pfb_types + 9 }, { "presented", "uuuuuuu", pfb_types }, { "discarded", "", pfb_types } };
const struct wl_interface wp_presentation_feedback_interface = { "wp_presentation_feedback", 1, 0, 0, 3, wp_feedback_events };

typedef struct { unsigned char major, minor, patch; } SDL_version;
extern void SDL_GetVersion(SDL_version *);
extern void *SDL_RenderGetWindow(void *);
extern void *SDL_GL_GetCurrentWindow(void);
extern void *SDL_GetWindowFromID(uint32_t);
static int pf_tries;
extern int SDL_GetWindowWMInfo(void *, void *);

static void *pf_display, *pf_surface, *pf_queue, *pf_pres;
static int pf_state;   // 0 = not tried, 1 = ready, -1 = unavailable
static int pf_dbg;
#define NPF 20000
static struct { long submit, shown, target; unsigned int seq; int status; } pf[NPF];  // status 1 presented, 2 discarded
static void pf_result(long target, long shown, int status, long submit);   // implemented by the pacer
static long pf_ring_target[256], pf_ring_submit[256];
static long pf_count;
static int npf;

static void reg_global(void *data, void *reg, uint32_t name, const char *iface, uint32_t ver) {
    (void)data; (void)ver;
    const char *a = iface, *b = "wp_presentation";
    while (*a && *a == *b) { a++; b++; }
    if (*a == 0 && *b == 0)
        pf_pres = wl_proxy_marshal_flags(reg, 0, &wp_presentation_interface, 1, 0, name, "wp_presentation", 1, (void *)0);
}
static void reg_remove(void *d, void *r, uint32_t n) { (void)d; (void)r; (void)n; }
static void (*reg_listener[])(void) = { (void (*)(void))reg_global, (void (*)(void))reg_remove };

static void (*wl_proxy_destroy_p)(void *);
static void fb_sync(void *d, void *f, void *o) { (void)d; (void)f; (void)o; }
static void fb_presented(void *d, void *f, uint32_t sh, uint32_t sl, uint32_t ns, uint32_t refresh, uint32_t qh, uint32_t ql, uint32_t flags) {
    (void)f; (void)refresh; (void)qh; (void)flags; (void)sh;
    long i = (long)d;
    long shown = (long)sl * 1000000L + ns / 1000;
    if (i < NPF) { pf[i].shown = shown; pf[i].seq = ql; pf[i].status = 1; }
    pf_result(pf_ring_target[i & 255], shown, 1, pf_ring_submit[i & 255]);
    wl_proxy_destroy_p(f);
}
static void fb_discarded(void *d, void *f) { long i = (long)d; if (i < NPF) pf[i].status = 2; pf_result(pf_ring_target[i & 255], 0, 2, pf_ring_submit[i & 255]); wl_proxy_destroy_p(f); }
static void (*fb_listener[])(void) = { (void (*)(void))fb_sync, (void (*)(void))fb_presented, (void (*)(void))fb_discarded };

static int wl_resolve(void) {
    void *h = dlopen("libwayland-client.so.0", 1 /* RTLD_LAZY */);
    if (!h) return 0;
    wl_registry_interface_p = dlsym(h, "wl_registry_interface");
    wl_surface_interface_p = dlsym(h, "wl_surface_interface");
    wl_output_interface_p = dlsym(h, "wl_output_interface");
    wl_proxy_marshal_flags = dlsym(h, "wl_proxy_marshal_flags");
    wl_display_create_queue = dlsym(h, "wl_display_create_queue");
    wl_proxy_create_wrapper = dlsym(h, "wl_proxy_create_wrapper");
    wl_proxy_set_queue = dlsym(h, "wl_proxy_set_queue");
    wl_proxy_add_listener = dlsym(h, "wl_proxy_add_listener");
    wl_display_roundtrip_queue = dlsym(h, "wl_display_roundtrip_queue");
    wl_display_dispatch_queue_pending = dlsym(h, "wl_display_dispatch_queue_pending");
    wl_proxy_destroy_p = dlsym(h, "wl_proxy_destroy");
    if (!wl_registry_interface_p || !wl_surface_interface_p || !wl_output_interface_p || !wl_proxy_marshal_flags ||
        !wl_display_create_queue || !wl_proxy_create_wrapper || !wl_proxy_set_queue || !wl_proxy_add_listener ||
        !wl_display_roundtrip_queue || !wl_display_dispatch_queue_pending || !wl_proxy_destroy_p) return 0;
    pfb_types[7] = wl_surface_interface_p;
    pfb_types[9] = wl_output_interface_p;
    return 1;
}

static void pf_init(void *renderer) {
    pf_state = -1;
    if (!wl_resolve()) { pf_dbg = 4; return; }
    struct { SDL_version v; int subsystem; void *display; void *surface; char pad[64]; } info = {0};
    SDL_GetVersion(&info.v);
    void *win = SDL_RenderGetWindow(renderer);
    if (!win) win = SDL_GL_GetCurrentWindow();
    for (uint32_t id = 1; !win && id < 8; id++) win = SDL_GetWindowFromID(id);
    if (!win) { pf_dbg = 1; if (++pf_tries < 300) pf_state = 0; return; }
    if (!SDL_GetWindowWMInfo(win, &info)) { pf_dbg = 2; return; }
    if (info.subsystem != 6 /* SDL_SYSWM_WAYLAND */) { pf_dbg = 100 + info.subsystem; return; }
    pf_display = info.display; pf_surface = info.surface;
    pf_queue = wl_display_create_queue(pf_display);
    void *wrapped = wl_proxy_create_wrapper(pf_display);
    wl_proxy_set_queue(wrapped, pf_queue);
    void *reg = wl_proxy_marshal_flags(wrapped, 1, wl_registry_interface_p, 1, 0, (void *)0);
    wl_proxy_add_listener(reg, reg_listener, 0);
    wl_display_roundtrip_queue(pf_display, pf_queue);
    if (pf_pres) pf_state = 1; else pf_dbg = 3;
}
// call right before the frame's wl_surface.commit (i.e. before the real present)
static void pf_request(void *renderer, long submit, long target) {
    if (pf_state == 0) pf_init(renderer);
    if (pf_state != 1) return;
    void *fb = wl_proxy_marshal_flags(pf_pres, 1, &wp_presentation_feedback_interface, 1, 0, pf_surface, (void *)0);
    long idx = pf_count++;
    pf_ring_target[idx & 255] = target;
    pf_ring_submit[idx & 255] = submit;
    if (idx < NPF) { pf[idx].submit = submit; pf[idx].target = target; npf = (int)idx + 1; }
    wl_proxy_add_listener(fb, fb_listener, (void *)idx);
}
static void pf_poll(void) { if (pf_state == 1) wl_display_dispatch_queue_pending(pf_display, pf_queue); }
