#!/bin/sh
# Brings the desktop back after a libdsflip game session: GPU governor, sway, ES. Called by session.sh when
# DraStic exits, and by the unit's ExecStopPost when the session was stopped or killed (then it's the only
# thing that runs). Safe to run twice: every step checks first.
#
# A sway started while DraStic's DRM state was still being torn down comes up with no outputs (both panels
# black, ES crash-looping on "the video driver did not add any displays"), so after starting it we check that
# it owns the panels and restart it once if not.
GPU=/sys/class/devfreq/fde60000.gpu
RT=/var/run/0-runtime-dir
if [ -f /tmp/dsflip-gpu-governor ]; then
    cat /tmp/dsflip-gpu-governor > $GPU/governor 2>/dev/null
    rm -f /tmp/dsflip-gpu-governor
fi
sway_has_outputs() {
    SOCK=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
    [ -n "$SOCK" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$SOCK" -t get_outputs 2>/dev/null | grep -q '"active": true'
}
sway_up() {
    systemctl is-active -q sway.service || systemctl start sway.service
    for i in 1 2 3 4 5 6 7 8 9 10; do sway_has_outputs && return 0; sleep 0.3; done
    echo "$(date) sway has no outputs: restarting it"
    systemctl restart sway.service
    for i in 1 2 3 4 5 6 7 8 9 10; do sway_has_outputs && return 0; sleep 0.3; done
    return 1
}
sway_up
systemctl is-active -q essway.service || systemctl start essway.service
exit 0
