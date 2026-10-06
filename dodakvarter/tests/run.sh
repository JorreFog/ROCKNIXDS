#!/bin/sh
# tests/run.sh: Döda Kvarter's tests, on any Linux machine with a C compiler (no display needed).
#   1. 500 generated towns: everything reachable, spawns, machines; Black Ops' round formulas
#   2. rules played out one by one (the elstängsel, a zombie in a window, a dry gun, the bosses); the bot plays whole
#      runs headless (every season, several towns) under AddressSanitizer and UBSan, fights each boss, and plays
#      sixteen more towns where no round may stall
#   3. a run saved and loaded goes on exactly as if it had never stopped; quitting and starting again continues it;
#      a monkey presses everything everywhere under the sanitizers
#   4. every sound, the music and the ambience come out of the mixer neither silent nor clipped
#   5. the tools the screenshots and the map pictures are made with build
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
SRCS="src/game.c src/mapgen.c src/world.c src/props.c src/render.c src/hud.c src/menu.c src/weapons.c src/zombies.c
      src/loot.c src/inter.c src/audio.c src/save.c src/lang.c src/data.c src/bot.c src/art.c src/art_data.c src/gfx.c
      src/font.c src/png.c src/plat.c src/plat_headless.c src/plat_kms.c src/plat_sdl.c src/selftest.c src/input_evdev.c src/audio_alsa.c src/boss.c"
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
# the bosses: the bot fights each one (round 20, alone, at 40% of its health, with a legendary Ak 5) under the
# sanitizers, and must fell it
echo "== bosses"
for k in 0 1 2 3 4 5 6 7; do
    ( DK_DATA=$T/boss$k DK_DEBUG_BOSS=$k DK_DEBUG_BOSS_HP=0.4 DK_DEBUG_BOSS_ONLY=1 DK_DEBUG_GUN=3 DK_DEBUG_ROUND=20 DK_DEBUG_KR=60000 DK_DEBUG_POWER=1 DK_DEBUG_OPEN=1 DK_DEBUG_GOD=1 \
        ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --seed $((40 + k)) --start --bot --frames ${BOSS_FRAMES:-9000} >/dev/null 2>$T/errboss$k \
        || echo "exit $?" >> $T/errboss$k ) &
done
wait
for k in 0 1 2 3 4 5 6 7; do
    grep -q "runtime error\|AddressSanitizer\|^exit" $T/errboss$k && { cat $T/errboss$k; exit 1; }
    grep "boss .* slain" $T/boss$k/dodakvarter.log || { echo "boss $k wasn't slain:"; grep boss $T/boss$k/dodakvarter.log; exit 1; }
done
# no round may stall: the bot plays sixteen more towns for six minutes each (dying and starting over), and no round
# may take two and a half minutes, the one still going when it stops included (its rounds take a minute or so)
echo "== stalled rounds"
for s in $(seq 201 216); do
    ( DK_DATA=$T/st$s ./build/dk-fast --backend headless --seed $s --season $((s % 3)) --start --bot --frames ${STALL_FRAMES:-21600} \
        >/dev/null 2>&1 || echo "exit $?" > $T/st$s.failed ) &
    [ $((s % 4)) -eq 0 ] && wait
done
wait
for s in $(seq 201 216); do
    [ -f $T/st$s.failed ] && { echo "seed $s: $(cat $T/st$s.failed)"; exit 1; }
    awk -v seed=$s '/^round [0-9]+: [0-9]+ s/ { r = $2 + 0; t = $3 + 0; if (r == pr + 1 && t - pt > worst) { worst = t - pt; wr = pr } pr = r; pt = t }
        /^quit during round [0-9]+ at [0-9]+ s/ { if ($6 - pt > worst) { worst = $6 - pt; wr = pr } }
        END { if (worst > 150) { printf "seed %d: round %d took %d s\n", seed, wr, worst; exit 1 } }' $T/st$s/dodakvarter.log || exit 1
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
# a monkey: random buttons, sticks and touches on both screens, through every menu and the play, under the
# sanitizers; twice in each data directory, the second time continuing what the first one left
echo "== monkey"
for s in 1 2 3 4; do
    ( for pass in 1 2; do
        DK_DATA=$T/mk$s ASAN_OPTIONS=detect_leaks=0 ./build/dk-test --backend headless --monkey $((s * 10 + pass)) \
            --seed $((s * 10 + pass)) --frames ${MONKEY_FRAMES:-12000} >/dev/null 2>>$T/mkerr$s || echo "exit $?" >> $T/mkerr$s
    done ) &
done
wait
for s in 1 2 3 4; do
    grep -q "runtime error\|AddressSanitizer\|^exit" $T/mkerr$s && { echo "monkey $s:"; cat $T/mkerr$s; exit 1; }
    logs="$T/mk$s/dodakvarter.log.1 $T/mk$s/dodakvarter.log"   # (each start keeps the one before as .1)
    echo "monkey $s: ok ($(cat $logs | grep -c 'game over') runs ended, $(cat $logs | grep -c 'continuing') continued, $(cat $logs | grep -c 'gave up the saved') given up on the title)"
done
echo "== sounds"
./build/sounds $T/sounds > $T/sounds.txt || { cat $T/sounds.txt; exit 1; }
grep "long" $T/sounds.txt || true; tail -n1 $T/sounds.txt
rm -rf "$T"
echo "all tests passed"
