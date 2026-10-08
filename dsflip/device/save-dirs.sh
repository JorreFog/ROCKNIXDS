#!/bin/sh
# save-dirs.sh: where DraStic keeps the DS games' save files and savestates (#42, #56). DraStic writes them to backup/
# and savestates/ in its folder, and ROCKNIX's start_drastic.sh points those at roms/nds (the saves next to the games)
# and roms/savestates/nds before every launch, removing whatever was there. The launcher (drastic-wrapper.sh) runs this
# right after, before DraStic starts, and points them where the DS system's options say:
#   nds.saves_dir   ("save files")  roms: next to the games (roms/nds, ROCKNIX's) | saves: roms/saves/nds
#   nds.states_dir  ("save states") savestates: roms/savestates/nds (ROCKNIX's) | saves: roms/saves/nds/states
# roms/saves/nds keeps the DS's saves (and with both on saves, its savestates) out of the games' folder, in one folder
# to sync (Syncthing and the like). The DS system's values only: a game's own value is ignored (one place per system).
# Neither set (the default): nothing is touched, so a link changed by hand stays as ROCKNIX's launch leaves it.
# Once one has been set, the folder last used is recorded (rocknixds/saves-dir, states-dir): setting it back to the
# default moves the files back from there.
# Moving never loses a file: only DraStic's own (*.dsv saves, *.dss savestates) move, and a name that exists in both
# places keeps the newer file under its name; the older one stays next to it as <name>.older-<date> (the same content
# twice: the duplicate goes). A link that is a real folder (not ROCKNIX's link) is emptied into the new place first,
# and kept if anything else is in it.
# --stock: back to ROCKNIX's places whatever the options say, and forget the records (uninstall).
# Host tests set SAVEDIRS_DRASTIC, SAVEDIRS_ROMS, SAVEDIRS_CFG and SAVEDIRS_STATE.
D=${SAVEDIRS_DRASTIC:-/storage/.config/drastic}
ROMS=${SAVEDIRS_ROMS:-/storage/roms}
CFG=${SAVEDIRS_CFG:-/storage/.config/system/configs/system.cfg}
REC=${SAVEDIRS_STATE:-/storage/.config/rocknixds}
LOG=$D/dsflip/save-dirs.log
STOCK=0; [ "$1" = --stock ] && STOCK=1
setting() { grep "^nds\.$1=" "$CFG" 2>/dev/null | tail -n1 | cut -d= -f2-; }
canon() { (cd "$1" 2>/dev/null && pwd -P); }
say() { mkdir -p "${LOG%/*}" 2>/dev/null; echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

# move_files <from> <to> <glob>: DraStic's files from one folder into another, never losing one
move_files() {
    for f in "$1"/$3; do
        [ -f "$f" ] && [ ! -L "$f" ] || continue
        b=${f##*/} t=$2/${f##*/}
        if [ ! -e "$t" ]; then
            mv "$f" "$t" && say "moved $f -> $2/" || say "couldn't move $f"
        elif cmp -s "$f" "$t"; then
            rm -f "$f" && say "$b: the same in $2/, the copy in $1/ removed"
        elif [ "$f" -nt "$t" ]; then
            old=$t.older-$(date +%Y%m%d-%H%M%S)
            mv "$t" "$old" && mv "$f" "$t" && say "$b: $1/'s is newer, moved in; $2/'s kept as ${old##*/}"
        else
            old=$f.older-$(date +%Y%m%d-%H%M%S)
            mv "$f" "$old" && say "$b: $2/'s is newer and stays; $1/'s kept as ${old##*/}"
        fi
    done
}

# place <link> <option> <record> <ROCKNIX's folder> <the saves folder> <glob> <the option's value for the latter>
place() {
    L=$1 S=$(setting "$2") R=$(cat "$REC/$3" 2>/dev/null) DEF=$4 ALT=$5 PAT=$6
    [ $STOCK = 1 ] && S=stock
    if [ "$S" = "$7" ]; then T=$ALT
    elif [ -n "$S" ] || [ -n "$R" ]; then T=$DEF          # ROCKNIX's place, chosen or back from a recorded one
    else return 0; fi                                    # never chosen: leave it as it is
    mkdir -p "$T" || { say "couldn't create $T: $L left as it is"; return 0; }
    TC=$(canon "$T")
    CUR=; [ -L "$L" ] && CUR=$(canon "$L")
    for src in "$R" "$CUR"; do
        [ -n "$src" ] && [ -d "$src" ] && [ "$(canon "$src")" != "$TC" ] && move_files "$(canon "$src")" "$TC" "$PAT"
    done
    if [ -d "$L" ] && [ ! -L "$L" ]; then                # a real folder where the link goes: empty it into the new place
        move_files "$L" "$TC" "$PAT"
        rmdir "$L" 2>/dev/null || { say "$L is a folder with other files in it: left as it is"; return 0; }
    fi
    if [ "$CUR" != "$TC" ]; then ln -sfn "$T" "$L" && say "$L -> $T"; fi
    if [ $STOCK = 1 ] || { [ -z "$S" ] && [ "$T" = "$DEF" ]; }; then rm -f "$REC/$3"
    else mkdir -p "$REC" && [ "$R" != "$T" ] && echo "$T" > "$REC/$3"; fi
    return 0
}

place "$D/backup" saves_dir saves-dir "$ROMS/nds" "$ROMS/saves/nds" '*.dsv' saves
place "$D/savestates" states_dir states-dir "$ROMS/savestates/nds" "$ROMS/saves/nds/states" '*.dss' saves
[ -f "$LOG" ] && [ "$(wc -l < "$LOG")" -gt 400 ] && { tail -n 200 "$LOG" > "$LOG.new" && mv -f "$LOG.new" "$LOG"; }
exit 0
