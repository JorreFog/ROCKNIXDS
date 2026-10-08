#!/bin/sh
# libdsflip game session (runs as systemd unit dsflip-game, outside ES's process tree).
# Fast switching (DSFLIP_VT=1, the default): switches the console to another VT so seatd hands the panels to DraStic
# while ES and sway wait, and back afterwards. Otherwise stops ES + sway, runs DraStic with libdsflip on the bare
# panels, then brings sway + ES back. If this script is killed with the unit (systemctl stop dsflip-game), the unit's
# ExecStopPost (restore.sh) does that instead.
# The exit hotkey (killall -9 drastic) works because DraStic runs through a symlink named "drastic".
D=/storage/.config/drastic
LOG=$D/dsflip/last-session.log
stop_perf() {  # once-a-second sampler (perf-session.py); restore.sh closes and uploads the log, once
  pid=$(cat /tmp/dsflip-perf/active/pid 2>/dev/null) || return 0
  kill "$pid" 2>/dev/null
  i=0
  while kill -0 "$pid" 2>/dev/null && [ $i -lt 40 ]; do sleep 0.05; i=$((i + 1)); done
  kill -9 "$pid" 2>/dev/null
  rm -f /tmp/dsflip-perf/active/pid
}
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
  # gptokeyb (start_drastic.sh starts it inside ES's unit) takes ~1.1 s to die on the stop's TERM, and the stop
  # waits for it: that was most of the switch. Nothing uses it in this session (the exit hotkey is ROCKNIX's
  # own), so kill it outright, as start_drastic.sh itself does after a game. One stop for both units: systemd
  # orders it (ES first) without a second round trip.
  kill -9 $(pidof gptokeyb) 2>/dev/null
  if [ "$DSFLIP_VT" = 1 ] && systemctl is-active -q sway.service; then
    # VT mode (drastic-wrapper.sh sets DSFLIP_VT=1 when ES kept its window for this launch: fast switching, the
    # default): sway and ES stay up. Switching the console away from sway's VT makes seatd disable sway's session,
    # which releases the display (DRM master) in ~80 ms; restore.sh switches back. tty12 is unused. It stays a text
    # console: from one in graphics mode the kernel ignores every switch (chvt back never returns; tried on an RG DS
    # Plus, 2026-10-05), and the kernel console takes the panels on either. So the panels show that console from here
    # until libdsflip has them (~0.3 s), and again from the game's end until the menu is back: it is cleared and its
    # cursor hidden first, so that is plain black and not "an empty terminal" (the cursor showed, 1.5.13 test build).
    VT=1
    # The last game's switch back (restore.sh --vt-back) may still be waiting for ES to draw: a start that quick
    # (two launch requests in a row) must not let it switch the panels back to sway under this game, and the console
    # is then still tty12, which is not sway's: keep the VT that game recorded.
    [ -e /tmp/dsflip-vt-later ] && systemctl stop dsflip-vtback.service 2>/dev/null   # (pending exactly while it exists)
    CUR=$(fgconsole 2>/dev/null)
    if [ "$CUR" = 12 ] && [ -s /tmp/dsflip-vt ]; then echo "$(ms) ms: the last game's switch back was still pending: sway stays on tty$(cat /tmp/dsflip-vt)"
    elif [ "$CUR" = 12 ]; then echo 1 > /tmp/dsflip-vt; echo "$(ms) ms: on tty12 with no record of sway's VT: assuming tty1"   # never ours
    else echo "${CUR:-1}" > /tmp/dsflip-vt; fi
    rm -f /tmp/dsflip-vt-later
    python3 -c 'import fcntl, os; fcntl.ioctl(os.open("/dev/tty12", os.O_RDWR), 0x4B3A, 0)' 2>/dev/null   # KDSETMODE KD_TEXT
    printf '\033[2J\033[H\033[?25l' > /dev/tty12 2>/dev/null
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
  # A shader picked for this game in SuperDrastic's in-game menu (menu.cfg: shader.<game>=<pick> <the frontend's
  # shader it was picked over>) is what runs while ES still asks for the shader it was picked over; the clock follows it.
  SH=${DSHOOK_SHADER:-none}; [ "$SH" = bilinear ] && SH=none
  G=$(basename "$ROM"); G=${G%.*}
  PICK=$(awk -v k="shader.$G=" 'index($0, k) == 1 { v = substr($0, length(k) + 1) } END { print v }' \
         "${DSFLIP_DATA:-/storage/.config/drastic/dsflip}/menu.cfg" 2>/dev/null)
  [ -n "$PICK" ] && [ "${PICK#* }" = "$SH" ] && SH=${PICK%% *} && echo "shader picked in the in-game menu: $SH"
  case "$SH" in
    none|bilinear) GOV=powersave; MIN= ;;
    # ds-fsr (FSR 1.0) needs ~9.5 ms of GPU per frame on the RG DS (~14 ms on the Plus, where it draws 3x the DS
    # screen, 768x576, and the display controller scales the rest; SuperDrastic's shaders/ds-fsr.frag): under
    # simple_ondemand it sat at 800 MHz 97% of the time and dropped frames while ramping up from the floor at the
    # start, so it gets the full clock from the start
    ds-fsr) GOV=${DSFLIP_SHADER_GOV:-performance}; MIN=$DSFLIP_SHADER_GPU_MIN ;;
    *) GOV=${DSFLIP_SHADER_GOV:-simple_ondemand}; MIN=${DSFLIP_SHADER_GPU_MIN:-400000000} ;;
  esac
  [ -n "$GPU_GOV" ] && { echo "$GPU_GOV" > /tmp/dsflip-gpu-governor; echo "$GPU_MIN" > /tmp/dsflip-gpu-min; echo $GOV > $GPU/governor 2>/dev/null; [ -n "$MIN" ] && echo $MIN > $GPU/min_freq 2>/dev/null; }
  # libdsflip's CPU governor (cpugov.c) lowers the CPU's clock limit while the game runs; restore.sh puts it back.
  # A file left by a session that never got restored holds the real limit: keep it.
  CPU=/sys/devices/system/cpu/cpufreq/policy0
  [ -f /tmp/dsflip-cpu-max ] || cat $CPU/scaling_max_freq > /tmp/dsflip-cpu-max 2>/dev/null
  # cpugov.c sets the clock through scaling_max_freq, which only pins it under the "performance" governor (it turns
  # itself off under any other). One of four RG DS units in the 1.5 beta performance logs ran its games with the
  # clock floating between 816 and 1992 MHz and no cpugov at all (a balanced or battery profile at up to 1992 MHz),
  # as the RG DS Plus nightly's ondemand default did. Run the session on performance whatever ROCKNIX's setting;
  # restore.sh puts the menu's back.
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
  # The game's settings, else the DS system's; with "Apply recommended settings" on, the recommended picture and speed
  # settings instead (nds-settings.sh, recommended.cfg; #45)
  if [ -f $D/dsflip/nds-settings.sh ]; then . $D/dsflip/nds-settings.sh
  else
    nds_get() { _nv=$(grep -F "nds[\"$1\"].$2=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2); [ -n "$_nv" ] || _nv=$(grep "^nds\.$2=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2); echo "$_nv"; }
    nds_chosen() { grep -qF "nds[\"$1\"].$2=" $CFG 2>/dev/null; }
    nds_recommended_on() { false; }
  fi
  nds_recommended_on "$GAME" && echo "recommended settings: on (the game's own picture and speed settings are not used)"
  RES=$(nds_get "$GAME" resume_on_quit)
  if [ "$RES" != 0 ] && { [ ! -e /tmp/rocknixds-testing ] || [ -e /tmp/rocknixds-testing-resume ]; }; then
    # DraStic's savestates and saves folders (links: ROCKNIX's start_drastic.sh, then save-dirs.sh, decide where)
    RSTATE="$D/savestates/${GAME%.*}.resume.dss" DSV="$D/backup/${GAME%.*}.dsv"
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
  #   balanced:    1104-1416 MHz, 1-frame queue + 20 ms wait (1.5.5; was 2 frames: measured on the RG DS Plus, HeartGold
  #                walking at 1104 MHz, 2026-10-04: +16.7 ms of queue latency instead of +33.2, with as few repeated
  #                frames (13-14 against 15-19 in ~40 s) and dropped ones (2-6 against 2-4); without the wait 20 drops)
  #   battery:     1104 MHz, 3-frame queue + 20 ms wait (more cover for the late frames a low clock makes)
  # The 1104 MHz floor is libdsflip's; DSFLIP_CPU_MIN overrides it. The profiles' upper bounds are soft
  # (DSFLIP_CPU_MAX_SOFT): libdsflip goes past them only while the game is below full speed with real work going on.
  # The players' 1.5 logs had heavy 3D games at 2x (Call of Duty, Final Fantasy - The 4 Heroes of Light, Pokemon
  # Platinum with a shader) below full speed for 38-83% of their play at the clocks the governor held; a bound that
  # slows the game down saves nothing worth it.
  # DSFLIP_* already in the environment (tests, systemctl set-environment) win over the profile.
  PROF=$(nds_get "$GAME" power_profile)
  # 3D resolution (ES: the game's or DS system's "3D resolution", nds.resolution3d; Gengis Engine only, read with the
  # renderer below): 3x draws the 3D at three times the DS's size and brings it down to the 2x frame (smoother edges,
  # the same layout). It costs CPU, so the power profile is performance unless this game has its own set: HeartGold
  # walking held 59.8-60.2 fps at 1992 MHz in the 3x test build; the balanced profile's 1416 MHz cap would not.
  RES=$(nds_get "$GAME" resolution3d)
  RND=$(nds_get "$GAME" renderer)
  if [ "$RES" = 3x ] && [ "$RND" != drastic ] && [ "${DSFLIP_RAST:-1}" != 0 ] && ! nds_chosen "$GAME" power_profile; then
    PROF=performance
  fi
  case "$PROF" in
    performance) Q=1 QW=0 CMAX= ;;
    battery) Q=3 QW=20 CMAX=1104000 ;;
    *) PROF=balanced Q=1 QW=20 CMAX=1416000 ;;
  esac
  export DSFLIP_QUEUE=${DSFLIP_QUEUE:-$Q} DSFLIP_QUEUE_WAIT=${DSFLIP_QUEUE_WAIT:-$QW}
  [ -n "$CMAX" ] && export DSFLIP_CPU_MAX=${DSFLIP_CPU_MAX:-$CMAX} DSFLIP_CPU_MAX_SOFT=${DSFLIP_CPU_MAX_SOFT:-1}
  echo "power profile: $PROF (queue $DSFLIP_QUEUE, wait ${DSFLIP_QUEUE_WAIT} ms, CPU max ${DSFLIP_CPU_MAX:-hardware})"
  # Performance log, the same samples tools/rgds-monitor.py takes, and only after the player allowed the upload
  # (first launch asks; Nintendo DS > Share performance logs changes it). restore.sh uploads on quit. Test
  # launches don't record.
  if [ ! -e /tmp/rocknixds-testing ] && [ -f $D/dsflip/perf-session.py ]; then
    sv=$(grep -F "nds[\"$GAME\"].share_performance_logs=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
    [ -n "$sv" ] || sv=$(grep '^nds\.share_performance_logs=' $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
    case "$sv" in
      1|yes)
        rm -rf /tmp/dsflip-perf/active
        mkdir -p /tmp/dsflip-perf/active
        ROCKNIXDS_PROFILE=$PROF ROCKNIXDS_ROM="$GAME" \
          nice -n 19 python3 -u $D/dsflip/perf-session.py sample /tmp/dsflip-perf/active &
        echo $! > /tmp/dsflip-perf/active/pid
        ;;
    esac
  fi
  # 3D renderer (ES: the game's or DS system's "3D renderer"): superdrastic = Gengis Engine, SuperDrastic's own
  # rasterizer for DraStic's hi-res 3D (DSFLIP_RAST=1), drastic = DraStic's own. Auto (unset, the menu's default) is
  # Gengis Engine since 1.5.13: the same picture as DraStic's renderer pixel for pixel with ~12% less CPU (1.5.9), and
  # the README asked every new player to switch it on by hand. A player who chose DraStic keeps it. "3D texture filter"
  # and "3D resolution" (above) apply to Gengis Engine; DraStic's renderer ignores them.
  # DSFLIP_RAST=0 in the environment (tests, systemctl set-environment) forces DraStic's renderer whatever the setting.
  case "$RND" in drastic) ;; superdrastic|""|auto) export DSFLIP_RAST=${DSFLIP_RAST:-1} ;; esac
  [ "$DSFLIP_RAST" = 0 ] && unset DSFLIP_RAST
  case "$RES" in 3x|3) [ -n "$DSFLIP_RAST" ] && export DSFLIP_RAST_SCALE=${DSFLIP_RAST_SCALE:-3} ;; 2x|2) [ -n "$DSFLIP_RAST" ] && export DSFLIP_RAST_SCALE=${DSFLIP_RAST_SCALE:-2} ;; esac
  TF=$(nds_get "$GAME" texture_filter)
  case "$TF" in bilinear) export DSFLIP_RAST_TEXFILTER=${DSFLIP_RAST_TEXFILTER:-1} ;; sharp) export DSFLIP_RAST_TEXFILTER=${DSFLIP_RAST_TEXFILTER:-2} ;; esac
  if [ -n "$DSFLIP_RAST" ]; then echo "3D renderer: Gengis Engine (${RND:-Auto}; scale ${DSFLIP_RAST_SCALE:-2}, texture filter ${DSFLIP_RAST_TEXFILTER:-0})"; else echo "3D renderer: DraStic"; fi
  # Wi-Fi online play is parked for 1.6 (it doesn't get past the game's own Wi-Fi setup yet): ES no longer offers
  # "wfc dns" and libdsflip ignores nds.wfc_dns. Only the test switch turns it on (systemctl set-environment
  # DSFLIP_WFC=kaeru DSFLIP_WFC_DEBUG=1; docs/handoff-local.md, section 4).
  echo "wifi: ${DSFLIP_WFC:-off}"
  # The in-game menu's navigation sounds follow ES's (Sound settings > Enable navigation sounds:
  # EnableSounds in es_settings.cfg, off unless set), and its hint toasts ("Y to undo load" and the like) are on unless
  # rocknixds.hints=0 in system.cfg. The menu's own saved choice wins over these (SuperDrastic's docs/INTEGRATION.md);
  # DSFLIP_MENU_* already in the environment win over both.
  if [ -z "$DSFLIP_MENU_SOUNDS" ]; then
    DSFLIP_MENU_SOUNDS=0
    grep -q '<bool name="EnableSounds" value="true"' /storage/.config/emulationstation/es_settings.cfg 2>/dev/null && DSFLIP_MENU_SOUNDS=1
  fi
  if [ -z "$DSFLIP_MENU_HINTS" ]; then
    DSFLIP_MENU_HINTS=1
    [ "$(grep '^rocknixds\.hints=' $CFG 2>/dev/null | tail -n1 | cut -d= -f2)" = 0 ] && DSFLIP_MENU_HINTS=0
  fi
  export DSFLIP_MENU_SOUNDS DSFLIP_MENU_HINTS
  echo "in-game menu: sounds $DSFLIP_MENU_SOUNDS, hints $DSFLIP_MENU_HINTS"
  # The real microphone presses DraStic's "fake microphone" control (Scroll Lock, code 327 in the keyboard set).
  # ROCKNIX's drastic.cfg for the RG DS binds it in both control sets since 2026-02-04, but a config/drastic.cfg that
  # dates from an earlier nightly has it unbound (65535), and ROCKNIX copies its template only once: then blowing
  # into the mic reached nothing (issue 26's second suspect). With the mic on, bind it. DraStic saves this file on
  # exit, so a later rebinding by the player in DraStic's menu stays. libdsflip logs what it found ("[mic] fake
  # microphone: ..." in dsflip.log); DSFLIP_MIC_DEBUG=1, DSFLIP_MIC_GATE, DSFLIP_MIC_COUPLING_MAX, DSFLIP_MIC_HOLD_MS
  # and DSFLIP_MIC_KEY (systemctl set-environment) are its test switches: docs/handoff-local.md, section 3.
  if [ "${DSHOOK_MIC_THRESH:-0}" != 0 ] && grep -q '^controls_a\[CONTROL_INDEX_FAKE_MICROPHONE\] = 65535' $D/config/drastic.cfg 2>/dev/null; then
    sed -i 's/^controls_a\[CONTROL_INDEX_FAKE_MICROPHONE\] = 65535/controls_a[CONTROL_INDEX_FAKE_MICROPHONE] = 327/' $D/config/drastic.cfg
    echo "microphone: bound DraStic's fake microphone to Scroll Lock (327) in config/drastic.cfg (it was unbound)"
  fi
  cd $D
  # preload-guard.so keeps libdsflip out of the processes DraStic starts (SuperDrastic 0.3.0-beta.3, 1.5's, started pactl
  # and wpctl for its volume card; with libdsflip in them, they rotated the game's log and overwrote its verdict).
  # SuperDrastic has its own guard since 0.3.0-beta.2; this one stays for a package from before it.
  # Listed last: glibc runs it first.
  PRE=$D/dsflip/libdsflip.so; [ -f $D/dsflip/preload-guard.so ] && PRE="$PRE $D/dsflip/preload-guard.so"
  # no wait for the display: libdsflip retries DRM master itself while seatd lets go of it (~0.4 s after sway)
  SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSFLIP_LOG=$D/dsflip/dsflip.log \
    LD_PRELOAD="$PRE" $D/dsflip/drastic "$ROM" > $D/dsflip/drastic.out 2>&1 &
  P=$!; TG=$(date +%s)
  # A start that hangs often ends with a hard reset, which loses whatever of this log and dsflip.log is still only in
  # memory: write them out a few times while the game starts
  ( for s in 2 4 6; do sleep $s; sync; done ) &
  # ROCKNIX's powerstate service re-applies a GPU profile whenever the battery status flips between charging and
  # discharging ("auto" on AC, system.gpuperf on battery): plugging or unplugging mid-game, or a weak charger that
  # flaps, would leave e.g. a zero-copy game with the GPU pinned at 800 MHz. It polls every 2 s; so do we.
  [ -n "$GPU_GOV" ] && ( while kill -0 $P 2>/dev/null; do
      sleep 2
      kill -0 $P 2>/dev/null || break          # the game ended: restore.sh owns the governor now
      [ "$(cat $GPU/governor 2>/dev/null)" = "$GOV" ] || { echo $GOV > $GPU/governor 2>/dev/null; echo "$(ms) ms: GPU governor changed by another service: back to $GOV"; }
    done ) &
  # ES is stopped, so it can't record the session (play count, last played, time played): do what ES does after a
  # game. In VT mode ES is only waiting, and records it itself.
  # (not while /tmp/rocknixds-testing exists: the test tools launch games through ES and mustn't count as plays)
  record() { [ -z "$VT" ] && [ "$v" = ready ] && [ ! -e /tmp/rocknixds-testing ] && python3 $D/dsflip/playstats.py "$ROM" $(( $(date +%s) - TG )); }
  # `systemctl stop dsflip-game` sends TERM to everything here; DraStic ignores it and would be SIGKILLed only at
  # the unit's timeout. Kill it at once (what the exit hotkey does), put the governor back and leave: starting
  # sway/ES from inside a unit that systemd is stopping waits behind that stop (measured: 40 s), so the unit's
  # ExecStopPost (restore.sh) brings them back instead.
  trap 'kill -9 $P 2>/dev/null; wait $P; stop_perf; [ -s /tmp/dsflip-cpu-max ] && cat /tmp/dsflip-cpu-max > $CPU/scaling_max_freq 2>/dev/null; record; [ -n "$GPU_GOV" ] && echo "$GPU_GOV" > $GPU/governor 2>/dev/null; echo "$(date) stopped by the unit: restore.sh brings sway + ES back"; exit 0' TERM INT
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
  # And DraStic alive but wedged with libdsflip's own threads (libdsflip ends a game that only stops showing frames
  # itself: its stall watch). Its presenter thread wakes at least 20 times a second whatever the game does, in
  # DraStic's menu too: its CPU time (schedstat, ns) not moving for 15 s of this loop (which doesn't run while the
  # handheld sleeps) means nothing in there runs. Kill it; the check above takes over if even that can't land.
  stuck=0; k=0; hb=0; hbt=; n=0; PT=
  while alive $P; do
    if kill_pending $P; then k=$((k + 1)); [ $k -ge 100 ] && { stuck=1; break; }; else k=0; fi
    n=$((n + 1))
    if [ "$v" = ready ] && [ $((n % 20)) -eq 0 ]; then
      [ -n "$PT" ] || for t in /proc/$P/task/[0-9]*; do [ "$(cat $t/comm 2>/dev/null)" = dsf-present ] && PT=${t##*/}; done
      cpu=; [ -n "$PT" ] && read cpu _ < /proc/$P/task/$PT/schedstat 2>/dev/null
      if [ -z "$cpu" ]; then hb=0
      elif [ "$cpu" = "$hbt" ]; then hb=$((hb + 1))
      else hb=0; hbt=$cpu; fi
      # /tmp/dsflip-hold: someone has DraStic stopped on purpose (freeze-repro.sh's gdb, a person debugging): don't count
      [ -e /tmp/dsflip-hold ] && hb=0
      if [ $hb -eq 15 ]; then
        echo "$(ms) ms: libdsflip's presenter hasn't run for 15 s: DraStic is wedged, killing it"
        kill -9 $P 2>/dev/null
        echo "The game stopped responding and was closed. Logs: $D/dsflip" > $NOTICE
      fi
    fi
    sleep 0.05
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
  stop_perf
  RECORD_PID=
  if [ -s $NOTICE ]; then echo "notice: $(cat $NOTICE)"; else record & RECORD_PID=$!; fi
  # governor back, sway (checked for outputs), ES, then the notice. In VT mode the panels go back to sway a little
  # later, once ES is out of the launch command and draws again (DSFLIP_VT_LATER, restore.sh).
  RECORD_PID=$RECORD_PID DSFLIP_VT_LATER=$VT $D/dsflip/restore.sh
  echo "$(date) restored"
} >> $LOG 2>&1
