#!/bin/sh
# libdsflip game session (runs as systemd unit dsflip-game, outside ES's process tree).
# Stops ES + sway, runs DraStic with libdsflip on the bare panels, then brings sway + ES back. If this script is
# killed with the unit (systemctl stop dsflip-game), the unit's ExecStopPost (restore.sh) does that instead.
# The exit hotkey (killall -9 drastic) works because DraStic runs through a symlink named "drastic".
D=/storage/.config/drastic
LOG=$D/dsflip/last-session.log
STATE=/tmp/dsflip-state          # libdsflip's verdict: "ready" or "passthrough: <why>"
NOTICE=/tmp/dsflip-notice        # why the game ended early; restore.sh shows it in ES once ES is back
ROM="$1"
T0=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - T0) / 1000000 )); }
up() { read u _ < /proc/uptime; echo "[$u]"; }  # uptime stamp: lines up with restore.sh and switchtime.sh
# DraStic still running: alive and not a zombie (an exited child stays in /proc, and kill -0 succeeds, until waited for)
alive() { [ -d /proc/$1 ] && ! grep -q '^State:[[:space:]]*Z' /proc/$1/status 2>/dev/null; }
# a SIGKILL that hasn't been delivered: the process is stuck in the kernel (state D) and can't die until that returns
kill_pending() {
  for m in $(awk '/^(SigPnd|ShdPnd):/ { print $2 }' /proc/$1/status 2>/dev/null); do
    [ $(( 0x$m & 0x100 )) -ne 0 ] && return 0
  done
  return 1
}
# what a DraStic that can't be killed is waiting for, and the kernel's messages, in a file that survives a reset
stuck_report() {
  F=$D/dsflip/stuck-$(date +%Y%m%d-%H%M%S).txt
  { echo "$(date) DraStic (pid $P) still alive 5 s after SIGKILL"
    grep -E '^(State|SigPnd|ShdPnd):' /proc/$P/status 2>/dev/null
    for t in /proc/$P/task/[0-9]*; do
      echo "-- thread ${t##*/} $(cat $t/comm 2>/dev/null) state $(awk '{ print $3 }' $t/stat 2>/dev/null) wchan $(cat $t/wchan 2>/dev/null)"
      cat $t/stack 2>/dev/null
    done
    echo "-- dmesg"; dmesg 2>/dev/null | tail -n 100
  } > "$F" 2>&1
  ls -t $D/dsflip/stuck-*.txt 2>/dev/null | tail -n +6 | while read -r old; do rm -f "$old"; done   # the last five
  echo "DraStic is stuck in the kernel and can't be stopped: $F"
  echo "The game got stuck while starting and couldn't be stopped. If the screens stay white, hold the power button to restart. Details: $F" > $NOTICE
  sync
}
{
  echo "$(date) start: $ROM (shader: ${DSHOOK_SHADER:-none})"
  # 3x (SuperDrastic's renderer with ES's "3D resolution" at 3x): measured on the RG DS Plus (HeartGold walking,
  # 1992 MHz, 2026-10-04) it held 59.8-60.2 fps without the LCD shader and 56.5-58.1 with lcd3x: the shader's threads
  # take cores the three 3D threads need, and 3x already smooths edges by supersampling. So at 3x the shader is off
  # and the power profile is performance, unless this game has its own shader or power profile set in ES.
  CFG=/storage/.config/system/configs/system.cfg GAME=$(basename "$ROM")
  cfgval() { v=$(grep -F "nds[\"$GAME\"].$1=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
             [ -n "$v" ] || v=$(grep "^nds.$1=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2); echo "$v"; }
  PROF3X=
  if [ "$(cfgval renderer)" = superdrastic ] && [ "$(cfgval resolution3d)" = 3x ]; then
    grep -qF "nds[\"$GAME\"].shader=" $CFG 2>/dev/null || export DSHOOK_SHADER=none
    grep -qF "nds[\"$GAME\"].power_profile=" $CFG 2>/dev/null || PROF3X=performance
    echo "3x: shader ${DSHOOK_SHADER:-none}, power profile ${PROF3X:-the game's}"
  fi
  # gptokeyb (start_drastic.sh starts it inside ES's unit) takes ~1.1 s to die on the stop's TERM, and the stop
  # waits for it: that was most of the switch. Nothing uses it in this session (the exit hotkey is ROCKNIX's
  # own), so kill it outright, as start_drastic.sh itself does after a game. One stop for both units: systemd
  # orders it (ES first) without a second round trip.
  kill -9 $(pidof gptokeyb) 2>/dev/null
  if [ -e $D/dsflip/vt-switch ] && systemctl is-active -q sway.service; then
    # VT mode: sway and ES stay up. Switching the console away from sway's VT makes seatd disable sway's session,
    # which releases the display (DRM master) in ~80 ms; restore.sh switches back. tty12 is unused; graphics mode
    # keeps the kernel console from drawing on it in between.
    VT=1
    fgconsole > /tmp/dsflip-vt 2>/dev/null || echo 1 > /tmp/dsflip-vt
    python3 -c 'import fcntl, os; fcntl.ioctl(os.open("/dev/tty12", os.O_RDWR), 0x4B3A, 0)' 2>/dev/null   # KDSETMODE KD_TEXT
    chvt 12
    echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null   # a blanked console powers the panels down under us
    echo "$(ms) ms: display released by VT switch (sway on tty$(cat /tmp/dsflip-vt) stays up)"
  else
    # Stopping sway while its buffers are still being scanned out can leave the display controller in an underrun
    # loop (~84,000 interrupts/s, ~60% of a core, until a full modeset; seen once in ~40 starts). Switching the
    # panels off through sway first prevented it but made every launch ~0.6 s slower (libdsflip then has to power
    # the panels up), so libdsflip checks for the storm when it takes the display and cures it only then.
    systemctl stop essway.service sway.service
    echo "$(ms) ms: ES and sway stopped"
  fi
  # without a shader the Mali GPU does nothing in this mode (the display controller scans out DraStic's
  # buffers), so it goes to powersave (200 MHz): anything more is only heat. With a shader it draws every frame
  # (lcd1x-nds-color: ~3 ms per screen at 800 MHz), under simple_ondemand with a 400 MHz floor. Measured
  # 2026-09-27 (I6: Pokemon Black 2 at 2x, lcd1x-nds-color, ~5 min per setting in one session): pinned 800 MHz
  # dropped 0.11 frames/s, the 400 MHz floor 0.05/s and a 300 MHz floor 0.04/s, with the GPU averaging ~500 MHz
  # instead of 800 (the governor never went below 400, so 300 buys nothing). The SoC settled at ~67 C.
  # Tunable without a rebuild: DSFLIP_SHADER_GOV=performance restores the old pinned clock.
  GPU=/sys/class/devfreq/fde60000.gpu
  GPU_GOV=$(cat $GPU/governor 2>/dev/null)
  GPU_MIN=$(cat $GPU/min_freq 2>/dev/null)
  # The RG DS Plus (1024x768) has 2.56x the pixels per frame for a shader on the same GPU, so every shader
  # starts at the full clock there. ds-fsr at that size is about 24 ms per frame: expect drops; ds-crisp is 4x.
  PANEL=; for m in /sys/class/drm/card*-DSI-*/modes; do read -r PANEL < "$m" 2>/dev/null && [ -n "$PANEL" ] && break; done
  BIG=; [ "${PANEL%%x*}" -gt 640 ] 2>/dev/null && BIG=1
  case "${DSHOOK_SHADER:-none}" in
    none|bilinear) GOV=powersave; MIN= ;;
    # ds-fsr (FSR 1.0) needs ~9.5 ms of GPU per frame: under simple_ondemand it sat at 800 MHz 97% of the time
    # and dropped frames while ramping up from the floor at the start, so it gets the full clock from the start
    ds-fsr) GOV=${DSFLIP_SHADER_GOV:-performance}; MIN=$DSFLIP_SHADER_GPU_MIN ;;
    *) if [ -n "$BIG" ]; then GOV=${DSFLIP_SHADER_GOV:-performance}; MIN=$DSFLIP_SHADER_GPU_MIN
       else GOV=${DSFLIP_SHADER_GOV:-simple_ondemand}; MIN=${DSFLIP_SHADER_GPU_MIN:-400000000}; fi ;;
  esac
  [ -n "$BIG" ] && [ "$DSHOOK_SHADER" = ds-fsr ] &&
    echo "note: ds-fsr costs ~2.5x more at ${PANEL} (~24 ms per frame, estimated): expect dropped frames; ds-crisp is exact 4x here"
  [ -n "$GPU_GOV" ] && { echo "$GPU_GOV" > /tmp/dsflip-gpu-governor; echo "$GPU_MIN" > /tmp/dsflip-gpu-min; echo $GOV > $GPU/governor 2>/dev/null; [ -n "$MIN" ] && echo $MIN > $GPU/min_freq 2>/dev/null; }
  # libdsflip's CPU governor (cpugov.c) lowers the CPU's clock limit while the game runs; restore.sh puts it back.
  # A file left by a session that never got restored holds the real limit: keep it.
  CPU=/sys/devices/system/cpu/cpufreq/policy0
  [ -f /tmp/dsflip-cpu-max ] || cat $CPU/scaling_max_freq > /tmp/dsflip-cpu-max 2>/dev/null
  # cpugov.c sets the clock through scaling_max_freq, which only pins it under the "performance" governor (it turns
  # itself off under any other). ROCKNIX's RG DS builds ran DS games on performance; the RG DS Plus nightly
  # (20260930) defaults system.cpugovernor to ondemand, which left the clock floating between 408 MHz and the
  # profile's limit with cpugov off ("[cpugov] off: governor is ondemand"): Mario Kart ran at 58.8 fps and the
  # audio underran. Run the session on performance whatever ROCKNIX's setting; restore.sh puts the menu's back.
  [ -f /tmp/dsflip-cpu-governor ] || cat $CPU/scaling_governor > /tmp/dsflip-cpu-governor 2>/dev/null
  echo performance > $CPU/scaling_governor 2>/dev/null
  # PipeWire at DraStic's 44.1 kHz for the session (restore.sh puts its settings back): at its usual 48 kHz every
  # cycle resampled DraStic's audio, and DraStic's audio threads cost ~9% of a core; at 44.1 kHz ~5% (measured
  # 2026-09-28, HeartGold at 1608 MHz). Set before DraStic opens its stream: switching mid-stream made the drain
  # uneven for the session. The nightly only allows 48000 (clock.allowed-rates), so 44100 is allowed first.
  # NOT on the RG DS Plus: its speaker amp (aw88166 on I2S3) runs at 48 kHz whatever rate it is given (the I2S
  # clock stays 12.288 MHz): with the graph at 44.1 kHz, 60 s of audio played in 55.75 s and the sink xrun'd, so
  # DraStic was pulled ~3% fast against the display, held by the frame queue, and its ring underran (measured
  # 2026-10-01; at the stock 48 kHz graph, resampled, 60 s took 60.3 s with no xruns). DSFLIP_PW_RATE=44100 forces it.
  PWM="XDG_RUNTIME_DIR=/var/run/0-runtime-dir pw-metadata -n settings"
  if grep -q aw88166 /proc/asound/cards 2>/dev/null && [ -z "$DSFLIP_PW_RATE" ]; then
    echo "PipeWire kept at its own rate: the aw88166 speaker amp runs 48 kHz only"
  else
    eval $PWM 0 clock.allowed-rates 2>/dev/null | sed -n "s/.*value:'\([^']*\)'.*/\1/p" > /tmp/dsflip-pw-rates
    eval $PWM 0 clock.allowed-rates "'[ 44100 48000 ]'" >/dev/null 2>&1
    eval $PWM 0 clock.force-rate ${DSFLIP_PW_RATE:-44100} >/dev/null 2>&1
  fi
  rm -f $STATE $NOTICE
  # test launches (smoke.sh, switchtime.sh) don't teach libdsflip's CPU governor anything about the player's games
  [ -e /tmp/rocknixds-testing ] && export DSFLIP_CPUGOV_MEMORY=0
  # Resume on quit (ES: the game's or DS system's "resume on quit", on unless set off): the exit hotkey sends SIGUSR1
  # instead of killing DraStic, libdsflip saves a savestate to <savestates>/<game>.resume.dss (never one of the
  # player's slots) and quits; the next start of the game loads it, once. A resume state older than the game's own
  # save file is dropped: loading it would put back an older in-game save too (backup_in_savestates).
  CFG=/storage/.config/system/configs/system.cfg GAME=$(basename "$ROM")
  RES=$(grep -F "nds[\"$GAME\"].resume_on_quit=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$RES" ] || RES=$(grep "^nds.resume_on_quit=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  if [ "$RES" != 0 ] && { [ ! -e /tmp/rocknixds-testing ] || [ -e /tmp/rocknixds-testing-resume ]; }; then
    RSTATE="/storage/roms/savestates/nds/${GAME%.*}.resume.dss" DSV="$(dirname "$ROM")/${GAME%.*}.dsv"
    RLOAD=0
    if [ -f "$RSTATE" ]; then
      if [ -f "$DSV" ] && [ "$DSV" -nt "$RSTATE" ]; then rm -f "$RSTATE"; echo "resume state older than the game's save: dropped"
      else RLOAD=1; echo "resuming from $RSTATE"; fi
    fi
    export DSFLIP_RESUME_FILE="$RSTATE" DSFLIP_RESUME_LOAD=$RLOAD
    echo "-USR1 drastic" > /tmp/.process-kill-data    # ROCKNIX's exit hotkey: killall $(cat this); saves, then quits
  elif [ ! -e /tmp/rocknixds-testing ]; then
    # resume off. ES's start left the hotkey aimed at emulationstation, and this session has already stopped ES,
    # so killall would signal nothing and DraStic would keep running. Stock start_drastic.sh sets "-9 drastic".
    echo "-9 drastic" > /tmp/.process-kill-data
  fi
  # Power profile (ES: the game's or DS system's "power profile"; unset = balanced). libdsflip's CPU governor picks
  # the clock within the profile's range; the frame queue trades a refresh of input latency for riding out late
  # frames, and the wait keeps a full queue from dropping early ones (measured 2026-09-29, Black 2 at 2x, walking:
  # fixed 1416 MHz with a 2-frame queue + wait 0.07 hitches/s, 1104 MHz 0.13/s; without the wait 0.11-3.2 and 0.73).
  #   performance: 1104-1992 MHz, 1-frame queue (the lowest latency)
  #   balanced:    1104-1416 MHz, 2-frame queue + 20 ms wait
  #   battery:     1104 MHz, 3-frame queue + 20 ms wait (more cover for the late frames a low clock makes);
  #                on the Plus 1104-1416 MHz (see below)
  # DSFLIP_* already in the environment (tests, systemctl set-environment) win over the profile.
  PROF=$(grep -F "nds[\"$GAME\"].power_profile=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$PROF" ] || PROF=$(grep "^nds.power_profile=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$PROF3X" ] && PROF=$PROF3X
  case "$PROF" in
    performance) Q=1 QW=0 CMAX= ;;
    # the Plus's frames cost more (the main thread ~62% of a core at ~1475 MHz in Black 2 = ~83% at 1104, single
    # frames seen at 44-80% of a refresh at 1416): a fixed 1104 MHz overruns the heavy ones, so there battery keeps
    # the deep queue but lets the governor step to 1416 on CPU-bound drops (it steps back down when frames are light)
    battery) Q=3 QW=20; if [ -n "$BIG" ]; then CMAX=1416000; else CMAX=1104000; fi ;;
    *) PROF=balanced Q=2 QW=20 CMAX=1416000 ;;
  esac
  export DSFLIP_QUEUE=${DSFLIP_QUEUE:-$Q} DSFLIP_QUEUE_WAIT=${DSFLIP_QUEUE_WAIT:-$QW}
  # The latch (the commit of both panels' frames) must come early enough before the EARLIER panel's vblank; on the
  # Plus the bottom panel's vblank is 6.8 ms before the top's and a commit 1.2-1.5 ms ahead of it still missed it
  # (bottom flip pending at the next latch: a repeated frame, 2-4 times a second, Black 2 2026-10-01), where the RG DS
  # makes it with 0.6 ms. libdsflip widens the margin by itself on late latches; this is the floor it returns to.
  [ -n "$BIG" ] && export DSFLIP_LATCH_MARGIN=${DSFLIP_LATCH_MARGIN:-3000}
  # The bounds are soft (DSFLIP_CPU_MAX_SOFT): libdsflip goes past them only while the game is below full speed with
  # real work going on. The players' 1.5 logs (RG DS) had heavy 3D games at 2x below full speed for 38-83% of their
  # play at the clocks the governor held; a bound that slows the game down saves nothing worth it.
  [ -n "$CMAX" ] && export DSFLIP_CPU_MAX=${DSFLIP_CPU_MAX:-$CMAX} DSFLIP_CPU_MAX_SOFT=${DSFLIP_CPU_MAX_SOFT:-1}
  echo "power profile: $PROF (queue $DSFLIP_QUEUE, wait ${DSFLIP_QUEUE_WAIT} ms, CPU max ${DSFLIP_CPU_MAX:-hardware})"
  # 3D renderer (ES: the game's or DS system's "3D renderer"): superdrastic = SuperDrastic's own rasterizer for
  # DraStic's hi-res 3D (DSFLIP_RAST=1; a test feature, off unless chosen), anything else DraStic's own.
  RND=$(grep -F "nds[\"$GAME\"].renderer=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$RND" ] || RND=$(grep "^nds.renderer=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ "$RND" = superdrastic ] && export DSFLIP_RAST=${DSFLIP_RAST:-1}
  # its options (ES "3D texture filter", "3D resolution"; SuperDrastic ignores them with DraStic's renderer)
  TF=$(grep -F "nds[\"$GAME\"].texture_filter=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$TF" ] || TF=$(grep "^nds.texture_filter=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  case "$TF" in bilinear) export DSFLIP_RAST_TEXFILTER=${DSFLIP_RAST_TEXFILTER:-1} ;; sharp) export DSFLIP_RAST_TEXFILTER=${DSFLIP_RAST_TEXFILTER:-2} ;; esac
  R3=$(grep -F "nds[\"$GAME\"].resolution3d=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ -n "$R3" ] || R3=$(grep "^nds.resolution3d=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
  [ "$R3" = 3x ] && export DSFLIP_RAST_SCALE=${DSFLIP_RAST_SCALE:-3}
  echo "3D renderer: ${RND:-drastic}${DSFLIP_RAST:+ (DSFLIP_RAST=$DSFLIP_RAST, texture filter ${DSFLIP_RAST_TEXFILTER:-0}, scale ${DSFLIP_RAST_SCALE:-2})}"
  # SuperDrastic's sampling profiler (DSFLIP_PROF, src/prof.c) while the flag file exists: which thread spends its
  # time in which functions. Its report, prof-<pid>.txt, is rewritten every 10 s (the exit hotkey's kill -9 leaves the
  # last one) next to dsflip.log in $D/dsflip (the profiler's default: DSFLIP_LOG's directory).
  if [ -e /storage/.config/drastic/dsflip/profile ]; then
    export DSFLIP_PROF=${DSFLIP_PROF:-1}
    echo "profiler: on (DSFLIP_PROF=$DSFLIP_PROF, report in $D/dsflip)"
  fi
  cd $D
  # no wait for the display: libdsflip retries DRM master itself while seatd lets go of it (~0.4 s after sway)
  SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$D/dsflip/dsflip.log \
    LD_PRELOAD=$D/dsflip/libdsflip.so $D/dsflip/drastic "$ROM" > $D/dsflip/drastic.out 2>&1 &
  P=$!; TG=$(date +%s)
  # A start that hangs often ends with a hard reset, which loses whatever of this log and dsflip.log is still only in
  # memory: write them out a few times while the game starts
  ( for s in 2 4 6; do sleep $s; sync; done ) &
  # CPU placement (DSFLIP_PIN=0 turns it off): DraStic's main (emulation) thread alone on CPU 3; its 3D helper
  # threads, libdsflip's and PipeWire's threads on CPUs 0-2. Measured on the RG DS Plus 2026-10-01 (Black 2,
  # balanced, 1416 MHz): the main thread was runnable but not running ~10% of the time on every core (its helpers
  # 8-9%), DraStic presented 58.8-59.3 frames/s where the RG DS logs show 59.8-59.9, and every frame lost that way
  # is a repeated frame and a gap in the game's audio. The interrupts stay where the kernel put them (all on CPU 0,
  # ~5% of it): moving them off CPU 3 as well made the display controller's interrupt stop firing on some starts
  # (flips never completed, the game froze at its first frames) and once took the whole device down.
  # DSFLIP_PIN=1: the main thread to CPU 3 and every other DraStic thread to 0-2 (froze DraStic on 3 of 4 starts:
  # its main thread and its 3D helpers then waited on each other's condition variables forever, from the first 3D
  # frame on); 2: DraStic's own threads are left alone (its main thread is moved to CPU 3 only), libdsflip's,
  # Mali's and PipeWire's threads go to 0-2; 3: like 2 without touching DraStic at all; 0: off.
  PIN=${DSFLIP_PIN:-1}
  if [ "$PIN" != 0 ] && [ "$(cat /sys/devices/system/cpu/online 2>/dev/null)" = 0-3 ] && command -v taskset >/dev/null; then
    echo "CPU placement: mode $PIN"
    ( n=0; while kill -0 $P 2>/dev/null; do
        # Not before DraStic's 3D helper threads exist and have run: a thread created while the main thread is
        # confined to one CPU inherits that CPU, can't run alongside it, and DraStic's first hand-off to its
        # helpers is lost -- main and helpers then wait on each other forever (froze 8 of 9 starts, 2026-10-01).
        ran=0; for t in /proc/$P/task/[0-9]*; do
          [ "${t##*/}" != "$P" ] && [ "$(cat $t/comm 2>/dev/null)" = drastic ] && [ "$(awk '{print $14 + $15}' $t/stat 2>/dev/null)" -gt 0 ] 2>/dev/null && ran=$((ran + 1))
        done
        if [ $ran -lt 2 ]; then sleep 0.5; continue; fi
        [ $n -eq 0 ] && sleep 3          # and then a moment more: the pool's first hand-offs are the race
        # DraStic's helper threads one per CPU, busiest first (Dragon Quest Monsters runs three 3D helpers at ~35%
        # each; left to the scheduler on CPUs 0-2 they waited 17-19% of the time, and the main thread waits for the
        # slowest of them): the k-th busiest helper goes to CPU k mod 3
        if [ "$PIN" = 1 ]; then
          k=0; for line in $(for t in /proc/$P/task/[0-9]*; do tid=${t##*/}; [ "$tid" = "$P" ] && continue
                [ "$(cat $t/comm 2>/dev/null)" = drastic ] && echo "$(awk '{print $14 + $15}' $t/stat 2>/dev/null):$tid"; done | sort -t: -k1,1rn); do
            taskset -p -c $((k % 3)) ${line#*:} >/dev/null 2>&1; k=$((k + 1))
          done
        fi
        for t in /proc/$P/task/[0-9]*; do tid=${t##*/}; c=$(cat $t/comm 2>/dev/null)
          case "$PIN" in
            1) if [ "$tid" = "$P" ]; then taskset -p -c 3 $tid; else case "$c" in drastic*) ;; *) taskset -p -c 0-2 $tid ;; esac; fi ;;
            2) if [ "$tid" = "$P" ]; then taskset -p -c 3 $tid; else case "$c" in drastic*) ;; *) taskset -p -c 0-2 $tid ;; esac; fi ;;
            3) case "$c" in drastic*) ;; *) taskset -p -c 0-2 $tid ;; esac ;;
          esac
        done >/dev/null 2>&1
        for pp in $(pidof pipewire pipewire-pulse wireplumber 2>/dev/null); do
          for t in /proc/$pp/task/[0-9]*; do taskset -p -c 0-2 ${t##*/}; done
        done >/dev/null 2>&1
        n=$((n + 1)); if [ $n -lt 10 ]; then sleep 1; else sleep 10; fi
      done ) &
    PINNER=$!
  fi
  # ROCKNIX's powerstate service re-applies a GPU profile whenever the battery status flips between charging and
  # discharging ("auto" on AC, system.gpuperf on battery): plugging or unplugging mid-game, or a weak charger that
  # flaps, would leave e.g. a zero-copy game with the GPU pinned at 800 MHz. It polls every 2 s; so do we.
  # Re-apply $GOV if powerstate swaps the profile (charger flap). Kill this watcher before
  # putting the menu governor back: after `kill -0` succeeds it can still write $GOV once the game is gone.
  WATCH=
  if [ -n "$GPU_GOV" ]; then
    ( while kill -0 $P 2>/dev/null; do
        sleep 2
        kill -0 $P 2>/dev/null || break
        [ "$(cat $GPU/governor 2>/dev/null)" = "$GOV" ] || { echo $GOV > $GPU/governor 2>/dev/null; echo "$(ms) ms: GPU governor changed by another service: back to $GOV"; }
      done ) &
    WATCH=$!
  fi
  # ES is stopped, so it can't record the session (play count, last played, time played): do what ES does after a
  # game. In VT mode ES is only waiting, and records it itself.
  # (not while /tmp/rocknixds-testing exists: the test tools launch games through ES and mustn't count as plays)
  record() { [ -z "$VT" ] && [ "$v" = ready ] && [ ! -e /tmp/rocknixds-testing ] && python3 $D/dsflip/playstats.py "$ROM" $(( $(date +%s) - TG )); }
  # `systemctl stop dsflip-game` sends TERM to everything here; DraStic ignores it and would be SIGKILLed only at
  # the unit's timeout. Kill it at once (what the exit hotkey does), put the governor back and leave: starting
  # sway/ES from inside a unit that systemd is stopping waits behind that stop (measured: 40 s), so the unit's
  # ExecStopPost (restore.sh) brings them back instead.
  trap '[ -n "$WATCH" ] && kill $WATCH 2>/dev/null; [ -n "$PINNER" ] && kill $PINNER 2>/dev/null; kill -9 $P 2>/dev/null; wait $P; [ -s /tmp/dsflip-cpu-max ] && cat /tmp/dsflip-cpu-max > $CPU/scaling_max_freq 2>/dev/null; record; [ -n "$GPU_GOV" ] && echo "$GPU_GOV" > $GPU/governor 2>/dev/null; echo "$(date) stopped by the unit: restore.sh brings sway + ES back"; exit 0' TERM INT
  # libdsflip couldn't take the display: don't leave black panels. It decides within ~6 s at worst (3 s for DRM
  # master, 3 s for the shader); no verdict in 10 s means it isn't loaded or hangs.
  v=; i=0
  while [ $i -lt 200 ] && kill -0 $P 2>/dev/null; do
    v=$(cat $STATE 2>/dev/null); [ -n "$v" ] && break
    sleep 0.05; i=$((i + 1))
  done
  echo "$(ms) ms: libdsflip: ${v:-no verdict}"
  case "$v" in
    ready) ;;
    passthrough*)
      echo "abort"; kill -9 $P
      echo "The game couldn't take over the screens: ${v#passthrough: }. If this keeps happening, create the file $D/nodsflip to use the previous DraStic launcher. Logs: $D/dsflip" > $NOTICE ;;
    *)
      if kill -0 $P 2>/dev/null; then
        echo "abort"; kill -9 $P
        echo "The game didn't take over the screens within 10 seconds, so it was stopped. Logs: $D/dsflip" > $NOTICE
      fi ;;
  esac
  # Wait for DraStic, but not forever. One stuck in the kernel (state D: a display call that never returns) ignores
  # kill -9, and waiting on it left the panels white, the exit hotkey dead and a reset the only way out (reported on
  # the RG DS Plus, 1.5.1). Once a SIGKILL has been pending for 5 s, keep the evidence and bring the menu back.
  stuck=0; k=0
  while alive $P; do
    if kill_pending $P; then k=$((k + 1)); [ $k -ge 25 ] && { stuck=1; break; }; else k=0; fi
    sleep 0.2
  done
  if [ $stuck = 1 ]; then rc=255; stuck_report; else wait $P; rc=$?; fi
  # the watcher only now: killed before the wait, it never ran while the game did
  [ -n "$WATCH" ] && kill $WATCH 2>/dev/null
  [ -n "$PINNER" ] && kill $PINNER 2>/dev/null
  # the full CPU clock back at once: libdsflip's governor may have lowered the limit, and everything until restore.sh
  # (play stats, sway and ES starting) ran at it (the way back to the menu was ~1.2 s slower)
  cpu_full() { [ -s /tmp/dsflip-cpu-max ] && cat /tmp/dsflip-cpu-max > $CPU/scaling_max_freq 2>/dev/null; }
  cpu_full
  echo "$(up) drastic exited: $rc"
  # the game's RetroAchievements strip, redrawn with the progress just made while sway and ES start (in its own
  # unit, at a lower priority: ES doesn't wait for it)
  [ "$v" = ready ] && [ ! -e /tmp/rocknixds-testing ] && [ -x $D/dsflip/media-auto.sh ] && $D/dsflip/media-auto.sh --ra-rom "$ROM"
  if [ ! -s $NOTICE ]; then
    case $rc in
      0|137|138|143) why= ;;          # Exit DraStic in its menu; the exit hotkey (kill -9, or SIGUSR1 = resume); a stop
      132) why="illegal instruction" ;;
      134) why="it aborted" ;;
      135) why="bus error" ;;
      136) why="arithmetic error" ;;
      139) why="segmentation fault" ;;
      *) why="exit status $rc" ;;
    esac
    [ -n "$why" ] && echo "DraStic stopped unexpectedly ($why). Logs: $D/dsflip" > $NOTICE
  fi
  # the play stats (Python, ~0.3 s) are written while sway starts, not before it: restore.sh starts ES only once
  # they're done (ES reads them when it starts)
  RECORD_PID=
  if [ -s $NOTICE ]; then echo "notice: $(cat $NOTICE)"; else record & RECORD_PID=$!; fi
  RECORD_PID=$RECORD_PID $D/dsflip/restore.sh   # governor back, sway (checked for outputs), ES, then the notice
  echo "$(date) restored"
} >> $LOG 2>&1
