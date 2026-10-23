#!/bin/sh
# irqtest.sh <outdir> [rounds] [layout...]   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
#
# A/B test of where the hardware interrupts go (and, optionally, the scheduler's tick): each layout (tools/irqab.sh)
# runs the same two workloads, round after round, in a different order each round so heat and battery drift fall on
# every layout alike:
#   hg      HeartGold at 2x from savestate 0, walking, 90 s (tools/hgpower.sh via power.sh): frames presented and
#           dropped, the clock libdsflip's CPU governor settles at, battery current, temperature
#   stress  the 3D stress ROM ramp at 2x, 75 s (tools/stressgov.sh): frames per second on the heaviest levels
# libdsflip's CPU governor runs as in a game but forgets between runs (DSFLIP_CPUGOV_MEMORY=0), so no layout inherits
# what an earlier one taught it. Default layouts:
#   base             as ROCKNIX leaves it (every interrupt on CPU0 in practice)
#   irq=3            the interrupts on CPU3
#   irq=0,app=1-3    the interrupts on CPU0, DraStic kept off it
#   irq=3,app=0-2    the interrupts on CPU3, DraStic kept off it
# Tick variants (no kernel rebuild): add e.g. hrtick=1 or irq=3,app=0-2,hrtick=1 to the list.
# Env: WORK=hg|stress|both (default both), GAME=hg|b2, HGSECS (90), SGSECS (75), any extra hgpower.sh option in
# HGARGS (e.g. "DSFLIP_SHADER=ds-crisp"). 3 rounds of the 4 default layouts take ~1 h. Needs ES idle in its menu, no
# game running, the device on the same charger state throughout (or unplugged), and nothing touching it.
# Results: <outdir>/probe.txt (tools/schedprobe.sh, before any run), probe/ and logs/ for each run, irqtest.log, and
# the summary (tools/irqtest-summary.py <outdir>) in summary.txt.
HERE=$(cd "$(dirname "$0")" && pwd)
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
OUT=${1:?outdir}; ROUNDS=${2:-3}; shift; [ $# -gt 0 ] && shift
[ $# -gt 0 ] || set -- base irq=3 irq=0,app=1-3 irq=3,app=0-2
WORK=${WORK:-both} GAME=${GAME:-hg} HGSECS=${HGSECS:-90} SGSECS=${SGSECS:-75}
mkdir -p "$OUT"
log() { echo "$(date +%T) $*" | tee -a "$OUT/irqtest.log"; }
if $SSH 'systemctl is-active -q dsflip-game || pidof drastic.real >/dev/null || pidof drastic >/dev/null'; then
    echo "irqtest: a game is running on the device: not starting" >&2; exit 3
fi
for f in irqab.sh schedprobe.sh stressgov.sh kmsrun.sh; do
    $SSH "cat > /storage/dsflip/$f && chmod +x /storage/dsflip/$f" < "$HERE/$f"
done
$SSH "cat /storage/.config/rocknixds-version 2>/dev/null; md5sum /storage/.config/drastic/dsflip/libdsflip.so" > "$OUT/version.txt"
$SSH "/storage/dsflip/schedprobe.sh 10" > "$OUT/probe.txt" 2>&1
log "probe: $(grep -m1 '^CONFIG_HZ=' "$OUT/probe.txt") $(sed -n '/== cpuidle/{n;p}' "$OUT/probe.txt")"

fetch() {   # fetch <tag>: the run's files from the device
    $SSH "cd /storage/dsflip && tar cf - probe/$1.irq \$(ls probe/$1.txt probe/$1.json logs/hp-$1.log logs/sg-$1.log 2>/dev/null)" \
        | tar xf - -C "$OUT"
}
r=1
while [ $r -le $ROUNDS ]; do
    # this round's order: the layouts rotated by r-1
    order=
    for l in "$@"; do order="$order $l"; done
    k=1; while [ $k -lt $r ]; do order="$(echo $order | cut -d' ' -f2-) $(echo $order | cut -d' ' -f1)"; k=$((k + 1)); done
    for l in $order; do
        slug=$(echo "$l" | tr ',=' '_-')
        if [ "$WORK" != stress ]; then
            t=irq-$r-hg-$slug
            RGDS_SSH="$SSH" HGWRAP="/storage/dsflip/irqab.sh $t $l" "$HERE/power.sh" $t $HGSECS GAME=$GAME \
                DSFLIP_CPUGOV_MEMORY=0 $HGARGS > "$OUT/$t.out" 2>&1
            fetch $t
            log "$r $l hg: $(grep -h '^frames' "$OUT/$t.out" | head -n1)"
        fi
        if [ "$WORK" != hg ]; then
            t=irq-$r-sg-$slug
            $SSH "systemd-run --wait --quiet --unit=dsflip-kms --collect -E KMSRUN_TIMEOUT=$((SGSECS + 75)) \
                  /storage/dsflip/kmsrun.sh /storage/dsflip/irqab.sh $t $l /storage/dsflip/stressgov.sh $t $SGSECS \
                  DSFLIP_CPUGOV_MEMORY=0" > "$OUT/$t.out" 2>&1
            fetch $t
            log "$r $l stress: done"
            sleep 10
        fi
    done
    r=$((r + 1))
done
python3 "$HERE/irqtest-summary.py" "$OUT" | tee "$OUT/summary.txt"
