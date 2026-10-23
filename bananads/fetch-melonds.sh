#!/bin/sh
# fetch-melonds.sh [dir]: a melonDS source tree at the commit in MELONDS_COMMIT, with melonds-core.patch
# applied (the two friend declarations the Banana renderer needs). Default dir: third_party/melonDS.
set -e
cd "$(dirname "$0")"
DST=${1:-third_party/melonDS}
REV=$(grep -v '^#' MELONDS_COMMIT | head -n1)
if [ -e "$DST/src/GPU_Soft.h" ]; then
    echo "$DST already exists"
else
    mkdir -p "$DST"
    git init -q "$DST"
    git -C "$DST" remote add origin https://github.com/melonDS-emu/melonDS.git
    git -C "$DST" fetch -q --depth 1 origin "$REV"
    git -C "$DST" checkout -q FETCH_HEAD
    git -C "$DST" apply "$PWD/melonds-core.patch"
    echo "melonDS $REV in $DST, patched"
fi
