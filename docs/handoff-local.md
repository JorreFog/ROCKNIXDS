# Hand-off: 1.6 on the handheld

What was prepared without a handheld, and exactly what a person (or an AI) with an RG DS or RG DS Plus on the desk
runs to finish each item. ssh in as `root` (password `rocknix`); the device paths below are the installed ones.
Each item ends with its acceptance test. Update this file as items close.

1.6 is one release for both handhelds, on two branches kept in step (the night of 2026-10-05/06, from the 1.6-prep
hand-off in `docs/1.6-prep/HANDOFF.md`):

| Handheld | Branch | Install it |
|---|---|---|
| RG DS Plus | `claude/1-6-prep-work-kwq9lc-plus` (from `1.6-prep`, 357ca9d) | `curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh \| RGDS_BRANCH=claude/1-6-prep-work-kwq9lc-plus sh` |
| RG DS | `claude/1-6-prep-work-kwq9lc` (from the RG DS 1.5.13 branch `claude/tender-volta-a9nkpk`) | `curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh \| RGDS_BRANCH=claude/1-6-prep-work-kwq9lc sh` |

Both ship SuperDrastic `0.5.0-beta.1-rocknixds.6` (SuperDrastic branch `claude/1-6-prep-work-kwq9lc`, without
`1.6-prep`'s parked Wi-Fi commit) and the same EmulationStation build. Neither bumps VERSION: that stays the owner's
call, like a release, a tag or a push to `main` / `plus-beta`. `RGDS_SRC=/path/to/checkout sh install.sh` installs from
a copy on the device. Logs that matter: `/storage/.config/drastic/dsflip/dsflip.log` (the engine's log of the last
game, `.1`-`.3` the ones before), `last-session.log` (the launcher's), `/var/log/es_log.txt` (EmulationStation), and
new in 1.6 `/storage/.config/emulationstation/es-mem.log`.

Sections A-H are 1.6's; 1-4 below them are 1.5.13's, still to finish on a handheld.

**Where to start** (a Plus first, then the RG DS):
1. RG DS Plus: install its branch (table above). After one game,
   `grep -a 'stall watch' /storage/.config/drastic/dsflip/dsflip.log` shows the 1.6 library.
2. Copy the scripts over (`scp docs/1.6-prep/*.sh root@<handheld>:/storage/`) and run
   `sh /storage/stall-checks.sh <rom>` (section A): seven PASS lines in about 5 minutes. 4c may be a SKIP when
   the save is quicker than the second press.
3. `sh /storage/freeze-repro.sh "black version 2" 10 old`, then `new` (section A): the freeze's likely cause.
4. Sections B, C (the menu-over-a-game-list check first), D (row by row), E and F.
5. RG DS: install its branch and repeat 1-4. Nothing on that line has run on an RG DS since 1.5.8, and section F's
   in-game menu never has.
6. Send back:
   - the reports the two scripts write to `/storage`;
   - `es-mem.log`;
   - `dsflip.log` / `last-session.log` of anything odd;
   - screenshots of anything that looks wrong on either panel.

## A. The freeze after a resume load (1.6 task 1)

**What happened** (Black 2, RG DS Plus, 2026-10-05 18:07; the 1.5.13 test build): the game loaded its resume state,
both RetroAchievements logins failed with an empty error, the login-failed pop-up showed, and two seconds later
DraStic stopped presenting frames for good. libdsflip's presenter, touch and audio threads kept running. The handheld
needed a hard reset.

**Why nothing could end it** (certain, from the code):
- With resume on quit, the exit hotkey sends SIGUSR1 (`/tmp/.process-kill-data` = `-USR1 drastic`).
- libdsflip's handler only set a flag (`resume.c` `on_usr1` → `want_save`). DraStic's main thread acts on that flag at
  its next frame (`resume_frame`, called from `SDL_RenderPresent`), and that frame never came.
- A second press did not escalate either: it only escalated while `saving`, which is set on that same thread.
- `session.sh`'s safety check (`kill_pending`) only fires when a SIGKILL is pending but undeliverable (state D).
  Nobody had sent a SIGKILL, so it never fired.

**Why DraStic's main thread stopped** (likely, not proven: no backtrace exists from that run):
- **Not the RetroAchievements failure by itself.**
  - rcheevos calls our login callbacks without its state mutex held.
  - The pop-up is drawn on libdsflip's own thread, with one lock order (ui's `mx`, then `mu`).
  - HeartGold with RetroAchievements unreachable showed the same pop-up at 60 fps.
  - The empty error text has a plain cause: when curl got no answer, `http_thread` handed rcheevos an empty body, and
    rcheevos uses that body as the message. The token login then deleted the saved token and tried the password,
    which failed the same way.
  - The other session with the same `[rc] Login failed:` (Platinum, 2026-10-03, in the uploaded logs) ran on after
    its password login worked.
- **The signature matches the DraStic deadlock that 68dfccd documented on the Plus line.** There, DraStic's main
  thread and its 3D helpers waited on each other's condition variables forever, with the helpers at zero CPU time.
  It happened when the main thread was confined to one CPU while its thread pool made its first hand-offs.
  - The CPU placement (`session.sh`, `DSFLIP_PIN=1`, Plus only) starts 3 s after two helpers have run and repeats
    every second for ~10 s. It confines the main thread to CPU 3.
  - Any thread DraStic creates after that inherits CPU 3 alone, until the next placement pass. Checked on the host
    under qemu: a thread created from a thread confined to CPU 3 is allowed CPU 3 only.
  - A resume load (frame 120, ~2 s in) lands in that window. DraStic redoes its video set-up around a state load
    (new screen textures right after it in the uploaded logs), which is when it would make new helpers.
  - On the RG DS (no placement), this cannot happen.

**What changed** (SuperDrastic `0.5.0-beta.1-rocknixds.4`/`.5`, both lines; `session.sh`, both lines):
1. **The stall watch** (`dsflip.c` `stall_watch`, `stall_report`, on the presenter thread). It counts only time
   outside DraStic's own menu (which presents only when it changes), the in-game menu (which holds DraStic on
   purpose) and the quit's save.
   - **5 s without a frame:** every thread's name, state, CPU time, allowed CPUs, syscall, wait channel and kernel
     stack go to `dsflip.log` (fsync'd), and a red card says *The game stopped responding*. The exit hotkey then
     quits at once.
   - **20 s without a frame** (`DSFLIP_STALL_QUIT`, 0 = never): the game is ended. The menu then shows *The game
     stopped responding (no picture for 20 seconds) and was closed*.
2. **The exit hotkey always ends the game** (`resume.c` `on_usr1`, `quit_watch`). It quits at once, without the
   resume state, in three cases:
   - a second press: during the save (as in 1.5), or a second or more after a request DraStic hasn't taken;
   - a stalled game;
   - DraStic not taking the save within 3 s (5 s while the in-game menu, which closes itself for it, is up).
   A repeat within a second of a request DraStic hasn't taken yet is ignored. In a running game it takes the request
   at its next frame, so a second press lands during the save and quits without the resume state.
3. **New threads start on every CPU** (`dsflip.c` `pthread_create`): no thread inherits the main thread's
   confinement. `DSFLIP_SPREAD_THREADS=0` gives the old behaviour.
4. **RetroAchievements** (`ra.c`):
   - curl's error and the HTTP status are logged with the API name: `[ra] login2: no connection to
     RetroAchievements (Could not resolve host: ...) (curl 6)`, then `[ra] token login: ... trying again in 15 s`.
   - A login with no answer, a 429 or a 5xx keeps the token. It is tried again at 15 s, 30 s and 60 s, then every
     2 minutes, with one pop-up: *RetroAchievements: no connection*.
   - No empty pop-up lines.
5. **`session.sh` watches libdsflip from outside.** The presenter thread wakes 20+ times a second whatever the game
   does. If its CPU time (`/proc/<pid>/task/<tid>/schedstat`) stands still for 15 s, the whole process is wedged:
   DraStic is killed and the menu comes back with a notice. The loop doesn't count time while the handheld sleeps.
   Host check: a stand-in process was killed at 17 s with its presenter blocked, and left alone for 22 s while it ran.

**Reproduce it** (over ssh; RG DS Plus first). The scripts run on the handheld: copy them over first
(`scp docs/1.6-prep/*.sh root@<handheld>:/storage/`) and run them from there.
- `sh /storage/freeze-repro.sh "black version 2" 10 old`, then the same with `new`. Each run:
  1. plays 25 s and quits with the hotkey's SIGUSR1 (a resume state is saved);
  2. starts the game again with RetroAchievements unreachable;
  3. watches 40 s.
- A run that stalls writes libdsflip's `[stall]` thread dump and gdb backtraces of every thread to
  `/storage/freeze-repro-<date>.txt`.
- Expected if the explanation is right:
  - `old` stalls now and then;
  - in the dump, a `drastic` thread has `allowed 3` and no CPU time, and the main thread waits too;
  - `new` never stalls.
- If `new` stalls too, the dump and backtraces are the next step: send them over.
- Any game works, but Black 2 is the one that froze.

**On the device:** steps 2-5 also run unattended: `sh /storage/stall-checks.sh <rom-substring>` (about 5 minutes,
ES up, no game running). It prints one PASS/FAIL/SKIP line per check and writes the log lines behind them to
`/storage/stall-checks-<date>.txt`. It skips the resume checks when the game has *resume on quit* off. It puts back a
resume state the player had. What the steps below add is what to look at on the screens.
1. Install the branch:
   - RG DS Plus: `RGDS_BRANCH=claude/1-6-prep-work-kwq9lc-plus`.
   - RG DS: `RGDS_BRANCH=claude/1-6-prep-work-kwq9lc`.
   - `grep -a 'stall watch' /storage/.config/drastic/dsflip/dsflip.log` after a game shows `[dsflip] stall watch: a
     card after 5 s without a frame, the game ended after 20 s; new threads start on 4 CPUs`.
2. **The stall watch.** Run `systemctl set-environment DSFLIP_STALL_TEST=20`, start any game and wait.
   - At ~25 s the top panel shows the red card *The game stopped responding*.
   - `dsflip.log` has `[stall] no frame from DraStic for 5.0 s`, then one `[stall] thread ...` line per thread.
   - At ~40 s the game ends by itself. The menu comes back and shows the notice *The game stopped responding (no
     picture for 20 seconds) and was closed*.
3. **The exit hotkey on a stuck game.** Same switch, game started again, resume on quit on.
   - After the card, press the exit hotkey once: the menu is back at once.
   - Repeat, pressing it *before* the card (between 20 and 25 s): the menu is back ~3 s later.
     `dsflip.log` says `[resume] quit requested, but DraStic didn't take it within 3000 ms`.
   - Then run `systemctl unset-environment DSFLIP_STALL_TEST`.
4. **Normal quits still save.** Play a game 30 s, press the exit hotkey once, then start the game again: it resumes
   (`[resume] resumed`).
   - Press the hotkey, then again at once (while it saves): the game ends at once, without a resume state, and the
     next start doesn't resume.
   - In DraStic's own menu (from the in-game menu's *DraStic menu*), press the hotkey: the game ends within ~3 s
     (without a resume state; the log says why).
5. **Wedged process, from outside.** Start a game, then over ssh run `kill -STOP $(pidof drastic)`.
   - Within 15-25 s `last-session.log` says `libdsflip's presenter hasn't run for 15 s: DraStic is wedged, killing it`.
   - The menu comes back with *The game stopped responding and was closed*.
6. **RetroAchievements without network**, done with the RA routes blocked as in `ra-offline-repro.sh`:
   - `dsflip.log` has `[ra] login2: no connection to RetroAchievements (...) (curl 6)` (or 7, or 28) and
     `[ra] token login: ... trying again in 15 s`.
   - The pop-up says *RetroAchievements: no connection / Trying again in the background*.
   - `ra.token` still exists afterwards.
   - Unblock the routes: within 2 minutes `[ra] logged in with token as ...`, and achievements work.
7. **The freeze run itself**, as in "Reproduce it" above.

**Acceptance:**
- Steps 2-6 behave as described on the Plus (and on the RG DS, which has no CPU placement).
- `freeze-repro.sh ... 10 new` shows no stall. A stall there comes with its dump, which then tells the real cause.
- Black 2's resume plus a failed login has never again needed a reset.

## B. Starting and quitting a game (1.6 task 2)

**What changed** (both lines; the RG DS line got the Plus line's two fixes from 1.6-prep, 357ca9d):
- **Black, not "an empty terminal".** `session.sh` clears tty12 and hides its cursor before the switch. The moments
  the kernel console has the panels (before libdsflip takes them, and after the game) are plain black.
- **The menu shows only once ES draws again.**
  - `restore.sh --vt-back` (transient unit `dsflip-vtback`) switches the panels back to sway once ES's main thread is
    back in its loop: `/proc/<pid>/syscall` 115, 101, 73 or 22, not wait4 (260). It waits 3 s at most.
  - The second `restore.sh` run (ExecStopPost) leaves at once.
  - On the RG DS the window placement is the Plus line's code, which keeps 1920x480 for a 640-wide panel.
- **New tonight, both lines:** a start that comes while the last game's switch back is still waiting (two launch
  requests in a row) stops that switch, and keeps the VT that game recorded as sway's. Before, the pending switch
  could hand the panels back to sway under the new game, or tty12 could be recorded as sway's VT.

**On the device** (the RG DS has never run any of this):
1. `tools/switchtime.sh` (HeartGold, 4 cycles) on each handheld. `last-session.log` per quit:
   `restore: the panels go back to sway when ES draws again`, then `restore: ES is drawing again: the panels back to
   sway` and `restore: menu shown`. The Plus measured the menu ~1.7 s after the quit; note the RG DS's numbers.
2. By eye, a game started from the menu: after the start animation the panels are black, no cursor and no text, until
   the game's first frame. Quit with the exit hotkey: black, then the menu. The start animation's last frame must
   not reappear.
3. `grep -c "did not come back in 3 s" last-session.log` over a dozen quits. Each such line means the fallback ran; note
   what ES was doing (a big library reloading?).
4. Two launches in a row: `ROM=/storage/roms/nds/<game>.nds; curl -s -X POST --data-binary "$ROM"
   localhost:1234/launch; sleep 0.15; curl -s -X POST --data-binary "$ROM" localhost:1234/launch`.
   - One game starts. `/var/log/es_log.txt` has `Launch of ... ignored: a game is already starting`.
   - Quit it: the menu comes back, and the game does not start a second time.
5. Start a game over the API while the last one is quitting: `killall -USR1 drastic; sleep 0.3; curl -s -X POST
   --data-binary "$ROM" localhost:1234/launch`. Either it waits (ES still in the launch command, as before), or
   `last-session.log` says `the last game's switch back was still pending: sway stays on ttyN`. In both cases the
   menu comes back after this game.

**Acceptance:** steps 1-5 on both handhelds; no stale frame, no cursor, no double start.

## C. The menu's memory, and the double launch (1.6 task 3)

**What was found.**
- **The likely cause: ROCKNIXDS Pixel under a menu.**
  - ES keeps drawing the game list under its menus (game options, settings, a scrape) but updates only the menu.
  - The theme's engine took its decode worker's finished pictures only when updated, and forgot a picture's request
    as soon as the worker had made it.
  - So under a menu, every frame asked for the same pictures again, and every finished picture woke ES for another
    frame and was kept.
  - On a PC, 120 such frames grew the heap by 43 MB at 640x480 and 111 MB at 1024x768, and it doesn't stop while
    the menu stays up.
- The lead in the hand-off (`reloadAllGames` after every game) doesn't apply. `ViewController::doLaunchGame` returns
  true only for `windows_installers`, so DS games never trigger that reload.
- The rnds engine's texture cache was bounded by count per group (400/200/120/90/8), not by bytes.
- Images are decoded at full size on several threads. glibc keeps what they free in per-thread arenas and raises its
  mmap threshold after the first big free, so the process only grows.
- The double start: `/launch` posts `ViewController::launch()` to the UI thread, and a launch only schedules the game
  (for the rnds theme, a 1 ms animation). A second `launch()` replaced that animation; the replaced animation's
  finish callback started the first game at once, and the second game started after it.

**What changed** (one ES binary for both lines):
- `es-rgds-launchonce.patch`: while one launch is starting or running, another is ignored, with a warning in the
  log. A `/launch` request received while a game ran is dropped (`ViewController::lastGameEndedAt`).
- `es-rgds-memory.patch`: two malloc arenas and a fixed 256 KB mmap threshold, so big buffers return to the system
  when freed (`MALLOC_ARENA_MAX` / `MALLOC_MMAP_THRESHOLD_` in the environment still win). `malloc_trim(0)` runs
  right before each game.
- `es-rgds-rnds.patch`:
  - Finished pictures are taken in when drawing too, and a request stands until its picture is taken: under a menu
    the memory stays flat (the PC test above).
  - The worker's queue keeps the newest 64 requests.
  - The texture cache drops its least recently used pictures past 20 MB at 640x480, scaled by the panel's area (51 MB
    on the Plus). Normal browsing of a 740-game library needs 11 MB (28 MB on the Plus); only box art in many sizes
    reached the old count limits' 32 MB (81 MB).
  - The first time the budget applies, ES's log says `rnds: texture cache at its budget: ...`.
- `es-memwatch.sh` (started by `start_es_rgds.sh`, ends with ES's unit):
  - Every 30 s it reads ES's memory. It writes `/storage/.config/emulationstation/es-mem.log` on a change of 16 MB or
    more, and at least once an hour: heap, mapped files, shared/GPU memory, and what the system has available.
  - Over half the RAM (487 MB on the Plus; `ROCKNIXDS_ES_MEMLIMIT_MB`, 0 = never) with no game running, it writes
    ES's status line and restarts ES.

**On the device:**
1. After boot and a few minutes of browsing, `cat /storage/.config/emulationstation/es-mem.log` has a line like
   `pid N rss 180 MB (heap ..., files ..., shared/GPU ...)`.
2. **A menu over a game list** (the likely cause):
   - Restart ES, open a big ROCKNIXDS Pixel game list at once (its pictures still loading), and open the game options
     (or START's menu) over it. Leave it 2 minutes.
   - Then `grep VmRSS /proc/$(pidof emulationstation)/status`, and again after 2 more minutes: the same within a few
     MB. Before 1.6 it climbed for as long as the menu stayed up.
3. **Browse hard:**
   - Hold right in the largest library for a minute, flip systems, open the game options a few times.
   - Run 10 games (start/quit), then leave the menu idle for 10 minutes.
   - Note es-mem.log's lines. Expected: growth that levels off, under ~300 MB on the Plus.
   - If it keeps climbing, the split (heap vs shared/GPU) says where. Send es-mem.log and `grep -i rnds
     /var/log/es_log.txt`.
4. **The valve:**
   - `systemctl set-environment ROCKNIXDS_ES_MEMLIMIT_MB=120; systemctl restart essway.service`.
   - Within a minute ES restarts by itself, and es-mem.log says `over 120 MB with no game running: restarting it`
     with the status line.
   - It must not happen during a game: start one before the minute is up, and the restart waits until after the
     game.
   - Then `systemctl unset-environment ROCKNIXDS_ES_MEMLIMIT_MB; systemctl restart essway.service`.
5. The double launch: section B, step 4.

**Acceptance:** es-mem.log is written; steps 2 and 3 level off; the valve restarts ES only outside games.

## D. The open issues (1.6 task 4)

Closed on 2026-10-06 with a short note each, as the owner asked: #24, #25, #28 (the RG DS reporter asked to reopen if
needed), #33 (the RG DS untested) and #37 (fixed in 1.5.7; 1.6 adds the parse check below).

| # | What changed | Lines | Device check |
|---|---|---|---|
| 26 | The in-game menu's *Blow* presses the fake microphone through whatever `drastic.cfg` binds it to (the key, or the joystick button when only that set has it). The real-mic tuning still needs a person: section 3 | both (library) | Section 3 steps 1-4. Then *Quick settings > Microphone > Blow* by the first candle in Phantom Hourglass, with the keyboard binding removed from `drastic.cfg` (`controls_a[CONTROL_INDEX_FAKE_MICROPHONE] = 65535`) and a joystick button bound in `controls_b`: the candle goes out |
| 27 | Item 7 (touch): Item 7 (touch), in `es-rgds-rnds.patch`: the game list's progress rail takes a tap (the cursor jumps there) and a drag (it follows the finger), with the game's first letter in a bubble over the thumb while dragging; ◀ ▶ arrows by the shoulder names, whose tap zones now take a finger (half the row each). No stock ES file changed (ES's mouse capture brings the finger's moves to the view). Items 1, 3b, 4 and 6 were fixed in 1.5.2 and wait for the reporter's re-test. Checked with the engine's harness at both sizes: [640](1.6-prep/img/rnds-27-drag-640.png), [1024](1.6-prep/img/rnds-27-drag-1024.png). The home screen's rail doesn't take a drag (that would need an ES hunk) | both (ES) | A game list with 50+ games: put a finger on the progress bar and slide: the reel follows, a letter bubble rides over the thumb, nothing overlaps; lift: the game under the finger stays selected. Tap the bar near an end: the cursor jumps there. Tap ◀ / ▶ (or the names): one game back / forward, the arrow blue while held. A swipe on the reel still moves one game |
| 30 | Fixed in 1.5.2 (`es-rgds-help.patch`); the reporter never answered | both | ES's on-screen keyboard (any text field): the help line fits the bottom panel on both handhelds |
| 31 | `rocknixds-media.py`'s background RetroAchievements id hashes the first `.nds` in a `.zip` (else its first file) the way rcheevos does; `.7z` is skipped with a log line. Checked on the host: a synthetic ROM, plain and zipped (deflated and stored), gives rcheevos' own hash | both | A zipped DS game with achievements: after the menu's background job (or `rocknixds-media.py --local --auto`), its gamelist entry has a `cheevosId` and the Pixel library shows its achievement count |
| 32, 34, 35 | `es-rgds-panelguis.patch` (ES): *View Game Media* (its pictures, the zoom view and *View fullscreen video*), the Save State Manager and *Manual scrape* are one panel wide, on the bottom panel. The zoom view opens with the whole picture fitted, L/R zoom, the D-pad moves it, and it never leaves the panel. From *View Game Media* it had always been empty: it was given the entry's number instead of its picture (an upstream bug). *Manual scrape*'s details column grows from the result list's share (24% to 45% of the window), so the values and the date fit; values longer than about 10 letters still end in "...". #34's other half, the Pixel font's 2/5/S, Z and B/G, has been on this line since 1.5.13 beta 1. Checked on a PC at 1920x480 and 3072x768 with Pixel dark: before and after in [1.6-prep/img](1.6-prep/img) (`es-32-*`, `es-34-*`, `es-35-*`) | both (ES) | 1. A scraped game with a picture and a video: *View Game Media* and *View fullscreen video* (game options) stay on the bottom panel. A on a picture shows all of it there; L/R zoom and the D-pad moves it, and nothing reaches the top panel. 2. *Game settings > Show savestate manager: Always*, then start a GBA game (RetroArch): the title, START NEW GAME and the slots are all on the bottom panel (START NEW GAME is shortened to "START NEW ..." at 640 px, as on any 4:3 screen). 3. *Scrape* on a game: the publisher, genre and the whole date are inside the window. `grim` grabs ES's screens to compare with the pictures |
| 36, 37 | `es-features.sh`: a new `es_features.cfg` replaces the old one only if it parses with `<features>` as its root; otherwise the old one stays, or both go and ES reads ROCKNIX's copy. The RG DS line's depth-aware repair and its test are on the Plus line now; an option written on one line is no longer dropped. Tests: 74 (RG DS) and 73 (Plus) pass | both | `grep es-features /storage/.config/drastic/dsflip/install.log` (or the installer's output) after an update; the DS's per-system and per-game advanced settings list every DraStic option; Tools and Music Player are not in the per-system list |
| 42 | Not done: the plan is in section G. It moves the players' save files, so it is the owner's call | | |
| 44 | `es-rgds-rnds.patch`: *UI settings > ROM icon on cartridges (DS)* (ROCKNIXDS Pixel only, off by default). On, a DS game's cartridges on the bottom screen show the 32x32 icon from its ROM (`.nds`, or the first `.nds` in a `.zip`) instead of the label art, scaled up in whole steps without smoothing. It is read once per ROM, off the UI thread; odd ROMs, `.7z` and other systems keep their art; DSi animated icons show their still frame. Checked with the engine's harness: 20 synthetic ROMs, the setting off byte-identical to before, the sanitizers clean ([640](1.6-prep/img/rnds-44-rom-icon-640.png), [1024](1.6-prep/img/rnds-44-rom-icon-1024.png)) | both (ES) | Turn it on, open the DS game list: each cartridge on the bottom screen shows the game's own icon (the one the DS menu shows), sharp, on a light label, zipped games too, and the top screen is unchanged. Scroll a big list: no stall. Turn it off: the art is back. With a `.7z` game, that game keeps its art |
| 43 | Not a bug: ROCKNIX turns front-end music on but ships no music. The README and the release notes now say to copy `.mp3`/`.ogg` files to `roms/music` | both (docs) | Copy one `.ogg` to `/storage/roms/music`, restart ES: it plays in the menu |

## E. Online play parked (1.6 task 5)

Nintendo WFC doesn't get past the game's own Wi-Fi setup yet, so 1.6 neither shows nor runs it.
- ES no longer offers *wfc dns*.
- `session.sh` doesn't export it.
- libdsflip ignores `nds.wfc_dns` (only the `DSFLIP_WFC` test switch turns the hook on; `DSFLIP_WFC_CONFIG=1` reads
  the setting again).
- The work in progress is SuperDrastic `1.6-prep`'s last commit (01269ed), outside the 1.6 package. Section 4 below is
  the plan for when it returns.

**On the device:**
- *Nintendo DS* advanced settings (system and per game) have no *wfc dns*.
- `last-session.log` says `wifi: off`, and `dsflip.log` has no `[wfc]` line, even with `nds.wfc_dns=kaeru` left in
  `system.cfg` from 1.5.13.
- `systemctl set-environment DSFLIP_WFC=kaeru` still gives `[wfc] online via Kaeru WFC` (then `unset-environment`).

## F. The in-game menu on the RG DS

Not on this line's list: the Plus has had it since 1.5.13 beta 1. The RG DS line now ships the same library, and its
hand-off (`claude/1-6-prep-work-kwq9lc`, section F) has the checks for 640x480. `.6` changes nothing at 1024x768
except two fixes for both handhelds (rendered on a PC):
- a selected empty slot on the Load page is readable (it was grey on blue);
- dates have no double space.

Japanese and Chinese game names show as empty boxes in the menu, as since 1.5.13: a fallback font is drafted in
[`1.6-prep/superdrastic-menu-cjk-fallback.diff`](1.6-prep/superdrastic-menu-cjk-fallback.diff), not applied.

## G. Not done tonight: #42, with a plan

- **#42, in-game saves in a folder of their own.**
  - ROCKNIX's `start_drastic.sh` runs `rm -rf /storage/.config/drastic/backup; ln -sf /storage/roms/nds ...` on every
    launch, so the `.dsv` files live beside the ROMs.
  - Plan: in `drastic-wrapper.sh`, which runs after that, point `backup` (as a symlink: a real directory would be
    deleted by that `rm -rf`) at `roms/saves/nds`, and move the existing `.dsv` files once, never overwriting.
    Point `session.sh`'s `DSV` (the resume state's staleness check) there, and make uninstall move them back.
  - It moves the players' save files, so it should be an opt-in ES switch, and the owner's call. Not started.
- **#44** was done after all (section D), as an opt-in Pixel setting read by the engine itself.

## H. Where the two lines still differ

The 1.6 work above went into both lines. They still differ where they did before 1.6:
- **RG DS line only:**
  - the performance-log upload: `perf-session.py`, *Share performance logs*, `.github/ingest-perf.py` and the
    `perf-logs` workflow;
  - `es-share-logs.sh`, `preload-guard`;
  - the *3D resolution* option.
- **RG DS Plus line only:**
  - panel-size handling (`session.sh`'s `BIG`: shader GPU clock, the battery profile up to 1416 MHz, the latch
    margin) and the CPU placement (`DSFLIP_PIN`);
  - the 2048x768 splash, mako notifications, `input-rocknixds.conf`, `tools/threadsample.sh`.
- **The same on both:**
  - SuperDrastic `0.5.0-beta.1-rocknixds.6`;
  - the EmulationStation binary and all its patches (`es-rgds-dsfirst.patch` is gone: its collection names are in
    the rnds patch, the rest was superseded by `es-rgds-emptylibrary.patch`);
  - the Pixel theme, its font included;
  - the 1.6 changes to `session.sh`, `restore.sh`, `es-features.sh` and `es-memwatch.sh`;
  - `docs/1.6-prep`'s scripts.
- **The owner's call:**
  - whether the Plus gets the performance logs;
  - whether the lines merge into one branch. The Plus line's `session.sh` and `install.sh` already handle both
    panel sizes.

## Results on an RG DS Plus, 2026-10-05

Run on an RG DS Plus (ROCKNIX 20260930) with this tree ported to the Plus line (`plus-beta` + these changes, SuperDrastic
`0.5.0-beta.1-rocknixds.3`: v0.5.0-beta.1, the in-game menu, plus the four patch commits). That port is the branch
`claude/tender-volta-a9nkpk-plus` (version `1.5.13-plus-beta.2-dev`, a test build: its release notes are still the RG
DS's `v1.5.13.md`); on an RG DS Plus, `RGDS_BRANCH=claude/tender-volta-a9nkpk-plus` in the command above installs it.
Games were started through ES's API and quit with the exit hotkey's signal; nobody held the handheld.

- **Fast switching (1.5.13's first item): works after one fix.** The kept ES window came back from the VT switch one
  panel wide (1024x768 instead of 3072x768), so the bottom panel stayed black. `restore.sh` now lifts sway's floating
  size limit, resizes to three panels and puts the limit back (Plus branch only; the RG DS path is unchanged and
  untested here). `tools/switchtime.sh`, HeartGold, 4 cycles after the fix: ES shown 0.99-1.02 s after the kill, ES's
  API idle at 2.6-2.7 s. A real quit (SIGUSR1, resume state saved in 1.05 s): unit ended at 2.6 s, API idle at 3.7 s;
  the next start resumed from it.
- **1, defaults:** with `nds.renderer` unset the log says `3D renderer: Gengis Engine (Auto; texture filter 0)` (the
  Plus line has no 3D resolution, so no `scale`). The rasterizer's own line (`[rast] Gengis Engine: hooked
  video_3d_render_bins_4x, mode ours, scale 2`) is in `drastic.out`, not `dsflip.log`. Steps 3-5 not run.
- **ds-fsr at 3x:** `[dsflip] shader output: 768x576 per panel, scaled to 1024x768 by the display controller`,
  59.6-60.9 presents a second with `dropped=0` over 25 s of HeartGold. The picture was not looked at by a person.
- **2, empty library:** steps 2, 4 and 5 pass (screenshots taken): the shelf lists Nintendo DS with 0 games, the
  library shows "No games yet / Copy .nds files to roms/nds", `00 / 00`, *B Back*, a muted *No games*; A, up, down, L,
  R, Y and taps on the reel, START's old spot and *No games* change nothing; a tap on *Back* goes back; no `System
  "nds" has no games` line. One game copied in + `/reloadgames`: "1 game", it launches, the menu is back on the DS.
  Seen with that one never-played game: the home card shows it as *Last played* with an empty cover.
- **3, microphone:** H1 and H3 are out on this unit (`controls_a 327, controls_b 327`; a 3 s capture reads RMS 0.067).
  With sensitivity *high* and `DSFLIP_MIC_DEBUG=1` the new lines all appear; 28 s of HeartGold with nobody blowing
  gave 2 presses (one at level 0.35, lf 0.97). Background blocks read lf 0.7-0.97, as high as a blow is expected to.
  The blow test itself (steps 1, 3, 4, 6) still needs a person.
- **4, Wi-Fi:** step 1: build id `7a5e0e5f...0748`, type 3. Step 2 from the handheld (on a phone hotspot): all four
  servers answer all three names with their own addresses, and all four conntest pages return 200. Step 3 (Mario
  Kart DS, `DSFLIP_WFC=kaeru`): `[wfc] online via Kaeru WFC (178.62.43.212)` and `[wfc] layout ok (reg ok, mac ok, IF
  store plain)`, 60 presents a second. Steps 4-9 need a person in the game's Wi-Fi setup.
- The in-game menu (0.5.0-beta.1) opens and closes in this build under fast switching (`[menu] open` / `closed`).

## 1. The recommended settings are the defaults

**Done on the host.** `dsflip/device/session.sh` runs Gengis Engine when *3D renderer* is *Auto* (unset) or
*Gengis Engine*, DraStic's renderer only when the player chose *DraStic*; `install.sh` sets `nds.threaded_3d=1` once
where it was never set (uninstall removes it); README step 6 and the release notes describe the defaults.

**On the device:**

1. Fresh state: `sed -i '/^nds\.renderer=/d; /^nds\.threaded_3d=/d' /storage/.config/system/configs/system.cfg`, then
   run the installer. Check `grep '^nds\.' /storage/.config/system/configs/system.cfg` shows `nds.hires_3d=1` and
   `nds.threaded_3d=1` and no `nds.renderer=` line.
2. Start a 3D game (Pokémon HeartGold) with every Nintendo DS setting on *Auto*. `grep '3D renderer'
   /storage/.config/drastic/dsflip/last-session.log` must say `Gengis Engine (Auto; scale 2, texture filter 0)`, and
   `grep rast /storage/.config/drastic/dsflip/dsflip.log` must show the rasterizer starting.
3. Set *3D renderer* to *DraStic* for that game (X on it > advanced game options), start it again: the log says
   `3D renderer: DraStic`.
4. Play 5 minutes of HeartGold walking at *Auto*: `tools/rgds-monitor.py <ip>` on a PC should show the same or fewer
   dropped frames than with *DraStic* (1.5.9 measured 53.1 vs 50.6 fps at 816 MHz on a Plus).
5. `sh install.sh --uninstall` removes `nds.threaded_3d=1` again (`grep threaded_3d system.cfg` empty).

**Acceptance:** steps 2, 3 and 5 as described; no visible difference in any 3D game between *Auto* and *DraStic*
(Gengis Engine is pixel-identical by design; a difference is a bug to report with the game and a screenshot).

## 2. Empty `roms/nds` breaks the menu

**What it was.** EmulationStation drops a game system with no files, so with `roms/nds` empty the DS did not exist:
the `LastSystem=nds` the installer seeds resolved to nothing, the menu opened on the first system left (Music Player,
Tools, Favorites, Last played), and the two empty collections' library showed ES's `<No Entries Found>` placeholder
drawn as a cartridge. A tap on that cartridge or on START entered the ready screen and stayed on "Starting..." for
good (the gamepad's A was filtered out, the touch path was not). ES also overwrote `LastSystem` with the fallback
system on exit, so even after adding a game the menu did not open on the DS. Found by reading the code and running
the Pixel engine on the host with empty data (no crash, no bad layout: the engine itself survives 0 games and 0
systems, under the address sanitizer).

**Done on the host.** `dii-ess-aye/es-rgds-emptylibrary.patch` (in `tools/build-es.sh`'s list): the systems in
`RGDS_KEEP_SYSTEMS` (default `nds`) are loaded, themed and visible with zero games (five gates in `SystemData.cpp`).
The Pixel engine (its files live in `es-rgds-rnds.patch`; after editing them in the patched tree,
`tools/regen-rnds-patch.sh <es src> dii-ess-aye/es-rgds-rnds.patch` rewrites their sections) counts ES's placeholder
as no game and draws an empty state: the bubble **No games yet / Copy .nds files to roms/nds** over an empty reel (no
cartridge, no plate, no START), the rail `00 / 00`, the faces *B Back* and a muted *No games*; the top screen the DS
icon in the cover frame with the same two lines. Collections say **Nothing here yet** with their own hint (Favorites:
*Y on a game adds it here*; Last played: *Games you play show up here*); a list that a filter from ES's game list
options empties says **Nothing matches / Clear the filter in the game list options**. The engine never asks the
host for a game past the count, never opens a library with no systems and never favourites nothing (`RndsUI.cpp`,
`RndsEs.cpp`).
The host harness (`dii-ess-aye/rnds/test`, built with `-DMOCKDATA='"empty.inc"'`) renders that state under the
address sanitizer, and with games present 24 frames (home, library, ready screen, back; dark and light) are
byte-identical to the previous engine. `emulationstation-rgds` rebuilt; `install.sh` sends `LastSystem` back to
`nds` when it holds one of the fallback systems. README and release notes updated.

**On the device** (a handheld with DS games; simulate the fresh card):

1. `mv /storage/roms/nds /storage/roms/nds.bak && mkdir /storage/roms/nds && systemctl restart essway.service`
   (ES reads `roms/nds` at start; the directory must exist, as ROCKNIX creates it at boot).
2. The shelf shows **Nintendo DS** (Games 0) and the menu opens on it. Open it: the bottom screen shows the bubble
   "No games yet / Copy .nds files to roms/nds" over an empty reel (no cartridge, no START), the rail `00 / 00`,
   *B Back* and a muted *No games*; the top screen the DS icon in the frame with the same two lines. **A, X, Y, up,
   down, L, R**, a tap on the reel, on START's old spot and on the *No games* face do nothing harmful (X opens ES's
   own game options on the placeholder, as it always did: close it; before the fix a tap on START hung the ready
   screen on "Starting..."); **B** goes back; X on the shelf (resume) does nothing. Right to Favorites, A: "Nothing
   here yet / Y on a game adds it here"; Last played: "Games you play show up here".
   `grep -c 'System "nds" has no games' /var/log/es_log.txt` is `0` (ES's log may instead be
   `/storage/.config/emulationstation/es_log.txt`). Grab both panels for the record: `export
   XDG_RUNTIME_DIR=/var/run/0-runtime-dir SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock | head -n1); grim -o
   DSI-2 /storage/roms/screenshots/empty-top.png; grim -o DSI-1 /storage/roms/screenshots/empty-bottom.png`.
3. Still with the empty folder: Music Player / Tools / the collections look as before; nothing else gained a shelf
   entry (`LoadEmptySystems` stays off).
4. Add a game live: copy one `.nds` into the empty `/storage/roms/nds`, then `curl -s
   http://127.0.0.1:1234/reloadgames` (or restart essway): the library shows its cartridge, the shelf "Games 1", it
   launches, and the menu is back on the DS after the quit.
5. `rm -rf /storage/roms/nds && mv /storage/roms/nds.bak /storage/roms/nds && systemctl restart essway.service`: the
   games are back, play stats intact (they live in gamelist.xml inside the folder, which moved with it); the shelf,
   the library, the ready screen and a subfolder look exactly as before.
6. The real thing, if a spare card is at hand: flash the image, boot with no games: step 2's menu on the first boot,
   then copy one game over the network and restart: it opens on the DS with the game.
7. A card that ran 1.5.10-1.5.12 without games holds `LastSystem` = music/tools/favorites/recent in
   `/storage/.config/emulationstation/es_settings.cfg`; after the update it says `nds` and the menu opens on the DS.

**Acceptance:** steps 2, 4 and 5; with games present nothing looks different; on the empty run ES's log (default
level: warnings only) has no `System "nds" has no games! Ignoring it.` line, and the shelf shows the DS (there is no
positive "system loaded" line at that level; `LogLevel=information` in `es_settings.cfg` adds `Parsing XML file
"/storage/roms/nds/gamelist.xml"` on the run with games).

## 3. The real microphone

**What is known.** Issue 26: on an RG DS Plus (ROCKNIX 20261001) blowing into the mic does nothing at *microphone
sensitivity* high, while a button bound to DraStic's *Fake Microphone* in DraStic's own menu works. So DraStic's
fake-microphone path (it plays `microphone/microphone.wav`, next to `config/`, into the DS mic while the control is
held) is fine;
what fails is between libdsflip's capture and that control. The path: ES's setting → `DSHOOK_MIC_THRESH` (high 0.03,
medium 0.15, low 0.3; *Auto* is off) → libdsflip's mic thread (`src/audio.c`: ALSA `default` capture, 44.1 kHz mono,
RMS per 1024-sample block, a noise floor learned over the first 1.4 s, then `level > floor + threshold`, and an
**echo gate** that also demands `level > 3 × learned speaker leak × speaker level + threshold`) → an SDL key event
for Scroll Lock, which DraStic reads as control code 327 → `controls_a[CONTROL_INDEX_FAKE_MICROPHONE]` in
`config/drastic.cfg`. Four hypotheses came out of the code, in order of likelihood, each now visible in the log:

- **H2, the echo gate swallows real blows.** The leak factor is learned from every block that is not a blow,
  including quiet passages where the room noise, not the speaker, sets the ratio, and it is capped at 4: with music
  at speaker level 0.15 the bar can sit at 0.5-1.8 RMS, above any blow (0.1-0.4). *Low* (0.3) is unreachable with
  any music. The gate was tuned on an RG DS at one volume (0 false presses); the Plus's louder speakers next to its
  mic make it worse.
- **H1, the control is unbound.** ROCKNIX's `drastic.cfg.rgds` has bound 327 in both control sets since 2026-02-04,
  but a `/storage/.config/drastic` created by an older nightly keeps `65535` (unbound) for ever, and rebinding a
  button in DraStic's menu overwrites whichever set DraStic edits.
- **H3, the Plus's capture is silent.** The Plus's mic sits on the `rk817_hp` card (the speakers are `aw88166`), its
  UCM declares capture gains it never sets; a muted or zero-gain source reads as all zeros, a missing source as a
  capture that never returns frames.
- **H5, the key flickers.** The key follows every 23 ms block; a blow hovering around the bar presses and releases
  several times inside one DraStic frame, which a game's blow detector never sees as held.

**What the uploaded logs say** (branch `device-logs`, `docs/data/device/<device>/`; the handheld uploads one record a
second with `dsflip.log`'s new lines). Two handhelds have `[mic]` lines. `48acc59e82bc43ed`, Phantom Hourglass on
2026-10-03 (the issue's day and game): `noise floor 0.037-0.060, peak 0.21-0.29, speaker leak x0.30-0.65`, 1 press
in the first 10 s and 0 in the next 70. `36ae0e2f8fd74c2a` (Mario & Luigi, Mario Kart, HeartGold; nobody blowing):
`floor 0.003-0.09, peak 0.22-0.35, leak x0.39-0.58, 0 presses`: the game's own sound reaches that mic at 0.22-0.35
RMS, as loud as a blow (0.2-0.4). So on these two the capture works and the units are right (H3 and H4 out), the gate
held the bar at 3 × leak × output, around 0.5-0.7, which no blow reaches (H2 is the mechanism), and lowering the
factor alone cannot be the whole fix where the leak is as loud as a blow: at ×1 the music would press the key.
That is what the low-frequency share below is for. Also in the logs: one session with `floor 0.0000, peak 0.0000`
for its whole 2.5 minutes, and two where the peak sat at `0.0000` for 100 s mid-game and came back: the capture
delivers silence at times (a suspended PipeWire source? the pause menu?), which the `capture is silent` line now
names at start and which step 2 can catch mid-game.

**Done on the host** (SuperDrastic commit *Microphone: tunables and logs for the handheld*, d8291e0, in every
package since `0.4.0-beta.2-rocknixds.3`; 1.6 ships `0.5.0-beta.1-rocknixds.6`; nothing needs a rebuild to test):

- `dsflip.log` says at start how the control is bound: `[mic] fake microphone: drastic.cfg controls_a 327 (Scroll
  Lock), controls_b N: pressing the key`, or `... pressing joystick button N` when the keyboard set is unbound but
  the joystick set has a button (the thread then sends that button, as DraStic's own binding would), or `...
  pressing Scroll Lock (327), which this config does NOT map` (H1 confirmed from the log alone).
- `session.sh` repairs H1 before the game starts: with the mic on and `controls_a[...FAKE_MICROPHONE] = 65535` it
  writes 327 there and says so in `last-session.log` (`microphone: bound DraStic's fake microphone ...`). DraStic
  saves the file on exit, so a later rebinding by the player stays.
- `[mic] capture is silent (peak 0.00031 in 5 s): is a microphone source behind ALSA's default? (wpctl status)`
  after ~5 s with every block's RMS below 0.0005, near silence (H3; a muted source with dither noise counts). A
  capture that never returns frames shows `[mic] listening` and then never the `[mic] 10 s:` line.
- Switches, read once at the game's start, set with `systemctl set-environment NAME=value` over ssh (the game unit
  inherits them; `systemctl unset-environment NAME` removes them; `last-session.log` does not list them):
  - `DSFLIP_MIC_DEBUG=1`: a line every 8 blocks (~190 ms) `[mic] level L floor F out O coup C bleed B bar X down D`
    and one on every `PRESS` / `release`.
  - `DSFLIP_MIC_GATE=k`: the gate's factor (3 is the shipped value; `0` turns the gate off, which is libdrastouch's
    behaviour: floor + threshold only).
  - `DSFLIP_MIC_COUPLING_MAX=r`: the most leak the gate may learn (4 shipped; 1 keeps the bar reachable at any
    volume while music plays).
  - `DSFLIP_MIC_HOLD_MS=ms`: the key stays down at least this long after a press (0 shipped).
  - `DSFLIP_MIC_KEY=auto|key|button`: what presses the control (auto as described above).
  - `DSFLIP_MIC_LF=r`: a press also needs the block's low-frequency share above r: the RMS below ~250 Hz over the
    whole RMS. A blow into the mic is wind, mostly below that; game sound is mostly above (synthetic check on the
    host: filtered wind 0.7-0.8, tones 0.5-0.6, white noise 0.14; the real numbers come from the device). 0 shipped
    (logged only): every `[mic] 10 s:` line ends with `lf lo-hi above floor`, the share's range over the blocks
    above floor + threshold, and the debug lines carry `lf`.
  - The `[mic] listening, threshold T, echo gate on (x3.0, leak cap 4.0), hold 0 ms, low-frequency share logged only
    (DSFLIP_MIC_LF=0)` line echoes what was read.

**On the device** (an RG DS Plus, the reporter's model; an RG DS too if one is at hand). Phantom Hourglass's first
candle (Mercay Island, the two candles in Oshus's house, blow to put them out) or Nintendogs are the games; the
*Blow* entry of SuperDrastic 0.5.0's in-game menu (the plus-beta line) holds the control for 3 s *without* the mic
thread, so it separates DraStic's side from ours when both lines are on the desk.

1. **Baseline.** In ES set *Nintendo DS > microphone sensitivity* to *high*. `systemctl set-environment
   DSFLIP_MIC_DEBUG=1`. Start the game, reach the candle, blow 3-4 times over ~15 s with the music on, quit.
   `grep -n '\[mic\]' /storage/.config/drastic/dsflip/dsflip.log` and `grep -n microphone
   /storage/.config/drastic/dsflip/last-session.log`. Read it like this:
   - no `[mic] listening` but `[mic] off`: the setting never reached the game (`grep -i micro
     /storage/.config/system/configs/system.cfg`; ROCKNIX's `start_drastic.sh` exports `DSHOOK_MIC_THRESH`).
   - `[mic] no capture device` or `capture is silent`, or `listening` with no `10 s:` line ever: **H3**, step 2.
   - `fake microphone: ... does NOT map`: **H1**; the launcher should have repaired it (its `microphone:` line);
     if `controls_a` holds a button code instead of 65535, `DSFLIP_MIC_KEY=button` or rebind in DraStic's menu.
   - debug lines during a blow with `level` well above `floor + 0.03` but below `bar`, and no `PRESS`: **H2**, step 3.
   - `PRESS`/`release` pairs many times per blow (10+ in one `10 s:` line): **H5**, step 4.
   - `PRESS` once or twice per blow, held through it, and the candle still burns: the key reached DraStic and was
     ignored: run DraStic's menu, *Fake Microphone*, and check the keyboard set shows Scroll Lock; try
     `DSFLIP_MIC_KEY=button` after binding a button there.
2. **H3, capture sanity** (ES menu, no game running): `cat /proc/asound/cards` (expect `rk817_hp` and `aw88166`);
   `XDG_RUNTIME_DIR=/var/run/0-runtime-dir wpctl status | sed -n '/Sources:/,/Filters:/p'` (an rk817 mic source,
   not muted); `XDG_RUNTIME_DIR=/var/run/0-runtime-dir arecord -q -D default -d 3 -f S16_LE -r 44100 -c 1
   /tmp/blow.wav` while blowing, then its RMS with `python3 -c "import wave,struct,math;w=wave.open('/tmp/blow.wav');
   d=w.readframes(w.getnframes());s=struct.unpack('<%dh'%(len(d)//2),d);print(math.sqrt(sum(x*x for x in s)/len(s))/32768)"`
   (a blow: > 0.05; < 0.002: silent). Silent: `amixer -c <rk817 index> contents | grep -A4 -i capture`, raise *Mic
   Capture Gain* / *Master Capture Volume* with `amixer cset`, and then the fix is a `wpctl set-volume` / amixer line
   in `session.sh`'s audio block (next to the 48 kHz PipeWire setting) plus ROCKNIX's UCM, to report upstream.
3. **H2, the gate.** Repeat step 1 with `DSFLIP_MIC_GATE=0`: if the candle now goes out, the gate was the cause
   (expected from the logs); then find the setting that keeps it out and gives 0 false presses in 2 minutes of the
   game's music at the handheld's full volume: try `DSFLIP_MIC_GATE=1.5`, then `DSFLIP_MIC_COUPLING_MAX=1` with the
   gate at 3. Note the debug lines' `coup` and `bleed` during music for the record. The fix is the found values as
   the defaults in `src/audio.c` (`envf("DSFLIP_MIC_GATE", ...)`, `envf("DSFLIP_MIC_COUPLING_MAX", ...)`).
   **The leak as loud as a blow.** When every factor either misses blows or presses on music (the logged leak says
   it will), read `lf` from the `10 s:` lines of two runs, one blowing with the game paused or its volume down and
   one music only: a blow should sit at 0.7-0.95, music lower. Pick r between the two and run `DSFLIP_MIC_GATE=1
   DSFLIP_MIC_LF=<r>` (or `DSFLIP_MIC_GATE=0 DSFLIP_MIC_LF=<r>`) through the candle and the 2 minutes of music; the
   fix is those two as the defaults. If the two ranges overlap, the share needs another corner frequency (`lpa` in
   `src/audio.c`, 0.035 = 250 Hz, 0.014 = 100 Hz) or real echo cancellation, and that is the finding to report.
4. **H5, flicker.** `DSFLIP_MIC_HOLD_MS=150` (then 300): the `PRESS` count per blow drops to 1-2 and the candle goes
   out. The fix is that value as `envf("DSFLIP_MIC_HOLD_MS", ...)`'s default.
5. **Rebuild** once the defaults are known: in a SuperDrastic checkout of branch `claude/1-6-prep-work-kwq9lc` (or
   `git am dsflip/superdrastic-0.5.0-beta.1-rocknixds.6.patch` on tag `v0.5.0-beta.1`), bump `VERSION` to
   `0.5.0-beta.1-rocknixds.6`, `sh build.sh <arm64 sysroot>` and `sh package.sh` (the `SUPERDRASTIC` file's comment
   names the toolchain), ship the tarball in `dsflip/`, regenerate the patch (`git format-patch --stdout
   v0.5.0-beta.1..HEAD`), pin version and sha256 in `SUPERDRASTIC`, and test it with `RGDS_SRC=<checkout>
   sh install.sh` on the device (the installer takes `dsflip/superdrastic-<ver>-aarch64.tar.gz` from the checkout by
   itself; without `RGDS_SRC` it fetches the latest release and compares the tarball against that release's pin, and
   dies; `RGDS_SUPERDRASTIC=<tarball>` is only for a package that is not in `dsflip/`). Then `systemctl
   unset-environment DSFLIP_MIC_DEBUG DSFLIP_MIC_GATE
   DSFLIP_MIC_COUPLING_MAX DSFLIP_MIC_HOLD_MS DSFLIP_MIC_KEY DSFLIP_MIC_LF` and run the acceptance with no switches
   set.
6. **Medium and low.** Repeat the candle at *medium*; note whether *low* can work at all with music (if not, say so
   in the README's microphone paragraph: *high* or *medium*).

**Acceptance:** with no switches set and the shipped defaults, *microphone sensitivity* at *high* and at *medium*, a
real blow puts out the candle in Phantom Hourglass on the first or second try, and 2 minutes of the game's music at
full volume with nobody blowing give `0 presses` in every `[mic] 10 s:` line. Then close issue 26 with the log
excerpt, and add the found defaults to the release notes' microphone section.

## 4. Wi-Fi online play (Nintendo WFC)

**Parked for 1.6** (section E): ES no longer offers *wfc dns*, `session.sh` doesn't export it, and libdsflip turns the
hook on only with the `DSFLIP_WFC` test switch (`DSFLIP_WFC_CONFIG=1` reads `nds.wfc_dns` again). The steps below
still apply with `systemctl set-environment DSFLIP_WFC=kaeru`; where they say "the ES option *wfc dns*", use that.
The work in progress since then is SuperDrastic `1.6-prep`'s last commit (01269ed, outside the 1.6 package).

**Nothing in this section has run on a handheld.** DraStic has no Wi-Fi emulation: its wifi register handlers are
stubs. The SuperDrastic package (since `0.4.0-beta.2-rocknixds.3`; in 1.6 `0.5.0-beta.1-rocknixds.6`, source in
`dsflip/superdrastic-0.5.0-beta.1-rocknixds.6.patch`) carries the port of
ROCKNIXDS's unmerged `origin/cursor/drastic-wfc-dns-24ad` (b1a4564): `src/wifi.c` replaces DraStic r2.5.2.2's wifi
load/store handler tables (at fixed offsets, for build id `7a5e0e5fc6e52e6e8f5499c3d4d667ef51db0748` only), answers
the game as an open access point named `rocknixds`, hands it the chosen DNS server over DHCP and carries the game's
traffic over the handheld's own sockets (`src/wfcnet.c`: DHCP, ARP, ICMP, UDP NAT, client-only TCP NAT). The DNS
server is what makes it online play: the game resolves `*.nintendowifi.net` through it and lands on a community
replacement for Nintendo's servers (shut down in 2014). No patched ROM and no account are needed on any of them.

**Done on the host.** The port cross-builds warning-free (`sh build.sh <arm64 sysroot>`) and its host test passes
(`sh build.sh wfc_test`: CRC, beacon/probe/auth/assoc frames, the RX ring, DHCP with the DNS option, ARP, ICMP, UDP
and TCP NAT over loopback). Three things a review against melonDS found are already fixed in the shipped package: the
gateway answered ARP probes for the DS's own address (a DHCP client would decline its lease), RX records left out the
FCS and ignored `W_RXLEN_CROP`, and the hook assumed DraStic stores `W_IF` as written (the layout line now reports
`IF store plain` or `write-1-to-clear` and adapts). ROCKNIXDS side: the ES option *wfc dns* (`nds.wfc_dns`: off /
Kaeru WFC / WiiLink DNS / AltWFC), `session.sh` exporting `DSFLIP_WFC` (`wifi: ...` in last-session.log), the README
paragraph, the es-features test, the package pinned in `SUPERDRASTIC`. The library also reads `nds.wfc_dns` and
`nds["<game>.nds"].wfc_dns` from `system.cfg` itself, so `DSFLIP_WFC=kaeru` in the environment (`systemctl
set-environment`) or the ES option both work. With the option off nothing is hooked: the package is safe to ship.

**Not verifiable on the host** (each has a step below): DraStic's build id and the six table pointers (no DraStic
binary here); the ARM7 IRQ path (`STATE_AT`, `ARM7_PEND/HALT/ALERT` are guesses that only the device can confirm);
whether Nintendo's Wi-Fi library accepts the hook's register semantics (a review against melonDS and dswifi found
two likely mismatches still open: TX fired only on `W_TXREQ_SET` writes, `W_POWERSTATE` bit 9 forced set, and
the beacon/compare timers gated on `W_RXCNT`); whether DraStic
keeps the game's saved WFC connection across launches; DraStic's DS MAC address (a MAC shared by every unit is
already banned on Wiimmfi); and whether any server is alive, because this host's resolver intercepts UDP/53 (all
four servers "answered" Nintendo's dead AWS hosts) and every server site is proxy-blocked. Known gap on top: the
UDP NAT is symmetric, so logins, lobbies and the GTS can work while races and battles between players (GameSpy
NAT negotiation, error 86420) will not until it is made full-cone.

### Servers

Status is from 2025-2026 web sources (gbatemp's "List of every known Nintendo WFC DNS", Delta 1.7's server picker,
the melonDS docs, the dwc_network_server_emulator wiki); none was probed. All are DNS-only: the game's own
*Auto-obtain DNS* gets the address from libdsflip's DHCP.

| Server | DNS | Status | DS games | Notes |
|---|---|---|---|---|
| **Kaeru WFC** (the one to try first) | `178.62.43.212` | reported alive | Wiimmfi's 300+ DS titles: Mario Kart DS "works flawlessly", Pokémon Gen IV/V GTS and battles via Poké Classic Network on the same DNS | hackless DNS + SSL offload in front of Wiimmfi, so Wiimmfi's rules apply (cheat detection, bans, MAC-hopping detection over 42 days) — https://kaeru.world/projects/wfc |
| WiiLink DNS → Wiimmfi | `167.235.229.36` | reported alive since July 2024 | same backend as Kaeru | the branch's "WiiLink" choice; it is a DNS operator in front of Wiimmfi, not WiiLink WFC; moved twice in 2024 — https://wiilink.ca/guide/dns/ |
| WiiLink WFC | `5.161.56.11` | reported alive | Mario Kart DS, Animal Crossing WW, Pokémon D/P/HG/SS pages; its own player pool | open-source wfc-server; not in the menu yet: `DSFLIP_WFC=5.161.56.11` — https://wfc.wiilink24.com |
| AltWFC / WFZwei | `172.104.88.237` | "mostly abandoned" (2025) | DS + Wii, unpatched DS | keep last or drop — https://github.com/barronwaffles/dwc_network_server_emulator/wiki/List-of-Servers |
| Wiimmfi direct | `95.217.77.181` | alive, Wii-oriented | 300+ DS games | DS players are sent to Kaeru/WiiLink DNS; raw address only |
| NewWFC | `89.117.58.143` | alive | Mario Kart DS only | cheats allowed; not offered — https://newwfc.xyz/ |
| dead: `164.132.44.106` (old RC24, in old DS guides), `167.86.108.126`, `185.82.22.28`, `185.59.132.99`, `185.82.21.64`, Twilit `34.66.49.81`, BenFi `24.218.177.103` | | do not use | | https://gbatemp.net/threads/list-of-every-known-nintendo-wfc-dns.661049/ |

Default: the option ships **off** (stock DraStic). When on, Kaeru WFC first.

### On the device

Logs: `/storage/.config/drastic/dsflip/dsflip.log` (every line of the hook starts with `[wfc]`),
`last-session.log` (`wifi: ...`), `drastic.out`. Each step says what to try first when it fails; stop and write
down what you saw where it says stop.

0. **The build.** Installing this branch (the command at the top) puts the `.2` package in place, with the ES option.
   Check: `grep -a -c "online via %s" /storage/.config/drastic/dsflip/libdsflip.so` prints `1` (`0` = an older
   library). To try a library built by hand instead: back it up (`cp libdsflip.so libdsflip.so.bak`) and copy the new
   `libsuperdrastic.so` over `libdsflip.so`. If a game then fails to start at all: put the `.bak` back and report
   dsflip.log's first lines.
1. **The DraStic binary.** (If `drastic.real` is missing, the stock launcher is still in place: start a DS game once,
   then `sh /storage/.config/drastic/dsflip/install.sh`.)
   ```sh
   python3 - <<'PY'
   d=open('/storage/.config/drastic/drastic.real','rb').read()
   i=d.find(b'\x04\x00\x00\x00\x14\x00\x00\x00\x03\x00\x00\x00GNU\x00')
   print('build id', d[i+16:i+36].hex() if i>=0 else 'not found', '| type', int.from_bytes(d[16:18],'little'), '(3 = PIE)')
   PY
   ```
   Expected: `build id 7a5e0e5fc6e52e6e8f5499c3d4d667ef51db0748 | type 3 (3 = PIE)`. A different id: **stop**,
   nothing else can work (the table offsets are for that build only); send the id, `ls -l` and `md5sum` of
   `drastic.real`. Type `2`: report it (the load-bias code assumes a PIE).
2. **The servers, from the handheld's network** (ROCKNIX on Wi-Fi; `ping -c1 1.1.1.1` works):
   ```sh
   for s in 178.62.43.212 5.161.56.11 167.235.229.36 172.104.88.237; do python3 - $s conntest.nintendowifi.net nas.nintendowifi.net gpcm.gs.nintendowifi.net <<'PY'
   import socket,struct,sys
   s=sys.argv[1]
   for name in sys.argv[2:]:
       q=b'\x124\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00'+b''.join(bytes([len(p)])+p.encode() for p in name.split('.'))+b'\x00\x00\x01\x00\x01'
       k=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);k.settimeout(3)
       try: k.sendto(q,(s,53));d=k.recv(512)
       except Exception as e: print(s,name,'NO ANSWER',e);continue
       n=struct.unpack('>H',d[6:8])[0];i=12
       while d[i]: i+=d[i]+1
       i+=5;a=[]
       for _ in range(n):
           if d[i]&0xc0==0xc0: i+=2
           else:
               while d[i]: i+=d[i]+1
               i+=1
           t,c,ttl,l=struct.unpack('>HHIH',d[i:i+10]);i+=10
           if t==1: a.append('.'.join(map(str,d[i:i+4])))
           i+=l
       print(s,name,'->',' '.join(a) or 'no A record')
   PY
   done
   ```
   Expected: every line answers, and **not** with `35.160.180.49`, `44.229.101.156`, `54.201.103.197` or
   `44.245.86.170` (Nintendo's dead AWS hosts: that answer means the query was not redirected, which is what this
   sandbox saw for all four). Then
   `curl -s -o /dev/null -w '%{http_code}\n' -H 'Host: conntest.nintendowifi.net' http://<conntest ip from the Kaeru line>/`
   must print `200` (what the DS's connection test needs). All `NO ANSWER`: the router blocks UDP/53 to outside
   resolvers or the handheld is offline; try the loop from a PC on the same LAN. One server fails while the others
   answer: it is down or gone; remove its choice from `es-features.sh` before release. Record the answers here.
3. **First launch.** `systemctl set-environment DSFLIP_WFC=kaeru DSFLIP_WFC_DEBUG=1` over ssh (the game unit
   inherits it; the ES option *wfc dns* = Kaeru WFC does the same without the debug log). Start Mario Kart DS or
   Pokémon HeartGold from ES, then `grep '\[wfc\]' /storage/.config/drastic/dsflip/dsflip.log`. Expected:
   `[wfc] online via Kaeru WFC (178.62.43.212)`; last-session.log says `wifi: kaeru`.
   `online left off: DraStic build id is not r2.5.2.2`: step 1 again. `online left off: wifi handler table was not
   where r2.5.2.2 keeps it`: the build matches but the pointers at base+0x15d3a0/0x15d3b8 are not the expected
   ones: **stop**, copy `drastic.real` to the host and look at those offsets (`llvm-objdump -s
   --start-address=0x15d3a0 --stop-address=0x15d3d0 drastic.real`) before touching `wifi.c:40-53`. No `[wfc]`
   line at all: `grep wifi: last-session.log` (the setting never reached the environment), step 0's grep (wrong
   library), or the game died within 300 ms (`drastic.out`).
4. **Scan for the access point.** In the game: Nintendo WFC Setup (Nintendo Wi-Fi Connection Settings) > Connection
   1 > Search for an Access Point. Expected in the log: `[wfc] layout ok (reg ok, mac ok, IF store plain)` (or
   `write-1-to-clear`: fine too, the hook adapts; note which) and, with debug, several
   `[wfc] tx fc 0040 len N` lines (probe requests). On screen: `rocknixds` with an open (no key) lock.
   - `[wfc] layout mismatch (reg no, ...)` or `(..., mac no)`: the register/RAM offsets are wrong for this binary:
     **stop**, report the line.
   - no `[wfc] layout` line at all: the game never touched the wifi registers through the hook. First suspect:
     the power-up handshake (`W_POWERSTATE` bit 9 forced set, `wifi.c:384`; melonDS treats bit 9 as "powered
     off"): apply that fix, then check whether DraStic dispatches wifi accesses elsewhere.
   - `layout ok` but no `tx fc` line: the firmware arms `W_TXREQ_SET` once and only writes `W_TXBUF_LOCn`: apply
     the TXREQ-shadow fix (`wifi.c:430-433`).
   - `tx fc 0040` lines but an empty list: the probe response does not reach the game. `[wfc] no ARM7 state at
     ...` or `[wfc] ARM7 io ..., expected mem+0x23070` in the log = the IRQ offsets are wrong: **stop**, report.
     Otherwise the probe response is in the ring but the stack rejects it: log `W_RXLEN_CROP` (add a debug line
     for register 0x0DA reads/writes) and compare the RX header with melonDS's `Wifi.cpp` FinishRX.
   - `rocknixds` listed with a closed lock: a saved connection holds a WEP key: Options > Erase Nintendo WFC
     Configuration, rescan.
5. **Join and test.** Select `rocknixds`, keep *Auto-obtain IP Address* = Yes and *Auto-obtain DNS* = Yes
   (Advanced Setup), save, Test Connection. Expected log, in order: `[wfc] associated to rocknixds, dns
   178.62.43.212` (plus a 4 s toast "Wi-Fi / Kaeru WFC" on the panel), `[wfc] dhcp offer, dns 178.62.43.212`,
   `[wfc] dhcp ack, dns 178.62.43.212`, with debug `[wfc] dns query N bytes` / `[wfc] dns reply N bytes`, then
   `[wfc] tcp <ip>:80 from :<port>`. On screen: "Connection test successful".
   - 52000-52003 (no IP) with `associated` but no `dhcp offer`: the DHCP DISCOVER never arrived as a data frame:
     look for `tx fc 0208` lines; none = the data path dies before the hook (RX/TX fixes above); `fc` with bit
     `0x40` = WEP frames, dropped on purpose (erase the WFC configuration).
   - `dhcp offer` but no `dhcp ack`, 52000: the DS declined the lease. The gateway no longer answers ARP probes for
     10.13.37.20; with debug on, look for the DECLINE (a `dhcp` line) and what the DS sent before it.
   - `dhcp ack` but no `dns query`: the saved connection has a manual DNS: set Auto-obtain DNS = Yes or erase
     the configuration.
   - `dns query` but no `dns reply`, 52100-52103: this network cannot reach UDP/53 on the server (step 2 again
     from here), or the reply holds Nintendo's AWS addresses (wrong server).
   - `tcp <ip>:80` logged but the test fails: the conntest page did not return 200: try `DSFLIP_WFC=167.235.229.36`
     or `5.161.56.11` and compare.
6. **Persistence and the MAC.** Quit, start the game again: Connection 1 should still show `rocknixds` and Test
   Connection pass without a new scan. If Connection 1 is empty, DraStic does not keep the firmware's WFC area:
   note it (setup every launch; the friend code changes too) and send `ls -la /storage/.config/drastic` so the
   firmware file can be found. Then Options > System Information: write down the MAC address, and do the same on
   a second handheld. `00:09:BF:11:22:33`, or the same MAC on both units: **do not enable any server by default**
   (a shared identity is banned on Wiimmfi/Kaeru: 20102/23914/23915/20104); the per-device MAC becomes a blocker.
7. **A real login.** HeartGold/SoulSilver: Pokémon Center upstairs > GTS; Mario Kart DS: Nintendo WFC > Worldwide.
   Expected log: `tcp <ip>:80` (NAS login), `tcp <ip>:29900` (GameSpy gpcm), `tcp <ip>:28910` (server browser).
   On screen: the GTS loads / the lobby searches for opponents. 20100 or 20110: the game reached a wrong or dead
   server (manual DNS in the saved connection, or the server is down: step 2). 20102/23914/23915: banned (step 6's
   MAC). 20104: identifier already in use (another unit with the same MAC is online). Other 23xxx = NAS HTTP status
   + 23000 (23502 game server offline, 23800 game unsupported there). 60100: stale profile data (erase the game's
   WFC settings). 61020/61070: profile server unreachable (server side).
8. **Player-to-player (expected to fail on this build).** Start a Worldwide race or a GTS trade/battle. Opponents
   found but the race never starts, or 86420: the symmetric UDP NAT, the known gap, not a device problem. Log it.
   If it works, say so: the gap is smaller than assumed.
9. **Regression with the option off.** `systemctl unset-environment DSFLIP_WFC DSFLIP_WFC_DEBUG`, *wfc dns* off or
   Auto, start the same game: no `[wfc]` lines, `wifi: off` in last-session.log, the game's Wi-Fi menu behaves as
   on stock DraStic (no AP found). Play 5 minutes of a non-Wi-Fi game on the WFC build too. Restore the pinned
   library if step 0 used the quick swap.
10. **Collect:** dsflip.log (and .1-.3), last-session.log, drastic.out, `dmesg | tail -n 50`, the outputs of steps 1
    and 2, the MAC from step 6, a photo of each error code, and which game/server combination reached which step.

### Error codes (DS, replacement servers)

20100 AP joined but WFC servers unreachable (DNS not redirected, server down) · 20102 banned · 20103/20104 console
identifier broken / already in use · 20110 "service discontinued" (reached Nintendo's shutdown page: DNS not
redirected) · 23xxx NAS login HTTP status + 23000 (23302 captive portal, 23502 game server offline, 23800 game
unsupported, 23913/23914/23917 banned, 23915 too many MAC changes) · 31020 download server failed · 51300-51399
cannot connect to the AP · 52000-52003 no IP (DHCP failed: libdsflip's NAT did not answer) · 52100-52103 IP but no
internet (DNS/conntest failed) · 52200-52203 too many attempts, reboot · 60100 profile error · 61020/61070 profile
server unreachable · 86420 peer-to-peer connection failed (the NAT gap) · 91010 server maintenance or kicked.

**Acceptance:** step 1 prints the expected build id and type 3; a Kaeru launch logs `online via` then `layout ok
(reg ok, mac ok)` with no ARM7 error line; the scan lists `rocknixds` and Test Connection succeeds with
`associated`, `dhcp offer`, `dhcp ack` and `tcp <ip>:80` in the log, and the saved connection survives a restart (or
this file records that it does not); step 2 from the handheld's network answers for every server kept in the menu
with non-Nintendo addresses and a 200 conntest, and failing servers are removed; one real login works (GTS loads or
the MKDS lobby searches, `tcp :29900`) with no 2xxxx error, and the P2P result is recorded either way; the MAC is
not `00:09:BF:11:22:33` and differs between two units, otherwise no server ships enabled by default; with the
option off there are no `[wfc]` lines and no regression; `sh tests/run.sh` and `sh build.sh wfc_test` pass on the
host.
