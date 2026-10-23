#!/bin/sh
# ROCKNIXDS: ES runs this (scripts/theme-changed/) with the new and the old theme when a theme is picked.
# The launcher sizes ES's canvas for the theme when ES starts: 1920x480 across both panels for the dual-screen themes
# (dii-ess-aye, canvas-ds, rocknixds-pixel-dark, rocknixds-pixel-light), stock ROCKNIX's layout (the top panel, bottom panel off)
# for any other theme, which is drawn for one 640x480 screen. ES keeps its canvas when the theme changes, so restart
# ES once the new choice is saved, when the layout changes. Dark and light share that canvas, so switching between
# them does not restart ES. The wait runs in its own unit: restarting ES stops everything in ES's own unit, this
# script included. Keep DUAL_THEMES and dual_theme in sync with start_es_rgds.sh.
DUAL_THEMES="dii-ess-aye canvas-ds rocknixds-pixel-dark rocknixds-pixel-light"
# A theme not in the list gets the canvas too when it has a layout for it: an aspect-ratio include for the canvas'
# 4:1 ratio in its theme.xml (if="{screen.ratio} == '4/1'"), such as TechDweeb's RG DS layout.
dual_theme() {
    case " $DUAL_THEMES " in *" $1 "*) return 0 ;; esac
    [ -n "$1" ] || return 1
    for d in /storage/.config/emulationstation/themes /storage/roms/themes /usr/share/emulationstation/themes; do
        grep -qsF "{screen.ratio} == '4/1'" "$d/$1/theme.xml" && return 0
    done
    return 1
}
dual() { if dual_theme "$1"; then echo 1; else echo 0; fi; }
NEW=$1 OLD=$2
[ "$NEW" = "$OLD" ] && exit 0
[ "$(dual "$NEW")" = "$(dual "$OLD")" ] && exit 0
# a second layout change while the first one's restart still waits: the latest choice wins (the unit name is taken
# until the first one ends, so the new one would not start)
systemctl stop rocknixds-theme-restart.service 2>/dev/null
systemd-run --collect --unit=rocknixds-theme-restart sh -c '
    for i in $(seq 1 120); do
        grep -q "name=\"ThemeSet\" value=\"$0\"" /storage/.config/emulationstation/es_settings.cfg && break
        sleep 0.5
    done
    sleep 1; systemctl restart essway.service' "$NEW" >/dev/null 2>&1
exit 0
