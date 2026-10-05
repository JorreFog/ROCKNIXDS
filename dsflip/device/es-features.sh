#!/bin/sh
# es-features.sh: puts ROCKNIXDS's ds-* shaders into EmulationStation's DraStic "shader" option, and adds its
# "resume on quit" option (nds.resume_on_quit, read by session.sh; unset = on) and its "power profile" option
# (nds.power_profile: balanced, performance or battery; unset = balanced; session.sh) and its "3D renderer" option
# (nds.renderer: superdrastic = Gengis Engine, SuperDrastic's own hi-res rasterizer, DSFLIP_RAST=1 in session.sh;
# drastic = DraStic's; unset, the menu's Auto, is Gengis Engine since 1.5.13) with a "3D texture filter"
# (nds.texture_filter: nearest, bilinear, sharp; DSFLIP_RAST_TEXFILTER) that only applies to Gengis Engine, and its
# "wfc dns" (nds.wfc_dns: off, kaeru, wiilink, altwfc; DSFLIP_WFC in session.sh: Wi-Fi online play through a Nintendo
# WFC replacement server, 1.5.13). Their value attributes are the system.cfg keys: without one ES derives the key
# from the name (nds.3D_renderer), which session.sh doesn't read. It also keeps the DS system on
# ROCKNIXDS's DraStic: es_systems.cfg's nds entry offers only drastic/drastic-sa (ROCKNIX also lists RetroArch cores
# and standalone melonDS, which don't use libdsflip). --unlock-nds puts ROCKNIX's list back (uninstall).
# Run by the installer and at every boot (autostart hook rocknixds-es-features), before ES starts.
#
# ES reads /storage/.config/emulationstation/es_features.cfg instead of ROCKNIX's read-only
# /usr/config/emulationstation/es_features.cfg whenever the user copy exists, and there's no other way to add
# choices to an existing option: es_features_*.cfg overlays append features, so a second "shader" row would
# appear (CustomFeatures.cpp, ROCKNIX/emulationstation-next). So we keep a user copy, and:
#   - a copy the installer created (flag .esf-created) is rebuilt from ROCKNIX's file whenever that file changes,
#     so a ROCKNIX update's new cores and options show up. The previous copy is kept as es_features.cfg.rocknixds-old.
#   - a copy that existed before the install is the user's own: only our entries are (re)inserted.
# The file is only rewritten when its content would change.
SYS=${ESF_SYSTEM:-/usr/config/emulationstation/es_features.cfg}
ESF=${ESF_USER:-/storage/.config/emulationstation/es_features.cfg}
STATE=${ESF_STATE:-/storage/rgds-rocknix-backup}     # the installer's backup dir: .esf-created, .esf-system-md5

# our choices go at the end of the drastic-sa core's <feature name="shader">, indented like its other choices;
# any earlier copy of them is dropped first, so this is idempotent. The resume option follows the shader option.
add_ours() {
    grep -vE 'value="ds-(crisp|grid|grid-2x|crisp-color|grid-color|fsr|integer)"' "$1" | awk '
        /<feature name="resume on quit"/ || /<feature name="power profile"/ || /<feature name="3D renderer"/ || /<feature name="3D texture filter"/ || /<feature name="3D resolution"/ || /<feature name="wfc dns"/ { skip = 1 }
        skip { if (/<\/feature>/) skip = 0; next }
        /<core name="drastic-sa"/ { core = 1 }
        core && /<\/core>/ { core = 0 }
        core && /<feature name="shader"/ { shader = 1 }
        shader && /<choice / { ind = $0; sub(/<choice.*/, "", ind) }
        shader && /<\/feature>/ {
            print ind "<choice name=\"ds-crisp (sharp, 1x and 2x)\" value=\"ds-crisp\" />"
            print ind "<choice name=\"ds-crisp + NDS color\" value=\"ds-crisp-color\" />"
            print ind "<choice name=\"ds-grid (sharp + DS pixel grid)\" value=\"ds-grid\" />"
            print ind "<choice name=\"ds-grid + NDS color\" value=\"ds-grid-color\" />"
            print ind "<choice name=\"ds-grid-2x (pixel-perfect + even DS grid)\" value=\"ds-grid-2x\" />"
            print ind "<choice name=\"ds-fsr (FSR 1.0, smooth edges)\" value=\"ds-fsr\" />"
            print ind "<choice name=\"ds-integer (pixel-perfect)\" value=\"ds-integer\" />"
            shader = 0; added = 1; resume = 1
            print; fi = ind; sub(/  $/, "", fi)
            print fi "<feature name=\"resume on quit\">"
            print ind "<choice name=\"on\" value=\"1\" />"
            print ind "<choice name=\"off\" value=\"0\" />"
            print fi "</feature>"
            print fi "<feature name=\"power profile\">"
            print ind "<choice name=\"balanced\" value=\"balanced\" />"
            print ind "<choice name=\"performance\" value=\"performance\" />"
            print ind "<choice name=\"battery saver\" value=\"battery\" />"
            print fi "</feature>"
            print fi "<feature name=\"3D renderer\" value=\"renderer\">"
            print ind "<choice name=\"DraStic\" value=\"drastic\" />"
            print ind "<choice name=\"Gengis Engine\" value=\"superdrastic\" />"
            print fi "</feature>"
            print fi "<feature name=\"3D texture filter\" value=\"texture_filter\">"
            print ind "<choice name=\"nearest (DS)\" value=\"nearest\" />"
            print ind "<choice name=\"bilinear\" value=\"bilinear\" />"
            print ind "<choice name=\"sharp bilinear\" value=\"sharp\" />"
            print fi "</feature>"
            print fi "<feature name=\"wfc dns\" value=\"wfc_dns\">"
            print ind "<choice name=\"off\" value=\"off\" />"
            print ind "<choice name=\"Kaeru WFC (Wiimmfi)\" value=\"kaeru\" />"
            print ind "<choice name=\"WiiLink DNS (Wiimmfi)\" value=\"wiilink\" />"
            print ind "<choice name=\"AltWFC (unmaintained)\" value=\"altwfc\" />"
            print fi "</feature>"
            next
        }
        { print }
        END { if (!added) exit 3 }'
}

# the DS system's emulators: DraStic only (ROCKNIX's own copy of the list when unlocking)
ESS=${ESS_USER:-/storage/.config/emulationstation/es_systems.cfg}
ESS_SYS=${ESS_SYSTEM:-/usr/config/emulationstation/es_systems.cfg}
nds_block() {  # the nds system's <emulators> block from $1
    awk '/<system>/ { s = 0 } /<name>nds<\/name>/ { s = 1 } s && /<emulators>/ { e = 1 } e { print } e && /<\/emulators>/ { exit }' "$1"
}
if [ -f "$ESS" ]; then
    if [ "$1" = --unlock-nds ]; then nds_block "$ESS_SYS" > "$ESS.block"
    else
        ind=$(nds_block "$ESS" | head -n1 | sed 's/<emulators>.*//')
        { echo "$ind<emulators>"; echo "$ind	<emulator name=\"drastic\">"; echo "$ind		<cores>"
          echo "$ind			<core default=\"true\">drastic-sa</core>"; echo "$ind		</cores>"; echo "$ind	</emulator>"
          echo "$ind</emulators>"; } | sed 's/\\t/\t/g' > "$ESS.block"
    fi
    if [ -s "$ESS.block" ]; then
        awk -v blk="$ESS.block" '
            /<system>/ { s = 0 } /<name>nds<\/name>/ { s = 1 }
            s && /<emulators>/ { while ((getline l < blk) > 0) print l; skip = 1; next }
            skip { if (/<\/emulators>/) skip = 0; next }
            { print }' "$ESS" > "$ESS.new"
        if cmp -s "$ESS.new" "$ESS"; then rm -f "$ESS.new"; else mv "$ESS.new" "$ESS"; echo "es-features: DS emulators in $ESS ${1:+un}locked"; fi
    fi
    rm -f "$ESS.block"
fi
[ "$1" = --unlock-nds ] && exit 0
[ -f "$SYS" ] || exit 0

SRC=$ESF
if [ -e "$STATE/.esf-created" ]; then
    sum=$(md5sum < "$SYS" | cut -d' ' -f1)
    if [ ! -f "$ESF" ] || [ "$sum" != "$(cat "$STATE/.esf-system-md5" 2>/dev/null)" ]; then
        SRC=$SYS                        # ROCKNIX's file changed (or first run): rebuild from it
        [ -f "$ESF" ] && cp -p "$ESF" "$ESF.rocknixds-old"
    fi
fi
[ -f "$SRC" ] || exit 0
if ! add_ours "$SRC" > "$ESF.new"; then
    echo "es-features: no drastic-sa shader option in $SRC, left unchanged"; rm -f "$ESF.new"; exit 0
fi
if [ -f "$ESF" ] && cmp -s "$ESF.new" "$ESF"; then rm -f "$ESF.new"
else mv "$ESF.new" "$ESF"; echo "es-features: $ESF updated$([ "$SRC" = "$SYS" ] && echo " from ROCKNIX's copy")"; fi
[ "$SRC" = "$SYS" ] && md5sum < "$SYS" | cut -d' ' -f1 > "$STATE/.esf-system-md5"
exit 0
