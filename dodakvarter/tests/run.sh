#!/bin/sh
# tests/run.sh: Döda Kvarter's tests, on any Linux machine with a C compiler (no display needed).
#   1. 500 generated towns: everything reachable, spawns, machines; Black Ops' round formulas
#   2. rules played out one by one (the elstängsel, a zombie in a window, a dry gun); the bot plays whole runs
#      headless (every season, several towns) under AddressSanitizer and UBSan, and sixteen more towns where no
#      round may stall
#   3. a run saved and loaded goes on exactly as if it had never stopped; quitting and starting again continues it
#   4. every sound, the music and the ambience come out of the mixer neither silent nor clipped
#   5. the tools the screenshots and the map pictures are made with build
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
SRCS="src/game.c src/mapgen.c src/world.c src/props.c src/render.c src/hud.c src/menu.c src/weapons.c src/zombies.c
      src/loot.c src/inter.c src/audio.c src/save.c src/lang.c src/data.c src/bot.c src/art.c src/art_data.c src/gfx.c
      src/font.c src/png.c src/plat.c src/plat_headless.c src/plat_kms.c src/plat_sdl.c src/selftest.c src/input_evdev.c src/audio_alsa.c"
INC="-I/usr/include/libdrm -I/usr/include/SDL2"
mkdir -p build
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' > build/san.c
$CC $SAN -o build/san build/san.c 2>/dev/null && ./build/san 2>/dev/null || { SAN=; echo "(no sanitizers with $CC)"; }
$CC -O1 -g -w $INC -o build/test_map tests/test_map.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $SAN $INC -o build/dk-test src/main.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $SAN $INC -o build/test_save tests/test_save.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $SAN $INC -o build/test_rules tests/test_rules.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O2 -w $INC -o build/dk-fast src/main.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/scene tests/scene.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/sounds tests/sounds.c $SRCS -ldrm -lm -lpthread -ldl
$CC -O1 -g -w $INC -o build/mapview tests/mapview.c $SRCS -ldrm -lm -lpthread -ldl
echo "== towns"
./build/test_map ${MAPS:-500}
echo "== rules"
ASAN_OPTIONS=detect_leaks=0 ./build/test_rules
echo "== runs (bot, headless)"
T=$(mktemp -d)
for s in 1 2 3 4 5 6; do
    season=$((s % 3))
    DK_DATA=$T/$s ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --seed $s --season $season --start --bot \
        --frames ${FRAMES:-7200} >/dev/null 2>$T/err$s || { echo "run $s failed:"; cat $T/err$s; exit 1; }
    grep -q "runtime error\|AddressSanitizer" $T/err$s && { echo "run $s:"; cat $T/err$s; exit 1; }
    r=$(awk '/^round [0-9]+:/ { if ($2 + 0 > m) m = $2 + 0 } END { print m + 0 }' $T/$s/dodakvarter.log)
    echo "seed $s, season $season: ok (reached round $r)"
done
# a run that starts deep (round 25, everything open, the power coming on, money, nothing hurts) for the late game
DK_DATA=$T/late DK_DEBUG_ROUND=25 DK_DEBUG_KR=60000 DK_DEBUG_POWER=1 DK_DEBUG_OPEN=1 DK_DEBUG_GOD=1 ASAN_OPTIONS=detect_leaks=0 \
    ./build/dk-test --backend headless --seed 77 --start --bot --frames 5400 >/dev/null 2>$T/errlate || { cat $T/errlate; exit 1; }
grep -q "runtime error\|AddressSanitizer" $T/errlate && { cat $T/errlate; exit 1; }
echo "late game: ok"
# no round may stall: the bot plays sixteen more towns (dying and starting over) and no round may take three minutes
echo "== stalled rounds"
for s in $(seq 201 216); do
    DK_DATA=$T/st$s ./build/dk-fast --backend headless --seed $s --season $((s % 3)) --start --bot --frames ${STALL_FRAMES:-10800} >/dev/null 2>&1 &
    [ $((s % 4)) -eq 0 ] && wait
done
wait
for s in $(seq 201 216); do
    awk -v seed=$s '/^round [0-9]+: [0-9]+ s/ { r = $2 + 0; t = $3 + 0; if (r == pr + 1 && t - pt > worst) worst = t - pt; pr = r; pt = t }
        END { if (worst > 180) { printf "seed %d: a round took %d s\n", seed, worst; exit 1 } }' $T/st$s/dodakvarter.log || exit 1
done
echo "no stalled rounds in 16 towns"
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
