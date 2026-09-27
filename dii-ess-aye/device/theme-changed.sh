#!/bin/sh
# ROCKNIXDS: ES runs this (scripts/theme-changed/) with the new and the old theme when a theme is picked.
# The launcher sizes ES's canvas for the theme when ES starts: 1920x480 across both panels for dii-ess-aye, stock
# ROCKNIX's layout (the top panel, bottom panel off) for any other theme, which is drawn for one 640x480 screen.
# ES keeps its canvas when the theme changes, so restart ES once the new choice is saved. The wait runs in its own
# unit: restarting ES stops everything in ES's own unit, this script included.
NEW=$1 OLD=$2
[ "$NEW" = "$OLD" ] && exit 0
[ "$NEW" = dii-ess-aye ] || [ "$OLD" = dii-ess-aye ] || exit 0
systemd-run --collect --unit=rocknixds-theme-restart sh -c '
    for i in $(seq 1 120); do
        grep -q "name=\"ThemeSet\" value=\"$0\"" /storage/.config/emulationstation/es_settings.cfg && break
        sleep 0.5
    done
    sleep 1; systemctl restart essway.service' "$NEW" >/dev/null 2>&1
exit 0
