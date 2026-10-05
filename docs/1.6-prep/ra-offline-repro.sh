#!/bin/sh
# ra-offline.sh <rom-substring> <block 0|1> [seconds]: start a game as a test launch (no resume, no play stats), with the
# RetroAchievements servers unreachable (1) or not (0); libdsflip's presents per second; if DraStic stops presenting, what
# every one of its threads is doing (gdb). The game is ended and the routes are removed afterwards.
D=/storage/.config/drastic/dsflip
ROM=$(ls /storage/roms/nds/*.nds | grep -i -- "$1" | head -n1); [ -n "$ROM" ] || { echo "no rom"; exit 1; }
systemctl is-active -q dsflip-game && { echo "a game is running: leaving it alone"; exit 2; }
NETS="104.16.0.0/12 172.64.0.0/13"      # Cloudflare, where retroachievements.org lives
cleanup() { for n in $NETS; do ip route del unreachable $n 2>/dev/null; done; [ -n "$KEEPFLAG" ] || rm -f /tmp/rocknixds-testing; }
trap cleanup EXIT; trap 'exit 1' HUP INT TERM
[ "$2" = 1 ] && for n in $NETS; do ip route add unreachable $n; done
[ -n "$REAL" ] || touch /tmp/rocknixds-testing
i=0; while ! curl -s -m 1 localhost:1234/isIdle | grep -q true && [ $i -lt 40 ]; do sleep 0.5; i=$((i+1)); done
curl -s -m 5 -X POST --data-binary "$ROM" -w "launch request: http %{http_code}\n" localhost:1234/launch
i=0; while ! systemctl is-active -q dsflip-game && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
echo "game unit: $(systemctl is-active dsflip-game) ($(basename "$ROM" | cut -c1-40))"
n=0; zero=0
while [ $n -lt ${3:-14} ]; do
    sleep 1; n=$((n+1))
    l=$(grep -a 'present/s' $D/dsflip.log | tail -n1 | cut -c10-41)
    echo "t+$n: $l"
    case "$l" in *"present/s=0.0"*) zero=$((zero+1)) ;; *) zero=0 ;; esac
    [ $zero -ge 4 ] && break
done
echo "--- libdsflip's RetroAchievements, resume and overlay lines"; grep -a '^\[ra\]\|^\[rc\]\|^\[ui\]\|^\[resume\]' $D/dsflip.log | grep -v "DS RAM candidate" | head -14 | cut -c1-170
P=$(pidof drastic || pidof drastic.real)
if [ $zero -ge 4 ] && [ -n "$P" ]; then
    echo "=== DraStic (pid $P) has stopped presenting. Threads: tid, name, state, cpu ticks, syscall"
    for t in /proc/$P/task/*; do echo "${t##*/} $(cat $t/comm) $(sed 's/.*) //' $t/stat | awk '{print $1, $12+$13}') | $(cut -c1-60 $t/syscall 2>/dev/null)"; done
    echo "=== backtraces"
    timeout 90 gdb -p $P -batch -ex "set pagination off" -ex "thread apply all bt 16" 2>&1 | grep -v '^\[New LWP\|^warning\|^Reading\|^$\|^\[Thread debugging\|^Using host\|No such file' | cut -c1-210 | head -170
    grep -a 'libdsflip\|drastic' /proc/$P/maps | head -8
fi
[ -n "$P" ] && kill -9 $P
i=0; while systemctl is-active -q dsflip-game && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
echo "game unit after: $(systemctl is-active dsflip-game)"
