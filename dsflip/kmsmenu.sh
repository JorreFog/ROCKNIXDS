#!/bin/sh
# kmsmenu.sh: under KMS, run a dsstress ROM through libdsflip, open DraStic's menu, dump both panels.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic
cd $CFGD
SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/menu.dsflip.log \
  LD_PRELOAD=$D/libdsflip.so ./drastic.real $D/roms/dsstress-L3.nds >$L/menu.out 2>&1 &
P=$!
sleep 6; kill -USR2 $P; sleep 1; mv $L/scan0.raw $L/game0.raw; mv $L/scan1.raw $L/game1.raw
python3 $D/padkey.py 316 0.3; sleep 2
kill -USR2 $P; sleep 1
python3 $D/padkey.py 316 0.3; sleep 2          # MODE again: back to the game?
kill $P; sleep 1; kill -9 $P 2>/dev/null
