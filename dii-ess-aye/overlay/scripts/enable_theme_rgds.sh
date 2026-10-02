#!/bin/bash

THEME_PATH=""
if [ -d "/roms/themes/dii-ess-aye" ]; then
    THEME_PATH="/roms/themes/dii-ess-aye"
elif [ -d "/roms/themes/dii-ess-aye-main" ]; then
    THEME_PATH="/roms/themes/dii-ess-aye-main"
elif [ -d "/storage/.config/emulationstation/themes/dii-ess-aye" ]; then
    THEME_PATH="/storage/.config/emulationstation/themes/dii-ess-aye"
elif [ -d "/storage/.config/emulationstation/themes/dii-ess-aye-main" ]; then
    THEME_PATH="/storage/.config/emulationstation/themes/dii-ess-aye-main"
else
    echo "ERROR! Couldn't find a dii-ess-aye theme folder on your device!"
    exit 1
fi

chmod +x "${THEME_PATH}/scripts/start_es_rgds.sh"
mount --bind "${THEME_PATH}/scripts/start_es_rgds.sh" "/usr/bin/start_es.sh"

cat <<EOF >/storage/.config/sway/config
seat * hide_cursor 1000
default_border none
exec_always mako
output DSI-2 transform 0
output DSI-2 bg #000000 solid_color
output DSI-1 bg #000000 solid_color
output DSI-2 allow_tearing yes
output DSI-2 max_render_time off
for_window [title=".*(Secondary|\[w2\]|Sub|Bottom|Screen 2|GamePad).*"] move window to output DSI-1
for_window [title="RetroArch.*"] exec /usr/bin/vertical-check
for_window [app_id="lowerdeck"] floating enable, fullscreen enable, move window to output DSI-1
no_focus [app_id="lowerdeck"]
exec_always swaymsg '[app_id="emulationstation"]' seat seat0 attach "0:0:wlr_virtual_keyboard_v1"
exec_always swaymsg '[app_id="emulationstation"]' seat seat1 attach "0:0:wlr_virtual_keyboard_v1"
for_window [title=".*(Secondary|\[w2\]|Sub|Bottom|Screen 2|GamePad).*"] output DSI-1 power on
for_window [app_id="drastic"] input "1046:911:Goodix_Capacitive_TouchScreen" map_to_output DSI-2
for_window [app_id="drastic"] floating enable, border none, move absolute position 0 0, focus
for_window [app_id="emulationstation"] move scratchpad
for_window [app_id="swayimg"] move scratchpad
exec_always swaymsg '[app_id="emulationstation"]' floating enable, fullscreen disable, move absolute position 0 0
exec_always swaymsg '[app_id="emulationstation"]' focus
exec_always swaymsg '[app_id="emulationstation"]' seat seat1 attach "1046:911:Goodix_Capacitive_TouchScreen"
exec_always swaymsg '[app_id="emulationstation"]' seat seat1 fallback yes
# Touch in ES: ROCKNIX's lines above put the touchscreens on seat1 only, and ES then gets no touch
# at all. With them on seat0 as well, ES gets every touch (same as sway-config.theme / autostart).
seat seat0 attach "1046:911:Goodix_Capacitive_TouchScreen"
EOF

swaymsg reload

ES_SETTINGS="/storage/.config/emulationstation/es_settings.cfg"
if grep -q '<string name="FullScreenMenu"' "$ES_SETTINGS" 2>/dev/null; then
    sed -i 's|<string name="FullScreenMenu" value="[^"]*" />|<string name="FullScreenMenu" value="false" />|' "$ES_SETTINGS"
else
    sed -i 's|</config>|\t<string name="FullScreenMenu" value="false" />\n</config>|' "$ES_SETTINGS"
fi

if grep -q '<string name="GameTransitionStyle"' "$ES_SETTINGS" 2>/dev/null; then
    sed -i 's|<string name="GameTransitionStyle" value="[^"]*" />|<string name="GameTransitionStyle" value="fade" />|' "$ES_SETTINGS"
else
    sed -i 's|</config>|\t<string name="GameTransitionStyle" value="fade" />\n</config>|' "$ES_SETTINGS"
fi

# Keep a dual-screen theme the user already picked. A fresh install, or any other
# theme, starts on dii-ess-aye; dark and light are in the theme menu.
CURRENT_THEME=$(sed -n 's/.*<string name="ThemeSet" value="\([^"]*\)".*/\1/p' "$ES_SETTINGS" 2>/dev/null)
case "$CURRENT_THEME" in
    dii-ess-aye|canvas-ds|rocknixds-dark|rocknixds-light|rocknixds-pixel) ;;
    *)
        if grep -q '<string name="ThemeSet"' "$ES_SETTINGS" 2>/dev/null; then
            sed -i 's|<string name="ThemeSet" value="[^"]*" />|<string name="ThemeSet" value="dii-ess-aye" />|' "$ES_SETTINGS"
        else
            sed -i 's|</config>|\t<string name="ThemeSet" value="dii-ess-aye" />\n</config>|' "$ES_SETTINGS"
        fi
        ;;
esac

if [ ! -f "/tmp/has-restarted-for-theme" ]; then
    touch /tmp/has-restarted-for-theme
    systemctl restart essway
fi
