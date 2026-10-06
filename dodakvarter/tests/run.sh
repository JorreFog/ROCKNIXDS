#!/bin/sh
# tests/run.sh: Döda Kvarter's tests, on any Linux machine with a C compiler (no display needed).
#   1. 500 generated towns: everything reachable, spawns, machines; Black Ops' round formulas
#   2. the bot plays whole runs headless (every season, several towns) under AddressSanitizer and UBSan
#   3. a run saved and loaded goes on exactly as if it had never stopped; quitting and starting again continues it
#   4. every sound, the music and the ambience come out of the mixer neither silent nor clipped
#   5. the tools the screenshots and the map pictures are made with build
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
SRCS="src/game.c src/mapgen.c src/world.c src/props.c src/render.c src/hud.c src/menu.c src/weapons.c src/zombies.c
      src/loot.c src/inter.c src/audio.c src/save.c src/lang.c src/data.c src/bot.c src/art.c src/art_data.c src/gfx.c
      src/font.c src/png.c src/plat.c src/plat_headless.c src/plat_kms.c src/plat_sdl.c src/input_evdev.c src/audio_alsa.c"
INC="-I/usr/include/libdrm -I/usr/include/SDL2"
mkdir -p build
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' > build/san.c
$CC $SAN -o build/san build/san.c 2>/dev/null && ./build/san 2>/dev/null || { SAN=; echo "(no sanitizers with $CC)"; }
$CC -O1 -g -w $INC -o build/test_map tests/test_map.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $SAN $INC -o build/dk-test src/main.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $SAN $INC -o build/test_save tests/test_save.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/scene tests/scene.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/sounds tests/sounds.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/mapview tests/mapview.c $SRCS -ldrm -lm -lpthread -ldl
echo "== towns"
./build/test_map ${MAPS:-500}
echo "== runs (bot, headless)"
T=$(mktemp -d)
for s in 1 2 3 4 5 6; do
    season=$((s % 3))
    DK_DATA=$T/$s ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --seed $s --season $season --start --bot \
        --frames ${FRAMES:-7200} >/dev/null 2>$T/err$s || { echo "run $s failed:"; cat $T/err$s; exit 1; }
    grep -q "runtime error\|AddressSanitizer" $T/err$s && { echo "run $s:"; cat $T/err$s; exit 1; }
    r=$(grep -o 'round [0-9]*' $T/$s/dodakvarter.log | tail -n1)
    echo "seed $s, season $season: ok ($r)"
done
# a run that starts deep (round 25, everything open, the power coming on, money, nothing hurts) for the late game
DK_DATA=$T/late DK_DEBUG_ROUND=25 DK_DEBUG_KR=60000 DK_DEBUG_POWER=1 DK_DEBUG_OPEN=1 DK_DEBUG_GOD=1 ASAN_OPTIONS=detect_leaks=0 \
    ./build/dk-test --backend headless --seed 77 --start --bot --frames 5400 >/dev/null 2>$T/errlate || { cat $T/errlate; exit 1; }
grep -q "runtime error\|AddressSanitizer" $T/errlate && { cat $T/errlate; exit 1; }
echo "late game: ok"
echo "== saving"
ASAN_OPTIONS=detect_leaks=0 ./build/test_save
# quit in the middle of a run (as the exit hotkey does), start again, and the bot continues it from the title
DK_DATA=$T/cont ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --seed 9 --start --bot --frames 4000 >/dev/null 2>$T/errc1 || { cat $T/errc1; exit 1; }
grep -q "saved the run" $T/cont/dodakvarter.log || { echo "the run wasn't saved on quitting"; exit 1; }
DK_DATA=$T/cont ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --bot --frames 1200 >/dev/null 2>$T/errc2 || { cat $T/errc2; exit 1; }
grep -q "continuing the run" $T/cont/dodakvarter.log || { echo "the saved run wasn't continued"; cat $T/cont/dodakvarter.log; exit 1; }
grep -q "runtime error\|AddressSanitizer" $T/errc1 $T/errc2 && { cat $T/errc1 $T/errc2; exit 1; }
echo "continue after quitting: ok"
echo "== sounds"
./build/sounds $T/sounds | tail -n1
rm -rf "$T"
echo "all tests passed"
