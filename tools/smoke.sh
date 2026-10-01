#!/bin/sh
# smoke.sh: end-to-end check of a ROCKNIXDS install on a device, run from a PC.
#
#   tools/smoke.sh <device-ip> [seconds] [rom-substring]
#
# Launches a DS game through ES's HTTP API, lets it run, and reports what libdsflip logged: that it took the
# display, the presents per second, dropped frames, audio pump underruns and the RetroAchievements state. While
# the game runs it records the device's own microphone: the speaker is audible to it, so a clear level above the
# menu's silence proves sound is playing. It then dumps both panels' scanout buffers (SIGUSR2) and converts them
# to PNGs next to this script's output dir, kills the game the way ROCKNIX's exit hotkey does, and checks that ES
# is back. Exit status 0 = every check passed.
#
# Needs ssh access as root (set RGDS_SSH to a wrapper if you use an askpass), and python3 on the PC.
# Output: smoke-out/<timestamp>/ with the log excerpt, the mic WAV and the scanout PNGs.
set -u
IP=${1:?usage: smoke.sh <device-ip> [seconds] [rom-substring]}
SECS=${2:-30}
ROMPAT=${3:-}
SSH=${RGDS_SSH:-"ssh -o ConnectTimeout=5 root@$IP"}
OUT=$(dirname "$0")/smoke-out/$(date +%Y%m%d-%H%M%S); mkdir -p "$OUT"
D=/storage/.config/drastic/dsflip
fail=0
ok()   { printf '  \033[1;32mok\033[0m   %s\n' "$*"; }
bad()  { printf '  \033[1;31mFAIL\033[0m %s\n' "$*"; fail=1; }
info() { printf '  ..   %s\n' "$*"; }

echo "== ROCKNIXDS smoke test on $IP ($SECS s)"
$SSH 'curl -s localhost:1234/isIdle 2>/dev/null | grep -q true' || { bad "ES's API isn't answering (is ES running?)"; exit 1; }
$SSH 'systemctl is-active -q dsflip-game' && { bad "a game is already running"; exit 1; }
ROM=$($SSH "ls /storage/roms/nds/*.nds 2>/dev/null | grep -i -- \"${ROMPAT}\" | head -n1")
[ -n "$ROM" ] || { bad "no .nds ROM matching '$ROMPAT' in /storage/roms/nds"; exit 1; }
info "ROM: $ROM"

# quiet reference: the mic in the menu (PipeWire needs the session's runtime dir)
RT='XDG_RUNTIME_DIR=/var/run/0-runtime-dir'
$SSH "$RT arecord -q -D default -d 2 -f S16_LE -r 44100 -c 1 /tmp/smoke-quiet.wav 2>/dev/null; echo" >/dev/null
# no play stats for test launches (session.sh skips them while this file exists)
$SSH "touch /tmp/rocknixds-testing"; trap '$SSH "rm -f /tmp/rocknixds-testing" 2>/dev/null' EXIT
$SSH "curl -s -X POST --data-binary '$ROM' localhost:1234/launch" >/dev/null
sleep 12
$SSH 'systemctl is-active -q dsflip-game' && ok "game unit is running" || bad "game unit didn't start"
# DraStic's children inherit LD_PRELOAD. The volume watcher runs `sh -c pactl subscribe` via popen,
# and that shell's constructor renames dsflip.log aside, then writes a passthrough line of its own.
# The game's log is whichever name still has the DRM-master line.
LOG=$($SSH 'f='"$D"'/dsflip.log
  for c in "$f" "$f.1" "$f.2" "$f.3"; do
    if grep -q "DRM master" "$c" 2>/dev/null || grep -q "^\[dsflip\] ready" "$c" 2>/dev/null; then echo "$c"; exit 0; fi
  done
  echo "$f"')
if $SSH "grep -q '^\[dsflip\] ready' '$LOG' || grep -q 'DRM master' '$LOG'"; then ok "libdsflip took the display"; else bad "libdsflip didn't take the display (passthrough?)"; fi
$SSH "head -n 20 '$LOG'" > "$OUT/start.log"
grep -m1 '^\[dsflip\] libdsflip' "$OUT/start.log" | sed 's/^/  ..   /' || info "no version line (pre-1.3 build)"
grep -m1 '^\[shader\]' "$OUT/start.log" | sed 's/^/  ..   /'
grep -m1 '^\[audio\] pump' "$OUT/start.log" | sed 's/^/  ..   /'

# let it run, sampling the per-second lines; record the speaker through the mic meanwhile
$SSH "$RT arecord -q -D default -d 4 -f S16_LE -r 44100 -c 1 /tmp/smoke-game.wav 2>/dev/null; $RT timeout 4 pw-record -P '{ stream.capture.sink = true }' --rate 44100 --channels 1 --format s16 /tmp/smoke-mon.wav >/dev/null 2>&1" &
sleep $SECS
wait
# re-pick: another child may have rotated the name again while the game ran
LOG=$($SSH 'f='"$D"'/dsflip.log
  for c in "$f" "$f.1" "$f.2" "$f.3"; do
    if grep -q "DRM master" "$c" 2>/dev/null || grep -q "^\[dsflip\] ready" "$c" 2>/dev/null; then echo "$c"; exit 0; fi
  done
  echo "$f"')
$SSH "grep '^\[dsflip\] present/s' '$LOG' | tail -n $SECS" > "$OUT/present.log"
python3 - "$OUT/present.log" "$SECS" <<'EOF' || fail=1
import re, sys
lines = open(sys.argv[1]).read().splitlines()
ps = [float(re.search(r'present/s=([\d.]+)', l).group(1)) for l in lines]
dr = [int(re.search(r'dropped=(\d+)', l).group(1)) for l in lines]
n = len(ps)
if n < 5: print("  FAIL fewer than 5 seconds of pacing lines"); sys.exit(1)
avg = sum(ps) / n; drops = sum(dr) / n
print(f"  {'ok  ' if 58 <= avg <= 62 else 'FAIL'} {avg:.1f} presents/s over {n} s")
print(f"  {'ok  ' if drops <= 0.5 else 'FAIL'} {drops:.2f} dropped frames/s ({sum(dr)} in {n} s)")
sys.exit(0 if 58 <= avg <= 62 and drops <= 0.5 else 1)
EOF
AUD=$($SSH "grep '^\[audio\] pump' '$LOG' | tail -n1")
info "${AUD:-no audio pump line}"
case "$AUD" in *"underruns 0,"*) ok "no audio underruns" ;; "") ;; *) bad "audio underruns reported" ;; esac
RA=$($SSH "grep -E '^\[ra\] (game |logged|RetroAchievements off|login|game load)' '$LOG' | tail -n1")
info "RetroAchievements: ${RA:-nothing logged}"

# audio: the sink monitor is what DraStic actually plays (must have signal); the mic hears the speaker (advisory:
# a muted or quiet mic is not a game fault)
$SSH 'cat /tmp/smoke-quiet.wav' > "$OUT/quiet.wav"; $SSH 'cat /tmp/smoke-game.wav' > "$OUT/game.wav"
$SSH 'cat /tmp/smoke-mon.wav 2>/dev/null' > "$OUT/monitor.wav"
python3 - "$OUT/quiet.wav" "$OUT/game.wav" "$OUT/monitor.wav" <<'EOF' || fail=1
import wave, struct, math, sys, os
def rms(p):
    try:
        w = wave.open(p); d = w.readframes(w.getnframes()); w.close()
    except Exception:
        return -1
    s = struct.unpack("<%dh" % (len(d) // 2), d)
    return math.sqrt(sum(v * v for v in s) / max(1, len(s))) / 32768 if s else -1
q, g, m = (rms(p) for p in sys.argv[1:4])
if m < 0: print("  FAIL sink monitor capture failed (pw-record)"); sys.exit(1)
print(f"  {'ok  ' if m > 0.005 else 'FAIL'} audio reaches the sink: monitor {m:.4f} rms")
print(f"  {'ok  ' if g > max(0.01, 3 * q) else 'warn'} speaker heard by the mic: game {g:.4f} rms vs menu {q:.4f}")
sys.exit(0 if m > 0.005 else 1)
EOF

# what's on the panels
# Production sessions exec the dsflip/drastic symlink, so /proc/*/comm is "drastic" (not drastic.real).
$SSH "rm -f /storage/dsflip/logs/scan*.raw; mkdir -p /storage/dsflip/logs; kill -USR2 \$(pidof drastic || pidof drastic.real); sleep 1; cd /storage/dsflip/logs && tar cf - scan0.raw scan1.raw 2>/dev/null" | tar xf - -C "$OUT" 2>/dev/null
python3 - "$OUT" <<'EOF' || bad "scanout dump missing"
import sys, os
out = sys.argv[1]
try:
    from PIL import Image
except ImportError:
    Image = None
for i in (0, 1):
    p = os.path.join(out, f"scan{i}.raw")
    if not os.path.exists(p): sys.exit(1)
    with open(p, "rb") as f:
        w, h, pitch, bpp = (int(v) for v in f.readline().split()); px = f.read()
    if Image is None: continue
    if bpp == 32:
        im = Image.frombuffer("RGBX", (w, h), px, "raw", "BGRX", pitch, 1).convert("RGB")
    else:
        im = Image.frombuffer("RGB", (w, h), px, "raw", "BGR;16", pitch, 1)
    im.save(os.path.join(out, f"panel{i}.png"))
print("  ok   scanout buffers dumped" + ("" if Image else " (no Pillow: raw only)"))
EOF

# quit like the exit hotkey, then ES must come back
$SSH 'kill -9 $(pidof drastic || pidof drastic.real) 2>/dev/null; for i in $(seq 1 40); do sleep 1; [ "$(systemctl is-active dsflip-game)" != active ] && curl -s localhost:1234/isIdle 2>/dev/null | grep -q true && exit 0; done; exit 1' \
    && ok "ES is back after quitting" || bad "ES didn't come back within 40 s"
$SSH "tail -n 4 $D/last-session.log" > "$OUT/session.log"
echo "== $( [ $fail = 0 ] && echo PASSED || echo FAILED ), details in $OUT"
exit $fail
