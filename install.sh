#!/bin/sh
# RG DS x ROCKNIX installer: dual-screen dii-ess-aye theme + patched EmulationStation + libdsflip (DraStic
# straight to both panels) + hires 3D. Run ON the Anbernic RG DS as root (ssh in, default password: rocknix):
#
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/rgds-rocknix/main/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/rgds-rocknix/main/install.sh | sh -s -- --with-60hz
#
# Options:
#   --with-60hz     also retune both panels to 60.000 Hz (edits the device tree in /flash; backed up; reboot needed)
#   --no-theme      skip the theme + patched ES
#   --no-dsflip     skip libdsflip (keep the stock DraStic display path)
#   --no-hires      don't switch on hires 3D for Nintendo DS
#   --uninstall     undo everything this installer changed (restores the backups it made)
# Env: RGDS_SRC=/path/to/checkout installs from a local copy instead of downloading.
# Everything it replaces is backed up under /storage/rgds-rocknix-backup/ first.
set -e

REPO=JorreFog/rgds-rocknix
BRANCH=${RGDS_BRANCH:-main}
THEME_UPSTREAM=beebono/dii-ess-aye
THEME_COMMIT=9fd5eee                     # the upstream commit the overlay was made against
ES_THEMES=/storage/.config/emulationstation/themes
THEME=$ES_THEMES/dii-ess-aye
ES_SETTINGS=/storage/.config/emulationstation/es_settings.cfg
SYSCFG=/storage/.config/system/configs/system.cfg
DRASTIC=/storage/.config/drastic
BACKUP=/storage/rgds-rocknix-backup
WORK=/storage/.rgds-install

WITH_60HZ=0 THEME_ON=1 DSFLIP_ON=1 HIRES_ON=1 UNINSTALL=0
for a in "$@"; do
    case $a in
    --with-60hz) WITH_60HZ=1 ;;
    --no-theme) THEME_ON=0 ;;
    --no-dsflip) DSFLIP_ON=0 ;;
    --no-hires) HIRES_ON=0 ;;
    --uninstall) UNINSTALL=1 ;;
    *) echo "unknown option: $a"; exit 1 ;;
    esac
done

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*"; exit 1; }

# ---- sanity checks -----------------------------------------------------------------------------------
[ "$(id -u)" = 0 ] || die "run as root (ssh root@<device>)"
grep -qi rocknix /etc/os-release 2>/dev/null || die "this isn't ROCKNIX"
[ -f /flash/device_trees/rk3568-anbernic-rg-ds.dtb ] || tr -d '\0' < /proc/device-tree/model 2>/dev/null | grep -qi "rg.\?ds" \
    || die "this doesn't look like an Anbernic RG DS"

systemctl is-active -q dsflip-game.service 2>/dev/null && die "a DS game is running: quit it first"

es_stop()  { systemctl stop essway.service 2>/dev/null || true; trap 'systemctl start essway.service 2>/dev/null' EXIT; }
es_start() { systemctl start essway.service 2>/dev/null || true; }
backup_once() {   # backup_once <file>: keep the first (pre-install) copy only
    [ -e "$1" ] || return 0
    dst=$BACKUP$1
    [ -e "$dst" ] && return 0
    mkdir -p "$(dirname "$dst")"; cp -a "$1" "$dst"
}
set_cfg() {       # set_cfg <key> <value> in system.cfg
    if grep -q "^$1=" $SYSCFG; then sed -i "s|^$1=.*|$1=$2|" $SYSCFG; else echo "$1=$2" >> $SYSCFG; fi
}

# ---- uninstall ---------------------------------------------------------------------------------------
if [ $UNINSTALL = 1 ]; then
    [ -d $BACKUP ] || die "no backup at $BACKUP: nothing to undo"
    say "Restoring the pre-install state from $BACKUP"
    systemctl stop dsflip-game.service 2>/dev/null || true
    es_stop
    grep -q " /usr/bin/start_es.sh " /proc/mounts && umount /usr/bin/start_es.sh || true
    rm -f /storage/.config/autostart/dii-ess-aye
    # restore every backed-up file (sway config, es_settings, system.cfg, drastic launcher)
    [ -d $BACKUP/storage ] && ( cd $BACKUP && find ./storage -type f ) | while read -r f; do
        p=${f#.}
        mkdir -p "$(dirname "$p")"; cp -a "$BACKUP$p" "$p"
    done
    if [ -e $BACKUP/.had-no-launcher-wrapper ] && [ -e $DRASTIC/drastic.real ]; then
        rm -f $DRASTIC/drastic $DRASTIC/drastic.dvsync; mv $DRASTIC/drastic.real $DRASTIC/drastic   # stock layout again
    elif grep -q dsflip $DRASTIC/drastic 2>/dev/null; then
        # the pre-install launcher was itself a libdsflip wrapper (manual install): fall back to what it wraps
        if [ -e $DRASTIC/drastic.dvsync ]; then cp -p $DRASTIC/drastic.dvsync $DRASTIC/drastic
        else printf '#!/bin/sh\nexec /storage/.config/drastic/drastic.real "$@"\n' > $DRASTIC/drastic; chmod +x $DRASTIC/drastic; fi
    fi
    rm -rf $DRASTIC/dsflip
    [ -e $BACKUP/.theme-installed-by-us ] && rm -rf $THEME
    [ -d $BACKUP/theme-previous ] && mv $BACKUP/theme-previous $THEME
    systemctl restart sway.service 2>/dev/null || true; sleep 2
    es_start
    mv $BACKUP $BACKUP.undone-$(date +%Y%m%d-%H%M%S)
    say "Done. (The 60 Hz DTB, if applied, stays: restore /storage/rg-ds.dtb.bak to /flash by hand to undo it.)"
    exit 0
fi

# ---- fetch this repo (or use a local copy: RGDS_SRC=/path/to/checkout) --------------------------------
rm -rf $WORK; mkdir -p $WORK
if [ -n "$RGDS_SRC" ]; then
    SRC=$RGDS_SRC; say "Using local source $SRC"
else
    say "Downloading $REPO ($BRANCH)"
    curl -fsSL "https://codeload.github.com/$REPO/tar.gz/$BRANCH" | tar xz -C $WORK
    SRC=$(echo $WORK/*/)
fi
[ -f "$SRC/dsflip/libdsflip.so" ] || die "download incomplete"
mkdir -p $BACKUP

es_stop

# ---- theme + patched EmulationStation -----------------------------------------------------------------
if [ $THEME_ON = 1 ]; then
    say "Installing the dii-ess-aye theme (upstream $THEME_UPSTREAM@$THEME_COMMIT + RG DS overlay)"
    mkdir -p $WORK/theme
    curl -fsSL "https://codeload.github.com/$THEME_UPSTREAM/tar.gz/$THEME_COMMIT" | tar xz -C $WORK/theme --strip-components=1
    [ -f $WORK/theme/theme.xml ] || die "theme download failed"
    if [ -d $THEME ] && [ ! -e $BACKUP/.theme-installed-by-us ] && [ ! -d $BACKUP/theme-previous ]; then
        cp -a $THEME $BACKUP/theme-previous     # someone's own dii-ess-aye: keep it
    fi
    grep -q " /usr/bin/start_es.sh " /proc/mounts && umount /usr/bin/start_es.sh || true
    rm -rf $THEME; mkdir -p $ES_THEMES
    cp -a $WORK/theme $THEME
    cp -a "$SRC/dii-ess-aye/overlay/." $THEME/
    mkdir -p $THEME/bin
    cp "$SRC/dii-ess-aye/emulationstation-rgds" $THEME/bin/emulationstation
    chmod +x $THEME/bin/emulationstation $THEME/scripts/*.sh
    touch $BACKUP/.theme-installed-by-us

    say "Sway config, boot hook and ES settings"
    backup_once /storage/.config/sway/config
    backup_once $ES_SETTINGS
    backup_once /storage/.config/autostart/dii-ess-aye        # an earlier manual install's hook: keep it on uninstall
    backup_once /storage/dii-ess-aye-backup/sway-config.theme
    mkdir -p /storage/dii-ess-aye-backup /storage/.config/autostart
    cp "$SRC/dii-ess-aye/device/sway-config.theme" /storage/dii-ess-aye-backup/sway-config.theme
    cp "$SRC/dii-ess-aye/device/autostart-dii-ess-aye" /storage/.config/autostart/dii-ess-aye
    chmod +x /storage/.config/autostart/dii-ess-aye
    # the theme's own enable script: launcher bind mount, sway config, ThemeSet/menu settings
    # (ES is stopped; the flag stops it from restarting ES itself, we do that at the end)
    touch /tmp/has-restarted-for-theme
    XDG_RUNTIME_DIR=/var/run/0-runtime-dir SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1) \
        bash $THEME/scripts/enable_theme_rgds.sh
fi

# ---- libdsflip ---------------------------------------------------------------------------------------
if [ $DSFLIP_ON = 1 ]; then
    say "Installing libdsflip as the default DraStic launcher"
    if [ ! -d $DRASTIC ]; then          # first run of DraStic hasn't happened yet: do what start_drastic.sh does
        mkdir -p $DRASTIC; cp -r /usr/config/drastic/* $DRASTIC/
    fi
    backup_once $DRASTIC/drastic
    [ -e $DRASTIC/drastic.real ] || touch $BACKUP/.had-no-launcher-wrapper
    mkdir -p $WORK/dsflip
    cp "$SRC/dsflip/libdsflip.so" "$SRC/dsflip/device/session.sh" "$SRC/dsflip/device/drastic-wrapper.sh" \
       "$SRC/dsflip/device/install.sh" $WORK/dsflip/
    sh $WORK/dsflip/install.sh
fi

# ---- hires 3D ----------------------------------------------------------------------------------------
if [ $HIRES_ON = 1 ] && [ -f $SYSCFG ]; then
    say "Switching on hires 3D (2x internal resolution) for Nintendo DS"
    backup_once $SYSCFG
    set_cfg nds.hires_3d 1
fi

# ---- 60 Hz panels (opt-in) -----------------------------------------------------------------------------
if [ $WITH_60HZ = 1 ]; then
    say "Retuning both panels to 60.000 Hz"
    sh "$SRC/dii-ess-aye/device/apply-60hz-dtb.sh" || say "60 Hz step skipped (see message above)"
    NEED_REBOOT=1
fi

rm -rf $WORK
es_start
say "Installed. Start a DS game from EmulationStation as usual."
say "Undo: curl -fsSL https://raw.githubusercontent.com/$REPO/main/install.sh | sh -s -- --uninstall"
[ -n "$NEED_REBOOT" ] && say "Reboot for the 60 Hz panel timing to take effect." || true
