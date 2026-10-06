#!/usr/bin/env python3
# padkey.py <code> [secs] -- press a key on the real gamepad by writing into its evdev node
# (the kernel re-dispatches written events to every reader, so SDL/DraStic see a real press).
# evdev codes on the RG DS pad, by the letters on the handheld: A=305 (BTN_EAST) B=304 (BTN_SOUTH) X=307 Y=308 L=310 R=311
# 6 L2=312 (load state) 7 R2=313 (save state) 8 SELECT=314 9 START=315 10 MODE=316 (DraStic menu)
import glob, os, struct, sys, time
dev = next(d for d in sorted(glob.glob("/sys/class/input/event*")) if open(d + "/device/name").read().strip() == "retrogame_joypad")
fd = os.open("/dev/input/" + os.path.basename(dev), os.O_WRONLY)
code, secs = int(sys.argv[1]), float(sys.argv[2]) if len(sys.argv) > 2 else 0.3
def ev(t, c, v): os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))
ev(1, code, 1); ev(0, 0, 0); time.sleep(secs); ev(1, code, 0); ev(0, 0, 0)
