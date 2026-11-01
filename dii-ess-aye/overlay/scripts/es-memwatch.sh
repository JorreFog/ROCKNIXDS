#!/bin/sh
# es-memwatch.sh: EmulationStation's memory while it runs (start_es_rgds.sh starts it beside ES; it ends with ES's unit).
#
# On 2026-10-05 an RG DS Plus (975 MB, no swap) stopped answering for minutes until the kernel killed ES at 713 MB (it
# idles at ~180 MB). It wasn't reproduced afterwards and nothing recorded how it grew. Every 30 s this reads ES's
# memory and writes a line to /storage/.config/emulationstation/es-mem.log when it moved by 16 MB or more (and once an
# hour): heap (RssAnon) apart from mapped files and GPU buffers (RssFile, RssShmem), so the next report says which
# grew. The file is kept on the card (a hard reset loses /var/log) and kept small (two files of 256 KB).
#
# And a safety valve: ES above half the handheld's memory (MemTotal; 487 MB on the RG DS Plus) while no game runs is
# restarted, with a line in the log, before the system starts thrashing. ROCKNIXDS_ES_MEMLIMIT_MB overrides the limit
# (0: never restart).
LOG=${ROCKNIXDS_ES_MEMLOG:-/storage/.config/emulationstation/es-mem.log}

# kB. ROCKNIXDS_ES_MEMLIMIT_MB overrides half of MemTotal ($1, kB). 0 never restarts.
memwatch_limit_kb() {
    echo $(( ${ROCKNIXDS_ES_MEMLIMIT_MB:-$(( $1 / 2048 ))} * 1024 ))
}

# How far RSS moved, either way (a drop is logged too).
memwatch_delta_kb() {
    d=$(( $1 - $2 ))
    [ "$d" -lt 0 ] && d=$(( -d ))
    echo "$d"
}

# A sample is written when RSS moved by 16 MB or more, or an hour has passed.
memwatch_should_log() {
    [ "$1" -ge 16384 ] || [ $(( $2 - $3 )) -ge 3600 ]
}

# Restart only above the limit, with no DS game unit and ES not reporting a running game.
# $3 is "active" while dsflip-game is up. $4 is ES's /runningGame code: 200 while a game runs
# (RetroArch and the others run inside ES's unit; a restart would kill them), 201 when none
# runs, and empty or 000 when ES does not answer.
memwatch_should_restart() {
    [ "$2" -gt 0 ] && [ "$1" -gt "$2" ] && [ "$3" != active ] && [ "$4" != 200 ]
}

say() {
    [ -f "$LOG" ] && [ "$(stat -c %s "$LOG" 2>/dev/null || echo 0)" -gt 262144 ] && mv -f "$LOG" "$LOG.1"
    echo "$(date '+%F %T') $*" >> "$LOG"
}

# Host tests source this and call the decisions above. The watch loop needs the handheld.
if [ "${ROCKNIXDS_MEMWATCH_FUNCS:-}" = 1 ]; then
    return 0 2>/dev/null || exit 0
fi

TOTAL=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
LIMIT=$(memwatch_limit_kb "$TOTAL")
last=0 lastt=0
sleep 30
while :; do
    P=$(pidof emulationstation | cut -d' ' -f1)
    if [ -n "$P" ] && [ -r /proc/$P/status ]; then
        set -- $(awk '/^VmRSS:/ { r = $2 } /^RssAnon:/ { a = $2 } /^RssFile:/ { f = $2 } /^RssShmem:/ { s = $2 }
                      END { print r + 0, a + 0, f + 0, s + 0 }' /proc/$P/status)
        rss=$1 anon=$2 file=$3 shmem=$4
        now=$(cut -d. -f1 /proc/uptime)
        d=$(memwatch_delta_kb "$rss" "$last")
        if memwatch_should_log "$d" "$now" "$lastt"; then
            avail=$(awk '/^MemAvailable:/ { print $2 }' /proc/meminfo)
            say "pid $P rss $((rss / 1024)) MB (heap $((anon / 1024)), files $((file / 1024)), shared/GPU $((shmem / 1024))), system available $((avail / 1024)) MB, uptime ${now} s"
            last=$rss lastt=$now
        fi
        # Ask ES and systemd only once the process is already over the limit.
        if [ "$LIMIT" -gt 0 ] && [ "$rss" -gt "$LIMIT" ]; then
            if systemctl is-active -q dsflip-game; then dsflip=active; else dsflip=; fi
            code=$(curl -s -m 2 -o /dev/null -w '%{http_code}' localhost:1234/runningGame 2>/dev/null)
            if memwatch_should_restart "$rss" "$LIMIT" "$dsflip" "$code"; then
                say "ES at $((rss / 1024)) MB, over $((LIMIT / 1024)) MB with no game running: restarting it"
                grep -E '^(Vm|Rss|Threads)' /proc/$P/status | tr '\n' ' ' | sed 's/^/  /' >> "$LOG"; echo >> "$LOG"
                sync
                # from its own unit: this script is in ES's unit, which the restart stops
                systemd-run --collect --quiet systemctl restart essway.service >/dev/null 2>&1
                exit 0
            fi
        fi
    fi
    sleep 30
done
