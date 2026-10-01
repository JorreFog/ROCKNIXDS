#!/bin/sh
# Run ON the device. Retunes both DSI panels to (almost exactly) 60 Hz, DraStic's frame rate, by changing the porches
# in the DTB's panel_description text in place (same length, no dtc needed). The pixel clock is never touched: the
# VOP divides a fixed PLL by an integer, so a different clock falls to the next divider.
#
# RG DS: 60.10 Hz -> 60.0013 Hz. The pixel clock stays 42134 kHz: pll_vpll is 126.4 MHz, /3 = 42133334, and any
#   lower clock falls to /4, which is about 45 Hz. Check dclk_vop0/1 = 42133334 in /sys/kernel/debug/clk/clk_summary
#   after rebooting.
# RG DS Plus: 60.17 Hz -> 60.0027 Hz. ROCKNIX's DTS asks for 62770 kHz from a 816 MHz PLL, /13 = 62769231 Hz; with
#   the stock 1304x800 total that's 60.170 Hz. The porches only grow (h front 120 -> 122, v front 16 -> 17, total
#   1306x801). The script checks the live pixel clock first and refuses if it isn't 62769231.
#
# Only patches a DTB that contains the stock timing string exactly twice (both panels).
# A ROCKNIX update overwrites /flash and undoes this. Backup: /storage/<dtb name>.bak
set -e
MODEL=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
case "$MODEL" in
*"RG DS Plus"*)
    DTB=/flash/device_trees/rk3568-anbernic-rg-ds-plus.dtb
    OLD="horizontal=1024,120,80,80 vertical=768,16,8,8"
    NEW="horizontal=1024,122,80,80 vertical=768,17,8,8"
    DCLK=62769231 HZ=60.0027 ;;
*)
    DTB=/flash/device_trees/rk3568-anbernic-rg-ds.dtb
    OLD="horizontal=640,260,220,260 vertical=480,10,2,16"
    NEW="horizontal=640,233,220,260 vertical=480,21,2,16"
    DCLK=42133334 HZ=60.0013 ;;
esac
NAME=$(basename $DTB .dtb)
BAK=/storage/$NAME.dtb.bak PATCHED=/storage/$NAME.dtb.60hz
[ "$NAME" = rk3568-anbernic-rg-ds ] && BAK=/storage/rg-ds.dtb.bak PATCHED=/storage/rg-ds.dtb.60hz   # the names 1.0-1.4 used
[ -f $DTB ] || { echo "no $DTB: not an RG DS / RG DS Plus, not touching anything"; exit 1; }

# the porches below are only right for the pixel clock they were worked out for: check the running one
CLK=/sys/kernel/debug/clk/clk_summary
[ -r $CLK ] || mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
RATE=$(awk '$1 == "dclk_vop0" || $1 == "dclk_vop1" { for (i = 2; i <= NF; i++) if ($i ~ /^[0-9][0-9][0-9][0-9][0-9][0-9][0-9]+$/) { print $i; exit } }' $CLK 2>/dev/null)
if [ -z "$RATE" ]; then
    echo "can't read the panels' pixel clock ($CLK): not touching the DTB"; exit 1
fi
if [ $((RATE - DCLK)) -gt 2000 ] || [ $((DCLK - RATE)) -gt 2000 ]; then
    echo "pixel clock is $RATE Hz, expected $DCLK: the 60 Hz porches don't apply here, not touching the DTB"; exit 1
fi

RESULT=$(python3 - "$DTB" "$OLD" "$NEW" "$PATCHED" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
old, new = sys.argv[2].encode(), sys.argv[3].encode()
assert len(old) == len(new)
if d.count(new) == 2 and d.count(old) == 0: print("already"); sys.exit()
if d.count(old) != 2: print("unknown"); sys.exit()
p = d.replace(old, new)
assert len(p) == len(d) and p.count(new) == 2
open(sys.argv[4], "wb").write(p)
print("ok")
PY
)
case $RESULT in
already) echo "60 Hz panel timing already applied"; exit 0 ;;
unknown) echo "DTB doesn't contain the stock panel timing twice: not touching it"; exit 1 ;;
ok) ;;
*) echo "DTB check failed"; exit 1 ;;
esac
cp $DTB $BAK
mount -o remount,rw /flash
cp $PATCHED $DTB && sync
mount -o remount,ro /flash
cmp -s $PATCHED $DTB || { echo "write verify failed; restoring"; mount -o remount,rw /flash; cp $BAK $DTB; sync; mount -o remount,ro /flash; exit 1; }
echo "60 Hz panel timing ($HZ Hz) applied; reboot to use it (backup: $BAK)"
