#!/bin/sh
# The Store app's tests on a computer: built with AddressSanitizer and UBSan, it runs headless against a handheld in a
# folder (sandbox.py) and is driven by a script, as a person would: refresh, the tabs, installing an app ROCKNIXDS
# doesn't know (its tile appears), updating Döda Kvarter over the copy ROCKNIXDS installed (its high scores stay),
# removing an app. The package manager's own tests are in ../../tests/test_store.py.
set -e
cd "$(dirname "$0")/.."
STORE_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" sh build.sh >/dev/null
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 tests/sandbox.py "$T/hh" > "$T/env"
. "$T/env"
R=$RNDS_STORE_ROOT/storage/.config
export DK_DATA=$T/data ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
# 1. the first start refreshes; Apps tab; Hello Handheld is the third app: install it; Games: update Döda Kvarter
./build/store --backend headless --script "job; wait 2; shot $T/home.png; right; down; down; a; job; wait 2; shot $T/done.png;
    a; wait 2; left; a; job; a; wait 2; shot $T/games.png; quit"
grep -qx 1.0.0 "$R/rocknixds/hello/VERSION" || { echo "FAIL: hello wasn't installed"; exit 1; }
grep -q '<name>hello</name>' "$R/emulationstation/es_systems_rocknixds-store.cfg" || { echo "FAIL: no tile for hello"; exit 1; }
grep -qx 0.3.0 "$R/rocknixds/dodakvarter/VERSION" || { echo "FAIL: Döda Kvarter wasn't updated"; exit 1; }
grep -q JRF "$R/rocknixds/dodakvarter/data/scores.txt" || { echo "FAIL: Döda Kvarter's high scores went"; exit 1; }
for f in home done games; do [ -s "$T/$f.png" ] || { echo "FAIL: no $f.png"; exit 1; }; done
# 2. the Installed tab: remove Hello Handheld (X, then A to confirm), at the RG DS Plus' size
RNDS_STORE_NO_REFRESH=1 ./build/store --backend headless --size 1024x768 --script "right; right; down; x; a; job; a; wait 2; shot $T/plus.png; quit"
[ ! -e "$R/rocknixds/hello/hello" ] || { echo "FAIL: hello wasn't removed"; exit 1; }
[ ! -e "$R/emulationstation/es_systems_rocknixds-store.cfg" ] || { echo "FAIL: hello's tile stayed"; exit 1; }
grep -qx 0.3.0 "$R/rocknixds/dodakvarter/VERSION" || { echo "FAIL: the wrong app went"; exit 1; }
[ -s "$T/plus.png" ] || { echo "FAIL: no plus.png"; exit 1; }
# 3. the package installs as the Store installs itself
sh build.sh >/dev/null
sh tools/package.sh build/store "$T/dist" >/dev/null
tar tzf "$T"/dist/rocknixds-store-*-aarch64.tar.gz | grep -qx 'store/rocknixds-store' || { echo "FAIL: package"; exit 1; }
echo "store tests: OK"
