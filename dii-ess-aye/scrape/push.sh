#!/bin/sh
# run on the device: push metadata + media for each game through the ES HTTP API
cd "$(dirname "$0")"
while read -r id p; do
  u=localhost:1234/systems/nds/games/$id
  echo "$p meta:  $(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' --data-binary @$p.json $u)"
  for m in "thumbnail $p-Boxarts.png" "image $p-Snaps-wide.png" "titleshot $p-Titles-wide.png"; do
    set -- $m
    echo "$p $1: $(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: image/png' --data-binary @$2 $u/media/$1)"
  done
done < map.txt
