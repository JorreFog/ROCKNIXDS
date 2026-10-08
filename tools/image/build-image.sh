#!/bin/sh
# Builds the ROCKNIXDS SD card images, one per handheld: ROCKNIX's own RK3566 "Specific" image with its boot config
# (extlinux.conf's FDT line) set to the handheld's device tree, plus ROCKNIXDS on its FAT partition. ROCKNIX's image
# boots the Powkiddy X55's device tree until that line is changed, which ROCKNIX leaves to the player. On the first
# boot after ROCKNIX's resize, mount-storage.sh (sourced by ROCKNIX's initramfs) hands firstboot.sh to ROCKNIX's
# autostart, which installs the release from the card, with no network, and then deletes itself and the payload.
# No root needed: mtools writes the FAT.
#
#   tools/image/build-image.sh [--rocknix 20261001] [--rgds v1.5.3] [--plus v1.5.3-plus] [--out DIR]
#
# --rocknix: the ROCKNIX nightly to build on (default: the one this checkout was verified on, the repo's ROCKNIX file).
# --rgds/--plus: git refs of this repo (default: the newest vX.Y and vX.Y-plus tags); "worktree" packs the working
# tree as it is (tracked files), for testing; "none" builds no image for that handheld.
# Needs: git, curl, mtools, gzip, sha256sum, sfdisk.
# Output: DIR/rocknixds-<ref>-rocknix-<rocknix>.img.gz (+ .sha256) per handheld. Cache: DIR/cache.
set -e
ROCKNIX=
RGDS_REF= PLUS_REF= OUT=$PWD/image-out
while [ $# -gt 0 ]; do
    case $1 in
    --rocknix) ROCKNIX=$2; shift ;;
    --rgds) RGDS_REF=$2; shift ;;
    --plus) PLUS_REF=$2; shift ;;
    --out) OUT=$2; shift ;;
    *) echo "unknown option: $1"; exit 1 ;;
    esac
    shift
done
say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*"; exit 1; }
for t in git curl mcopy mmd mtype gzip sha256sum sfdisk; do command -v $t >/dev/null || die "needs $t"; done
REPO=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
HERE=$REPO/tools/image
[ -n "$ROCKNIX" ] || ROCKNIX=$(grep -v '^#' "$REPO/ROCKNIX" 2>/dev/null | grep -o '^[0-9]\{8\}' | head -n1)
[ -n "$ROCKNIX" ] || die "no ROCKNIX version: pass --rocknix YYYYMMDD (the repo's ROCKNIX file has none)"
[ -n "$RGDS_REF" ] || RGDS_REF=$(git -C "$REPO" tag -l 'v[0-9]*' --sort=-v:refname | grep -v plus | head -n1)
[ -n "$PLUS_REF" ] || PLUS_REF=$(git -C "$REPO" tag -l 'v[0-9]*-plus' --sort=-v:refname | head -n1)
mkdir -p "$OUT/cache"; OUT=$(cd "$OUT" && pwd)
W=$OUT/work

# ---- ROCKNIX's image ------------------------------------------------------------------------------------
BASE=ROCKNIX-RK3566.aarch64-$ROCKNIX-Specific.img
URL=https://github.com/ROCKNIX/distribution/releases/download/$ROCKNIX/$BASE.gz
if ! (cd "$OUT/cache" && sha256sum -c $BASE.gz.sha256 >/dev/null 2>&1); then
    say "Downloading ROCKNIX $ROCKNIX ($BASE.gz)"
    curl -fL -o "$OUT/cache/$BASE.gz" "$URL" && curl -fsSL -o "$OUT/cache/$BASE.gz.sha256" "$URL.sha256" || die "download failed"
    (cd "$OUT/cache" && sha256sum -c $BASE.gz.sha256) || die "ROCKNIX image doesn't match its checksum"
fi
# ---- a release, and what its installer downloads ------------------------------------------------
pack() {    # pack <rgds|plus> <ref>: the release as rocknixds-<h>.tar.gz, its ref, and its themes' tarballs
    h=$1 ref=$2
    if [ "$ref" = worktree ]; then
        commit=$(git -C "$REPO" stash create); [ -n "$commit" ] || commit=HEAD
        id=local
    else
        git -C "$REPO" rev-parse -q --verify "$ref^{commit}" >/dev/null || die "no ref $ref"
        commit=$ref id=$ref
    fi
    say "Packing $h: $ref"
    git -C "$REPO" archive --prefix=rocknixds/ "$commit" | gzip -9 > "$W/payload/rocknixds-$h.tar.gz"
    echo "$id" > "$W/payload/ref-$h"
    inst=$(git -C "$REPO" show "$commit:install.sh")
    echo "$inst" | grep -q RGDS_FIRSTBOOT || die "$ref's install.sh can't install offline (no RGDS_FIRSTBOOT): too old for the image"
    for dep in "THEME_UPSTREAM THEME_COMMIT dii-ess-aye" "CANVAS_UPSTREAM CANVAS_COMMIT canvas-ds"; do
        set -- $dep
        up=$(echo "$inst" | sed -n "s/^$1=\([^ ]*\).*/\1/p") c=$(echo "$inst" | sed -n "s/^$2=\([0-9a-f]*\).*/\1/p")
        [ -n "$up" ] && [ -n "$c" ] || die "$ref's install.sh has no $1/$2"
        f=$3-$c.tar.gz
        if [ ! -s "$OUT/cache/$f" ]; then
            say "Downloading $up@$c"
            curl -fsSL -o "$OUT/cache/$f.part" "https://codeload.github.com/$up/tar.gz/$c" && mv "$OUT/cache/$f.part" "$OUT/cache/$f" \
                || die "couldn't download $up@$c"
        fi
        cp "$OUT/cache/$f" "$W/payload/"
    done
}

build() {   # build <rgds|plus> <ref> <dtb>: the image for that handheld
    h=$1 ref=$2 dtb=$3
    rm -rf "$W"; mkdir -p "$W/payload"
    say "Unpacking ROCKNIX $ROCKNIX for the $h image"
    gzip -dc "$OUT/cache/$BASE.gz" > "$W/image.img"
    # the FAT partition (ROCKNIX's /flash): the first one, labelled system
    START=$(sfdisk -d "$W/image.img" | sed -n 's/.*img1 : start= *\([0-9]*\),.*name="system".*/\1/p')
    [ -n "$START" ] || die "no system partition in $BASE"
    FAT="$W/image.img@@$((START * 512))"
    mdir -i "$FAT" -b ::/device_trees | grep -q "/$dtb\$" || die "ROCKNIX $ROCKNIX has no $dtb"
    mdir -i "$FAT" ::/ | grep -qi rocknixds && die "$BASE already has ROCKNIXDS on it"
    pack $h "$ref"
    cp "$HERE/firstboot.sh" "$W/payload/"
    printf 'ROCKNIXDS %s on ROCKNIX %s.\nThis folder installs ROCKNIXDS on the first start of the handheld and is then deleted.\nhttps://github.com/JorreFog/ROCKNIXDS\n' \
        "$ref" "$ROCKNIX" > "$W/payload/README.txt"

    need=$(du -sk "$W/payload" | cut -f1)
    free=$(minfo -i "$FAT" :: | awk '/^free clusters=/ {sub("free clusters=", ""); f=$1} /^cluster size:/ {s=$3} END {print int(f * s / 2)}')
    say "Payload $((need / 1024)) MB, $((free / 1024)) MB free on ROCKNIX's boot partition"
    [ "$need" -lt "$free" ] || die "doesn't fit"
    mmd -i "$FAT" ::/rocknixds
    mcopy -i "$FAT" "$W"/payload/* ::/rocknixds/
    mcopy -i "$FAT" "$HERE/mount-storage.sh" ::/mount-storage.sh
    # the handheld's device tree in the boot config (ROCKNIX ships the X55's)
    mtype -i "$FAT" ::/extlinux/extlinux.conf > "$W/extlinux.conf"
    grep -q '^ *FDT ' "$W/extlinux.conf" || die "no FDT line in ROCKNIX's extlinux.conf"
    sed -i "s|^\( *FDT \).*|\1/device_trees/$dtb|" "$W/extlinux.conf"
    mcopy -o -i "$FAT" "$W/extlinux.conf" ::/extlinux/extlinux.conf
    say "Boot config: $(grep '^ *FDT ' "$W/extlinux.conf" | sed 's/^ *//')"

    name=rocknixds-$ref-rocknix-$ROCKNIX.img
    [ "$ref" = worktree ] && name=rocknixds-worktree-$h-rocknix-$ROCKNIX.img
    say "Compressing $name.gz"
    gzip -9 -c "$W/image.img" > "$OUT/$name.gz"
    (cd "$OUT" && sha256sum "$name.gz" > "$name.gz.sha256")
    rm -rf "$W"
    say "Done: $OUT/$name.gz"
}
export MTOOLS_SKIP_CHECK=1
[ "$RGDS_REF" = none ] || build rgds "$RGDS_REF" rk3568-anbernic-rg-ds.dtb
[ "$PLUS_REF" = none ] || build plus "$PLUS_REF" rk3568-anbernic-rg-ds-plus.dtb
