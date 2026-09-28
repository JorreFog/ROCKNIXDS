#!/bin/sh
# shbench.sh [shader...]   (runs ON the device while ES runs; tools/shaders.sh pushes and runs it)
#
# GPU time per 640x480 panel for each shader, the way libdsflip draws (DraStic's buffer imported as a dma-buf; COPY=1:
# uploaded first, libdsflip's DSFLIP_SHADER_COPY=1), draws queued back to back with a fence each (PIPE=1: GPU throughput, not round trips). Real frames:
# frames/top2x.raw + bot2x.raw (512x384) and top1x.raw + bot1x.raw (256x192); ms = the mean of both screens.
# "null" is a pass-through (one bilinear fetch): the floor every shader pays for the upload and writing the panel.
# GPU clock: CLOCK=<Hz> (default 800000000), pinned for the run and restored after. REPS (default 300).
# Shaders: dsflip/shaders/*.frag pushed to shaders/ here, ROCKNIX's built-ins by name (read from libdrastouch).
cd "$(dirname "$0")"
[ -n "$COPY" ] || unset COPY               # shtest takes COPY being set at all as upload mode
G=/sys/class/devfreq/fde60000.gpu
OLD_G=$(cat $G/governor) OLD_MIN=$(cat $G/min_freq) OLD_MAX=$(cat $G/max_freq)
CLK=${CLOCK:-800000000}
echo performance > $G/governor; echo 200000000 > $G/min_freq; echo $CLK > $G/max_freq
sleep 1
[ $# -gt 0 ] || set -- null ds-crisp ds-crisp-color ds-grid ds-grid-color ds-grid-2x ds-integer ds-fsr \
                       sharp-bilinear lcd1x-nds-color lcd3x scanlines sharp-shimmerless quilez
printf '%-18s %8s %8s   (GPU at %d MHz, ms per panel)\n' shader 2x 1x $((CLK / 1000000))
for sh in "$@"; do
    line=$(printf '%-18s' $sh)
    for res in 2x 1x; do
        [ $res = 2x ] && W=512 H=384 || W=256 H=192
        tot=0; ok=1
        for scr in top bot; do
            ms=$(env ${COPY:+COPY=1} PIPE=1 REPS=${REPS:-300} SRC=frames/$scr$res.raw DSFLIP_SHADER_DIR=$PWD/shaders ./shtest $sh $W $H /tmp/shbench.ppm 2>&1 |
                 sed -n 's/avg \(.*\) ms per draw/\1/p')
            [ -n "$ms" ] || { ok=0; break; }
            tot=$(echo "$tot $ms" | awk '{print $1 + $2}')
        done
        [ $ok = 1 ] && line="$line $(echo $tot | awk '{printf "%8.2f", $1 / 2}')" || line="$line     FAIL"
    done
    echo "$line"
done
echo $OLD_G > $G/governor; echo $OLD_MAX > $G/max_freq; echo $OLD_MIN > $G/min_freq
rm -f /tmp/shbench.ppm
