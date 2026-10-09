#!/bin/sh
# Döda Kvarter's own updates, without a ROCKNIXDS release (/storage/.config/rocknixds/dodakvarter/update.sh). The game
# runs it from Settings > Game updates (the session names it in DK_UPDATER); over ssh it works the same:
#   update.sh check     one line: UPDATE <version> when a newer game is out, UPTODATE <version>, or OFFLINE
#   update.sh install   gets the newest (or DK_UPDATE_TAG=dodakvarter-v<version>), says each step (STEP ...), and ends
#                       with DONE <version> or FAIL <why>. A failed update leaves the game as it was.
# Releases are GitHub releases tagged dodakvarter-v<version>, published as pre-releases (so ROCKNIXDS's own updater never
# takes them for a ROCKNIXDS release), with dodakvarter-<version>-aarch64.tar.gz and its .sha256
# (.github/workflows/dodakvarter-release.yml). The download is checked against the .sha256 before anything changes.
# What's kept: data/ (high scores, settings, the saved run), which the package doesn't hold. A run saved by another
# version doesn't load in the new one (the game says so on its Game updates row before it updates).
# Host tests point DK_DIR, DK_UPDATE_API (a releases list as GitHub gives it) and DK_UPDATE_BASE (where the packages
# are, file:// works) at a temp tree; DK_MENU_DIRS at the menu folders to refresh; DK_ROCKNIXDS_ID and DK_TESTERS_BASE
# at a test build's commit and where its testers/ folder is.
REPO=JorreFog/ROCKNIXDS
D=${DK_DIR:-/storage/.config/rocknixds/dodakvarter}
API=${DK_UPDATE_API:-https://api.github.com/repos/$REPO/releases?per_page=100}
BASE=${DK_UPDATE_BASE:-https://github.com/$REPO/releases/download}
MENUS=${DK_MENU_DIRS:-/storage/.config/rocknixds/apps/dodakvarter /storage/roms/ports}
# A private test build of ROCKNIXDS (installed by its commit, in installed-id) can carry the game's releases for its
# testers in testers/: releases.json as GitHub lists releases, and dodakvarter-v<version>/<package> beside it. Those
# are used when the installed commit has them, else GitHub's releases.
if [ -z "$DK_UPDATE_API" ]; then
    id=$(cat "${DK_ROCKNIXDS_ID:-/storage/.config/rocknixds/installed-id}" 2>/dev/null)
    if echo "$id" | grep -qE '^[0-9a-f]{40}$'; then
        t="${DK_TESTERS_BASE:-https://raw.githubusercontent.com/$REPO}/$id/testers"     # (host tests: file://)
        curl -fsS --max-time 10 -o /dev/null "$t/releases.json" 2>/dev/null && API=$t/releases.json BASE=$t
    fi
fi

installed() { v=$(cat "$D/VERSION" 2>/dev/null); echo "${v:-0.1.0}"; }   # (0.1.0, in 1.6, had no VERSION file)
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n1)" = "$1" ]; }   # newer A B: A > B

newest() {   # the newest release's version (by version, not by date); NONE: the list came, without a release of the game
    curl -fsSL --max-time 15 "$API" 2>/dev/null | python3 -c '
import json, sys
try:
    rel = json.load(sys.stdin)
except ValueError:
    sys.exit()
if not isinstance(rel, list):       # an error from GitHub (a rate limit, say): not known
    sys.exit()
best = None
def key(v):
    return [int(x) if x.isdigit() else -1 for x in v.split(".")]
for r in rel:
    t = r.get("tag_name") or ""
    if r.get("draft") or not t.startswith("dodakvarter-v"):
        continue
    v = t[len("dodakvarter-v"):]
    if all(p.isdigit() for p in v.split(".")) and (best is None or key(v) > key(best)):
        best = v
print(best or "NONE")' 2>/dev/null
}

case "$1" in
check)
    v=$(newest)
    [ -n "$v" ] || { echo OFFLINE; exit 0; }
    [ "$v" = NONE ] && { echo "UPTODATE $(installed)"; exit 0; }
    if newer "$v" "$(installed)"; then echo "UPDATE $v"; else echo "UPTODATE $(installed)"; fi
    ;;
install)
    if [ -n "$DK_UPDATE_TAG" ]; then v=${DK_UPDATE_TAG#dodakvarter-v}
    else echo "STEP Looking for it"; v=$(newest); fi
    [ -n "$v" ] || { echo "FAIL No network"; exit 1; }
    [ "$v" = NONE ] && { echo "DONE $(installed)"; exit 0; }
    [ -n "$DK_UPDATE_TAG" ] || newer "$v" "$(installed)" || { echo "DONE $(installed)"; exit 0; }
    W="$D/.update"
    rm -rf "$W"; mkdir -p "$W" || { echo "FAIL Can't write $D"; exit 1; }
    trap 'rm -rf "$W"' EXIT
    PKG="dodakvarter-$v-aarch64.tar.gz"
    echo "STEP Downloading v$v"
    curl -fsSL --max-time 300 -o "$W/pkg.tar.gz" "$BASE/dodakvarter-v$v/$PKG" 2>/dev/null || { echo "FAIL Download failed"; exit 1; }
    curl -fsSL --max-time 30 -o "$W/pkg.sha256" "$BASE/dodakvarter-v$v/$PKG.sha256" 2>/dev/null || { echo "FAIL Download failed"; exit 1; }
    echo "STEP Checking it"
    want=$(cut -d' ' -f1 < "$W/pkg.sha256"); have=$(sha256sum "$W/pkg.tar.gz" | cut -d' ' -f1)
    [ -n "$want" ] && [ "$want" = "$have" ] || { echo "FAIL Download damaged"; exit 1; }
    mkdir -p "$W/x" && tar -xzf "$W/pkg.tar.gz" -C "$W/x" 2>/dev/null || { echo "FAIL Package damaged"; exit 1; }
    X="$W/x/dodakvarter"
    for f in dodakvarter VERSION launch.sh session.sh restore.sh selftest.sh gamelist.py update.sh "Doda Kvarter.sh"; do
        [ -f "$X/$f" ] || { echo "FAIL Package incomplete"; exit 1; }
    done
    [ "$(cat "$X/VERSION")" = "$v" ] || { echo "FAIL Wrong version inside"; exit 1; }
    echo "STEP Installing v$v"
    # each file next to its old one, then renamed over it: the running game and session keep the old ones open,
    # and a power cut leaves each file whole. The old game stays as dodakvarter.prev.
    cp -p "$D/dodakvarter" "$D/dodakvarter.prev" 2>/dev/null
    for f in dodakvarter launch.sh session.sh restore.sh selftest.sh gamelist.py update.sh; do
        cp "$X/$f" "$D/$f.new" && chmod +x "$D/$f.new" && mv -f "$D/$f.new" "$D/$f" || { echo "FAIL Couldn't install $f"; exit 1; }
    done
    chmod -x "$D/gamelist.py"
    # the entry in the menu: its script and pictures where the installer put them (a tile of its own, or Ports)
    for m in $MENUS; do
        [ -f "$m/Doda Kvarter.sh" ] || continue
        cp "$X/Doda Kvarter.sh" "$m/Doda Kvarter.sh.new" && chmod +x "$m/Doda Kvarter.sh.new" && mv -f "$m/Doda Kvarter.sh.new" "$m/Doda Kvarter.sh"
        if [ -d "$X/media" ]; then mkdir -p "$m/images"; cp "$X"/media/dodakvarter-*.png "$m/images/" 2>/dev/null; fi
    done
    cp "$X/VERSION" "$D/VERSION.new" && mv -f "$D/VERSION.new" "$D/VERSION"   # last: what's installed now
    echo "DONE $v"
    ;;
*)
    echo "usage: update.sh check|install" >&2; exit 2 ;;
esac
