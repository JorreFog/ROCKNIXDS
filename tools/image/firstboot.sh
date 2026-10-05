#!/bin/bash
# ROCKNIXDS SD card image: installs ROCKNIXDS from /flash/rocknixds on the first boot after ROCKNIX's resize, with no
# network. mount-storage.sh puts it in /storage/.config/autostart; ROCKNIX's autostart runs it before the menu starts.
# Picks the release for this handheld (rgds or plus), runs that release's install.sh in its offline first-boot mode,
# then removes itself and the payload from /flash. A failed install is tried again on the next 2 boots; the log is
# /storage/.config/rocknixds-firstboot.log.
P=/flash/rocknixds
HOOK=/storage/.config/autostart/zz-rocknixds-firstboot
LOG=/storage/.config/rocknixds-firstboot.log
TRIES=/storage/.rocknixds-firstboot-tries
W=/storage/.rocknixds-firstboot
. /etc/profile.d/001-functions 2>/dev/null
command -v tocon >/dev/null || tocon() { echo "$*" > /dev/console; }

rgds_plus() {   # the installer's check: the model string, or a panel wider than the RG DS's 640
    model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
    case "$model" in *"RG DS Plus"*) return 0 ;; esac
    for m in /sys/class/drm/card*-DSI-*/modes; do
        [ -f "$m" ] || continue
        read -r mode < "$m" 2>/dev/null || continue
        w=${mode%%x*}
        [ "$w" -gt 640 ] 2>/dev/null && return 0
        break
    done
    return 1
}

finish() {      # no more tries: the hook goes, and with success the payload too
    rm -f "$HOOK" "$TRIES"; rm -rf "$W"
    if [ "$1" = ok ]; then
        mount -o remount,rw /flash && { rm -rf $P /flash/mount-storage.sh; sync; mount -o remount,ro /flash; }
    fi
}

tries=$(($(cat $TRIES 2>/dev/null || echo 0) + 1)); echo $tries > $TRIES
H=rgds; rgds_plus && H=plus
REF=$(cat $P/ref-$H 2>/dev/null)
tocon "Installing ROCKNIXDS $REF (first boot, about a minute)..."
(
    echo "$(date): ROCKNIXDS first boot, try $tries: $H, $REF"
    [ -f $P/rocknixds-$H.tar.gz ] && [ -n "$REF" ] || { echo "no ROCKNIXDS for $H in $P"; exit 1; }
    rm -rf $W; mkdir -p $W/src
    tar xzf $P/rocknixds-$H.tar.gz -C $W/src --strip-components=1 || exit 1
    RGDS_SRC=$W/src RGDS_REF=$REF RGDS_DEPS=$P RGDS_FIRSTBOOT=1 sh $W/src/install.sh
) >> $LOG 2>&1
rc=$?
if [ $rc = 0 ]; then
    echo "$(date): installed" >> $LOG
    # ROCKNIX's autostart listed its hooks before the install added these: run them for this boot
    for h in /storage/.config/autostart/dii-ess-aye /storage/.config/autostart/rocknixds-es-features; do
        [ -x "$h" ] && "$h" >> $LOG 2>&1
    done
    finish ok
elif [ $tries -ge 3 ]; then
    echo "$(date): failed ($rc) $tries times: giving up" >> $LOG
    tocon "ROCKNIXDS couldn't be installed: see $LOG"
    finish
else
    echo "$(date): failed ($rc): trying again next boot" >> $LOG
fi
exit 0
