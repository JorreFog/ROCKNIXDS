#!/bin/sh
# shaders-compare.sh <old-ref> <outdir>   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
# GPU time per shader for two versions, as each draws: the old ref's shader files with DraStic's frame uploaded
# (COPY=1, what 1.3 did) and this checkout's shader files with the frame imported (1.4). ROCKNIX's built-in shaders
# are the same in both, only the upload differs. Writes <outdir>/shaders-<old-ref>.txt and shaders-v1.4.txt.
# The shaders, shtest and shbench.sh live in SuperDrastic since 1.5: SUPERDRASTIC_SRC=<its checkout> (default: next to
# this one), with build/shtest built (build.sh <sysroot> shtest). Needs the frames on the device already (SuperDrastic's
# tools/shaders.sh with FRAMES=... pushes them once).
HERE=$(cd "$(dirname "$0")/.." && pwd)
SD=${SUPERDRASTIC_SRC:-$HERE/../superdrastic}
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
OLD=${1:?old git ref, e.g. v1.3}; OUT=${2:?outdir}
B=/storage/dsflip/shbench
BUILTIN="sharp-bilinear lcd1x-nds-color lcd3x scanlines sharp-shimmerless quilez"
mkdir -p "$OUT"
push() {    # push <ref or "worktree">: that version's ds-*.frag into shaders/ (plus the null pass-through)
    $SSH "rm -f $B/shaders/ds-*.frag"
    if [ "$1" = worktree ]; then
        (cd "$SD/shaders" && tar cf - *.frag) | $SSH "tar xf - -C $B/shaders"
    else
        for f in $(cd "$HERE" && git ls-tree --name-only "$1" dsflip/shaders/ | grep '\.frag$'); do
            (cd "$HERE" && git show "$1:$f") | $SSH "cat > $B/shaders/$(basename "$f")"
        done
    fi
}
$SSH "cat > $B/shtest && chmod +x $B/shtest" < "$SD/build/shtest"
$SSH "cat > $B/shbench.sh && chmod +x $B/shbench.sh" < "$SD/tools/shbench.sh"
push "$OLD"
OLDNAMES="null $(cd "$HERE" && git ls-tree --name-only "$OLD" dsflip/shaders/ | sed -n 's|.*/\(ds-[^/]*\)\.frag$|\1|p' | tr '\n' ' ')"
$SSH "COPY=1 $B/shbench.sh $OLDNAMES $BUILTIN" | tee "$OUT/shaders-$OLD.txt"
push worktree    # SuperDrastic's
NEWNAMES="null $(cd "$SD/shaders" && ls ds-*.frag | sed 's/\.frag$//' | tr '\n' ' ')"
$SSH "$B/shbench.sh $NEWNAMES $BUILTIN" | tee "$OUT/shaders-v1.4.txt"
