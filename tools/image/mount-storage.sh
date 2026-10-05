# ROCKNIXDS SD card image: sourced by ROCKNIX's initramfs (init's mount_storage) in place of its own
# mount_part "$disk" /storage. It mounts /storage the same way, then, on the first boot after ROCKNIX's resize, puts
# the first-boot installer in /storage/.config/autostart. Not before the resize: fs-resize refuses to run once
# /storage/.config exists. Busybox sh; firstboot.sh removes this file once ROCKNIXDS is installed.
mount_part "$disk" "/storage" "rw,noatime"
if [ ! -e /storage/.please_resize_me ] && [ ! -e /storage/.rocknixds-seeded ] && [ -f /flash/rocknixds/firstboot.sh ]; then
    mkdir -p /storage/.config/autostart
    cp /flash/rocknixds/firstboot.sh /storage/.config/autostart/zz-rocknixds-firstboot
    chmod 755 /storage/.config/autostart/zz-rocknixds-firstboot
    touch /storage/.rocknixds-seeded
fi
