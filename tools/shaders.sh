#!/bin/sh
# shaders.sh [shader...]   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
# Pushes shtest, the repo's shaders and tools/shbench.sh to /storage/dsflip/shbench on the device and runs it (see
# shbench.sh). Needs real frames on the device (frames/{top,bot}{2x,1x}.raw, libdsflip scanout-dump format); push
# them once with FRAMES=<local dir holding those four files>. They are game pictures, so they aren't in the repo.
HERE=$(cd "$(dirname "$0")/.." && pwd)
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
B=/storage/dsflip/shbench
$SSH "mkdir -p $B/shaders $B/frames"
$SSH "cat > $B/shtest && chmod +x $B/shtest" < "$HERE/dsflip/shtest"
$SSH "cat > $B/shbench.sh && chmod +x $B/shbench.sh" < "$HERE/tools/shbench.sh"
(cd "$HERE/dsflip/shaders" && tar cf - *.frag) | $SSH "tar xf - -C $B/shaders"
$SSH "cat > $B/shaders/null.frag" <<'FRAG'
// pass-through: one bilinear fetch (the cost floor: upload + writing the panel)
precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D u_texture;
void main() { gl_FragColor = SWIZ(texture2D(u_texture, v_texcoord)); }
FRAG
if [ -n "$FRAMES" ]; then
    for f in top2x top1x bot2x bot1x; do $SSH "cat > $B/frames/$f.raw" < "$FRAMES/$f.raw"; done
fi
$SSH "CLOCK=${CLOCK:-} REPS=${REPS:-} ${COPY:+COPY=1} $B/shbench.sh $*"    # shtest: COPY set at all = upload mode
