#!/bin/sh
# ROCKNIXDS private test build 2 (2026-10-09), for testers only: not a release. Run ON the handheld as root (ssh in,
# password rocknix):
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/<this commit>/testers.sh | sh
# It picks this handheld's build and runs that build's own installer (TESTERS.md has what is in them). Back to the
# public release: UPDATES & DOWNLOADS > ROCKNIXDS in the menu, or
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh
set -e
REPO=JorreFog/ROCKNIXDS
RGDS_BUILD=9339a94f505bc0f7d324a60aef656c165942633a     # the RG DS: Banana + today's fixes + SuperDrastic .9-test.1
PLUS_BUILD=3d9a7d274de39a3b777271c9e67562f393db9bcd     # the RG DS Plus: 1.6.1-plus line + SuperDrastic .9-test.1 + the 3x queue wait
LABEL=private-test-2
die() { echo "testers.sh: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run this as root on the handheld"
plus=0   # as install.sh's rgds_plus: the model string, or a panel wider than the RG DS's 640 (install.sh checks the rest)
model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)
case "$model" in *"RG DS Plus"*) plus=1 ;; esac
if [ $plus = 0 ]; then
    for m in /sys/class/drm/card*-DSI-*/modes; do
        [ -f "$m" ] || continue
        read -r mode < "$m" 2>/dev/null || continue
        [ "${mode%%x*}" -gt 640 ] 2>/dev/null && plus=1
        break
    done
fi
if [ $plus = 1 ]; then REF=$PLUS_BUILD NAME="RG DS Plus"; else REF=$RGDS_BUILD NAME="RG DS"; fi
echo "==> $NAME: ROCKNIXDS private test build 2 ($REF)"
tmp=$(mktemp)
curl -fsSL --max-time 120 "https://raw.githubusercontent.com/$REPO/$REF/install.sh" -o "$tmp" \
    || { rm -f "$tmp"; die "couldn't download the installer: check the network"; }
set +e
RGDS_BRANCH=$LABEL RGDS_REF=$REF sh "$tmp" "$@"
rc=$?
rm -f "$tmp"
exit $rc
