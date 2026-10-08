#!/bin/sh
# Run ON the RG DS. Retunes both DSI panels from 60.10 Hz to 60.0013 Hz by changing the porches
# in the DTB's panel_description text in place (same length, no dtc needed). The pixel clock stays
# 42134 kHz: pll_vpll is 126.4 MHz and the VOP divides by an integer, so any lower clock falls to
# /4, which is about 45 Hz. Check dclk_vop0/1 = 42133334 in /sys/kernel/debug/clk/clk_summary after rebooting.
# Only patches a DTB that contains the stock RG DS timing string exactly twice (both panels).
# A ROCKNIX update overwrites /flash and undoes this: the installer keeps a copy of this script, and the first boot on
# the new ROCKNIX runs it again (autostart-rocknixds-os). Backup: /storage/rg-ds.dtb.bak
# --check only says which timing the DTB has: "60hz", "stock" or "unknown" (no RG DS DTB: "none"); nothing is written.
set -e
DTB=/flash/device_trees/rk3568-anbernic-rg-ds.dtb
if [ "$1" = --check ]; then
    [ -f $DTB ] || { echo none; exit 0; }
    python3 - "$DTB" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
old = b"horizontal=640,260,220,260 vertical=480,10,2,16"
new = b"horizontal=640,233,220,260 vertical=480,21,2,16"
print("60hz" if d.count(new) == 2 and d.count(old) == 0 else "stock" if d.count(old) == 2 else "unknown")
PY
    exit 0
fi
[ -f $DTB ] || { echo "no $DTB: not an RG DS, not touching anything"; exit 1; }
RESULT=$(python3 - "$DTB" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
old = b"horizontal=640,260,220,260 vertical=480,10,2,16"
new = b"horizontal=640,233,220,260 vertical=480,21,2,16"
if d.count(new) == 2 and d.count(old) == 0: print("already"); sys.exit()
if d.count(old) != 2: print("unknown"); sys.exit()
p = d.replace(old, new)
assert len(p) == len(d) and p.count(new) == 2
open("/storage/rg-ds.dtb.60hz", "wb").write(p)
print("ok")
PY
)
case $RESULT in
already) echo "60 Hz panel timing already applied"; exit 0 ;;
unknown) echo "DTB doesn't contain the stock RG DS panel timing twice: not touching it"; exit 1 ;;
ok) ;;
*) echo "DTB check failed"; exit 1 ;;
esac
cp $DTB /storage/rg-ds.dtb.bak
mount -o remount,rw /flash
cp /storage/rg-ds.dtb.60hz $DTB && sync
mount -o remount,ro /flash
cmp -s /storage/rg-ds.dtb.60hz $DTB || { echo "write verify failed; restoring"; mount -o remount,rw /flash; cp /storage/rg-ds.dtb.bak $DTB; sync; mount -o remount,ro /flash; exit 1; }
echo "60 Hz panel timing applied; reboot to use it (backup: /storage/rg-ds.dtb.bak)"
