#!/bin/sh
# package.sh [binary] [outdir]: the game's release package, as device/update.sh installs it:
# <outdir>/dodakvarter-<VERSION>-aarch64.tar.gz (one folder, dodakvarter/: the game, VERSION, the device scripts and the
# menu's pictures) and its .sha256. The binary is bin/dodakvarter-aarch64 by default (the one ROCKNIXDS installs).
set -e
cd "$(dirname "$0")/.."
BIN=${1:-bin/dodakvarter-aarch64}
OUT=${2:-dist}
V=$(cat VERSION)
[ -f "$BIN" ] || { echo "no $BIN" >&2; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
P=$T/dodakvarter
mkdir -p "$P/media"
cp "$BIN" "$P/dodakvarter"
cp VERSION device/launch.sh device/session.sh device/restore.sh device/selftest.sh device/gamelist.py device/update.sh \
   "device/Doda Kvarter.sh" "$P/"
cp device/media/dodakvarter-*.png "$P/media/"
chmod +x "$P/dodakvarter" "$P"/*.sh
mkdir -p "$OUT"
F="dodakvarter-$V-aarch64.tar.gz"
tar -czf "$OUT/$F" -C "$T" --owner=0 --group=0 dodakvarter
(cd "$OUT" && sha256sum "$F" > "$F.sha256")
echo "$OUT/$F"
