#!/bin/sh
# build-es.sh [workdir]: builds ROCKNIXDS's patched EmulationStation (dii-ess-aye/emulationstation-rgds) for the RG DS
# and RG DS Plus without ROCKNIX's build system.
#
# ROCKNIX/emulationstation-next at the commit ROCKNIX 20260901 ships, plus dii-ess-aye/es-rgds-*.patch, compiled with
# clang + lld against an arm64 sysroot made of Ubuntu 24.04 (noble) packages. Their sonames and symbol versions are the
# ones ROCKNIX's libraries provide (libstdc++ from GCC 13, glibc 2.39 <= ROCKNIX's 2.41, CURL_OPENSSL_4, LIBUDEV_183,
# PULSE_0); the check at the end compares the result with what the previous binary needs.
#
# Needs: a Linux host with apt-get (Debian/Ubuntu), clang, lld, cmake, gettext (msgfmt), git, and network access to
# github.com and ports.ubuntu.com. Takes ~15 minutes on 4 cores.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-$HERE/build/es}
ES_REPO=https://github.com/ROCKNIX/emulationstation-next.git
ES_COMMIT=bccd7157
PATCHES="uiwidth bindings-clock carousel-repeat devkeys powersaver firstview lockdown rnds"
UBUNTU=noble
PKGS="libc6 libc6-dev linux-libc-dev libgcc-13-dev libgcc-s1 libstdc++-13-dev libstdc++6 libsdl2-dev libsdl2-2.0-0
      libsdl2-mixer-dev libsdl2-mixer-2.0-0 libfreetype-dev libfreetype6 libfreeimage-dev libfreeimage3
      libcurl4-openssl-dev libcurl4t64 libvlc-dev libvlc5 libvlccore9 libpulse-dev libpulse0 libpulse-mainloop-glib0
      libasound2-dev libasound2t64 libudev-dev libudev1 libegl-dev libegl1 libgles-dev libgles2 libgles1 libglvnd0
      libglvnd-dev libgl-dev libgl1 libglx-dev libglx0 rapidjson-dev zlib1g-dev zlib1g libpng-dev libpng16-16t64
      libbrotli1 libbz2-1.0 libx11-dev x11proto-dev libxcb1-dev libxau-dev libxdmcp-dev"

mkdir -p "$WORK"
cd "$WORK"

# ---- source ---------------------------------------------------------------------------------------------------
if [ ! -d src ]; then
    git clone -q --filter=blob:none "$ES_REPO" src
fi
cd src
git checkout -q -f "$ES_COMMIT"
git clean -qfdx
git submodule update -q --init
for p in $PATCHES; do
    git apply --whitespace=nowarn "$HERE/dii-ess-aye/es-rgds-$p.patch"
done
cd ..

# ---- arm64 sysroot from Ubuntu's ports archive (its own apt config: the host's apt is not touched) -----------------
if [ ! -f sysroot/.done ]; then
    A=$WORK/apt
    mkdir -p $A/etc/apt/apt.conf.d $A/var/lib/apt/lists/partial $A/var/cache/apt/archives/partial $A/var/lib/dpkg debs
    touch $A/var/lib/dpkg/status
    for s in $UBUNTU $UBUNTU-updates; do
        echo "deb [arch=arm64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] http://ports.ubuntu.com/ubuntu-ports $s main universe multiverse"
    done > $A/etc/apt/sources.list
    cat > $A/apt.conf <<EOF
Dir "$A/";
Dir::State "$A/var/lib/apt/";
Dir::State::status "$A/var/lib/dpkg/status";
Dir::Cache "$A/var/cache/apt/";
Dir::Etc "$A/etc/apt/";
APT::Architecture "arm64";
APT::Architectures { "arm64"; };
Acquire::Languages "none";
EOF
    APT_CONFIG=$A/apt.conf apt-get -qq update
    (cd debs && APT_CONFIG=$A/apt.conf apt-get -qq download $PKGS)
    rm -rf sysroot && mkdir sysroot
    for d in debs/*.deb; do dpkg-deb -x "$d" sysroot; done
    ln -sfn usr/lib sysroot/lib
    touch sysroot/.done
fi

cat > aarch64.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT $WORK/sysroot)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--allow-shlib-undefined")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH $WORK/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "$WORK/sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:$WORK/sysroot/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "$WORK/sysroot")
EOF

# ---- build: ROCKNIX's options (projects/ROCKNIX/packages/ui/emulationstation/package.mk), DEVICE=RK3566 -------------
mkdir -p build && cd build
DEVICE=RK3566 cmake ../src -DCMAKE_TOOLCHAIN_FILE=$WORK/aarch64.cmake -DCMAKE_BUILD_TYPE=Release -DROCKNIX=1 \
    -DDISABLE_KODI=1 -DENABLE_FILEMANAGER=0 -DCEC=0 -DENABLE_PULSE=1 -DGLES3=1 >/dev/null
make -j"$(nproc)" emulationstation
cd ..
OUT=$WORK/emulationstation-rgds
llvm-strip -o "$OUT" src/emulationstation

# ---- check: the libraries and symbol versions it needs are the ones ROCKNIX's ES binary needs ----------------------
need() { llvm-readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort; }
vers() { llvm-readelf -V "$1" | grep -oE '(GLIBC|GLIBCXX|CXXABI|GCC|CURL_OPENSSL|LIBUDEV|PULSE)_[0-9A-Z_.]*' | sort -u; }
OLD=$HERE/dii-ess-aye/emulationstation-rgds
if [ -f "$OLD" ]; then
    extra=$(need "$OUT" | comm -23 - "$(need "$OLD" > need.old; echo need.old)")
    [ -z "$extra" ] || { echo "needs libraries the previous build didn't: $extra"; exit 1; }
    vextra=$(vers "$OUT" | comm -23 - "$(vers "$OLD" > vers.old; echo vers.old)")
    [ -z "$vextra" ] || { echo "needs symbol versions the previous build didn't: $vextra"; exit 1; }
fi
echo "built $OUT"
echo "install: cp $OUT $HERE/dii-ess-aye/emulationstation-rgds"
