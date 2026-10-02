/* preload-guard.so: listed after libdsflip.so in DraStic's LD_PRELOAD, it takes LD_PRELOAD out of DraStic's
 * environment before DraStic or libdsflip runs anything, so the processes DraStic starts don't load libdsflip.
 *
 * SuperDrastic 0.3.0-beta.3 (the RG DS build) watches the volume keys through `pactl subscribe` and
 * `wpctl get-volume`, started with popen(). Those shells and tools inherited the preload: libdsflip came up in
 * each of them, tried for 3 s to take the display the game already had, rotated dsflip.log out from under the
 * game and wrote "passthrough" over the session's verdict (every RG DS performance log since beta 5 ends at that
 * line). SuperDrastic's own guard (0.3.0-beta.2 "the RG DS Plus round", e60dc80) does the same from inside.
 *
 * glibc runs the preloaded objects' constructors last-listed first, so this one runs before libdsflip's.
 * Build: clang --target=aarch64-linux-gnu --sysroot=<arm64 sysroot> -fuse-ld=lld -O2 -shared -fPIC \
 *        -o preload-guard.so preload-guard.c && llvm-strip preload-guard.so
 */
#include <stdlib.h>

__attribute__((constructor)) static void rocknixds_preload_guard(void)
{
    unsetenv("LD_PRELOAD");
}
