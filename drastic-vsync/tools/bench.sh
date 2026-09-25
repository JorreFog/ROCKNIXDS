#!/bin/sh
# bench.sh <tag> <hires> [env...]
D=$(dirname "$0"); T=$1; shift
$D/rg "/storage/drastic-vsync/go.sh start $*"
$D/rg '/storage/drastic-vsync/go.sh key l'; sleep 4
$D/rg '/storage/drastic-vsync/go.sh key "hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:down:1 hold:up:1 hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:left:2 hold:right:2 hold:left:2 hold:right:2"'
sleep 20; $D/shot $T.png; sleep 22
$D/rg "top -b -n1 | grep drastic | head -2; /storage/drastic-vsync/go.sh stop $T"
$D/rg "cat /tmp/go-$T.log" > $D/go-$T.log
