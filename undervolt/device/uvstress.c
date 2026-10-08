/* uvstress: a CPU stress test that checks its own answers, for testing an undervolt (rocknixds-undervolt test).
 *
 * Too little voltage at a clock shows up as wrong results long before it crashes the handheld, so a stress test
 * that only burns cycles proves nothing. Each worker (one per core, pinned) computes rounds of a fixed workload:
 * integer multiply/rotate/xor passes over 512 KB (the multipliers and the L2), a single-precision rotation over
 * 16 KB that the compiler vectorizes to NEON, and a 40x40 double matrix product (the FP pipeline). Each round's
 * input is one of 16 seeds, and its result is a 64-bit hash. The first worker to finish a seed records its hash
 * in shared memory; every later round of that seed, on any core, must produce the same hash. Any difference is
 * a computation error: exit 1. A worker that dies (a crash from corrupted state) is an error too.
 *
 * -t SECONDS  how long (default 60)    -j N  workers (default 4, one per core)
 * -b          burst: sleep 1..40 ms between rounds, so the load (and the regulator) steps up and down
 *
 * No libc: raw syscalls, so it builds without an arm64 sysroot (like dsflip/device/drastic-launch.c).
 * Build: clang --target=aarch64-linux-gnu -O2 -static -nostdlib -ffreestanding -fno-stack-protector \
 *        -mno-outline-atomics -fuse-ld=lld -Wl,--build-id=none -o uvstress uvstress.c && llvm-strip uvstress
 */
typedef unsigned long u64;
typedef unsigned int u32;

#define SYS_write 64
#define SYS_exit_group 94
#define SYS_nanosleep 101
#define SYS_clock_gettime 113
#define SYS_sched_setaffinity 122
#define SYS_clone 220
#define SYS_mmap 222
#define SYS_wait4 260

static long sys6(long n, long a, long b, long c, long d, long e, long f)
{
    register long x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c,
                  x3 __asm__("x3") = d, x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#define sys3(n, a, b, c) sys6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

static void die(int code) { for (;;) sys3(SYS_exit_group, code, 0, 0); }

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void puts1(const char *s) { sys3(SYS_write, 1, s, slen(s)); }
static void putu(u64 v)
{
    char b[24]; int i = 23; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts1(b + i);
}
static void putx(u64 v)
{
    char b[17]; b[16] = 0;
    for (int i = 15; i >= 0; i--) { b[i] = "0123456789abcdef"[v & 15]; v >>= 4; }
    puts1(b);
}

static u64 now_ms(void)
{
    struct { long s, ns; } t;
    sys3(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, &t, 0);
    return (u64)t.s * 1000 + (u64)t.ns / 1000000;
}

static void sleep_ms(u64 ms)
{
    struct { long s, ns; } t = { (long)(ms / 1000), (long)(ms % 1000) * 1000000 };
    sys3(SYS_nanosleep, &t, 0, 0);
}

static u64 rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }
static u64 mix(u64 z)          /* splitmix64's finalizer */
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9UL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebUL;
    return z ^ (z >> 31);
}

#define NSEEDS 16
#define IWORDS (512 * 1024 / 8)
#define FN 4096
#define MN 40

static u64 ibuf[IWORDS];
static float fx[FN], fy[FN];
static double ma[MN][MN], mb[MN][MN], mc[MN][MN];

static u64 round_hash(u64 seed)
{
    u64 h = mix(seed + 0x9e3779b97f4a7c15UL);
    /* integer: 6 dependent passes over 512 KB */
    for (u64 i = 0; i < IWORDS; i++) ibuf[i] = mix(seed * 0x100000001b3UL + i);
    for (int pass = 0; pass < 6; pass++) {
        u64 prev = h;
        for (u64 i = 0; i < IWORDS; i++) {
            u64 v = ibuf[i] * 0xd6e8feb86659fd93UL ^ ibuf[(i * 7 + 3 + pass) & (IWORDS - 1)];
            v = rotl(v, 17 + pass) + prev;
            ibuf[i] = v;
            prev = v;
        }
        h = mix(h ^ prev);
    }
    /* NEON float: rotate 4096 points by a fixed angle 200 times (the magnitude stays put) */
    for (int i = 0; i < FN; i++) {
        fx[i] = (float)(ibuf[i] & 0xffff) / 65536.0f - 0.5f;
        fy[i] = (float)((ibuf[i] >> 16) & 0xffff) / 65536.0f - 0.5f;
    }
    const float c = 0.99500416f, s = 0.09983342f;    /* cos, sin 0.1 */
    for (int it = 0; it < 200; it++)
        for (int i = 0; i < FN; i++) {
            float x = fx[i], y = fy[i];
            fx[i] = x * c - y * s;
            fy[i] = x * s + y * c;
        }
    u32 fa = 0;
    for (int i = 0; i < FN; i++) {
        union { float f; u32 u; } a = { fx[i] }, b = { fy[i] };
        fa = (fa * 31) ^ a.u ^ (b.u << 1);
    }
    h = mix(h ^ fa);
    /* double: a 40x40 matrix product, 4 times */
    for (int i = 0; i < MN; i++)
        for (int j = 0; j < MN; j++) {
            ma[i][j] = (double)(ibuf[i * MN + j] & 0xfffff) / 1048576.0 - 0.5;
            mb[i][j] = (double)(ibuf[IWORDS / 2 + i * MN + j] & 0xfffff) / 1048576.0 - 0.5;
        }
    for (int rep = 0; rep < 4; rep++) {
        for (int i = 0; i < MN; i++)
            for (int j = 0; j < MN; j++) {
                double acc = 0;
                for (int k = 0; k < MN; k++) acc += ma[i][k] * mb[k][j];
                mc[i][j] = acc;
            }
        for (int i = 0; i < MN; i++)
            for (int j = 0; j < MN; j++) ma[i][j] = mc[i][j] * 0.25;
    }
    for (int i = 0; i < MN; i++)
        for (int j = 0; j < MN; j++) {
            union { double d; u64 u; } v = { mc[i][j] };
            h = mix(h ^ v.u ^ ((u64)(i * MN + j) << 48));
        }
    return h;
}

struct shared {
    u64 golden[NSEEDS];           /* 0: not computed yet */
    u64 rounds[8], errors[8];
    u64 bad_seed, bad_got, bad_want;
};

static int worker(struct shared *sh, int id, u64 until, int burst)
{
    u64 mask = 1UL << id;
    sys3(SYS_sched_setaffinity, 0, sizeof mask, &mask);
    u64 rnd = mix(id + 1);
    for (u64 r = id; now_ms() < until; r++) {
        u64 seed = r % NSEEDS;
        u64 got = round_hash(seed) | 1;           /* never 0, so 0 can mean "not computed" */
        u64 want = 0;
        if (!__atomic_compare_exchange_n(&sh->golden[seed], &want, got, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
            && want != got) {
            __atomic_add_fetch(&sh->errors[id], 1, __ATOMIC_SEQ_CST);
            sh->bad_seed = seed; sh->bad_got = got; sh->bad_want = want;
            return 1;
        }
        __atomic_add_fetch(&sh->rounds[id], 1, __ATOMIC_SEQ_CST);
        if (burst) { rnd = mix(rnd); sleep_ms(1 + rnd % 40); }
    }
    return 0;
}

static long num(const char *s)
{
    long v = 0;
    if (!s || !*s) return -1;
    for (; *s; s++) { if (*s < '0' || *s > '9') return -1; v = v * 10 + (*s - '0'); }
    return v;
}

void cmain(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    long secs = 60, jobs = 4;
    int burst = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == 't' && !a[2] && i + 1 < argc) secs = num(argv[++i]);
        else if (a[0] == '-' && a[1] == 'j' && !a[2] && i + 1 < argc) jobs = num(argv[++i]);
        else if (a[0] == '-' && a[1] == 'b' && !a[2]) burst = 1;
        else secs = -1;
    }
    if (secs < 1 || jobs < 1 || jobs > 8) { puts1("usage: uvstress [-t seconds] [-j workers 1-8] [-b]\n"); die(2); }

    struct shared *sh = (struct shared *)sys6(SYS_mmap, 0, 4096, 3 /* RW */, 0x21 /* SHARED|ANON */, -1, 0);
    if ((long)sh < 0 && (long)sh > -4096) { puts1("uvstress: mmap failed\n"); die(2); }
    u64 until = now_ms() + (u64)secs * 1000;
    for (int id = 0; id < jobs; id++) {
        long pid = sys6(SYS_clone, 17 /* SIGCHLD */, 0, 0, 0, 0, 0);
        if (pid == 0) die(worker(sh, id, until, burst));
        if (pid < 0) { puts1("uvstress: fork failed\n"); die(2); }
    }
    int failed = 0;
    for (int id = 0; id < jobs; id++) {
        int st = 0;
        if (sys6(SYS_wait4, -1, (long)&st, 0, 0, 0, 0) < 0) { failed = 1; break; }
        if (st != 0) failed = 1;          /* exit 1 (a wrong result), or killed by a signal */
    }
    u64 rounds = 0, errors = 0;
    for (int id = 0; id < jobs; id++) { rounds += sh->rounds[id]; errors += sh->errors[id]; }
    puts1("uvstress: "); putu(jobs); puts1(" workers, "); putu(secs); puts1(burst ? " s burst, " : " s, ");
    putu(rounds); puts1(" rounds checked, ");
    if (!failed && rounds) { puts1("no errors: PASS\n"); die(0); }
    if (errors) {
        puts1("WRONG RESULT (seed "); putu(sh->bad_seed); puts1(": "); putx(sh->bad_got);
        puts1(" != "); putx(sh->bad_want); puts1("): FAIL\n");
    } else {
        puts1(rounds ? "a worker crashed: FAIL\n" : "no round finished: FAIL\n");
    }
    die(1);
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl cmain\n");
