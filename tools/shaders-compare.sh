#!/bin/sh
# shaders-compare.sh <old-ref> <outdir>   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
# GPU time per shader for two versions, as each draws: the old ref's shader files with DraStic's frame uploaded
# (COPY=1, what 1.3 did) and this checkout's shader files with the frame imported (1.4). ROCKNIX's built-in shaders
# are the same in both, only the upload differs. Writes <outdir>/shaders-<old-ref>.txt and shaders-v1.4.txt.
# Needs the frames on the device already (tools/shaders.sh with FRAMES=... pushes them once).
HERE=$(cd "$(dirname "$0")/.." && pwd)
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
OLD=${1:?old git ref, e.g. v1.3}; OUT=${2:?outdir}
B=/storage/dsflip/shbench
BUILTIN="sharp-bilinear lcd1x-nds-color lcd3x scanlines sharp-shimmerless quilez"
mkdir -p "$OUT"
push() {    # push <ref or "worktree">: that version's ds-*.frag into shaders/ (plus the null pass-through)
    $SSH "rm -f $B/shaders/ds-*.frag"
    if [ "$1" = worktree ]; then
        (cd "$HERE/dsflip/shaders" && tar cf - *.frag) | $SSH "tar xf - -C $B/shaders"
    else
        for f in $(cd "$HERE" && git ls-tree --name-only "$1" dsflip/shaders/ | grep '\.frag$'); do
            (cd "$HERE" && git show "$1:$f") | $SSH "cat > $B/shaders/$(basename "$f")"
        done
    fi
}
$SSH "cat > $B/shtest && chmod +x $B/shtest" < "$HERE/dsflip/shtest"
$SSH "cat > $B/shbench.sh && chmod +x $B/shbench.sh" < "$HERE/tools/shbench.sh"
push "$OLD"
OLDNAMES="null $(cd "$HERE" && git ls-tree --name-only "$OLD" dsflip/shaders/ | sed -n 's|.*/\(ds-[^/]*\)\.frag$|\1|p' | tr '\n' ' ')"
$SSH "COPY=1 $B/shbench.sh $OLDNAMES $BUILTIN" | tee "$OUT/shaders-$OLD.txt"
push worktree
NEWNAMES="null $(cd "$HERE/dsflip/shaders" && ls ds-*.frag | sed 's/\.frag$//' | tr '\n' ' ')"
$SSH "$B/shbench.sh $NEWNAMES $BUILTIN" | tee "$OUT/shaders-v1.4.txt"
