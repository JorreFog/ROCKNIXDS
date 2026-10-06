#!/bin/sh
# Brings the desktop back after a libdsflip game session: GPU governor, CPU clock limit, sway, ES. Called by session.sh when
# DraStic exits, and by the unit's ExecStopPost when the session was stopped or killed (then it's the only
# thing that runs). Safe to run twice: every step checks first.
#
# A sway started while DraStic's DRM state was still being torn down comes up with no outputs (both panels
# black, ES crash-looping on "the video driver did not add any displays"), so after starting it we check that
# it owns the panels and restart it once if not.
GPU=/sys/class/devfreq/fde60000.gpu
RT=/var/run/0-runtime-dir
up() { read u _ < /proc/uptime; echo "[$u]"; }  # uptime stamps for the log (switchtime.sh uses the same clock)
# After a normal exit this script runs twice, from session.sh and then as the unit's ExecStopPost, and ES waits for
# both (the launcher waits for the unit). The second run has nothing left to do: it leaves at once (it used to
# repeat the half-second interrupt check below, with the menu's old picture on the panels all the while).
DONE=/tmp/dsflip-restored.${INVOCATION_ID:-none}
if [ "$1" != --vt-back ] && [ -n "$INVOCATION_ID" ] && [ -e $DONE ]; then rm -f $DONE; exit 0; fi
[ "$1" = --vt-back ] || echo "$(up) restore: start"
if [ -f /tmp/dsflip-gpu-governor ]; then
    cat /tmp/dsflip-gpu-governor > $GPU/governor 2>/dev/null
    [ -s /tmp/dsflip-gpu-min ] && cat /tmp/dsflip-gpu-min > $GPU/min_freq 2>/dev/null
    rm -f /tmp/dsflip-gpu-governor /tmp/dsflip-gpu-min
fi
if [ -s /tmp/dsflip-cpu-max ]; then                  # the CPU clock limit libdsflip's governor lowered
    cat /tmp/dsflip-cpu-max > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq 2>/dev/null
    rm -f /tmp/dsflip-cpu-max
fi
if [ -s /tmp/dsflip-cpu-governor ]; then             # the governor session.sh switched to performance
    cat /tmp/dsflip-cpu-governor > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null
    rm -f /tmp/dsflip-cpu-governor
fi
if [ -f /tmp/dsflip-pw-rates ]; then             # session.sh forced 44.1 kHz: PipeWire's own rate and rate list again
    XDG_RUNTIME_DIR=$RT pw-metadata -n settings 0 clock.force-rate 0 >/dev/null 2>&1
    if [ -s /tmp/dsflip-pw-rates ]; then XDG_RUNTIME_DIR=$RT pw-metadata -n settings 0 clock.allowed-rates "$(cat /tmp/dsflip-pw-rates)" >/dev/null 2>&1
    else XDG_RUNTIME_DIR=$RT pw-metadata -n settings -d 0 clock.allowed-rates >/dev/null 2>&1; fi
    rm -f /tmp/dsflip-pw-rates
fi
# Resume-on-quit wrote "-USR1 drastic" for the exit hotkey; put stock's target back so a later non-DS
# launcher (or a session that skipped resume) is not left signalling USR1. "-9" alone is not that
# target: killall needs the process name (start_drastic.sh writes "-9 drastic").
[ -f /tmp/.process-kill-data ] && grep -qx -- '-USR1 drastic' /tmp/.process-kill-data 2>/dev/null && echo "-9 drastic" > /tmp/.process-kill-data
sway_has_outputs() {
    SOCK=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
    [ -n "$SOCK" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$SOCK" -t get_outputs 2>/dev/null | grep -q '"active": true'
}
wait_outputs() {                # up to 3 s
    i=0; while [ $i -lt 60 ]; do sway_has_outputs && return 0; sleep 0.05; i=$((i + 1)); done; return 1
}
# VT mode (session.sh switched the console away from sway): switch back, sway takes the display again and ES,
# which kept running and kept its window (es-rgds-keepwindow.patch), carries on; its window is placed again in case
# sway resized it while the console was away.
vt_back() {
    # /tmp/dsflip-vt(-later) stay until the panels are sway's again: a game started meanwhile (session.sh) sees them,
    # stops this unit first, and keeps sway's VT instead of recording tty12 or having sway restarted under it
    VT=$(cat /tmp/dsflip-vt 2>/dev/null)
    chvt "${VT:-1}"
    if wait_outputs; then
        S=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
        # sway's input devices come back with the display, but only once something makes its libinput look. After
        # a game sway had none (swaymsg -t get_inputs: 0) until a touch or a button: the menu had no keyboard focus,
        # so SDL dropped the first gamepad press in it, and that press was what woke sway (an RG DS Plus, 2026-10-06:
        # the first press after a game was ignored in 6 of 9 tries; ES never got it). A udev "change" for one input
        # device is read by libinput, which then adds its devices: all 8 back within half a second.
        udevadm trigger --action=change /sys/class/input/event0 2>/dev/null
        # RG DS: the window is resized to the 1920 canvas (sway allows it past the 1280 desktop).
        # Plus: ES's --resolution made the window 3072x768, and it comes back from the VT switch one panel wide
        # (1024x768, the bottom panel black: seen on an RG DS Plus, 1.5.13). A plain resize to 3072 is clamped to the
        # 2048 desktop and the theme scales, so sway's floating size limit is lifted for the resize and put back.
        THEME_SET=$(sed -n 's/.*<string name="ThemeSet" value="\([^"]*\)".*/\1/p' /storage/.config/emulationstation/es_settings.cfg 2>/dev/null)
        case "$THEME_SET" in
            ""|dii-ess-aye|canvas-ds|rocknixds-pixel-dark|rocknixds-pixel-light)
                PANEL=; for m in /sys/class/drm/card*-DSI-*/modes; do read -r PANEL < "$m" 2>/dev/null && [ -n "$PANEL" ] && break; done
                case "$PANEL" in [0-9]*x[0-9]*) ;; *) PANEL=640x480 ;; esac
                PW=${PANEL%%x*}
                if [ "$PW" -gt 640 ]; then
                    PH=${PANEL#*x}
                    if [ -n "$S" ]; then
                        XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -- floating_maximum_size -1 x -1 >/dev/null 2>&1
                        XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -- "[app_id=\"emulationstation\"] floating enable, fullscreen disable, resize set $((PW * 3)) $PH, move absolute position 0 0" >/dev/null 2>&1
                        XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -- floating_maximum_size 0 x 0 >/dev/null 2>&1
                    fi
                else
                    [ -n "$S" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$S" '[app_id="emulationstation"] floating enable, fullscreen disable, resize set 1920 480, move absolute position 0 0' >/dev/null 2>&1
                fi
                ;;
        esac
    else
        echo "$(date) sway has no outputs after the VT switch: restarting it"
        systemctl restart sway.service; wait_outputs
        systemctl is-active -q essway.service || systemctl start essway.service
    fi
    rm -f /tmp/dsflip-vt /tmp/dsflip-vt-later
}
# The display controller stuck in an underrun loop (see session.sh: ~84,000 interrupts/s, 60% of a core, until
# reboot): count its interrupts for half a second (normal: ~60 per panel per second) and clear it by switching the
# panels off and on through sway, which does a full modeset. The panels blink once.
vop_irqs() { awk '/fe040000.vop/ { s = 0; for (i = 2; i <= 5; i++) s += $i; print s }' /proc/interrupts; }
storm_check() {
    a=$(vop_irqs); sleep 0.5; b=$(vop_irqs)
    if [ -n "$a" ] && [ -n "$b" ] && [ $((b - a)) -gt 2000 ]; then
        echo "$(date) display controller interrupt storm ($(( (b - a) * 2 ))/s): power-cycling the panels"
        S=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
        XDG_RUNTIME_DIR=$RT swaymsg -s "$S" output '*' power off >/dev/null 2>&1; sleep 0.3
        XDG_RUNTIME_DIR=$RT swaymsg -s "$S" output '*' power on >/dev/null 2>&1
    fi
}
# The panels back to sway, later (restore.sh --vt-back, its own transient unit, started below). When the game ends
# ES is still inside the launch command: the launcher waits for this unit, ROCKNIX's scripts tidy up after it
# (~0.6 s), and ES then loads its pictures again (~0.7 s). Switched back at once, the panels showed ES's last
# picture, the end of the start animation, for all of that (2.8 s measured on an RG DS Plus, with the half-second
# interrupt check run twice in between). So wait until ES's main thread is back in its own loop (asleep between
# frames, clock_nanosleep, or waiting for sway, ppoll: /proc/<pid>/syscall; inside the launch command it is in
# wait4), for 3 s at most, and switch then: the menu is live the moment it shows. Until then the panels are black.
if [ "$1" = --vt-back ]; then
    [ -f /tmp/dsflip-vt ] || exit 0
    P=$(pidof emulationstation | cut -d' ' -f1)
    i=0; ok=0
    while [ $i -lt 100 ] && [ -n "$P" ] && [ -r /proc/$P/syscall ]; do
        read sc _ < /proc/$P/syscall 2>/dev/null
        case "$sc" in 115|73|22|101) ok=$((ok + 1)) ;; *) ok=0 ;; esac
        [ $ok -ge 4 ] && break
        sleep 0.03; i=$((i + 1))
    done
    echo "$(up) restore: ES $([ $ok -ge 4 ] && echo "is drawing again" || echo "did not come back in 3 s"): the panels back to sway" >> /storage/.config/drastic/dsflip/last-session.log
    vt_back >> /storage/.config/drastic/dsflip/last-session.log 2>&1
    echo "$(up) restore: menu shown" >> /storage/.config/drastic/dsflip/last-session.log
    storm_check >> /storage/.config/drastic/dsflip/last-session.log 2>&1
    exit 0
fi
LATER=0     # 1: the panels go back later, from the watcher. Nothing here may wait for sway's outputs then: ES only draws
            # again once this unit has ended, and the outputs are only sway's again once ES draws.
if [ -f /tmp/dsflip-vt ]; then
    if [ -e /tmp/dsflip-vt-later ]; then LATER=1                         # the watcher has it
    elif [ -n "$DSFLIP_VT_LATER" ] && touch /tmp/dsflip-vt-later &&
         systemd-run --collect --quiet --unit=dsflip-vtback /storage/.config/drastic/dsflip/restore.sh --vt-back >/dev/null 2>&1; then
        LATER=1; echo "$(up) restore: the panels go back to sway when ES draws again"
    else rm -f /tmp/dsflip-vt-later; vt_back; fi
fi
# ES's launcher waits for sway's outputs itself (the theme's start_es_rgds.sh; stock ROCKNIX starts both together at
# boot too), so start both at once and let ES's settings script run while sway comes up. If sway comes up without
# outputs, restarting it restarts ES as well (Requires=).
systemctl is-active -q sway.service || { echo "$(up) restore: starting sway"; systemctl start sway.service; }
# session.sh may still be writing the play stats (RECORD_PID): ES reads them when it starts, so it waits for them
if [ -n "$RECORD_PID" ]; then
    i=0; while kill -0 "$RECORD_PID" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    echo "$(up) restore: play stats written"
fi
systemctl is-active -q essway.service || { echo "$(up) restore: starting ES"; systemctl start essway.service; }
if [ $LATER = 0 ]; then
    echo "$(up) restore: waiting for sway's outputs"
    if ! wait_outputs; then
        echo "$(date) sway has no outputs: restarting it"
        systemctl restart sway.service
        wait_outputs
        systemctl is-active -q essway.service || systemctl start essway.service
    fi
    echo "$(up) restore: outputs up"
    storm_check
fi
# The menus' CPU governor (menu-power.sh), once ES is up: sway's and ES's start are CPU-heavy, and switching to
# schedutil before them made the way back to the menu ~0.8 s slower. Its own transient unit, like the notice below.
systemd-run --collect --quiet /storage/.config/drastic/dsflip/menu-power.sh --after-es-idle >/dev/null 2>&1
# session.sh's message about why the game ended early: show it in ES once ES answers. From a transient unit of its
# own, because this script may be running as dsflip-game's ExecStopPost, whose processes die when it finishes.
# The mv makes it show once even though this script runs twice after a normal exit (session.sh + ExecStopPost).
N=/tmp/dsflip-notice.$$
if [ -s /tmp/dsflip-notice ] && mv /tmp/dsflip-notice $N 2>/dev/null; then
    systemd-run --collect --quiet sh -c "for i in \$(seq 1 120); do curl -s -m 1 localhost:1234/isIdle 2>/dev/null | grep -q true && break; sleep 0.5; done; sleep 1; curl -s -m 5 -X POST --data-binary @$N localhost:1234/messagebox >/dev/null; rm -f $N" >/dev/null 2>&1 || rm -f $N
fi
[ -n "$INVOCATION_ID" ] && : > $DONE
echo "$(up) restore: done"
exit 0
