#!/usr/bin/env python3
# padtouch.py tap <x> <y> [secs] | drag <x1> <y1> <x2> <y2> [secs] [hold]
# A finger on the real touchscreen, by writing into its evdev node (the kernel hands written events to every reader,
# as with padkey.py): sway and the menu, or a game, see a real touch. x and y are in the panel's pixels (the bottom
# screen: 0-639 x 0-479 on the RG DS, 0-1023 x 0-767 on the RG DS Plus). A drag moves in steps of about 16 ms and
# stays down <hold> seconds at its end (0.2) before the finger lifts.
import fcntl, glob, os, struct, sys, time
EV_SYN, EV_KEY, EV_ABS, BTN_TOUCH = 0, 1, 3, 0x14a
ABS_X, ABS_Y, MT_SLOT, MT_X, MT_Y, MT_ID = 0, 1, 0x2f, 0x35, 0x36, 0x39
dev = next(d for d in sorted(glob.glob("/sys/class/input/event*")) if "touch" in open(d + "/device/name").read().lower())
fd = os.open("/dev/input/" + os.path.basename(dev), os.O_RDWR)
def absmax(code): return struct.unpack("6i", fcntl.ioctl(fd, 0x80184540 + code, bytes(24)))[2]      # EVIOCGABS
has_mt = True
try: absmax(MT_X)
except OSError: has_mt = False
def ev(t, c, v): os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))
def pos(x, y):
    if has_mt: ev(EV_ABS, MT_X, x); ev(EV_ABS, MT_Y, y)
    ev(EV_ABS, ABS_X, x); ev(EV_ABS, ABS_Y, y)
def down(x, y):
    if has_mt: ev(EV_ABS, MT_SLOT, 0); ev(EV_ABS, MT_ID, int(time.time() * 10) % 60000 + 1)
    pos(x, y); ev(EV_KEY, BTN_TOUCH, 1); ev(EV_SYN, 0, 0)
def up():
    if has_mt: ev(EV_ABS, MT_ID, -1)
    ev(EV_KEY, BTN_TOUCH, 0); ev(EV_SYN, 0, 0)
a = sys.argv[1:]
if a and a[0] == "tap" and len(a) >= 3:
    down(int(a[1]), int(a[2])); time.sleep(float(a[3]) if len(a) > 3 else 0.08); up()
elif a and a[0] == "drag" and len(a) >= 5:
    x1, y1, x2, y2 = (int(v) for v in a[1:5]); secs = float(a[5]) if len(a) > 5 else 0.5; hold = float(a[6]) if len(a) > 6 else 0.2
    n = max(2, int(secs / 0.016))
    down(x1, y1)
    for i in range(1, n + 1):
        time.sleep(secs / n); pos(x1 + (x2 - x1) * i // n, y1 + (y2 - y1) * i // n); ev(EV_SYN, 0, 0)
    time.sleep(hold); up()
else:
    sys.exit("usage: padtouch.py tap <x> <y> [secs] | drag <x1> <y1> <x2> <y2> [secs] [hold]")
