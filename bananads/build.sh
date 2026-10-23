#!/bin/sh
# build.sh: builds the Banana 2D engine and its test harness against melonDS.
#   sh build.sh              this machine: build/host/banana2d-test
#   sh build.sh aarch64      the RG DS / RG DS Plus, static: build/aarch64/banana2d-test
#   sh build.sh test         host build, then the scenes, the fuzzer and the benchmark
# MELONDS=path uses that melonDS tree (with melonds-core.patch applied) instead of fetching the pinned one.
set -e
cd "$(dirname "$0")"
MELONDS=${MELONDS:-third_party/melonDS}
[ -e "$MELONDS/src/GPU_Soft.h" ] || sh fetch-melonds.sh "$MELONDS"
GEN=
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
case "$1" in
aarch64)
    cmake -S . -B build/aarch64 $GEN -DCMAKE_BUILD_TYPE=Release -DMELONDS_SRC="$MELONDS" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
    cmake --build build/aarch64 --target banana2d-test
    ls -l build/aarch64/banana2d-test
    ;;
test)
    cmake -S . -B build/host $GEN -DCMAKE_BUILD_TYPE=Release -DMELONDS_SRC="$MELONDS"
    cmake --build build/host --target banana2d-test
    ./build/host/banana2d-test "$@"
    ;;
*)
    cmake -S . -B build/host $GEN -DCMAKE_BUILD_TYPE=Release -DMELONDS_SRC="$MELONDS"
    cmake --build build/host --target banana2d-test
    ls -l build/host/banana2d-test
    ;;
esac
