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
chmod +x $D/dsflip/session.sh $D/dsflip/restore.sh
ln -sf ../drastic.real $D/dsflip/drastic          # named 'drastic' so the exit hotkey (killall drastic) matches
[ -e $D/drastic.dvsync ] || cp -p $D/drastic $D/drastic.dvsync
[ -e $D/drastic.pre-dsflip.bak ] || cp $D/drastic $D/drastic.pre-dsflip.bak   # once: on an upgrade it's our own wrapper
cp "$HERE/drastic-wrapper.sh" $D/drastic; chmod +x $D/drastic
echo "libdsflip installed. Fallback: touch $D/nodsflip"
