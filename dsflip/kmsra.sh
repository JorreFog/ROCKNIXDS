#!/bin/sh
# kmsra.sh <rom> <secs>: RetroAchievements self-test under KMS (DSFLIP_RA_TEST=1: hash, RAM discovery, test toast).
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic
cd $CFGD
SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_RA_TEST=1 DSFLIP_LOG=$L/ra.dsflip.log \
  LD_PRELOAD=$D/libdsflip-ra.so ./drastic.real "$1" >$L/ra.out 2>&1 &
P=$!
sleep 5; kill -USR2 $P; sleep 1; cp $L/toast.raw $L/toast-test.raw 2>/dev/null
sleep $2
kill $P; sleep 1; kill -9 $P 2>/dev/null
