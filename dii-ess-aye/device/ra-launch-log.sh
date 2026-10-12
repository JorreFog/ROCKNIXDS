#!/bin/sh
# ra-launch-log.sh: what a RetroArch launch looked like, for a bug seen on the RG DS Plus (test build 4 feedback):
# now and then a game (GBA) opens with the wrong aspect ratio, and closing and reopening it fixes it. Not reproduced:
# ten launches in a row were all right (3:2, GBA pixels ~4.27 x 4.27; launched normally, with the bottom panel
# focused, and right after a DS game). sway runs this for each RetroArch window (sway-config.theme); it keeps the last
# 5 launches in /storage/.config/rocknixds/ra-launch/<time>/: the windows and outputs as sway has them, the aspect,
# viewport and shader settings the launch got, and the top panel 3 s and 10 s in. Compare a bad launch with a good one.
D=/storage/.config/rocknixds/ra-launch
export XDG_RUNTIME_DIR=/var/run/0-runtime-dir WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
export SWAYSOCK=${SWAYSOCK:-$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1)}
O=$D/$(date +%Y%m%d-%H%M%S)
mkdir -p "$O" || exit 0
ls -1d $D/*/ 2>/dev/null | head -n -5 | while read -r old; do rm -rf "$old"; done
snap() {   # snap <tag>
    {
        echo "== $1 ($(cut -d' ' -f1 /proc/uptime) s up)"
        swaymsg -t get_tree 2>/dev/null | python3 -c '
import json, sys
def walk(n, out=None):
    if n.get("type") == "output": out = n["name"]
    if n.get("pid"):
        r, g = n["rect"], n.get("geometry") or {}
        print("window %s %s on %s: %dx%d+%d+%d, surface %sx%s, fullscreen %s, %s, focused %s" % (n.get("app_id"),
              repr(n.get("name")), out, r["width"], r["height"], r["x"], r["y"], g.get("width"), g.get("height"),
              n.get("fullscreen_mode"), n.get("type"), n.get("focused")))
    for c in n.get("nodes", []) + n.get("floating_nodes", []): walk(c, out)
walk(json.load(sys.stdin))'
        swaymsg -t get_outputs 2>/dev/null | python3 -c '
import json, sys
for o in json.load(sys.stdin):
    r, m = o["rect"], o.get("current_mode") or {}
    print("output %s: %sx%s at %d,%d, scale %s, %s, power %s, focused %s" % (o["name"], m.get("width"), m.get("height"),
          r["x"], r["y"], o.get("scale"), o.get("transform"), o.get("power"), o.get("focused")))'
    } >> "$O/state.txt" 2>&1
    grim -o DSI-2 "$O/top-$1.png" 2>/dev/null
}
{
    echo "== the launch's settings (/tmp/.retroarch.cfg, then retroarch.cfg)"
    grep -E "aspect|viewport|scale_integer|shader|rotation|overlay|fullscreen" /tmp/.retroarch.cfg 2>/dev/null
    grep -E "^(aspect_ratio_index|video_aspect|video_force_aspect|custom_viewport|video_scale_integer|video_shader|video_fullscreen|video_windowed)" \
        /storage/.config/retroarch/retroarch.cfg 2>/dev/null
    ps -o args 2>/dev/null | grep -m1 "[r]etroarch -L"
} > "$O/state.txt" 2>&1
sleep 3; snap 3s
sleep 7; snap 10s
grep -a -E "\[Core\] Geometry|Set video size|monitor index|aspect" /var/log/exec.log 2>/dev/null | tail -n 6 > "$O/retroarch.txt"
