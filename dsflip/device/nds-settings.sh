# nds-settings.sh (sourced by drastic-wrapper.sh and session.sh): the DS settings a game starts with.
# A setting is the game's own (ES: hold A on the game > Advanced game options), else the DS system's, as ROCKNIX's
# get_setting reads them from system.cfg: nds["<rom file name>"].<key>=, then nds.<key>=.
# "Apply recommended settings" (#45; nds.recommended, the game's or the system's, 1 = on) replaces the picture and
# speed settings with ROCKNIXDS's recommended ones, whatever the game or the system says: the defaults below, which
# are what an unset menu already gives (README, "The recommended settings are the defaults"), plus the game's line in
# recommended.cfg, if it has one. The game's other settings (resume on quit, microphone, performance logs, follow 3D
# renderer unless its line sets it) stay its own.
# Host tests set NDS_CFG and NDS_RECOMMENDED.
NDS_CFG=${NDS_CFG:-/storage/.config/system/configs/system.cfg}
NDS_RECOMMENDED=${NDS_RECOMMENDED:-/storage/.config/drastic/dsflip/recommended.cfg}

nds_default() {     # ROCKNIXDS's recommended value of a picture/speed setting; empty: not one of them
    case $1 in
        hires_3d|threaded_3d) echo 1 ;;    # 2x internal resolution; the 3D off the main thread (install.sh sets both)
        renderer) echo superdrastic ;;     # Gengis Engine
        resolution3d) echo 2x ;;
        texture_filter) echo nearest ;;    # the DS's own
        power_profile) echo balanced ;;
        shader) echo none ;;               # no GPU pass (ROCKNIX's default bilinear)
    esac
}
nds_own() {         # nds_own <rom file name> <key>: the game's value, else the system's
    _nv=$(grep -F "nds[\"$1\"].$2=" "$NDS_CFG" 2>/dev/null | tail -n1 | cut -d= -f2-)
    [ -n "$_nv" ] || _nv=$(grep "^nds\.$2=" "$NDS_CFG" 2>/dev/null | tail -n1 | cut -d= -f2-)
    echo "$_nv"
}
nds_game_own() { grep -F "nds[\"$1\"].$2=" "$NDS_CFG" 2>/dev/null | tail -n1 | cut -d= -f2-; }  # the game's only
nds_recommended_on() { [ "$(nds_own "$1" recommended)" = 1 ]; }
nds_table() {       # nds_table <rom file name> <key>: the value recommended.cfg gives this game (the last line that matches)
    _ng=$(printf '%s' "$1" | tr 'A-Z' 'a-z') _nt=
    [ -f "$NDS_RECOMMENDED" ] || return 0
    while IFS='|' read -r _np _nk; do
        case "$_np" in ""|\#*) continue ;; esac
        _np=$(printf '%s' "$_np" | tr 'A-Z' 'a-z')
        case "$_ng" in
            $_np) for _nkv in $_nk; do [ "${_nkv%%=*}" = "$2" ] && _nt=${_nkv#*=}; done ;;
        esac
    done < "$NDS_RECOMMENDED"
    echo "$_nt"
}
nds_get() {         # nds_get <rom file name> <key>: the value this game starts with
    if nds_recommended_on "$1"; then
        _nv=$(nds_table "$1" "$2"); [ -n "$_nv" ] || _nv=$(nds_default "$2")
        [ -n "$_nv" ] && { echo "$_nv"; return; }
    fi
    nds_own "$1" "$2"
}
nds_chosen() {      # nds_chosen <rom file name> <key>: set for this game in particular (its own value, or its line)
    if nds_recommended_on "$1"; then
        [ -n "$(nds_table "$1" "$2")" ] && return 0
        [ -n "$(nds_default "$2")" ] && return 1
    fi
    [ -n "$(nds_game_own "$1" "$2")" ]
}
