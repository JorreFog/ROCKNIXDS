#!/bin/sh
# package-app.sh <app folder> [outdir]: an app's package for the ROCKNIXDS Store, from a folder laid out as the app is
# on the handheld, with its app.json (id, check, entry...: see ../README.md). Writes
# <outdir>/<id>-<version>-aarch64.tar.gz (one folder, named after the id) and its .sha256: attach both to a GitHub
# release tagged <tag prefix><version>, and the catalog entry's "release" finds them.
# The version is the folder's VERSION file (written into the package if the folder has none, from $VERSION).
set -e
SRC=${1:?usage: package-app.sh <app folder> [outdir]}
OUT=${2:-dist}
[ -f "$SRC/app.json" ] || { echo "$SRC has no app.json" >&2; exit 1; }
ID=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$SRC/app.json")
echo "$ID" | grep -Eq '^[a-z0-9][a-z0-9-]{0,31}$' || { echo "bad id: $ID" >&2; exit 1; }
V=$(cat "$SRC/VERSION" 2>/dev/null || echo "$VERSION")
echo "$V" | grep -Eq '^[0-9]+(\.[0-9]+)*$' || { echo "no version: a VERSION file, or VERSION=1.0.0" >&2; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp -R "$SRC" "$T/$ID"
[ -f "$T/$ID/VERSION" ] || echo "$V" > "$T/$ID/VERSION"
find "$T/$ID" -name '*.sh' -exec chmod +x {} +
mkdir -p "$OUT"
F="$ID-$V-aarch64.tar.gz"
tar -czf "$OUT/$F" -C "$T" --owner=0 --group=0 "$ID"
(cd "$OUT" && sha256sum "$F" > "$F.sha256")
echo "$OUT/$F"
