#!/bin/sh
# power.sh <tag> <secs> [VAR=value...]   (run on a PC; RGDS_SSH=<ssh command> or RGDS_HOST=<ip>)
# Runs tools/hgpower.sh on the device with the display free (ES and sway stopped, then started again) and prints
# its result. See hgpower.sh for the options.
HERE=$(cd "$(dirname "$0")" && pwd)
SSH=${RGDS_SSH:-ssh root@${RGDS_HOST:?set RGDS_HOST or RGDS_SSH}}
TAG=${1:?tag}; SECS=${2:?secs}; shift 2
# never alongside a real game (hgpower.sh checks too): the run presses keys on the real gamepad, and kmsrun.sh's
# cleanup would start sway and ES on top of the game's display
# A failed ssh used to look like "no game" (the check's non-zero status) and the run would continue.
alive=$($SSH 'if systemctl is-active -q dsflip-game || pidof drastic >/dev/null || pidof drastic.real >/dev/null; then echo yes; else echo no; fi') || {
    echo "power.sh: could not check the device over ssh" >&2; exit 3
}
case $alive in
    yes) echo "power.sh: a game is running on the device: not starting" >&2; exit 3 ;;
    no) ;;
    *) echo "power.sh: unexpected answer from the device ($alive)" >&2; exit 3 ;;
esac
for f in tools/powerprobe.py tools/hgpower.sh tools/padkey.py tools/kmsrun.sh; do
    $SSH "cat > /storage/dsflip/$(basename $f)" < "$HERE/../$f"
done
$SSH "cat > /storage/dsflip/walker.py" <<'PY'
#!/usr/bin/env python3
# walker.py <secs>: walks right/left/up/down (1.5 s each) on the real gamepad for <secs>.
import glob, os, struct, sys, time
dev = next(d for d in sorted(glob.glob("/sys/class/input/event*")) if open(d + "/device/name").read().strip() == "retrogame_joypad")
fd = os.open("/dev/input/" + os.path.basename(dev), os.O_WRONLY)
def ev(t, c, v): os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))
end = time.monotonic() + float(sys.argv[1])
while time.monotonic() < end:
    for k in (547, 546, 544, 545):
        ev(1, k, 1); ev(0, 0, 0); time.sleep(1.5); ev(1, k, 0); ev(0, 0, 0)
PY
$SSH "chmod +x /storage/dsflip/*.sh; KMSRUN_TIMEOUT=$((SECS + 90)) systemd-run --wait --quiet --unit=dsflip-kms --collect \
      -E KMSRUN_TIMEOUT=$((SECS + 90)) /storage/dsflip/kmsrun.sh /storage/dsflip/hgpower.sh $TAG $SECS $*; cat /storage/dsflip/probe/$TAG.txt"
