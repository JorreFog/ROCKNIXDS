#!/bin/sh
# taptest.sh "x y" ...: HeartGold under libdsflip. The savestate resumes inside the "save the game?" dialog,
# so first back out with B (No) until the overworld. Then per coordinate: X opens the field menu, a synthetic
# tap is injected with exactly those mouse coordinates, both panels are snapshotted, B backs out.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic CFG=$CFGD/config/drastic.cfg
cp $CFG /tmp/drastic.cfg.tap; sed -i "s/^hires_3d = .*/hires_3d = 1/" $CFG
cd $CFGD
SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_TAP_FIFO=1 DSFLIP_LOG=$L/tap.dsflip.log \
  LD_PRELOAD=$D/libdsflip.so ./drastic.real "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" >$L/tap.out 2>&1 &
P=$!
sleep 8; cp /tmp/drastic.cfg.tap $CFG
python3 $D/padkey.py 312 0.6; sleep 3
for k in 1 2 3 4; do python3 $D/padkey.py 305 0.2; sleep 1.2; done          # B: "No" to save, close menu
kill -USR2 $P; sleep 0.5; mv $L/scan1.raw $L/tap-start.raw
i=0
for c in "$@"; do
  python3 $D/padkey.py 307 0.2; sleep 1.5                                    # X: field menu
  kill -USR2 $P; sleep 0.5; mv $L/scan1.raw $L/tap$i-before.raw
  echo "$c" > /tmp/dsflip-tap; sleep 2
  kill -USR2 $P; sleep 0.5; mv $L/scan1.raw $L/tap$i-after.raw
  echo "try $i: $c" >> $L/tap.dsflip.log
  for k in 1 2 3; do python3 $D/padkey.py 305 0.2; sleep 1.2; done          # B: back to the overworld
  i=$((i+1))
done
kill $P; sleep 1; kill -9 $P 2>/dev/null
