#!/bin/sh
# Döda Kvarter's launcher on the RG DS (/storage/.config/rocknixds/dodakvarter/launch.sh, started by the Ports entry
# "Doda Kvarter.sh"). The game draws straight onto both panels (KMS), which needs the display: it runs in a
# detached systemd unit that stops ES and sway and brings them back afterwards, as ROCKNIXDS's DS games do
# (dsflip/device/drastic-wrapper.sh). With fast-switch on (dsflip's vt-switch), ES and sway stay up and the
# session switches the console away from sway instead, so this waits for the game.
D=/storage/.config/rocknixds/dodakvarter
DS=/storage/.config/drastic/dsflip
[ -x $D/dodakvarter ] || { echo "Döda Kvarter isn't installed in $D"; exit 1; }
if [ -e $DS/vt-switch ] && systemctl is-active -q sway.service; then
    systemd-run --wait --unit=dodakvarter-game --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=10 \
        $D/session.sh >/dev/null 2>&1
    exit $?
fi
if systemd-run --unit=dodakvarter-game --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=10 \
       $D/session.sh >/dev/null 2>&1; then
    exec sleep 86400        # stopping ES (from the unit) ends this and the Ports launcher with it
fi
# No systemd-run: a window under sway instead (both panels side by side, as sway lays them out)
cd $D
XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir} DK_DATA=$D/data DK_SDL_LAYOUT=side ./dodakvarter --backend sdl
