#!/bin/sh
# cursortest.sh "x y" ...: stress ROM under libdsflip; inject taps at the given mouse coordinates and log where
# DraStic draws its stylus cursor for each (no game state involved, nothing is saved).
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic
cd $CFGD
SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_TAP_FIFO=1 DSFLIP_CURSOR_LOG=1 DSFLIP_LOG=$L/cursor.dsflip.log \
  LD_PRELOAD=$D/libdsflip.so ./drastic.real $D/roms/dsstress-L1.nds >$L/cursor.out 2>&1 &
P=$!
sleep 7
for c in "$@"; do echo "$c" > /tmp/dsflip-tap; sleep 1; done
kill $P; sleep 1; kill -9 $P 2>/dev/null
