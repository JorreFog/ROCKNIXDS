#!/bin/sh
# Installs (or updates) ROCKNIXDS Bank & Trade on the handheld. As root (ssh in, password rocknix):
#
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/bank/device/install-bank.sh | sh
#
# Options:
#   --uninstall   remove the app and its Ports entry (the bank itself, in /storage/roms/rocknixds-bank, stays)
#   --version     the installed version
# Env: BANK_TARBALL=/path/to/rocknixds-bank-<version>-aarch64.tar.gz installs that file (no network needed).
#      BANK_TAG=bank-v0.1.0 installs that release instead of the newest one.
#
# The app goes to /storage/.config/rocknixds/bank, its Ports entry to /storage/roms/ports/ROCKNIXDS Bank.sh. Releases are
# GitHub releases tagged bank-v<version> (published as pre-releases, so ROCKNIXDS's own updater never takes them for a
# ROCKNIXDS release), with rocknixds-bank-<version>-aarch64.tar.gz and its .sha256.
set -e

REPO=JorreFog/ROCKNIXDS
DEST=/storage/.config/rocknixds/bank
PORTS=/storage/roms/ports
ENTRY="$PORTS/ROCKNIXDS Bank.sh"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*"; exit 1; }

case "$1" in
--uninstall)
    rm -rf "$DEST" "$DEST.new" "$DEST.old"
    rm -f "$ENTRY"
    say "Removed ROCKNIXDS Bank. Your bank, backups and history are still in /storage/roms/rocknixds-bank."
    exit 0 ;;
--version)
    cat "$DEST/VERSION" 2>/dev/null || echo "not installed"
    exit 0 ;;
"") ;;
*) die "unknown option: $1" ;;
esac

[ "$(id -u)" = 0 ] || die "run as root (ssh root@<handheld>)"
[ "$(uname -m)" = aarch64 ] || die "this is for the handheld (aarch64), not $(uname -m)"
[ -d /storage/roms ] || die "no /storage/roms: is this ROCKNIX?"

WORK=$(mktemp -d /storage/.rocknixds-bank-install.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

if [ -n "$BANK_TARBALL" ]; then
    [ -f "$BANK_TARBALL" ] || die "no such file: $BANK_TARBALL"
    cp "$BANK_TARBALL" "$WORK/bank.tar.gz"
else
    TAG=$BANK_TAG
    if [ -z "$TAG" ]; then
        TAG=$(curl -fsSL --max-time 20 "https://api.github.com/repos/$REPO/releases?per_page=100" 2>/dev/null | python3 -c '
import json, sys
try:
    rel = json.load(sys.stdin)
except ValueError:
    sys.exit()
for r in rel if isinstance(rel, list) else []:
    t = r.get("tag_name") or ""
    if t.startswith("bank-v") and not r.get("draft"):
        print(t)
        break' 2>/dev/null)
        [ -n "$TAG" ] || die "no ROCKNIXDS Bank release found (or GitHub unreachable: check the network)"
    fi
    VER=${TAG#bank-v}
    URL="https://github.com/$REPO/releases/download/$TAG/rocknixds-bank-$VER-aarch64.tar.gz"
    say "Downloading ROCKNIXDS Bank $VER"
    curl -fL --max-time 600 -o "$WORK/bank.tar.gz" "$URL" || die "couldn't download $URL"
    if curl -fsSL --max-time 30 -o "$WORK/bank.sha256" "$URL.sha256"; then
        want=$(cut -d' ' -f1 < "$WORK/bank.sha256")
        have=$(sha256sum "$WORK/bank.tar.gz" | cut -d' ' -f1)
        [ "$want" = "$have" ] || die "the download is damaged (checksum mismatch): try again"
    fi
fi

say "Unpacking"
mkdir -p "$WORK/x"
tar -xzf "$WORK/bank.tar.gz" -C "$WORK/x"
SRC="$WORK/x/rocknixds-bank"
[ -x "$SRC/rocknixds-bank" ] && [ -f "$SRC/rocknixds-bank.sh" ] || die "the package doesn't hold the app"

# replace the old version in one step: a failed install leaves the old one working
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST.new" "$DEST.old"
mv "$SRC" "$DEST.new"
chmod +x "$DEST.new/rocknixds-bank" "$DEST.new/rocknixds-bank.sh"
[ -d "$DEST" ] && mv "$DEST" "$DEST.old"
mv "$DEST.new" "$DEST"
rm -rf "$DEST.old"

mkdir -p "$PORTS"
cp "$DEST/ROCKNIXDS Bank.sh" "$ENTRY"
chmod +x "$ENTRY"

say "ROCKNIXDS Bank $(cat "$DEST/VERSION" 2>/dev/null) is installed: Ports > ROCKNIXDS Bank"
say "(If Ports doesn't show it yet: Start > Game settings > Update gamelists, or restart the handheld.)"
