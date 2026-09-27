#!/bin/sh
# es-features.sh: puts ROCKNIXDS's ds-* shaders into EmulationStation's DraStic "shader" option.
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
[ -f "$SYS" ] || exit 0

# our choices go at the end of the drastic-sa core's <feature name="shader">, indented like its other choices;
# any earlier copy of them is dropped first, so this is idempotent
add_ours() {
    grep -vE 'value="ds-(crisp|grid|grid-2x|crisp-color|grid-color|fsr|integer)"' "$1" | awk '
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
            print ind "<choice name=\"ds-integer (pixel-perfect 2x + bezel)\" value=\"ds-integer\" />"
            shader = 0; added = 1
        }
        { print }
        END { if (!added) exit 3 }'
}

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
