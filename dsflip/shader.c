// shader.c: optional GPU shader pass for libdsflip (ES's per-system/per-game DraStic "shader" setting).
//
// ES passes the choice as DSHOOK_SHADER (what ROCKNIX's libdrastouch reads). "bilinear"/"none"/unset keeps
// libdsflip's zero-copy path. Anything else: each DS screen buffer DraStic finished is drawn with that
// fragment shader into a panel-sized (640x480) dumb buffer, which is then scanned out 1:1 instead of the
// DraStic buffer. Same inputs as stock: u_texture (GL_LINEAR), u_texture_size = DS buffer size,
// u_output_size = panel size, v_texcoord, SWIZ(); gl_FragCoord.y is flipped to count from the bottom,
// as it does in stock's window, so pixel masks keep stock's phase.
//
// Shader sources: ROCKNIX's built-ins are read out of /usr/lib/libdrastouch.so at runtime (they are not
// copied into this repo), recognised by their content. Other names load /storage/.config/drastic/shaders/
// <name>.frag, the same files the stock path uses.
//
// GL runs only on the presenter thread, in a surfaceless context (it renders only into imported dumb buffers).
// The RG DS's GPU runs ARM's libmali (mali_kbase, /dev/mali0; there is no DRM render node), so libmali's EGL in
// /usr/lib/mali is preferred over the system (Mesa) one. Everything is dlopen()ed: a missing or failing GL
// stack just means no shader (zero-copy stays).
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

void dsflip_log(const char *fmt, ...);
#define SLOG(...) dsflip_log(__VA_ARGS__)

typedef void *EGLDisplay, *EGLContext, *EGLConfig, *EGLImage;
typedef intptr_t EGLAttrib; typedef int32_t EGLint; typedef unsigned EGLBoolean, EGLenum;
typedef unsigned GLuint, GLenum; typedef int GLint, GLsizei; typedef float GLfloat; typedef char GLchar;

static EGLDisplay (*eglGetPlatformDisplayEXT)(EGLenum, void *, const EGLint *);
static EGLDisplay (*eglGetDisplay)(void *);
static EGLint (*eglGetError)(void);
static EGLBoolean (*eglInitialize)(EGLDisplay, EGLint *, EGLint *);
static EGLBoolean (*eglBindAPI)(EGLenum);
static EGLContext (*eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
static EGLBoolean (*eglMakeCurrent)(EGLDisplay, void *, void *, EGLContext);
static EGLBoolean (*eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
static const char *(*eglQueryString)(EGLDisplay, EGLint);
static void *(*eglGetProcAddress)(const char *);
static EGLImage (*eglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum, void *, const EGLint *);
static EGLBoolean (*eglDestroyImageKHR)(EGLDisplay, EGLImage);
static void (*glEGLImageTargetTexture2DOES)(GLenum, void *);
static void *(*eglCreateSyncKHR)(EGLDisplay, EGLenum, const EGLint *);
static EGLBoolean (*eglDestroySyncKHR)(EGLDisplay, void *);
static EGLint (*eglDupNativeFenceFDANDROID)(EGLDisplay, void *);

#define GLFN(ret, name, ...) static ret (*name)(__VA_ARGS__);
GLFN(GLuint, glCreateShader, GLenum) GLFN(void, glShaderSource, GLuint, GLsizei, const GLchar *const *, const GLint *)
GLFN(void, glCompileShader, GLuint) GLFN(void, glGetShaderiv, GLuint, GLenum, GLint *)
GLFN(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei *, GLchar *) GLFN(GLuint, glCreateProgram, void)
GLFN(void, glAttachShader, GLuint, GLuint) GLFN(void, glBindAttribLocation, GLuint, GLuint, const GLchar *)
GLFN(void, glLinkProgram, GLuint) GLFN(void, glGetProgramiv, GLuint, GLenum, GLint *)
GLFN(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei *, GLchar *) GLFN(void, glUseProgram, GLuint)
GLFN(GLint, glGetUniformLocation, GLuint, const GLchar *) GLFN(void, glUniform1i, GLint, GLint)
GLFN(void, glUniform1f, GLint, GLfloat) GLFN(void, glUniform2f, GLint, GLfloat, GLfloat)
GLFN(void, glGenTextures, GLsizei, GLuint *) GLFN(void, glDeleteTextures, GLsizei, const GLuint *)
GLFN(void, glBindTexture, GLenum, GLuint) GLFN(void, glTexParameteri, GLenum, GLenum, GLint)
GLFN(void, glActiveTexture, GLenum) GLFN(void, glGenFramebuffers, GLsizei, GLuint *)
GLFN(void, glDeleteFramebuffers, GLsizei, const GLuint *) GLFN(void, glBindFramebuffer, GLenum, GLuint)
GLFN(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) GLFN(GLenum, glCheckFramebufferStatus, GLenum)
GLFN(void, glViewport, GLint, GLint, GLsizei, GLsizei) GLFN(void, glVertexAttribPointer, GLuint, GLint, GLenum, unsigned char, GLsizei, const void *)
GLFN(void, glEnableVertexAttribArray, GLuint) GLFN(void, glDrawArrays, GLenum, GLint, GLsizei)
GLFN(void, glFinish, void) GLFN(void, glFlush, void)
GLFN(const unsigned char *, glGetString, GLenum) GLFN(void, glPixelStorei, GLenum, GLint)
GLFN(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)
GLFN(void, glTexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) GLFN(GLenum, glGetError, void) GLFN(void, glDisable, GLenum)

#define EGL_NONE 0x3038
#define EGL_EXTENSIONS 0x3055
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_SURFACE_TYPE 0x3033
#define EGL_OPENGL_ES2_BIT 4
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT 0x1406
#define GL_TEXTURE0 0x84C0
#define GL_BLEND 0x0BE2
#define GL_DITHER 0x0BD0

static EGLDisplay dpy;
static GLenum up_fmt; int shader_copy_mode;      /* set by the caller before shader_init */
static GLuint up_tex[2]; static int up_w[2], up_h[2];
static GLuint prog; static GLint u_tex, u_tsize, u_osize, u_fch;
/* a shader that draws the DS screen into part of the panel (ds-integer) says where, in panel pixels, with a line
 * "dsflip-viewport: x y w h" in its source; libdsflip maps touches into that rectangle */
static int vp[4];
int shader_viewport(int *v) { if (vp[2] <= 0 || vp[3] <= 0) return 0; for (int i = 0; i < 4; i++) v[i] = vp[i]; return 1; }
static int drm_fd;

/* ---- shader sources ---- */
static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = len > 0 ? malloc(len + 1) : 0;
    if (b && fread(b, 1, len, f) != (size_t)len) { free(b); b = 0; }
    fclose(f);
    if (b) { b[len] = 0; *n = len; }
    return b;
}
/* ROCKNIX's built-ins, recognised in libdrastouch.so by what they contain (strings are NUL-separated) */
static char *drastouch_source(const char *name, int vertex) {
    static const struct { const char *name, *has, *hasnt; } sig[] = {
        { "sharp-shimmerless", "pixel_tl", 0 },
        { "lcd1x-nds-color", "vec3(1.91)", 0 },
        { "lcd3x", "col_r", "1.91" },
        { "scanlines", "float scan", 0 },
        { "quilez", "15.0", 0 },
        { "sharp-bilinear", "region_range", "gl_FragCoord" },
    };
    const char *has = vertex ? "attribute vec2 a_position" : 0, *hasnt = 0;
    for (size_t i = 0; !vertex && i < sizeof sig / sizeof sig[0]; i++)
        if (!strcmp(name, sig[i].name)) { has = sig[i].has; hasnt = sig[i].hasnt; }
    if (!has) return 0;
    size_t n; char *d = slurp("/usr/lib/libdrastouch.so", &n); if (!d) return 0;
    char *r = 0;
    for (size_t i = 0; i < n && !r; ) {
        char *s = d + i; size_t l = strnlen(s, n - i);
        if (l > 40 && (vertex ? strstr(s, "gl_Position") : strstr(s, "gl_FragColor")) && strstr(s, has) &&
            (!hasnt || !strstr(s, hasnt))) r = strdup(s);
        i += l + 1;
    }
    free(d);
    return r;
}
static const char *default_vs =
    "attribute vec2 a_position;\nattribute vec2 a_texcoord;\nvarying vec2 v_texcoord;\n"
    "void main() {\n    gl_Position = vec4(a_position, 0.0, 1.0);\n    v_texcoord = a_texcoord;\n}\n";

/* stock's gl_FragCoord.y counts from the window's bottom; ours from the buffer's first row (the panel's top) */
static char *flip_fragcoord(const char *src) {
    const char *tok = "gl_FragCoord", *rep = "dsf_FragCoord()";
    size_t n = 0; for (const char *p = src; (p = strstr(p, tok)); p += strlen(tok)) n++;
    char *out = malloc(strlen(src) + n * (strlen(rep) + 1) + 1), *o = out;
    for (const char *p = src, *q; ; p = q + strlen(tok)) {
        q = strstr(p, tok);
        if (!q) { strcpy(o, p); break; }
        memcpy(o, p, q - p); o += q - p; strcpy(o, rep); o += strlen(rep);
    }
    return out;
}

static GLuint compile(GLenum type, const char *pre, const char *src) {
    GLuint s = glCreateShader(type);
    const char *parts[2] = { pre, src };
    glShaderSource(s, 2, parts, 0); glCompileShader(s);
    GLint okc = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &okc);
    if (!okc) { char log[1024] = ""; glGetShaderInfoLog(s, sizeof log, 0, log); SLOG("[shader] compile failed: %s\n", log); return 0; }
    return s;
}

/* ---- EGL images of dumb buffers ---- */
typedef struct { uint32_t handle; int w, h, pitch; uint64_t gen; EGLImage img; GLuint tex, fbo; } gimg;
/* must hold every buffer that cycles (DraStic's 2x6 at 2x, our 2x6 outputs, plus the 1x ones from startup):
 * an eviction means re-importing a dma-buf every frame, and the kernel's GPU IOMMU mapping then interrupts every
 * CPU core (DraStic's too). shader_imports counts imports so a miss shows up in the log. */
#define NCACHE 64
static gimg cache[NCACHE]; static int ncache, cache_next, imports;
int shader_imports(void) { int n = imports; imports = 0; return n; }

static gimg *image_for(uint32_t handle, int w, int h, int pitch, uint64_t gen, int target) {
    for (int i = 0; i < ncache; i++) if (cache[i].gen == gen) return &cache[i];
    gimg *g = ncache < NCACHE ? &cache[ncache++] : &cache[cache_next++ % NCACHE];
    imports++;
    if (g->img) {                        /* evict */
        if (g->fbo) glDeleteFramebuffers(1, &g->fbo);
        glDeleteTextures(1, &g->tex); eglDestroyImageKHR(dpy, g->img);
    }
    memset(g, 0, sizeof *g);
    int prime = -1;
    if (drmPrimeHandleToFD(drm_fd, handle, DRM_CLOEXEC | DRM_RDWR, &prime)) { SLOG("[shader] prime export failed\n"); return 0; }
    EGLint at[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
                    EGL_DMA_BUF_PLANE0_FD_EXT, prime, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0, EGL_DMA_BUF_PLANE0_PITCH_EXT, pitch, EGL_NONE };
    g->img = eglCreateImageKHR(dpy, 0, EGL_LINUX_DMA_BUF_EXT, 0, at);
    close(prime);
    if (!g->img) { SLOG("[shader] eglCreateImage %dx%d failed\n", w, h); return 0; }
    glGenTextures(1, &g->tex); glBindTexture(GL_TEXTURE_2D, g->tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, g->img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   /* stock's texture is GL_LINEAR too */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target) {
        glGenFramebuffers(1, &g->fbo); glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g->tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { SLOG("[shader] render target %dx%d incomplete\n", w, h); return 0; }
    }
    g->handle = handle; g->w = w; g->h = h; g->pitch = pitch; g->gen = gen;
    return g;
}

/* ---- public ---- */
const char *shader_name(void) {
    const char *s = getenv("DSFLIP_SHADER"); if (!s) s = getenv("DSHOOK_SHADER");
    if (!s || !*s || !strcmp(s, "none") || !strcmp(s, "bilinear")) return 0;
    return s;
}

/* on the presenter thread; returns 0 if the shader can't be used (the caller stays zero-copy) */
int shader_init(int fd, const char *name) {
    drm_fd = fd;
    /* ROCKNIX exports MALI_DEFAULT_DISPLAY=wayland system-wide; sway is stopped while we run, so libmali's default
     * display would fail (and would take a GBM device passed to eglGetDisplay for a wl_display: crash). DraStic
     * itself doesn't use libmali (SDL dummy driver), so this only affects our context. */
    setenv("MALI_DEFAULT_DISPLAY", "gbm", 1);
    const char *dir = "/usr/lib/mali/";
    void *egl = dlopen("/usr/lib/mali/libEGL.so.1", RTLD_NOW | RTLD_LOCAL), *gl = 0;
    if (egl) gl = dlopen("/usr/lib/mali/libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    else { dir = ""; egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL); gl = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL); }
    if (!egl || !gl) { SLOG("[shader] no libEGL/libGLESv2\n"); return 0; }
#define E(n) if (!(*(void **)&n = dlsym(egl, #n))) { SLOG("[shader] missing " #n "\n"); return 0; }
    E(eglGetDisplay) E(eglGetError) E(eglInitialize) E(eglBindAPI) E(eglCreateContext) E(eglMakeCurrent) E(eglChooseConfig) E(eglQueryString) E(eglGetProcAddress)
#define X(n) if (!(*(void **)&n = eglGetProcAddress(#n))) { SLOG("[shader] missing " #n "\n"); return 0; }
    *(void **)&eglGetPlatformDisplayEXT = eglGetProcAddress("eglGetPlatformDisplayEXT");
    X(eglCreateImageKHR) X(eglDestroyImageKHR) X(glEGLImageTargetTexture2DOES)
    *(void **)&eglCreateSyncKHR = eglGetProcAddress("eglCreateSyncKHR");
    *(void **)&eglDestroySyncKHR = eglGetProcAddress("eglDestroySyncKHR");
    *(void **)&eglDupNativeFenceFDANDROID = eglGetProcAddress("eglDupNativeFenceFDANDROID");
#define G(n) if (!(*(void **)&n = dlsym(gl, #n))) { SLOG("[shader] missing " #n "\n"); return 0; }
    G(glCreateShader) G(glShaderSource) G(glCompileShader) G(glGetShaderiv) G(glGetShaderInfoLog) G(glCreateProgram)
    G(glAttachShader) G(glBindAttribLocation) G(glLinkProgram) G(glGetProgramiv) G(glGetProgramInfoLog) G(glUseProgram)
    G(glGetUniformLocation) G(glUniform1i) G(glUniform1f) G(glUniform2f) G(glGenTextures) G(glDeleteTextures)
    G(glBindTexture) G(glTexParameteri) G(glActiveTexture) G(glGenFramebuffers) G(glDeleteFramebuffers)
    G(glBindFramebuffer) G(glFramebufferTexture2D) G(glCheckFramebufferStatus) G(glViewport) G(glVertexAttribPointer)
    G(glEnableVertexAttribArray) G(glDrawArrays) G(glFinish) G(glFlush)
    G(glGetString) G(glPixelStorei) G(glTexImage2D) G(glTexSubImage2D) G(glGetError) G(glDisable)

    /* a display: libmali's default one (GBM, see above), else Mesa's surfaceless platform (needs a render node) */
    EGLint maj = 0, min = 0; const char *how = "default";
    dpy = eglGetDisplay(0);
    if (!dpy || !eglInitialize(dpy, &maj, &min)) {
        SLOG("[shader] default EGL display: error 0x%x\n", eglGetError());
        how = "surfaceless"; dpy = *dir || !eglGetPlatformDisplayEXT ? 0 : eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, 0, 0);
        if (!dpy || !eglInitialize(dpy, &maj, &min)) { SLOG("[shader] EGL init failed\n"); return 0; }
    }
    const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!ext || !strstr(ext, "EGL_EXT_image_dma_buf_import")) { SLOG("[shader] no dma-buf import\n"); return 0; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg = 0;
    if (!strstr(ext, "EGL_KHR_no_config_context")) {
        EGLint want[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE }, n = 0;
        if (!eglChooseConfig(dpy, want, &cfg, 1, &n) || n < 1) { SLOG("[shader] no EGL config\n"); return 0; }
    }
    EGLContext ctx = eglCreateContext(dpy, cfg, 0, ca);
    if (!ctx || !eglMakeCurrent(dpy, 0, 0, ctx)) { SLOG("[shader] no surfaceless GLES2 context\n"); return 0; }

    /* upload path: DraStic's XRGB8888 is B,G,R,X in memory; BGRA uploads need no swizzle in the shader */
    const char *gx = (const char *)glGetString(0x1F03 /* GL_EXTENSIONS */);
    up_fmt = gx && strstr(gx, "GL_EXT_texture_format_BGRA8888") ? 0x80E1 /* GL_BGRA_EXT */ : 0x1908 /* GL_RGBA */;
    char path[256]; size_t n;
    const char *sdir = getenv("DSFLIP_SHADER_DIR");   /* testing: load .frag files from elsewhere */
    snprintf(path, sizeof path, "%s/%s.frag", sdir ? sdir : "/storage/.config/drastic/shaders", name);
    char *fs = drastouch_source(name, 0), *vs = drastouch_source(name, 1);
    const char *from = "libdrastouch";
    if (!fs) { fs = slurp(path, &n); from = path; }
    if (!fs) { SLOG("[shader] unknown shader \"%s\" (not built into libdrastouch, no %s)\n", name, path); return 0; }
    { const char *m = strstr(fs, "dsflip-viewport:");
      if (m && sscanf(m + 16, "%d %d %d %d", &vp[0], &vp[1], &vp[2], &vp[3]) == 4)
          SLOG("[shader] draws the DS screen at %d,%d %dx%d of the panel (touch follows)\n", vp[0], vp[1], vp[2], vp[3]);
      else vp[2] = vp[3] = 0; }
    char *ffs = flip_fragcoord(fs);
    const char *pre = up_fmt == 0x1908 && shader_copy_mode ? "#define SWIZ(c) (c).bgra\nuniform highp float dsf_fch;\n"
                      "#define dsf_FragCoord() vec4(gl_FragCoord.x, dsf_fch - gl_FragCoord.y, gl_FragCoord.zw)\n"
                    : "#define SWIZ(c) (c)\nuniform highp float dsf_fch;\n"
                      "#define dsf_FragCoord() vec4(gl_FragCoord.x, dsf_fch - gl_FragCoord.y, gl_FragCoord.zw)\n";
    GLuint v = compile(GL_VERTEX_SHADER, "", vs ? vs : default_vs), f = compile(GL_FRAGMENT_SHADER, pre, ffs);
    free(fs); free(ffs); free(vs);
    if (!v || !f) return 0;
    prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f);
    glBindAttribLocation(prog, 0, "a_position"); glBindAttribLocation(prog, 1, "a_texcoord");
    glLinkProgram(prog);
    GLint okl = 0; glGetProgramiv(prog, GL_LINK_STATUS, &okl);
    if (!okl) { char log[1024] = ""; glGetProgramInfoLog(prog, sizeof log, 0, log); SLOG("[shader] link failed: %s\n", log); return 0; }
    glUseProgram(prog);
    u_tex = glGetUniformLocation(prog, "u_texture"); u_tsize = glGetUniformLocation(prog, "u_texture_size");
    u_osize = glGetUniformLocation(prog, "u_output_size"); u_fch = glGetUniformLocation(prog, "dsf_fch");
    glUniform1i(u_tex, 0);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    SLOG("[shader] \"%s\" from %s, EGL %d.%d (%s%s, %s display)\n", name, from, maj, min, *dir ? dir : "system ", "libEGL", how);
    return 1;
}

/* draw src (a DraStic screen buffer) into dst (a panel-sized buffer); finish: block until the GPU is done */
int shader_draw(uint32_t sh, int sw, int shh, int sp, uint64_t sgen, uint32_t dh, int dw, int dhh, int dp, uint64_t dgen, int finish) {
    gimg *s = image_for(sh, sw, shh, sp, sgen, 0), *d = image_for(dh, dw, dhh, dp, dgen, 1);
    if (!s || !d) return -1;
    static const GLfloat pos[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    static const GLfloat uv[] = { 0, 0, 1, 0, 0, 1, 1, 1 };     /* buffer row 0 (panel top) at GL y = -1 */
    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    glViewport(0, 0, dw, dhh);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s->tex);
    glUniform2f(u_tsize, (GLfloat)sw, (GLfloat)shh);
    glUniform2f(u_osize, (GLfloat)dw, (GLfloat)dhh);
    glUniform1f(u_fch, (GLfloat)dhh);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, pos); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, 0, uv); glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (finish) glFinish();
    return glGetError() ? -1 : 0;
}

/* a sync_file that signals when everything drawn so far is done; -1 (after waiting for the GPU) if unsupported */
int shader_fence(void) {
    int f = -1;
    void *sy = eglCreateSyncKHR && eglDupNativeFenceFDANDROID ? eglCreateSyncKHR(dpy, 0x3144 /* EGL_SYNC_NATIVE_FENCE_ANDROID */, 0) : 0;
    glFlush();
    if (sy) { f = eglDupNativeFenceFDANDROID(dpy, sy); eglDestroySyncKHR(dpy, sy); }
    if (f < 0) glFinish();
    return f;
}

/* copy path: upload a DraStic frame from ordinary (cached) memory into a GL-owned texture, then draw it into dst.
 * Like stock SDL: nothing DraStic writes is imported into the GPU. GL copies the pixels before glTexSubImage2D
 * returns, so the caller can hand the buffer back to DraStic right away. */
int shader_draw_mem(int panel, const void *px, int sw, int shh, int sp, uint32_t dh, int dw, int dhh, int dp, uint64_t dgen, int finish) {
    gimg *d = image_for(dh, dw, dhh, dp, dgen, 1);
    if (!d || panel < 0 || panel > 1) return -1;
    glActiveTexture(GL_TEXTURE0);
    if (!up_tex[panel]) glGenTextures(1, &up_tex[panel]);
    glBindTexture(GL_TEXTURE_2D, up_tex[panel]);
    glPixelStorei(0x0CF5 /* GL_UNPACK_ALIGNMENT */, 4);
    if (up_w[panel] != sw || up_h[panel] != shh) {
        glTexImage2D(GL_TEXTURE_2D, 0, up_fmt, sw, shh, 0, up_fmt, 0x1401 /* GL_UNSIGNED_BYTE */, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        up_w[panel] = sw; up_h[panel] = shh;
    }
    if (sp == sw * 4) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sw, shh, up_fmt, 0x1401, px);
    else for (int y = 0; y < shh; y++) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, sw, 1, up_fmt, 0x1401, (const char *)px + (size_t)y * sp);
    static const GLfloat pos[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    static const GLfloat uv[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    glViewport(0, 0, dw, dhh);
    glUniform2f(u_tsize, (GLfloat)sw, (GLfloat)shh);
    glUniform2f(u_osize, (GLfloat)dw, (GLfloat)dhh);
    glUniform1f(u_fch, (GLfloat)dhh);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, pos); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, 0, uv); glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (finish) glFinish();
    return glGetError() ? -1 : 0;
}
