#!/bin/sh
# build.sh: builds Döda Kvarter.
#   sh build.sh                      this machine (build/dodakvarter): play it on a desktop, run the tests
#   sh build.sh aarch64 <sysroot>    the RG DS (build/dodakvarter-aarch64)
# <sysroot>: an aarch64 glibc sysroot with libdrm's and SDL2's headers, e.g. Debian or Ubuntu arm64 packages libc6,
# libc6-dev, linux-libc-dev, libdrm-dev, libdrm2, libgcc-*-dev, libgcc-s1 and libsdl2-dev (headers only) extracted
# into one directory, then `ln -s usr/lib <sysroot>/lib` (as SuperDrastic's build.sh). Needs clang and lld.
# SDL2 and libasound are loaded at run time; the binary links libdrm, libm and libpthread only.
set -e
cd "$(dirname "$0")"
V=$(cat VERSION 2>/dev/null || echo dev)
SRCS="src/main.c src/game.c src/mapgen.c src/world.c src/props.c src/render.c src/hud.c src/menu.c src/weapons.c
      src/zombies.c src/loot.c src/inter.c src/audio.c src/save.c src/lang.c src/data.c src/bot.c src/art.c
      src/art_data.c src/gfx.c src/font.c src/png.c src/plat.c src/plat_headless.c src/plat_kms.c src/plat_sdl.c src/selftest.c
      src/input_evdev.c src/audio_alsa.c src/boss.c"
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation"
mkdir -p build
case "$1" in
aarch64)
    SR=${2:?usage: build.sh aarch64 <sysroot>}
    clang --target=aarch64-linux-gnu --sysroot="$SR" -fuse-ld=lld -O2 -mcpu=cortex-a55 -ffp-contract=off $WARN \
        -DDK_VERSION="\"$V\"" -I"$SR/usr/include/libdrm" -I"$SR/usr/include/SDL2" -o build/dodakvarter-aarch64 $SRCS \
        -ldrm -lm -lpthread -ldl
    llvm-strip --strip-unneeded build/dodakvarter-aarch64
    ls -l build/dodakvarter-aarch64
    ;;
*)
    CC=${CC:-cc}
    $CC -O2 -g -ffp-contract=off $WARN -DDK_VERSION="\"$V\"" $(pkg-config --cflags libdrm 2>/dev/null || echo -I/usr/include/libdrm) -I/usr/include/SDL2 \
        -o build/dodakvarter $SRCS -ldrm -lm -lpthread -ldl
    ls -l build/dodakvarter
    ;;
esac
