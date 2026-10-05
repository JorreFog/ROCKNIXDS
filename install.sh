#!/bin/sh
# ROCKNIXDS installer (Anbernic RG DS on ROCKNIX): dual-screen dii-ess-aye theme + patched EmulationStation + libdsflip (DraStic
# straight to both panels) + hires 3D. Run ON the Anbernic RG DS as root (ssh in, default password: rocknix):
#
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh -s -- --with-60hz
#
# Options:
#   --with-60hz     also retune both panels to 60.000 Hz (edits the device tree in /flash; backed up; reboot needed)
#   --no-theme      skip the theme + patched ES
#   --no-canvas     skip the second theme, canvas-ds (a ~180 MB download, once)
#   --no-dsflip     skip libdsflip (keep the stock DraStic display path)
#   --no-hires      don't switch on hires 3D for Nintendo DS
#   --uninstall     undo what this installer changed, leaving settings made since the install alone
#   --restore-files with --uninstall: put back whole config files from the install-time backups instead
#   --version       print the ROCKNIXDS version this installer belongs to
# Env: RGDS_SRC=/path/to/checkout installs from a local copy instead of downloading. With RGDS_REF as well, that's
#      the release it records for the update check (the SD card image's first boot).
#      RGDS_DEPS=/path/to/dir: the upstream themes from there (dii-ess-aye-<commit>.tar.gz, canvas-ds-<commit>.tar.gz,
#      codeload's tarballs) instead of downloading them, for an install with no network.
#      RGDS_FIRSTBOOT=1: run from ROCKNIX's autostart, before the menu starts (the SD card image): systemctl calls
#      only queue their jobs, since autostart is one of the jobs they would wait for.
#      RGDS_BRANCH: what to install. main (the default) is the newest release for this handheld: vX.Y on the
#      RG DS, vX.Y-plus on the RG DS Plus. beta is this handheld's beta branch (beta, or plus-beta on the Plus).
#      A tag (v1.5, v1.5-plus) is that release. The installer finds the exact tag or commit first, then runs that
#      ref's own install.sh (RGDS_REF), which installs that ref and records it for the update check.
#      ROCKNIXDS_UPLOAD_TOKEN, if set, is stored on the device for the performance-log upload (mode 600) and is
#      not printed. Leave it unset to keep an existing token.
# Everything it replaces is backed up under /storage/rgds-rocknix-backup/ first.
set -e

REPO=JorreFog/ROCKNIXDS
BRANCH=${RGDS_BRANCH:-main}
THEME_UPSTREAM=beebono/dii-ess-aye
CANVAS_UPSTREAM=toniremi/canvas-ds
CANVAS_COMMIT=ab3ba47ab8                  # the canvas-ds version verified on the RG DS's dual-screen setup
THEME_COMMIT=9fd5eee                     # the upstream commit the overlay was made against
ES_THEMES=/storage/.config/emulationstation/themes
THEME=$ES_THEMES/dii-ess-aye
ES_SETTINGS=/storage/.config/emulationstation/es_settings.cfg
SYSCFG=/storage/.config/system/configs/system.cfg
DRASTIC=/storage/.config/drastic
BACKUP=/storage/rgds-rocknix-backup   # old project name, kept so earlier installs can still be undone
WORK=/storage/.rgds-install
ESF=/storage/.config/emulationstation/es_features.cfg
VERSION_FILE=/storage/.config/rocknixds-version

WITH_60HZ=0 THEME_ON=1 CANVAS_ON=1 DSFLIP_ON=1 HIRES_ON=1 UNINSTALL=0 RESTORE_FILES=0
for a in "$@"; do
    case $a in
    --with-60hz) WITH_60HZ=1 ;;
    --no-theme) THEME_ON=0 ;;
    --no-canvas) CANVAS_ON=0 ;;
    --no-dsflip) DSFLIP_ON=0 ;;
    --no-hires) HIRES_ON=0 ;;
    --uninstall) UNINSTALL=1 ;;
    --restore-files) RESTORE_FILES=1 ;;
    --version) echo "ROCKNIXDS installer ${RGDS_VERSION:-$(cat $VERSION_FILE 2>/dev/null || echo unknown)} ($REPO $BRANCH)"; exit 0 ;;
    *) echo "unknown option: $a"; exit 1 ;;
    esac
done

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*"; exit 1; }

rgds_plus() {   # the RG DS Plus: the model string, or a panel wider than the RG DS's 640
    model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)
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

# ---- sanity checks -----------------------------------------------------------------------------------
[ "$(id -u)" = 0 ] || die "run as root (ssh root@<device>)"
grep -qi rocknix /etc/os-release 2>/dev/null || die "this isn't ROCKNIX"
[ -f /flash/device_trees/rk3568-anbernic-rg-ds.dtb ] \
    || [ -f /flash/device_trees/rk3568-anbernic-rg-ds-plus.dtb ] \
    || tr -d '\0' < /proc/device-tree/model 2>/dev/null | grep -qi "rg.\?ds" \
    || die "this doesn't look like an Anbernic RG DS or RG DS Plus"

systemctl is-active -q dsflip-game.service 2>/dev/null && die "a DS game is running: quit it first"
if [ "$RGDS_FIRSTBOOT" = 1 ]; then
    # ES, sway and the rest start after ROCKNIX's autostart, which runs this: waiting for them would never end
    systemctl() { command systemctl --no-block "$@"; }
fi

# ---- what to install: the exact release or commit for this handheld ------------------------------------
# Releases come in pairs from 1.5 on (vX.Y for the RG DS, vX.Y-plus for the RG DS Plus; pre-releases are
# betas), and each handheld has its beta branch. A device that runs the other handheld's installer (the README's
# command, or a 1.5 beta's updater, both fetch main's or beta's) is sent to its own here, before anything changes.
latest_release() {   # latest_release 0|1: the newest stable release's tag for the RG DS (0) or the Plus (1)
    curl -fsSL --max-time 20 "https://api.github.com/repos/$REPO/releases?per_page=50" 2>/dev/null | python3 -c '
import json, sys
plus = sys.argv[1] == "1"
try:
    rel = json.load(sys.stdin)
except ValueError:
    sys.exit()
for r in rel if isinstance(rel, list) else []:
    t = r.get("tag_name") or ""
    if r.get("draft") or r.get("prerelease"):
        continue
    if (t.endswith("-plus") if plus else "plus" not in t):
        print(t)
        break' "$1" 2>/dev/null
}
branch_head() { curl -fsSL --max-time 20 "https://api.github.com/repos/$REPO/commits/$1" 2>/dev/null | sed -n 's/^  "sha": *"\([0-9a-f]*\)".*/\1/p' | head -n1; }
PLUS=0; NAME="RG DS"; rgds_plus && PLUS=1 NAME="RG DS Plus"
if [ $UNINSTALL = 0 ] && [ -z "$RGDS_SRC" ] && [ -z "$RGDS_REF" ]; then
    case "$BRANCH" in
    main|stable)
        CH=main; REF=$(latest_release $PLUS)
        if [ -z "$REF" ]; then
            curl -fsS --max-time 15 -o /dev/null https://api.github.com/ 2>/dev/null || die "couldn't reach GitHub: check the network"
            die "there is no ROCKNIXDS release for the $NAME yet: install the beta (RGDS_BRANCH=beta)"
        fi ;;
    beta|plus-beta)
        CH=beta; [ $PLUS = 1 ] && CH=plus-beta
        REF=$(branch_head $CH); [ -n "$REF" ] || die "couldn't reach GitHub: check the network" ;;
    v[0-9]*)
        CH=$BRANCH REF=$BRANCH
        case "$REF" in
        *-plus|*-plus-*) [ $PLUS = 1 ] || die "$REF is the RG DS Plus's release; this is an RG DS" ;;
        *) [ $PLUS = 0 ] || die "$REF is the RG DS's release; this is an RG DS Plus (its releases end in -plus)" ;;
        esac ;;
    *)
        CH=$BRANCH; REF=$(branch_head "$BRANCH"); [ -n "$REF" ] || die "no branch $BRANCH (or GitHub unreachable)" ;;
    esac
    say "Anbernic $NAME: installing ROCKNIXDS $REF ($CH)"
    tmp=$(mktemp)
    curl -fsSL --max-time 120 "https://raw.githubusercontent.com/$REPO/$REF/install.sh" -o "$tmp" \
        || { rm -f "$tmp"; die "couldn't download the installer of $REF"; }
    set +e
    RGDS_BRANCH=$CH RGDS_REF=$REF sh "$tmp" "$@"
    rc=$?
    set -e
    rm -f "$tmp"
    exit $rc
fi

es_stop()  { systemctl stop essway.service 2>/dev/null || true; trap 'systemctl start essway.service 2>/dev/null' EXIT; }
es_start() { systemctl start essway.service 2>/dev/null || true; }
backup_once() {   # backup_once <file>: keep the first (pre-install) copy only
    dst=$BACKUP$1
    [ -e "$dst" ] || [ -e "$dst.rocknixds-absent" ] && return 0
    if [ ! -e "$1" ]; then
        # not there before the first install: a later install must not take ROCKNIXDS's own copy for the original
        # (uninstall would put it back)
        mkdir -p "$(dirname "$dst")"; touch "$dst.rocknixds-absent"; return 0
    fi
    mkdir -p "$(dirname "$dst")"; cp -a "$1" "$dst"
}
set_cfg() {       # set_cfg <key> <value> in system.cfg
    if grep -q "^$1=" $SYSCFG; then sed -i "s|^$1=.*|$1=$2|" $SYSCFG; else echo "$1=$2" >> $SYSCFG; fi
}
es_get() {        # es_get <key> <es_settings file>: the value of a <string name=...> setting, empty if absent
    sed -n 's|.*<string name="'"$1"'" value="\([^"]*\)" />.*|\1|p' "$2" 2>/dev/null | head -n1
}
es_set() {        # es_set <key> <value> in the live es_settings.cfg
    if grep -q "<string name=\"$1\"" $ES_SETTINGS 2>/dev/null; then
        sed -i "s|<string name=\"$1\" value=\"[^\"]*\" />|<string name=\"$1\" value=\"$2\" />|" $ES_SETTINGS
    else sed -i "s|</config>|\t<string name=\"$1\" value=\"$2\" />\n</config>|" $ES_SETTINGS; fi
}
themes_allow() { printf 'rocknixds-pixel-light\nrocknixds-pixel-dark\ndii-ess-aye\ncanvas-ds\n'; }   # pickable in the patched ES
es_setb() {       # es_setb <key> true|false: a <bool name=...> setting in the live es_settings.cfg
    if grep -q "<bool name=\"$1\"" $ES_SETTINGS 2>/dev/null; then
        sed -i "s|<bool name=\"$1\" value=\"[^\"]*\" />|<bool name=\"$1\" value=\"$2\" />|" $ES_SETTINGS
    else sed -i "s|</config>|\t<bool name=\"$1\" value=\"$2\" />\n</config>|" $ES_SETTINGS; fi
}
es_del() { sed -i "/<string name=\"$1\" /d" $ES_SETTINGS 2>/dev/null; }

# ---- uninstall ---------------------------------------------------------------------------------------
if [ $UNINSTALL = 1 ]; then
    [ -d $BACKUP ] || die "no backup at $BACKUP: nothing to undo"
    say "Restoring the pre-install state from $BACKUP"
    systemctl stop dsflip-game.service 2>/dev/null || true
    es_stop
    grep -q " /usr/bin/start_es.sh " /proc/mounts && umount /usr/bin/start_es.sh || true
    rm -f /storage/.config/autostart/dii-ess-aye
    B=$BACKUP/storage
    if [ $RESTORE_FILES = 1 ]; then
        # whole files from the install-time copies (what 1.2 always did): also undoes settings made since
        [ -d $B ] && ( cd $BACKUP && find ./storage -type f ! -name "*.rocknixds-absent" ) | while read -r f; do
            p=${f#.}
            mkdir -p "$(dirname "$p")"; cp -a "$BACKUP$p" "$p"
        done
    else
        # only what the installer changed, read out of the install-time copies, so everything else the user set
        # since then (RetroAchievements login, other emulators' options, ES settings) survives
        [ -f $B/.config/drastic/drastic ] && cp -a $B/.config/drastic/drastic $DRASTIC/drastic
        [ -f $B/.config/sway/config ] && cp -a $B/.config/sway/config /storage/.config/sway/config    # ROCKNIX regenerates it at boot anyway
        [ -f $B/dii-ess-aye-backup/sway-config.theme ] && cp -a $B/dii-ess-aye-backup/sway-config.theme /storage/dii-ess-aye-backup/sway-config.theme
        [ -f $B/.config/autostart/dii-ess-aye ] && cp -a $B/.config/autostart/dii-ess-aye /storage/.config/autostart/dii-ess-aye
        if [ -f $B/.config/retroarch/retroarch.cfg ] && [ -f /storage/.config/retroarch/retroarch.cfg ]; then
            # only the pause_nonactive line we change for lowerdeck + seat0 touch
            old=$(sed -n 's/^pause_nonactive = //p' $B/.config/retroarch/retroarch.cfg | head -n1)
            if [ -n "$old" ]; then
                sed -i "s/^pause_nonactive.*/pause_nonactive = $old/" /storage/.config/retroarch/retroarch.cfg
            else
                sed -i '/^pause_nonactive/d' /storage/.config/retroarch/retroarch.cfg
            fi
        fi
        if [ -f $B/.config/system/configs/system.cfg ] && [ -f $SYSCFG ]; then      # nds.hires_3d only
            old=$(sed -n 's/^nds\.hires_3d=//p' $B/.config/system/configs/system.cfg | head -n1)
            if [ -n "$old" ]; then set_cfg nds.hires_3d "$old"; else sed -i '/^nds\.hires_3d=/d' $SYSCFG; fi
        fi
        if [ -f $B/.config/emulationstation/es_settings.cfg ] && [ -f $ES_SETTINGS ]; then   # the theme's keys only
            for k in ThemeSet FullScreenMenu GameTransitionStyle PowerSaverMode; do
                old=$(es_get $k $B/.config/emulationstation/es_settings.cfg)
                if [ -n "$old" ]; then es_set $k "$old"; else es_del $k; fi
            done
        fi
        if [ -f $ESF ] && [ -f $DRASTIC/dsflip/es-features.sh ]; then
            # shader choices, resume, power profile, share performance logs, 3D renderer and texture filter.
            # Depth-aware (--strip-options): 1.5.5 nested the 3D options, and a skip that stops at the first
            # </feature> leaves the file unparsable.
            sh $DRASTIC/dsflip/es-features.sh --strip-options
        fi
    fi
    rm -f /storage/.config/emulationstation/scripts/theme-changed/rocknixds-layout.sh
    rm -f /storage/.config/emulationstation/scripts/game-end/rocknixds-menu-power.sh /storage/.config/autostart/rocknixds-menu-power \
          /storage/.config/emulationstation/scripts/start/rocknixds-menu-power.sh \
          /storage/.config/emulationstation/scripts/start/rocknixds-share-logs.sh
    rm -f /storage/.config/emulationstation/scripts/start/rocknixds-media.sh \
          /storage/.config/emulationstation/scripts/game-end/rocknixds-media.sh \
          /storage/.config/emulationstation/scripts/game-start/rocknixds-media.sh
    systemctl stop rocknixds-media.service rocknixds-media-ra.service 2>/dev/null
    rm -rf /storage/.cache/rocknixds-media
    if [ -f /storage/.config/system.d/batteryledstatus.service.d/rocknixds.conf ]; then      # ROCKNIX's LED monitor again
        rm -f /storage/.config/system.d/batteryledstatus.service.d/rocknixds.conf
        rmdir /storage/.config/system.d/batteryledstatus.service.d 2>/dev/null
        systemctl daemon-reload; systemctl restart batteryledstatus.service 2>/dev/null
    fi
    if [ -f /storage/.config/system.d/powerstate.service.d/rocknixds.conf ]; then           # ROCKNIX's powerstate again
        rm -f /storage/.config/system.d/powerstate.service.d/rocknixds.conf
        rmdir /storage/.config/system.d/powerstate.service.d 2>/dev/null
        systemctl daemon-reload; systemctl restart powerstate.service 2>/dev/null
    fi
    if [ -f /storage/.config/system.d/sway-touch.service.d/rocknixds.conf ]; then           # ROCKNIX's touch mapping again
        rm -f /storage/.config/system.d/sway-touch.service.d/rocknixds.conf
        rmdir /storage/.config/system.d/sway-touch.service.d 2>/dev/null
        systemctl daemon-reload     # takes effect at the next boot
    fi
    { echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor; } 2>/dev/null || true   # ROCKNIX's menu governor
    rmdir /storage/.config/emulationstation/scripts/theme-changed /storage/.config/emulationstation/scripts/game-end \
          /storage/.config/emulationstation/scripts/game-start /storage/.config/emulationstation/scripts/start \
          /storage/.config/emulationstation/scripts 2>/dev/null
    rm -f $VERSION_FILE /storage/.config/rocknixds-es-notice /storage/.config/rocknixds-stock-es /storage/.config/rocknixds-any-rocknix
    if [ -e $BACKUP/.had-no-launcher-wrapper ] && [ -e $DRASTIC/drastic.real ]; then
        rm -f $DRASTIC/drastic $DRASTIC/drastic.dvsync; mv $DRASTIC/drastic.real $DRASTIC/drastic   # stock layout again
    elif grep -q dsflip $DRASTIC/drastic 2>/dev/null; then
        # the pre-install launcher was itself a libdsflip wrapper (manual install): fall back to what it wraps
        if [ -e $DRASTIC/drastic.dvsync ]; then cp -p $DRASTIC/drastic.dvsync $DRASTIC/drastic
        else printf '#!/bin/sh\nexec /storage/.config/drastic/drastic.real "$@"\n' > $DRASTIC/drastic; chmod +x $DRASTIC/drastic; fi
    fi
    [ -e $DRASTIC/dsflip/vt-switch ] && es_del HideWindow      # fast-switch on set it; ROCKNIX's default again
    # ROCKNIX's DS emulators again (lockdown), while es-features.sh is still there
    [ -f $DRASTIC/dsflip/es-features.sh ] && sh $DRASTIC/dsflip/es-features.sh --unlock-nds
    rm -rf $DRASTIC/dsflip
    [ -f $BACKUP/.shaders-added ] && while read -r b; do rm -f "$DRASTIC/shaders/$b"; done < $BACKUP/.shaders-added
    [ -e $BACKUP/.esf-created ] && rm -f $ESF $ESF.rocknixds-old
    rm -f /storage/.config/autostart/rocknixds-es-features
    # lockdown and updates: ROCKNIX's DS emulators and settings menus again
    systemctl stop rocknixds-update-check.timer 2>/dev/null
    rm -f /storage/.config/system.d/rocknixds-update-check.service /storage/.config/system.d/rocknixds-update-check.timer \
          /storage/.config/system.d/timers.target.wants/rocknixds-update-check.timer
    rmdir /storage/.config/system.d/timers.target.wants 2>/dev/null; systemctl daemon-reload
    rm -rf /storage/.config/rocknixds
    [ -f $SYSCFG ] && sed -i '/^rocknixds\./d' $SYSCFG      # the update channel and the media and update switches
    [ -e $ES_THEMES/canvas-ds/.rocknixds-commit ] && rm -rf $ES_THEMES/canvas-ds      # the one this installer downloaded
    if [ -e $BACKUP/.theme-installed-by-us ]; then
        rm -rf $THEME $ES_THEMES/rocknixds-dark $ES_THEMES/rocknixds-light $ES_THEMES/rocknixds-pixel \
               $ES_THEMES/rocknixds-pixel-dark $ES_THEMES/rocknixds-pixel-light
    fi
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
    say "Downloading $REPO ${RGDS_REF:-$BRANCH} ($BRANCH)"
    curl -fsSL "https://codeload.github.com/$REPO/tar.gz/${RGDS_REF:-$BRANCH}" | tar xz -C $WORK
    SRC=$(echo $WORK/*/)
fi
[ -f "$SRC/dsflip/device/session.sh" ] || die "download incomplete"

# ---- SuperDrastic: the DraStic engine (libdsflip), from its pinned release ------------------------------
# (RGDS_SUPERDRASTIC=<superdrastic-*-aarch64.tar.gz>: a local package instead, for testing or offline installs)
SD=
if [ $DSFLIP_ON = 1 ]; then
    set -- $(grep -v '^#' "$SRC/SUPERDRASTIC"); SD_VER=$1 SD_SUM=$2
    [ -n "$SD_VER" ] || die "no SuperDrastic version in SUPERDRASTIC"
    SD_TGZ=$WORK/superdrastic.tar.gz
    VENDORED="$SRC/dsflip/superdrastic-$SD_VER-aarch64.tar.gz"
    if [ -n "$RGDS_SUPERDRASTIC" ]; then
        say "Using local SuperDrastic package $RGDS_SUPERDRASTIC"; cp "$RGDS_SUPERDRASTIC" $SD_TGZ || die "can't read $RGDS_SUPERDRASTIC"
    elif [ -f "$VENDORED" ]; then
        say "Using the SuperDrastic $SD_VER package shipped with this ROCKNIXDS"
        cp "$VENDORED" $SD_TGZ
    else
        say "Downloading SuperDrastic $SD_VER"
        curl -fsSL -o $SD_TGZ "https://github.com/JorreFog/SuperDrastic/releases/download/v$SD_VER/superdrastic-$SD_VER-aarch64.tar.gz" \
            || die "couldn't download SuperDrastic $SD_VER"
    fi
    set -- $(sha256sum $SD_TGZ)
    [ "$1" = "$SD_SUM" ] || die "SuperDrastic $SD_VER doesn't match its checksum"
    mkdir -p $WORK/sd && tar xzf $SD_TGZ -C $WORK/sd --strip-components=1 || die "SuperDrastic package damaged"
    [ -f $WORK/sd/libsuperdrastic.so ] || die "no libsuperdrastic.so in the SuperDrastic package"
    SD=$WORK/sd
fi
mkdir -p $BACKUP

es_stop

# ---- theme + patched EmulationStation -----------------------------------------------------------------
if [ $THEME_ON = 1 ]; then
    say "Installing the dii-ess-aye theme (upstream $THEME_UPSTREAM@$THEME_COMMIT + RG DS overlay)"
    mkdir -p $WORK/theme
    if [ -n "$RGDS_DEPS" ]; then tar xzf "$RGDS_DEPS/dii-ess-aye-$THEME_COMMIT.tar.gz" -C $WORK/theme --strip-components=1
    else curl -fsSL "https://codeload.github.com/$THEME_UPSTREAM/tar.gz/$THEME_COMMIT" | tar xz -C $WORK/theme --strip-components=1; fi
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
    # the ROCKNIX build (OS_VERSION) the patched ES was compiled against: on any other, the launcher checks it links
    cp "$SRC/dii-ess-aye/emulationstation-rgds.rocknix" $THEME/bin/rocknix-version
    ES_FOR=$(cat $THEME/bin/rocknix-version); OS_VER=$(. /etc/os-release; echo "$OS_VERSION")
    if [ "$ES_FOR" != "$OS_VER" ]; then
        if LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes /usr/lib/ld-linux-aarch64.so.1 $THEME/bin/emulationstation 2>&1 |
                grep -qE 'undefined symbol|not found|cannot open|error while loading'; then
            say "Note: the patched EmulationStation (built for ROCKNIX $ES_FOR) can't run on ROCKNIX $OS_VER."
            say "      The theme runs on stock EmulationStation instead (menus, popups and the keyboard span both"
            say "      screens) until a ROCKNIXDS release for this ROCKNIX."
        else
            say "Note: the patched EmulationStation is built for ROCKNIX $ES_FOR; this is $OS_VER. Its libraries all"
            say "      resolve here, so it runs; if it crashes twice while starting, stock EmulationStation takes over."
        fi
    fi
    chmod +x $THEME/bin/emulationstation $THEME/scripts/*.sh
    # ROCKNIXDS Pixel, dark and light: the menu mockup, drawn by the rnds engine in the patched ES. Everything it draws
    # is in rocknixds-pixel-dark/rnds (the light theme uses that folder too). Their theme.xml also includes
    # dii-ess-aye's layout, which only stock ES uses (when the patched ES can't run); assets/ is dii-ess-aye's, for it
    # and for the DSi sounds. 1.5 betas' rocknixds-dark, rocknixds-light and rocknixds-pixel go.
    rm -rf $ES_THEMES/rocknixds-dark $ES_THEMES/rocknixds-light $ES_THEMES/rocknixds-pixel
    for variant in rocknixds-pixel-dark rocknixds-pixel-light; do
        dest=$ES_THEMES/$variant
        rm -rf "$dest"
        cp -a "$SRC/dii-ess-aye/themes/$variant" "$dest"
        ln -s ../dii-ess-aye/assets "$dest/assets"
    done
    touch $BACKUP/.theme-installed-by-us
    # the patched ES offers every theme while this list is missing: write it as soon as the themes are in place
    mkdir -p /storage/.config/rocknixds && themes_allow > /storage/.config/rocknixds/themes.allow

    say "Sway config, boot hook and ES settings"
    backup_once /storage/.config/sway/config
    backup_once $ES_SETTINGS
    backup_once /storage/.config/autostart/dii-ess-aye        # an earlier manual install's hook: keep it on uninstall
    backup_once /storage/dii-ess-aye-backup/sway-config.theme
    backup_once /storage/.config/retroarch/retroarch.cfg
    mkdir -p /storage/dii-ess-aye-backup /storage/.config/autostart
    cp "$SRC/dii-ess-aye/device/sway-config.theme" /storage/dii-ess-aye-backup/sway-config.theme
    cp "$SRC/dii-ess-aye/device/autostart-dii-ess-aye" /storage/.config/autostart/dii-ess-aye
    chmod +x /storage/.config/autostart/dii-ess-aye
    # restart ES when the theme choice switches between this theme and another (their canvases differ)
    mkdir -p /storage/.config/emulationstation/scripts/theme-changed
    cp "$SRC/dii-ess-aye/device/theme-changed.sh" /storage/.config/emulationstation/scripts/theme-changed/rocknixds-layout.sh
    chmod +x /storage/.config/emulationstation/scripts/theme-changed/rocknixds-layout.sh
    # the theme's own enable script: launcher bind mount, sway config, ThemeSet/menu settings
    # (ES is stopped; the flag stops it from restarting ES itself, we do that at the end)
    touch /tmp/has-restarted-for-theme
    XDG_RUNTIME_DIR=/var/run/0-runtime-dir SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1) \
        bash $THEME/scripts/enable_theme_rgds.sh
    # Goodix is also on seat0 so ES gets touch (see sway-config.theme). A tap on lowerdeck then moves seat0
    # focus off RetroArch; with pause_nonactive on, the game stays paused even after Resume. Off: lowerdeck's
    # own MENU_TOGGLE still pauses for the VC menu, and Resume works.
    RACFG=/storage/.config/retroarch/retroarch.cfg
    if [ -f "$RACFG" ]; then
        if grep -q '^pause_nonactive' "$RACFG"; then
            sed -i 's/^pause_nonactive.*/pause_nonactive = "false"/' "$RACFG"
        else
            echo 'pause_nonactive = "false"' >> "$RACFG"
        fi
    fi
    # ROCKNIX's sway-touch service maps the touchscreen to the top panel at boot (the focused output under the
    # dual-screen menu): taps on the bottom panel then reached neither the menu nor RetroArch's bottom-screen menu
    # (issue #33). Off; the touchscreen is unmapped now as well, as it is after the next boot.
    if [ -f /usr/lib/systemd/system/sway-touch.service ]; then
        mkdir -p /storage/.config/system.d/sway-touch.service.d
        cp "$SRC/dsflip/device/sway-touch-rocknixds.conf" /storage/.config/system.d/sway-touch.service.d/rocknixds.conf
        systemctl daemon-reload
        XDG_RUNTIME_DIR=/var/run/0-runtime-dir SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock 2>/dev/null | head -n1) \
            swaymsg input 1046:911:Goodix_Capacitive_TouchScreen map_to_output '*' >/dev/null 2>&1 || true   # no sway yet at first boot
    fi
    # ES's power saver on "enhanced": an idle menu draws nothing instead of 25-60 frames a second (the patched ES
    # still wakes each minute for the clock). Only if it's on ES's default: a choice made in the menu stays.
    case "$(es_get PowerSaverMode $ES_SETTINGS)" in ""|default) es_set PowerSaverMode enhanced ;; esac
    # ES starts on the system it was last on; with none yet (a fresh SD card) it took the first in its list, Favorites,
    # not the DS. The patched ES always lists the DS, even before there are games.
    [ -n "$(es_get LastSystem $ES_SETTINGS)" ] || es_set LastSystem nds
    # 1.5: ROCKNIXDS Pixel Light is the theme, once (the first install of 1.5 or later; a choice made after that stays,
    # an uninstall forgets it). The 1.5 betas' rocknixds-dark/-light/-pixel are gone.
    if [ ! -e /storage/.config/rocknixds/.pixel-default ]; then
        es_set ThemeSet rocknixds-pixel-light
        mkdir -p /storage/.config/rocknixds && touch /storage/.config/rocknixds/.pixel-default
    fi
    # ROCKNIXDS Pixel's menu sounds: ES plays theme sounds only with "enable navigation sounds" on (off by default).
    # On once; switching them off in the menu stays.
    if [ ! -e /storage/.config/rocknixds/.sounds-default ]; then
        es_setb EnableSounds true
        touch /storage/.config/rocknixds/.sounds-default
    fi
fi

# ---- canvas-ds: a second dual-screen theme ---------------------------------------------------------------
# toniremi/canvas-ds (made for ROCKNIX on the RG DS, after dii-ess-aye's layout), downloaded from upstream at the
# commit verified here: its theme files only (its scripts set up the launcher and sway, which ROCKNIXDS does itself).
# Only the themes in /storage/.config/rocknixds/themes.allow can be picked (the patched ES), and both are listed.
if [ $THEME_ON = 1 ] && [ $CANVAS_ON = 1 ]; then
    C=$ES_THEMES/canvas-ds
    if [ "$(cat $C/.rocknixds-commit 2>/dev/null)" != $CANVAS_COMMIT ]; then
        rm -rf $WORK/canvas; mkdir -p $WORK/canvas
        if [ -n "$RGDS_DEPS" ]; then
            say "Installing the canvas-ds theme ($CANVAS_UPSTREAM@$CANVAS_COMMIT)"
            canvas_tgz() { cat "$RGDS_DEPS/canvas-ds-$CANVAS_COMMIT.tar.gz"; }
        else
            say "Downloading the canvas-ds theme ($CANVAS_UPSTREAM@$CANVAS_COMMIT, ~180 MB, once)"
            canvas_tgz() { curl -fsSL "https://codeload.github.com/$CANVAS_UPSTREAM/tar.gz/$CANVAS_COMMIT"; }
        fi
        if canvas_tgz | tar xz -C $WORK/canvas --exclude='*/previews' --exclude='*/customization examples' --exclude='*/scripts'; then
            [ -d $C ] && [ ! -e $C/.rocknixds-commit ] && backup_once $C      # someone's own copy: keep it
            # the old copy goes only once the new one is in place next to it
            if mv $WORK/canvas/canvas-ds-* $C.new 2>/dev/null; then
                rm -rf $C; mv $C.new $C && echo $CANVAS_COMMIT > $C/.rocknixds-commit
            else
                rm -rf $C.new; say "canvas-ds download had an unexpected layout: kept the installed copy"
            fi
        else
            say "canvas-ds couldn't be downloaded: skipped (the install goes on)"
        fi
        rm -rf $WORK/canvas
    fi
    # its game lists size the top screen's image to at most 672x288 px of the 1920 px canvas: a DS screenshot (both
    # screens side by side, 8:3) came out 672 px wide, 32 px more than the top screen, cut at its left edge and
    # spilling onto the bottom one. 614 px keeps it on the top screen; 4:3 screenshots and box art are limited by the
    # height and don't change. (Verified on the RG DS in all colour schemes, views and grid sizes.)
    [ -f $C/aspect-ratio-4-3.xml ] && sed -i 's|<maxSize>0.35 0.6</maxSize>|<maxSize>0.32 0.6</maxSize>|' $C/aspect-ratio-4-3.xml
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
    cp $SD/libsuperdrastic.so $WORK/dsflip/libdsflip.so     # the name ROCKNIXDS's scripts and older installs use
    cp "$SRC/dsflip/device/session.sh" "$SRC/dsflip/device/restore.sh" \
       "$SRC/dsflip/device/drastic-wrapper.sh" "$SRC/dsflip/device/install.sh" "$SRC/dsflip/device/es-features.sh" \
       "$SRC/dsflip/device/fast-switch" "$SRC/dsflip/device/playstats.py" "$SRC/dsflip/device/perf-session.py" \
       "$SRC/dsflip/device/es-share-logs.sh" "$SRC/dsflip/device/menu-power.sh" \
       "$SRC/dsflip/device/battery-led-status" "$SRC/dsflip/device/powerstate" \
       "$SRC/dsflip/device/media-auto.sh" "$SRC/dsflip/device/preload-guard.so" "$SRC/dsflip/device/drastic-launch" \
       $WORK/dsflip/
    sh $WORK/dsflip/install.sh

    # DS-pixel-aware shaders for DraStic (sharp and LCD-grid looks that work at 1x and 2x) + their ES entries
    mkdir -p $DRASTIC/shaders
    for f in $SD/shaders/*.frag; do
        b=$(basename "$f")
        if [ -e $DRASTIC/shaders/$b ]; then backup_once $DRASTIC/shaders/$b
        else grep -qx "$b" $BACKUP/.shaders-added 2>/dev/null || echo "$b" >> $BACKUP/.shaders-added; fi
        cp "$f" $DRASTIC/shaders/
    done
    # the ds-* entries in ES's DraStic shader option: es-features.sh, which also runs at every boot (its own
    # autostart hook), so a ROCKNIX update's es_features.cfg changes flow into a copy the installer created
    if [ -f $ESF ]; then backup_once $ESF
    elif [ -f /usr/config/emulationstation/es_features.cfg ]; then touch $BACKUP/.esf-created; fi
    sh $DRASTIC/dsflip/es-features.sh
    mkdir -p /storage/.config/autostart
    cp "$SRC/dsflip/device/autostart-rocknixds-es-features" /storage/.config/autostart/rocknixds-es-features
    chmod +x /storage/.config/autostart/rocknixds-es-features
    # the menus on schedutil instead of ROCKNIX's performance (menu-power.sh): whenever ES starts (after ROCKNIX's
    # autostart, which applies its own governor last) and after every game. (1.4-dev had an autostart hook: overridden.)
    rm -f /storage/.config/autostart/rocknixds-menu-power
    for ev in start game-end; do
        mkdir -p /storage/.config/emulationstation/scripts/$ev
        cp "$SRC/dsflip/device/es-menu-power.sh" /storage/.config/emulationstation/scripts/$ev/rocknixds-menu-power.sh
        chmod +x /storage/.config/emulationstation/scripts/$ev/rocknixds-menu-power.sh
    done
    $DRASTIC/dsflip/menu-power.sh
    # game art and RetroAchievements strips kept up to date on the device (media-auto.sh): the media tool, run in
    # the background each time the menu opens (ES start, after a game) and stopped when a game starts
    mkdir -p /storage/.config/rocknixds/media
    for f in rocknixds-media.py ra-fetch.py ra_panel.py box3d.py labelart.py nds-carts.json nds-meta.json.gz; do
        cp "$SRC/dii-ess-aye/scrape/$f" /storage/.config/rocknixds/media/
    done
    for ev in start game-end game-start; do
        mkdir -p /storage/.config/emulationstation/scripts/$ev
        cp "$SRC/dsflip/device/es-media.sh" /storage/.config/emulationstation/scripts/$ev/rocknixds-media.sh
        chmod +x /storage/.config/emulationstation/scripts/$ev/rocknixds-media.sh
    done
    # ROCKNIX's battery LED monitor started ~12 processes a second (6.5% of a core); the same monitor without them
    # (battery-led-status, which runs ROCKNIX's own if that ever changes) through a systemd drop-in
    if [ -f /usr/lib/systemd/system/batteryledstatus.service ]; then
        mkdir -p /storage/.config/system.d/batteryledstatus.service.d
        cp "$SRC/dsflip/device/batteryledstatus-rocknixds.conf" /storage/.config/system.d/batteryledstatus.service.d/rocknixds.conf
        systemctl daemon-reload; systemctl restart batteryledstatus.service 2>/dev/null
    fi
    # the same for ROCKNIX's powerstate service (~2% of a core: cat, awk and sleep every 2 s; runs ROCKNIX's own if
    # that ever changes)
    if [ -f /usr/lib/systemd/system/powerstate.service ]; then
        mkdir -p /storage/.config/system.d/powerstate.service.d
        cp "$SRC/dsflip/device/powerstate-rocknixds.conf" /storage/.config/system.d/powerstate.service.d/rocknixds.conf
        systemctl daemon-reload; systemctl restart powerstate.service 2>/dev/null
    fi
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

# ---- lockdown and updates ------------------------------------------------------------------------------
# The patched ES leaves out the settings that break the dual-screen setup or ROCKNIXDS's DraStic, offers only the
# themes in themes.allow, and its UPDATES & DOWNLOADS menu updates ROCKNIXDS (rocknixds-update) instead of ROCKNIX.
# DS games run on ROCKNIXDS's DraStic only (es-features.sh keeps es_systems.cfg's nds entry to it, at every boot).
say "Locking the settings that would break ROCKNIXDS; ROCKNIXDS updates"
RD=/storage/.config/rocknixds
mkdir -p $RD
cp "$SRC/dsflip/device/rocknixds-update" $RD/ && chmod +x $RD/rocknixds-update
themes_allow > $RD/themes.allow
[ -f $SYSCFG ] && backup_once $SYSCFG
[ -f $SYSCFG ] && sed -i '/^nds\(\[.*\]\)\{0,1\}\.\(emulator\|core\)=/d' $SYSCFG       # a per-game RetroArch/melonDS choice
# The update channel follows what was installed, unless the player already chose one in the menu (which stores
# only stable or beta; on a Plus, rocknixds-update maps beta to plus-beta). Plus beta 3 stored "plus", which the
# menu shows as Stable: it meant the beta.
if [ -f $SYSCFG ]; then
    case "$(sed -n 's/^rocknixds\.channel=//p' $SYSCFG | tail -n1)" in
    plus) set_cfg rocknixds.channel beta ;;
    "") case "$BRANCH" in beta|plus-beta) set_cfg rocknixds.channel beta ;; *) set_cfg rocknixds.channel stable ;; esac ;;
    esac
fi
[ -f $SYSCFG ] && { grep -q '^rocknixds.autocheck=' $SYSCFG || set_cfg rocknixds.autocheck 1; }
mkdir -p /storage/.config/system.d
cp "$SRC/dsflip/device/rocknixds-update-check.service" "$SRC/dsflip/device/rocknixds-update-check.timer" /storage/.config/system.d/
mkdir -p /storage/.config/system.d/timers.target.wants
ln -sf ../rocknixds-update-check.timer /storage/.config/system.d/timers.target.wants/rocknixds-update-check.timer
systemctl daemon-reload; systemctl start rocknixds-update-check.timer 2>/dev/null
# what's installed, for the update check: the release tag or the commit that was downloaded
if [ -n "$RGDS_REF" ]; then ID=$RGDS_REF
elif [ -n "$RGDS_SRC" ]; then ID=local
elif [ "$BRANCH" = main ]; then
    ID=$(curl -fsSL --max-time 15 https://api.github.com/repos/$REPO/releases/latest 2>/dev/null | sed -n 's/^ *"tag_name": *"\([^"]*\)".*/\1/p' | head -n1)
else
    ID=$(curl -fsSL --max-time 15 https://api.github.com/repos/$REPO/commits/$BRANCH 2>/dev/null | sed -n 's/^  "sha": *"\([0-9a-f]*\)".*/\1/p' | head -n1)
fi
echo "${ID:-unknown}" > $RD/installed-id; rm -f $RD/notified-id
# Optional: a token here commits the performance log directly. Without it the handheld still queues the log
# and the repository imports it. The token is never fetched from the repository.
if [ -n "$ROCKNIXDS_UPLOAD_TOKEN" ]; then
    oldmask=$(umask)
    umask 077
    printf '%s\n' "$ROCKNIXDS_UPLOAD_TOKEN" > $RD/upload.token
    chmod 600 $RD/upload.token
    umask "$oldmask"
    unset ROCKNIXDS_UPLOAD_TOKEN
fi

RGDS_VERSION=$(cat "$SRC/VERSION" 2>/dev/null || echo unknown)
echo "$RGDS_VERSION$([ "$BRANCH" = main ] || echo " ($BRANCH)")" > $VERSION_FILE
rm -rf $WORK
es_start
say "Installed ROCKNIXDS $RGDS_VERSION. Start a DS game from EmulationStation as usual."
say "Undo: curl -fsSL https://raw.githubusercontent.com/$REPO/main/install.sh | sh -s -- --uninstall"
[ -n "$NEED_REBOOT" ] && say "Reboot for the 60 Hz panel timing to take effect." || true
