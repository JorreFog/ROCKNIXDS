#!/bin/sh
# kmspace.sh <tag> <secs> [ENV=val...]: HeartGold boot intro (no savestate, no input) through the test libdsflip.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic
TAG=$1 SECS=$2; shift 2
cd $CFGD
( export SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/pace-$TAG.log "$@"
  LD_PRELOAD=$D/libdsflip-test.so exec ./drastic.real "/storage/roms/nds/Pokemon - HeartGold Version (USA).nds" >$L/pace-$TAG.out 2>&1 ) &
P=$!
sleep $SECS
kill $P; sleep 1; kill -9 $P 2>/dev/null
