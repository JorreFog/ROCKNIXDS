#!/bin/sh
# The game's own updates (device/update.sh, tools/package.sh) on the host: a handheld with 0.1.0 (no VERSION file),
# a releases list as GitHub gives it (with ROCKNIXDS's and the bank's releases in it), the package made by package.sh.
# check finds the newer one, install puts it in place and keeps data/, a damaged download changes nothing.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "test_update: FAIL: $*"; exit 1; }
V=$(cat VERSION)
D=$T/dk; mkdir -p $D/data $T/menu/images $T/rel/dodakvarter-v$V
echo old > $D/dodakvarter; echo old > $D/session.sh; echo "1 2 3" > $D/data/scores.txt
echo old > "$T/menu/Doda Kvarter.sh"
printf '#!/bin/sh\necho "Döda Kvarter %s"\n' "$V" > $T/bin
sh tools/package.sh $T/bin $T/rel/dodakvarter-v$V >/dev/null
cat > $T/releases.json <<J
[{"tag_name": "v1.6.1-plus", "draft": false, "prerelease": false},
 {"tag_name": "bank-v0.2.0", "draft": false, "prerelease": true},
 {"tag_name": "dodakvarter-v99.0.0", "draft": true, "prerelease": true},
 {"tag_name": "dodakvarter-v$V", "draft": false, "prerelease": true},
 {"tag_name": "dodakvarter-v0.1.5", "draft": false, "prerelease": true}]
J
export DK_DIR=$D DK_UPDATE_API=file://$T/releases.json DK_UPDATE_BASE=file://$T/rel DK_MENU_DIRS="$T/menu $T/nomenu"
r=$(sh device/update.sh check); [ "$r" = "UPDATE $V" ] || fail "check said '$r'"
# a damaged download: nothing changes
cp $T/rel/dodakvarter-v$V/dodakvarter-$V-aarch64.tar.gz $T/good.tgz
echo junk >> $T/rel/dodakvarter-v$V/dodakvarter-$V-aarch64.tar.gz
r=$(sh device/update.sh install | tail -n1); [ "$r" = "FAIL Download damaged" ] || fail "damaged: '$r'"
[ "$(cat $D/dodakvarter)" = old ] && [ ! -e $D/VERSION ] && [ ! -e $D/.update ] || fail "a damaged download changed the game"
cp $T/good.tgz $T/rel/dodakvarter-v$V/dodakvarter-$V-aarch64.tar.gz
out=$(sh device/update.sh install)
echo "$out" | grep -q '^STEP Downloading' || fail "no steps: $out"
[ "$(echo "$out" | tail -n1)" = "DONE $V" ] || fail "install: $out"
[ "$(cat $D/VERSION)" = "$V" ] || fail "VERSION not written"
[ "$($D/dodakvarter)" = "Döda Kvarter $V" ] || fail "the game wasn't replaced"
[ "$(cat $D/dodakvarter.prev)" = old ] || fail "the old game wasn't kept"
cmp -s $D/session.sh device/session.sh && [ -x $D/update.sh ] || fail "the scripts weren't replaced"
[ "$(cat $D/data/scores.txt)" = "1 2 3" ] || fail "data/ changed"
cmp -s "$T/menu/Doda Kvarter.sh" "device/Doda Kvarter.sh" && [ -f $T/menu/images/dodakvarter-image.png ] || fail "the menu entry wasn't refreshed"
[ ! -e $T/nomenu ] || fail "a menu folder was made"
[ ! -e $D/.update ] || fail "the work folder was left"
r=$(sh device/update.sh check); [ "$r" = "UPTODATE $V" ] || fail "after: '$r'"
r=$(sh device/update.sh install | tail -n1); [ "$r" = "DONE $V" ] || fail "again: '$r'"
DK_UPDATE_API=file://$T/none.json sh device/update.sh check | grep -qx OFFLINE || fail "no network: not OFFLINE"
# GitHub answering with an error (a rate limit) isn't "up to date": not known
echo '{"message": "API rate limit exceeded"}' > $T/err.json
DK_UPDATE_API=file://$T/err.json sh device/update.sh check | grep -qx OFFLINE || fail "an error reply: not OFFLINE"
# a list without a release of the game: up to date (not OFFLINE), and install changes nothing
echo '[{"tag_name": "v1.6.1", "draft": false, "prerelease": false}]' > $T/nogame.json
r=$(DK_UPDATE_API=file://$T/nogame.json sh device/update.sh check); [ "$r" = "UPTODATE $V" ] || fail "no game release: '$r'"
r=$(DK_UPDATE_API=file://$T/nogame.json sh device/update.sh install | tail -n1); [ "$r" = "DONE $V" ] || fail "no game release, install: '$r'"
# a private test build's commit with testers/ (releases.json, the package beside it): used instead of GitHub's
ID=0123456789abcdef0123456789abcdef01234567
mkdir -p $T/raw/$ID/testers; cp $T/releases.json $T/raw/$ID/testers/releases.json; cp -r $T/rel/dodakvarter-v$V $T/raw/$ID/testers/
echo $ID > $T/installed-id; echo 0.1.0 > $D/VERSION
r=$(env -u DK_UPDATE_API -u DK_UPDATE_BASE DK_ROCKNIXDS_ID=$T/installed-id DK_TESTERS_BASE=file://$T/raw sh device/update.sh check)
[ "$r" = "UPDATE $V" ] || fail "test build: '$r'"
r=$(env -u DK_UPDATE_API -u DK_UPDATE_BASE DK_ROCKNIXDS_ID=$T/installed-id DK_TESTERS_BASE=file://$T/raw sh device/update.sh install | tail -n1)
[ "$r" = "DONE $V" ] && [ "$(cat $D/VERSION)" = "$V" ] || fail "test build, install: '$r'"
echo "test_update: ok ($V)"
