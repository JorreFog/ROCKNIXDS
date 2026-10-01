#!/bin/sh
# ROCKNIXDS: the DS games' art and RetroAchievements strips, kept up to date on the device itself
# (rocknixds-media.py --local, installed in /storage/.config/rocknixds/media):
#   media-auto.sh               the menu has opened (ES started, or a game ended): once ES is idle, scrape the
#                               3D box, screenshot and cartridge of every game missing one, and redraw the strip of
#                               every game played since its strip was drawn. In the background, at idle priority.
#   media-auto.sh --ra-rom ROM  a game has just ended and ES isn't back yet (session.sh): redraw that game's strip
#                               in place, so the menu opens on the progress just made
#   media-auto.sh --stop        a game is starting: the background job stops
# Off with rocknixds.automedia=0 in system.cfg. Logs: /storage/.config/rocknixds/media*.log (the last run).
M=/storage/.config/rocknixds/media
RD=/storage/.config/rocknixds
case "$1" in
    --stop) systemctl stop --no-block rocknixds-media.service rocknixds-media-ra.service 2>/dev/null; exit 0 ;;
esac
grep -q '^rocknixds\.automedia=0' /storage/.config/system/configs/system.cfg 2>/dev/null && exit 0
[ -f $M/rocknixds-media.py ] || exit 0
if [ "$1" = --ra-rom ]; then
    [ -n "$2" ] || exit 0
    systemd-run --collect --quiet --unit=rocknixds-media-ra -p Nice=10 -p StandardOutput=truncate:$RD/media-ra.log \
        -p StandardError=inherit python3 $M/rocknixds-media.py --ra-rom "$2" >/dev/null 2>&1
    exit 0
fi
# one job at a time (a second start while one runs is refused by systemd); it waits for ES to be idle and a few
# seconds more, so the menu's first moments aren't shared with it
systemd-run --collect --quiet --unit=rocknixds-media -p Nice=19 -p CPUSchedulingPolicy=idle -p IOSchedulingClass=idle \
    -p StandardOutput=truncate:$RD/media.log -p StandardError=inherit \
    sh -c "for i in \$(seq 1 120); do curl -s -m 1 localhost:1234/isIdle 2>/dev/null | grep -q true && break; sleep 0.5; done
           sleep 5; exec python3 $M/rocknixds-media.py --local --auto" >/dev/null 2>&1
exit 0
