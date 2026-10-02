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
echo "$(up) restore: start"
if [ -f /tmp/dsflip-gpu-governor ]; then
    cat /tmp/dsflip-gpu-governor > $GPU/governor 2>/dev/null
    [ -s /tmp/dsflip-gpu-min ] && cat /tmp/dsflip-gpu-min > $GPU/min_freq 2>/dev/null
    rm -f /tmp/dsflip-gpu-governor /tmp/dsflip-gpu-min
fi
if [ -s /tmp/dsflip-cpu-max ]; then                  # the CPU clock limit libdsflip's governor lowered
    cat /tmp/dsflip-cpu-max > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq 2>/dev/null
    rm -f /tmp/dsflip-cpu-max
fi
XDG_RUNTIME_DIR=$RT pw-metadata -n settings 0 clock.force-rate 0 >/dev/null 2>&1          # PipeWire's own rate again
sway_has_outputs() {
    SOCK=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
    [ -n "$SOCK" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$SOCK" -t get_outputs 2>/dev/null | grep -q '"active": true'
}
wait_outputs() {                # up to 3 s
    i=0; while [ $i -lt 60 ]; do sway_has_outputs && return 0; sleep 0.05; i=$((i + 1)); done; return 1
}
# VT mode (session.sh switched the console away from sway): switch back, sway takes the display again and ES,
# which kept running, carries on (it makes a new window after a game; its launcher places it).
if [ -f /tmp/dsflip-vt ]; then
    VT=$(cat /tmp/dsflip-vt); rm -f /tmp/dsflip-vt
    chvt "${VT:-1}"
    if wait_outputs; then
        S=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
        THEME_SET=$(sed -n 's/.*<string name="ThemeSet" value="\([^"]*\)".*/\1/p' /storage/.config/emulationstation/es_settings.cfg 2>/dev/null)
        case "$THEME_SET" in
            ""|dii-ess-aye|canvas-ds|rocknixds-dark|rocknixds-light|rocknixds-pixel)
                [ -n "$S" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$S" '[app_id="emulationstation"] floating enable, fullscreen disable, resize set 1920 480, move absolute position 0 0' >/dev/null 2>&1
                ;;
        esac
    else
        echo "$(date) sway has no outputs after the VT switch: restarting it"
        systemctl restart sway.service; wait_outputs
        systemctl is-active -q essway.service || systemctl start essway.service
    fi
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
echo "$(up) restore: waiting for sway's outputs"
if ! wait_outputs; then
    echo "$(date) sway has no outputs: restarting it"
    systemctl restart sway.service
    wait_outputs
    systemctl is-active -q essway.service || systemctl start essway.service
fi
# The display controller stuck in an underrun loop (see session.sh: ~84,000 interrupts/s, 60% of a core, until
# reboot): count its interrupts for half a second (normal: ~60 per panel per second) and clear it by switching the
# panels off and on through sway, which does a full modeset. The panels blink once.
vop_irqs() { awk '/fe040000.vop/ { s = 0; for (i = 2; i <= 5; i++) s += $i; print s }' /proc/interrupts; }
echo "$(up) restore: outputs up"
a=$(vop_irqs); sleep 0.5; b=$(vop_irqs)
if [ -n "$a" ] && [ -n "$b" ] && [ $((b - a)) -gt 2000 ]; then
    echo "$(date) display controller interrupt storm ($(( (b - a) * 2 ))/s): power-cycling the panels"
    S=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
    XDG_RUNTIME_DIR=$RT swaymsg -s "$S" output '*' power off >/dev/null 2>&1; sleep 0.3
    XDG_RUNTIME_DIR=$RT swaymsg -s "$S" output '*' power on >/dev/null 2>&1
fi
# The menus' CPU governor (menu-power.sh), once ES is up: sway's and ES's start are CPU-heavy, and switching to
# schedutil before them made the way back to the menu ~0.8 s slower. Its own transient unit, like the notice below.
systemd-run --collect --quiet /storage/.config/drastic/dsflip/menu-power.sh --after-es-idle >/dev/null 2>&1
# The performance log (same samples as tools/rgds-monitor.py). mv so it closes once: this script runs twice after a
# normal exit (session.sh and the unit's ExecStopPost). The upload continues beside the menu.
P=/tmp/dsflip-perf/active
if [ -d "$P" ] && mv "$P" /tmp/dsflip-perf/closing.$$ 2>/dev/null; then
    C=/tmp/dsflip-perf/closing.$$
    # a transient unit, so it outlives this script (ExecStopPost kills whatever it started). Foreground only if
    # systemd-run itself fails: the log would otherwise be deleted with the unit.
    if ! systemd-run --collect --quiet python3 -u /storage/.config/drastic/dsflip/perf-session.py finish "$C" >/dev/null 2>&1; then
        python3 -u /storage/.config/drastic/dsflip/perf-session.py finish "$C" >> /storage/.config/drastic/dsflip/last-session.log 2>&1
    fi
fi
# session.sh's message about why the game ended early: show it in ES once ES answers. From a transient unit of its
# own, because this script may be running as dsflip-game's ExecStopPost, whose processes die when it finishes.
# The mv makes it show once even though this script runs twice after a normal exit (session.sh + ExecStopPost).
N=/tmp/dsflip-notice.$$
if [ -s /tmp/dsflip-notice ] && mv /tmp/dsflip-notice $N 2>/dev/null; then
    systemd-run --collect --quiet sh -c "for i in \$(seq 1 120); do curl -s -m 1 localhost:1234/isIdle 2>/dev/null | grep -q true && break; sleep 0.5; done; sleep 1; curl -s -m 5 -X POST --data-binary @$N localhost:1234/messagebox >/dev/null; rm -f $N" >/dev/null 2>&1 || rm -f $N
fi
echo "$(up) restore: done"
exit 0
