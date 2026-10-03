/* drastic-launch: installed as /storage/.config/drastic/drastic, the program ROCKNIX's start_drastic.sh runs.
 * It takes LD_PRELOAD out of the environment and execs the launcher script (dsflip/drastic-wrapper.sh).
 *
 * start_drastic.sh runs ./drastic with LD_PRELOAD=/usr/lib/libdrastouch.so and DSHOOK_MIC_THRESH set from ES's
 * microphone sensitivity. When that is above 0, libdrastouch's constructor calls SDL_WasInit through dlsym
 * without checking it: in anything that isn't DraStic (no SDL) that is a NULL call. Our launcher was a shell
 * script, so the shell crashed before its first line and the game never started (ROCKNIXDS issue 26).
 * This program is static, so the dynamic loader never runs and nothing is preloaded into it. The paths after it
 * set their own LD_PRELOAD (session.sh: libdsflip; drastic.dvsync: libdrastouch, inside DraStic, where it works).
 *
 * No libc: raw syscalls, so it builds without an arm64 sysroot.
 * Build: clang --target=aarch64-linux-gnu -O2 -static -nostdlib -ffreestanding -fno-stack-protector -fuse-ld=lld \
 *        -Wl,--build-id=none -o drastic-launch drastic-launch.c && llvm-strip drastic-launch
 */
#define SYS_execve 221
#define SYS_exit   93

static long sys3(long n, long a, long b, long c)
{
    register long x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static int is_preload(const char *e)
{
    const char *k = "LD_PRELOAD=";
    while (*k) if (*e++ != *k++) return 0;
    return 1;
}

__attribute__((used)) void launch(long *sp)
{
    long argc = sp[0];
    char **argv = (char **)(sp + 1), **envp = argv + argc + 1, **o = envp;
    for (char **e = envp; *e; e++) if (!is_preload(*e)) *o++ = *e;
    *o = 0;
    sys3(SYS_execve, (long)"/storage/.config/drastic/dsflip/drastic-wrapper.sh", (long)argv, (long)envp);
    sys3(SYS_execve, (long)"/storage/.config/drastic/drastic.dvsync", (long)argv, (long)envp);   /* older layout */
    sys3(SYS_exit, 127, 0, 0);
}

__asm__(".globl _start\n_start:\n  mov x0, sp\n  bl launch\n");
