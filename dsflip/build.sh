#!/bin/sh
# build.sh <sysroot>: builds libdsflip.so (display path + touch + RetroAchievements) for the RG DS.
# <sysroot>: an aarch64 glibc >= 2.38 sysroot with libdrm headers + libdrm.so. One way to make it (Debian trixie
# arm64 packages): libc6, libc6-dev, linux-libc-dev, libdrm-dev, libgcc-14-dev extracted into one directory,
# plus `ln -s usr/lib <sysroot>/lib`, libdrm.so.2 and libgcc_s.so.1 copied from the device.
set -e
SR=${1:?usage: build.sh <aarch64-sysroot>}
cd "$(dirname "$0")"
RC=third_party/rcheevos
SRCS="dsflip.c shader.c audio.c ra.c ui.c cpugov.c $(ls $RC/src/rc_client.c $RC/src/rc_compat.c $RC/src/rc_util.c $RC/src/rc_version.c \
      $RC/src/rcheevos/*.c $RC/src/rapi/*.c $RC/src/rhash/*.c)"
V=$(cat ../VERSION 2>/dev/null || echo dev)
clang --target=aarch64-linux-gnu --sysroot="$SR" -fuse-ld=lld -shared -fPIC -O2 -Wall -Wno-unused-function \
      -DDSFLIP_VERSION="\"$V\"" -DRC_DISABLE_LUA -DRC_CLIENT_SUPPORTS_HASH -I"$SR/usr/include/libdrm" -I$RC/include -I$RC/src -Ithird_party/stb \
      -o libdsflip.so $SRCS -ldrm -lpthread -lm
llvm-strip --strip-unneeded libdsflip.so
ls -l libdsflip.so
