#!/bin/sh
# Run ON the RG DS (ROCKNIX) from a directory holding libdsflip.so, session.sh and drastic-wrapper.sh:
#   sh install.sh
# Installs libdsflip as the default DraStic launcher. Keeps the previous launcher as drastic.dvsync
# (fallback: touch /storage/.config/drastic/nodsflip) and a backup as drastic.pre-dsflip.bak.
set -e
D=/storage/.config/drastic
HERE=$(cd "$(dirname "$0")" && pwd)
[ -x $D/drastic.real ] || [ -x $D/drastic ] || { echo "no DraStic in $D (start a DS game once first)"; exit 1; }
# first install on a stock ROCKNIX: the real binary is still called 'drastic'
if [ ! -e $D/drastic.real ]; then
    mv $D/drastic $D/drastic.real
    printf '#!/bin/sh\nexport LD_PRELOAD=/usr/lib/libdrastouch.so\nexec /storage/.config/drastic/drastic.real "$@"\n' > $D/drastic
    chmod +x $D/drastic
fi
mkdir -p $D/dsflip
cp "$HERE/libdsflip.so" "$HERE/session.sh" "$HERE/restore.sh" $D/dsflip/
[ -f "$HERE/es-features.sh" ] && cp "$HERE/es-features.sh" $D/dsflip/
[ -f "$HERE/preload-guard.so" ] && cp "$HERE/preload-guard.so" $D/dsflip/
[ -f "$HERE/playstats.py" ] && cp "$HERE/playstats.py" $D/dsflip/
[ -f "$HERE/perf-session.py" ] && cp "$HERE/perf-session.py" $D/dsflip/
if [ -f "$HERE/es-share-logs.sh" ]; then
    mkdir -p /storage/.config/emulationstation/scripts/start
    cp "$HERE/es-share-logs.sh" /storage/.config/emulationstation/scripts/start/rocknixds-share-logs.sh
    chmod +x /storage/.config/emulationstation/scripts/start/rocknixds-share-logs.sh
fi
[ -f "$HERE/menu-power.sh" ] && cp "$HERE/menu-power.sh" $D/dsflip/
[ -f "$HERE/battery-led-status" ] && cp "$HERE/battery-led-status" $D/dsflip/ && chmod +x $D/dsflip/battery-led-status
[ -f "$HERE/powerstate" ] && cp "$HERE/powerstate" $D/dsflip/ && chmod +x $D/dsflip/powerstate
[ -f "$HERE/fast-switch" ] && cp "$HERE/fast-switch" $D/dsflip/ && chmod +x $D/dsflip/fast-switch
[ -f "$HERE/media-auto.sh" ] && cp "$HERE/media-auto.sh" $D/dsflip/
chmod +x $D/dsflip/*.sh
ln -sf ../drastic.real $D/dsflip/drastic          # named 'drastic' so the exit hotkey (killall drastic) matches
[ -e $D/drastic.dvsync ] || cp -p $D/drastic $D/drastic.dvsync
[ -e $D/drastic.pre-dsflip.bak ] || cp $D/drastic $D/drastic.pre-dsflip.bak   # once: on an upgrade it's our own wrapper
cp "$HERE/drastic-wrapper.sh" $D/drastic; chmod +x $D/drastic
echo "libdsflip installed. Fallback: touch $D/nodsflip"
