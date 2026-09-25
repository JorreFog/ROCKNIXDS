#!/bin/sh
# Run ON the RG DS. Retunes both DSI panels from 60.10 Hz to 60.0013 Hz by changing the porches
# in the DTB's panel_description text in place (same length, no dtc needed). The pixel clock stays
# 42134 kHz: pll_vpll is 126.4 MHz and the VOP divides by an integer, so any lower clock falls to
# /4, which is about 45 Hz. Check dclk_vop0/1 = 42133334 in /sys/kernel/debug/clk/clk_summary after rebooting.
# A ROCKNIX update overwrites /flash and undoes this.
set -e
DTB=/flash/device_trees/rk3568-anbernic-rg-ds.dtb
STOCK=1ad5ac3f50701304de1a00b7728e5a0a PATCHED=459c6a96159a1c0efd851a6c0a3db85f
cur=$(md5sum $DTB | cut -d' ' -f1)
[ "$cur" = "$PATCHED" ] && { echo "already patched"; exit 0; }
[ "$cur" = "$STOCK" ] || { echo "unknown DTB ($cur), not touching it"; exit 1; }
cp $DTB /storage/rg-ds.dtb.bak
python3 - <<'PY'
d = open("/flash/device_trees/rk3568-anbernic-rg-ds.dtb", "rb").read()
old = b"horizontal=640,260,220,260 vertical=480,10,2,16"
new = b"horizontal=640,233,220,260 vertical=480,21,2,16"
assert d.count(old) == 2
open("/storage/rg-ds.dtb.60hz", "wb").write(d.replace(old, new))
PY
[ "$(md5sum /storage/rg-ds.dtb.60hz | cut -d' ' -f1)" = "$PATCHED" ] || { echo "md5 mismatch"; exit 1; }
mount -o remount,rw /flash && cp /storage/rg-ds.dtb.60hz $DTB && sync && mount -o remount,ro /flash
echo "patched; reboot to apply"
