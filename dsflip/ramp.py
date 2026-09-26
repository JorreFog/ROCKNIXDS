#!/usr/bin/env python3
# ramp.py <probe.log> [frames-per-level] -- per-level fps for a dsstress-ramp run (level = 1 + presents // ramp)
import re, sys
ramp = int(sys.argv[2]) if len(sys.argv) > 2 else 300
cum, lv = 0, {}
for line in open(sys.argv[1]):
    m = re.match(r"\[sec\] pres/s=([\d.]+).*lock=(\d+).*copy=(\d+) present=(\d+)", line)
    if not m: continue
    fps = float(m[1]); level = min(10, 1 + cum // ramp); cum += round(fps)
    lv.setdefault(level, []).append((fps, int(m[2]) + int(m[3]) + int(m[4])))
for l, v in sorted(lv.items()):
    f = [x[0] for x in v]
    inner = f[1:-1] or f                   # drop the seconds that straddle a level change
    print(f"L{l:<2} polys={l*192:<5} fps avg {sum(inner)/len(inner):5.1f} min {min(inner):5.1f}  display {sum(x[1] for x in v)//len(v)} us/frame  ({len(v)} s)")
