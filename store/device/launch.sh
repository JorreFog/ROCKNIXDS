#!/bin/sh
# The ROCKNIXDS Store's launcher (/storage/.config/rocknixds/store/launch.sh, started by its menu entry "ROCKNIXDS
# Store.sh"). The Store draws straight onto both panels (KMS), as Döda Kvarter does, in a detached systemd unit that
# stops ES and sway and brings them back afterwards. Unlike the game it always stops ES, fast-switch or not: ES reads
# its systems (an app's new tile) and game lists only when it starts.
D=/storage/.config/rocknixds/store
[ -x $D/store ] || { echo "The ROCKNIXDS Store isn't installed in $D"; exit 1; }
if systemd-run --unit=rocknixds-store --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=20 \
       $D/session.sh >/dev/null 2>&1; then
    exec sleep 86400        # stopping ES (from the unit) ends this and the launcher with it
fi
# No systemd-run: a window under sway instead (both panels side by side, as sway lays them out). The menu shows a new
# app's tile the next time it starts.
cd $D
XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir} DK_DATA=$D/data DK_LOG_NAME=store.log DK_SDL_LAYOUT=side \
    RNDS_STORE_CLI=$D/rocknixds-store ./store --backend sdl
