#!/bin/sh
# opt-suite.sh <label> <outdir>   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
#
# The measurement set behind docs/optimization-1.4.md, for whichever ROCKNIXDS version is installed, so two versions
# can be compared the same way (1.3 and 1.4 were each measured right after a reboot into them):
#   idle   the menu left alone: 100 s to settle (animations, power saver, audio), then 3 x 60 s powerprobe windows
#   hg     HeartGold at 2x, walking (tools/hgpower.sh via power.sh): no shader, ds-crisp, sharp-bilinear, 2 x 90 s each,
#          with the CPU governor ROCKNIX sets for DS games ("performance") and the installed session's audio rate
#   stress the 3D stress ROM ramp (tools/stressgov.sh), 75 s: presents per second as the load grows
#   switch launch/quit times (tools/switchtime.sh, 3 cycles)
# Results (powerprobe JSON + text, libdsflip logs) are copied to <outdir>. Takes ~30 minutes. Needs ES idle in its
# menu, no game running, and nothing touching the device.
HERE=$(cd "$(dirname "$0")" && pwd)
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
L=${1:?label}; OUT=${2:?outdir}
mkdir -p "$OUT"
log() { echo "$(date +%T) $*" | tee -a "$OUT/suite.log"; }
$SSH "cat > /storage/dsflip/powerprobe.py" < "$HERE/powerprobe.py"
$SSH "cat > /storage/dsflip/stressgov.sh && chmod +x /storage/dsflip/stressgov.sh" < "$HERE/stressgov.sh"
$SSH "cat /storage/.config/rocknixds-version 2>/dev/null; md5sum /storage/.config/drastic/dsflip/libdsflip.so" > "$OUT/version.txt"

log "idle: settling 100 s"
sleep 100
for i in 1 2 3; do
    $SSH "python3 /storage/dsflip/powerprobe.py 60 $L-idle-$i" > "$OUT/$L-idle-$i.txt"
    log "idle $i: $(head -n1 "$OUT/$L-idle-$i.txt" | cut -c1-110)"
done

for sh in none ds-crisp sharp-bilinear; do
    for r in 1 2; do
        RGDS_SSH="$SSH" "$HERE/power.sh" $L-hg-$sh-$r 90 CPUGOV=performance DSFLIP_SHADER=$sh > "$OUT/$L-hg-$sh-$r.out" 2>&1
        log "hg $sh $r: $(grep -E "^$L-hg|^frames" "$OUT/$L-hg-$sh-$r.out" | tr '\n' ' ' | cut -c1-200)"
    done
done

$SSH "systemd-run --wait --quiet --unit=dsflip-kms --collect -E KMSRUN_TIMEOUT=150 /storage/dsflip/kmsrun.sh /storage/dsflip/stressgov.sh $L-stress 75"
log "stress: done"
sleep 10
RGDS_SSH="$SSH" "$HERE/switchtime.sh" x 3 > "$OUT/$L-switch.txt" 2>&1
log "switch: $(tail -n 3 "$OUT/$L-switch.txt" | tr '\n' ' ')"

$SSH "cd /storage/dsflip && tar cf - probe/$L-* logs/hp-$L-* logs/sg-$L-stress.log 2>/dev/null" | tar xf - -C "$OUT"
log "results in $OUT"
