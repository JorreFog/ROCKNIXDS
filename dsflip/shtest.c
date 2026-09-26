// shtest.c: runs shader.c outside DraStic (no DRM master needed): a test image through a shader into a
// panel-sized dumb buffer, written as PPM. shtest <shader> <srcw> <srch> <out.ppm>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
int shader_init(int fd, const char *name);
int shader_draw(uint32_t sh, int sw, int shh, int sp, uint64_t sgen, uint32_t dh, int dw, int dhh, int dp, uint64_t dgen, int finish);
extern int shader_copy_mode;
int shader_draw_mem(int panel, const void *px, int sw, int shh, int sp, uint32_t dh, int dw, int dhh, int dp, uint64_t dgen, int finish);
void dsflip_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
static int fd;
typedef struct { uint32_t handle, pitch; uint32_t *map; } buf;
static buf mk(int w, int h) {
    struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 }; buf b = { 0 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) { perror("create_dumb"); exit(1); }
    struct drm_mode_map_dumb m = { .handle = c.handle }; drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m);
    b.handle = c.handle; b.pitch = c.pitch; b.map = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    return b;
}
int main(int argc, char **argv) {
    if (argc < 5) return 2;
    int sw = atoi(argv[2]), sh = atoi(argv[3]), dw = 640, dh = 480;
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    fprintf(stderr, "init...\n");
    shader_copy_mode = getenv("COPY") != 0;
    if (!shader_init(fd, argv[1])) { fprintf(stderr, "shader_init failed\n"); return 1; }
    buf s = mk(sw, sh), d = mk(dw, dh);
    for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {      /* colour bars + 1px checker + gradient */
        uint32_t v;
        if (y < sh / 3) { static const uint32_t bars[8] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000 }; v = bars[x * 8 / sw]; }
        else if (y < 2 * sh / 3) v = ((x ^ y) & 1) ? 0xffffff : 0x000000;
        else { uint32_t g = x * 255 / (sw - 1); v = g << 16 | g << 8 | g; }
        s.map[y * (s.pitch / 4) + x] = 0xff000000 | v;
    }
    void *mem = 0;                      /* copy mode: the source is plain cached memory, like DraStic's there */
    if (shader_copy_mode) { mem = aligned_alloc(64, (size_t)s.pitch * sh); memcpy(mem, s.map, (size_t)s.pitch * sh); s.map = mem; }
    int r = shader_copy_mode ? shader_draw_mem(0, s.map, sw, sh, s.pitch, d.handle, dw, dh, d.pitch, 2, 1)
                             : shader_draw(s.handle, sw, sh, s.pitch, 1, d.handle, dw, dh, d.pitch, 2, 1);
    fprintf(stderr, "draw %d\n", r);
    int reps = getenv("REPS") ? atoi(getenv("REPS")) : 0;
    if (reps) {
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        for (int k = 0; k < reps; k++) if (shader_copy_mode) shader_draw_mem(0, s.map, sw, sh, s.pitch, d.handle, dw, dh, d.pitch, 2, 1);
                                        else shader_draw(s.handle, sw, sh, s.pitch, 1, d.handle, dw, dh, d.pitch, 2, 1);
        clock_gettime(CLOCK_MONOTONIC, &b);
        fprintf(stderr, "avg %.2f ms per draw\n", ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / reps);
    }
    FILE *f = fopen(argv[4], "wb"); fprintf(f, "P6\n%d %d\n255\n", dw, dh);
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) { uint32_t v = d.map[y * (d.pitch / 4) + x]; fputc(v >> 16, f); fputc(v >> 8, f); fputc(v, f); }
    fclose(f);
    return r ? 1 : 0;
}
