#!/usr/bin/env python3
# touchtap.py <x> <y> [secs] [device-path-substring] [x1 y1]: a real tap (or, with x1 y1, a swipe to there over secs)
# on the RG DS touchscreen, written into its evdev node: the kernel hands written events to every reader, so sway/
# libinput or libdsflip see a real touch. Raw coords = panel pixels.
import glob, os, struct, sys, time
a = sys.argv[1:]
x, y = int(a[0]), int(a[1]); secs = float(a[2]) if len(a) > 2 else 0.12
want = a[3] if len(a) > 3 else "fe5e0000.i2c"
x1, y1 = (int(a[4]), int(a[5])) if len(a) > 5 else (x, y)
dev = next(d for d in sorted(glob.glob("/sys/class/input/event*")) if want in os.path.realpath(d + "/device"))
fd = os.open("/dev/input/" + os.path.basename(dev), os.O_WRONLY)
def ev(t, c, v): os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ev(EV_ABS, 0x2f, 0); ev(EV_ABS, 0x39, 77); ev(EV_ABS, 0x35, x); ev(EV_ABS, 0x36, y); ev(EV_ABS, 0x30, 30)
ev(EV_KEY, 0x14a, 1); ev(EV_ABS, 0, x); ev(EV_ABS, 1, y); ev(EV_SYN, 0, 0)
steps = 12 if (x1, y1) != (x, y) else 1
for k in range(1, steps + 1):
    time.sleep(secs / steps)
    if steps > 1:
        cx, cy = x + (x1 - x) * k // steps, y + (y1 - y) * k // steps
        ev(EV_ABS, 0x35, cx); ev(EV_ABS, 0x36, cy); ev(EV_ABS, 0, cx); ev(EV_ABS, 1, cy); ev(EV_SYN, 0, 0)
ev(EV_ABS, 0x2f, 0); ev(EV_ABS, 0x39, -1); ev(EV_KEY, 0x14a, 0); ev(EV_SYN, 0, 0)
print(("swiped %d,%d -> %d,%d" % (x, y, x1, y1)) if steps > 1 else ("tapped %d %d" % (x, y)), "on", os.path.basename(dev), file=sys.stderr)
