#!/bin/sh
# build.sh: builds the ROCKNIXDS Store app (src/) on Döda Kvarter's platform layer (../dodakvarter/src: the panels
# through DRM/KMS, a window through SDL2 loaded at run time, headless PNGs for the tests).
#   sh build.sh                      this machine (build/store): try it on a desktop, run the tests
#   sh build.sh aarch64 <sysroot>    the RG DS and RG DS Plus (build/store-aarch64)
# <sysroot>: as Döda Kvarter's build.sh (an aarch64 glibc sysroot with libdrm's and SDL2's headers). Needs clang and lld.
set -e
cd "$(dirname "$0")"
V=$(cat VERSION 2>/dev/null || echo dev)
DK=../dodakvarter/src
SRCS="src/main.c src/job.c src/catalog.c
      $DK/plat.c $DK/plat_kms.c $DK/plat_sdl.c $DK/plat_headless.c $DK/input_evdev.c $DK/audio_alsa.c
      $DK/gfx.c $DK/font.c $DK/png.c"
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation"
mkdir -p build
case "$1" in
aarch64)
    SR=${2:?usage: build.sh aarch64 <sysroot>}
    clang --target=aarch64-linux-gnu --sysroot="$SR" -fuse-ld=lld -O2 -mcpu=cortex-a55 $WARN \
        -DSTORE_VERSION="\"$V\"" -I$DK -I"$SR/usr/include/libdrm" -I"$SR/usr/include/SDL2" -o build/store-aarch64 $SRCS \
        -ldrm -lm -lpthread -ldl
    llvm-strip --strip-unneeded build/store-aarch64
    ls -l build/store-aarch64
    ;;
*)
    CC=${CC:-cc}
    $CC -O2 -g $WARN ${STORE_CFLAGS:-} -DSTORE_VERSION="\"$V\"" -I$DK \
        $(pkg-config --cflags libdrm 2>/dev/null || echo -I/usr/include/libdrm) -I/usr/include/SDL2 \
        -o build/store $SRCS -ldrm -lm -lpthread -ldl
    ls -l build/store
    ;;
esac
