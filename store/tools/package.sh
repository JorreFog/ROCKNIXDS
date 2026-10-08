#!/bin/sh
# package.sh [binary] [outdir]: the Store's release package, as the Store installs it (its own catalog entry, id
# "store"): <outdir>/rocknixds-store-<VERSION>-aarch64.tar.gz (one folder, store/: the app, the package manager, the
# catalog as it is in this tree, VERSION and the device scripts) and its .sha256. The binary is bin/store-aarch64 by
# default (the one ROCKNIXDS installs).
set -e
cd "$(dirname "$0")/.."
BIN=${1:-bin/store-aarch64}
OUT=${2:-dist}
V=$(cat VERSION)
[ -f "$BIN" ] || { echo "no $BIN" >&2; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
P=$T/store
mkdir -p "$P"
cp "$BIN" "$P/store"
cp VERSION catalog.json device/rocknixds-store device/launch.sh device/session.sh device/restore.sh \
   "device/ROCKNIXDS Store.sh" "$P/"
chmod +x "$P/store" "$P/rocknixds-store" "$P"/*.sh
mkdir -p "$OUT"
F="rocknixds-store-$V-aarch64.tar.gz"
tar -czf "$OUT/$F" -C "$T" --owner=0 --group=0 store
(cd "$OUT" && sha256sum "$F" > "$F.sha256")
echo "$OUT/$F"
