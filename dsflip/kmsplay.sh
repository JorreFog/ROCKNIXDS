#!/bin/sh
# kmsplay.sh <rom-substring> <hires> <secs>: under KMS, play a real game through libdsflip (probe first),
# load savestate 0 via the gamepad, dump both panels at the end.
D=/storage/dsflip L=$D/logs CFGD=/storage/.config/drastic CFG=$CFGD/config/drastic.cfg
ROM=$(ls /storage/roms/nds/*.nds | grep -i "$1" | head -1)
cp $CFG /tmp/drastic.cfg.kmsplay; sed -i "s/^hires_3d = .*/hires_3d = $2/" $CFG
cd $CFGD
SDL_VIDEODRIVER=dummy DSFLIP_TOUCH_DUMP=${DSFLIP_TOUCH_DUMP:-0} XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$L/play.dsflip.log DSPROBE_LOG=$L/play.probe.log \
  LD_PRELOAD=$D/libdsprobe.so:$D/libdsflip.so ./drastic.real "$ROM" >$L/play.out 2>&1 &
P=$!
sleep 8; cp /tmp/drastic.cfg.kmsplay $CFG
python3 $D/padkey.py 312 0.6
sleep $3
kill -USR2 $P; sleep 1
kill $P; sleep 1; kill -9 $P 2>/dev/null
