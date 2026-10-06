#!/bin/sh
# Installs (or updates) ROCKNIXDS Bank & Trade on the handheld. As root (ssh in, password rocknix):
#
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/bank/device/install-bank.sh | sh
#
# Options:
#   --uninstall   remove the app and its entry in the menu (the bank itself, in /storage/roms/rocknixds-bank, stays)
#   --version     the installed version
# Env: BANK_TARBALL=/path/to/rocknixds-bank-<version>-aarch64.tar.gz installs that file (no network needed).
#      BANK_TAG=bank-v0.1.0 installs that release instead of the newest one.
#
# The app goes to /storage/.config/rocknixds/bank. Its entry in the menu is a tile of its own on the shelf where this
# ROCKNIXDS lists the bank as a system (es_systems_rocknixds.cfg, installed with the theme from 1.6 on; the entry and
# its pictures are then in /storage/.config/rocknixds/apps/bank), else a line in Ports (/storage/roms/ports).
# Releases are GitHub releases tagged bank-v<version> (published as pre-releases, so ROCKNIXDS's own updater never takes
# them for a ROCKNIXDS release), with rocknixds-bank-<version>-aarch64.tar.gz and its .sha256.
set -e

REPO=JorreFog/ROCKNIXDS
DEST=/storage/.config/rocknixds/bank
PORTS=/storage/roms/ports
APPS=/storage/.config/rocknixds/apps/bank
SCRIPT="ROCKNIXDS Bank.sh"

# entry <add|remove> <dir>: the bank's line in that folder's gamelist.xml (name, description, pictures); other entries
# stay as they are
entry() {
    python3 - "$1" "$2" <<'PY'
import os, sys
import xml.etree.ElementTree as ET
PATH = "./ROCKNIXDS Bank.sh"
FIELDS = [
    ("name", "ROCKNIXDS Bank"),
    ("desc", "A Pokémon bank for both screens: reads your game saves (generation 1 to 5), stores Pokémon in boxes of "
             "its own, moves them between games through the bank (they change format only when they move up a "
             "generation), checks them with PKHeX's legality analysis, and trades with another handheld over Wi-Fi, "
             "in a lobby or a private room. Close the game first: emulators write their own copy of the save when "
             "they quit."),
    ("image", "./images/bank-image.png"),
    ("thumbnail", "./images/bank-thumb.png"),
    ("marquee", "./images/bank-marquee.png"),
    ("releasedate", "20261006T000000"),
    ("developer", "ROCKNIXDS"),
    ("publisher", "ROCKNIXDS"),
    ("genre", "Tool"),
    ("players", "1-2"),
]
action, folder = sys.argv[1], sys.argv[2]
gl = os.path.join(folder, "gamelist.xml")
if os.path.exists(gl):
    try:
        tree = ET.parse(gl)
        root = tree.getroot()
    except ET.ParseError:
        sys.exit(f"{gl} doesn't parse: left alone")
elif action == "remove":
    sys.exit(0)
else:
    root = ET.Element("gameList")
    tree = ET.ElementTree(root)
for g in root.findall("game"):
    p = g.find("path")
    if p is not None and p.text and p.text.strip() in (PATH, PATH[2:]):
        root.remove(g)
if action == "add":
    g = ET.SubElement(root, "game")
    ET.SubElement(g, "path").text = PATH
    for k, v in FIELDS:
        ET.SubElement(g, k).text = v
if hasattr(ET, "indent"):
    ET.indent(tree, space="\t")
tmp = gl + ".tmp"
tree.write(tmp, encoding="utf-8", xml_declaration=True)
os.replace(tmp, gl)
PY
}

# unlist <dir>: the bank's script, pictures and line out of that folder (the other place after a move, both at uninstall)
unlist() {
    [ -n "$1" ] && [ -f "$1/$SCRIPT" ] || return 0
    entry remove "$1" || true
    rm -f "${1:?}/${SCRIPT:?}" "${1:?}"/images/bank-image.png "${1:?}"/images/bank-thumb.png "${1:?}"/images/bank-marquee.png
}

# EmulationStation writes its lists back when it stops, and reads the systems when it starts: off while the entry
# changes (unless a game is running: then the entry shows at the next start of the menu). es_on is also what the
# script runs when it ends, however it ends: a step that fails never leaves the handheld without its menu.
ES_WAS= ES_BACK=
es_off() {
    if systemctl is-active -q essway.service 2>/dev/null && ! systemctl is-active -q dsflip-game.service 2>/dev/null &&
       ! curl -s -m 2 localhost:1234/runningGame 2>/dev/null | grep -q '"path"'; then
        ES_WAS=1; systemctl stop essway.service
    fi
}
es_on() {
    [ -n "$ES_WAS" ] || return 0
    ES_WAS=
    systemctl start essway.service && ES_BACK=1
    return 0
}
trap es_on EXIT

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*"; exit 1; }

case "$1" in
--uninstall)
    es_off
    unlist "$APPS"; unlist "$PORTS"
    rm -rf "${DEST:?}" "${DEST:?}.new" "${DEST:?}.old" "${APPS:?}"
    es_on
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
trap 'rm -rf "$WORK"; es_on' EXIT

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

# its entry in the menu: a tile of its own where this ROCKNIXDS lists the bank as a system, else a line in Ports. An
# entry left in the other place by an earlier install goes.
if [ -f /storage/.config/emulationstation/es_systems_rocknixds.cfg ]; then
    DIR=$APPS OLD=$PORTS WHERE="its own tile in the menu: Bank & Trade"
else
    DIR=$PORTS OLD=$APPS WHERE="Ports > ROCKNIXDS Bank"
fi
es_off
unlist "$OLD"
mkdir -p "$DIR/images"
cp "$DEST/$SCRIPT" "$DIR/$SCRIPT"
chmod +x "$DIR/$SCRIPT"
if [ -f "$DEST/media/bank-image.png" ]; then
    cp "$DEST"/media/bank-image.png "$DEST"/media/bank-thumb.png "$DEST"/media/bank-marquee.png "$DIR/images/"
    entry add "$DIR" || say "The menu's list wasn't updated (see above): the bank shows without its pictures"
fi
es_on

say "ROCKNIXDS Bank $(cat "$DEST/VERSION" 2>/dev/null) is installed: $WHERE"
[ -n "$ES_BACK" ] || say "(It shows the next time the menu starts: restart EmulationStation, or the handheld.)"
