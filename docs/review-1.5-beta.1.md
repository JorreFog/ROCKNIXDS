# ROCKNIXDS 1.5 beta 1: bugs and improvements

Review of `beta` at 1.5 beta 1 (SuperDrastic `0.3.0-beta.1`) against 1.4, read against SuperDrastic's own integration notes and `cpugov.c` / `dsflip.c` at that tag. Nothing here was re-run on the RG DS.

The beta's new pieces are the power profiles, the menu lockdown and updater, canvas-ds, the media-tool matching changes, and `rgds-monitor`. The findings below are the ones that change what a player or a test actually gets.

## 1. The power-profile cap is not a cap until the governor feels like stepping down

`session.sh` turns the ES choice into `DSFLIP_CPU_MAX` (balanced 1416000 kHz, battery 1104000) plus `DSFLIP_QUEUE` / `DSFLIP_QUEUE_WAIT`. It never writes `scaling_max_freq` itself. SuperDrastic's governor reads the max as a bound on the clock it *chooses*, and takes the clock it *starts from* out of sysfs:

```c
cur = rd_int(POL "scaling_max_freq");          /* still 1992 MHz: ROCKNIX's performance governor */
fmax_ = getenv("DSFLIP_CPU_MAX") ? atoi(getenv("DSFLIP_CPU_MAX")) : rd_int(POL "cpuinfo_max_freq");
fmin_ = getenv("DSFLIP_CPU_MIN") ? atoi(getenv("DSFLIP_CPU_MIN")) : 1104000;
```

`cur` is only lowered by `step_down()`, one OPP at a time, after 2 s of windows where fps is already ≥59, the resolution memory has loaded, and nothing else asked for a raise. The steps do not stop at `DSFLIP_CPU_MAX`. From 1992 the walk is 1800, then 1608, then 1416, then 1104: about 6 s to reach the balanced ceiling, about 8 s to reach battery, and only if the game is already smooth. fps under 59 cancels the walk (`low` is reset).

A drop during that walk freezes it there too. A drop sets `why`, and the "go up" branch does not fire because `fit(cur + 1)` is clamped to `fmax`, which is *below* the current clock, so `want > cur` is false. The step-down branch is the `else`. The clock stays at 1992, 1800, or 1608 for as long as frames keep dropping, which is the situation the profile was supposed to ride out at a lower clock.

Battery is coherent once it arrives: `fmin` and `fmax` are both 1104, so it cannot climb. Balanced is not the configuration that was measured. The README's number (Black 2, fixed 1416 MHz, 2-frame queue, 20 ms wait, 0.07 hitches/s) is a pinned clock. The profile lets the governor move anywhere in 1104–1416, and SuperDrastic 0.3.0-beta.1 says the wait is "meant for a capped clock (`DSFLIP_CPU_MAX`): the governor doesn't suit it yet." Inside the range, a light drop still does `want = fit(cur + 1)`. The "slow" branch (`fps < 58.5`) does the same, including if holds bunch presents into a short window. The queue was added so a late frame would not need a higher clock. The governor still treats one as a reason to climb, up to the cap.

The remembered strikes make this worse across profiles. The memory file is `<rom>.<shader>.<1x|2x>` with no queue depth in the name. Strikes earned on performance (1-frame queue, no wait) block `step_down()` to that clock on balanced and battery, where the same clock is fine. Forgiveness earned on battery's 3-frame queue clears a strike performance still needs. Bans are not checked on the way *up*, so they only affect the step down.

**Fix, in `session.sh`, before DraStic is started.** Apply the ceiling to sysfs so `cur` already equals it when the governor thread starts. `fit()` will not climb past it after that.

```sh
case "$PROF" in
    performance) Q=1 QW=0 CMAX=$(cat /tmp/dsflip-cpu-max) ;;
    battery)     Q=3 QW=20 CMAX=1104000 ;;
    *)           PROF=balanced Q=2 QW=20 CMAX=1416000 ;;
esac
export DSFLIP_QUEUE=${DSFLIP_QUEUE:-$Q} DSFLIP_QUEUE_WAIT=${DSFLIP_QUEUE_WAIT:-$QW}
export DSFLIP_CPU_MAX=${DSFLIP_CPU_MAX:-$CMAX}
# performance writes the saved hardware max back; a previous battery session must not linger in sysfs
echo "$DSFLIP_CPU_MAX" > $CPU/scaling_max_freq
```

Keep `DSFLIP_*` already set in the environment as the override, including an explicit `DSFLIP_CPU_MAX` for tests. For battery, also export `DSFLIP_CPU_MIN=$CMAX` if you want the pin to survive a governor that ignores a max equal to the default floor.

**Fix, in SuperDrastic, before calling balanced done.** Two changes:

- Do not step up on a light drop, or on `fps < 58.5`, while `DSFLIP_QUEUE` > 1 and the hold (`st_qwait`) is what absorbed the frame. A CPU-bound drop (`BLAME_PEAK` / `TARGET`) still steps up, inside the cap.
- Put the queue depth in the memory file name (`<rom>.<shader>.<2x>.q<N>`). Old files keep working as the `q1` case; balanced and battery stop inheriting performance's bans.

Until the governor knows about the queue, the honest balanced profile is a pin, not a range: `DSFLIP_CPU_MIN=DSFLIP_CPU_MAX=1416000`, which is the configuration the README already quotes. Say so in the ES choice if it stays a range ("up to 1416 MHz" is then true only after the governor has settled, and it will still chase light drops up to that ceiling).

The ES strings and the README disagree with the code on battery. The code holds DraStic up to 20 ms on a full queue, same as balanced. The menu says "+2 frames" and the README says "+33 ms" and does not mention the hold. Either drop `QW` to 0 for battery or add the hold to both strings. Balanced's menu line ("+1 frame") has the same hole; the README at least says "holds DraStic for a moment."

`dsflip/device/es-features.sh` should set `value="power_profile"` and `value="resume_on_quit"` on those `<feature>` tags. ES derives the key from the name today (`"power profile"` → `power_profile`, in `CustomFeatures::loadCustomFeatures`), which is why session.sh's grep works. A renamed label would silently stop matching and fall through to balanced, with resume stuck on (unset means on).

## 2. "Stable" installs `main`, and the id on disk is not the tree that was installed

`rocknixds-update install` runs the branch's `install.sh`:

```sh
B=main; [ $CH = beta ] && B=beta
curl -fsSL https://raw.githubusercontent.com/$REPO/$B/install.sh | RGDS_BRANCH=$B sh
```

Stable therefore installs whatever `main` is, not the release the check just offered. A commit on `main` between tags is delivered as a stable update. The check compares release tags, so the menu can say "up to date" while the files are ahead of that tag, or offer a tag whose tree is not what the installer will unpack.

`install.sh` records the id *after* the unpack, with a second GitHub call: latest release tag on `main`, latest commit on any other branch. That is not the tarball just unpacked. A commit pushed during the install, or a failed second request, writes the new sha or `unknown`. `unknown` never equals the next check, so the timer announces an update for the bits already running. There is no `pipefail`: if `curl` fails, `sh` reads an empty script, exits 0, and the menu has already been told `STARTED`.

Switching the menu from beta to stable compares a commit sha with a tag. They are never equal, so the stable release is offered as "A ROCKNIXDS UPDATE IS AVAILABLE" even when it is older than the beta on the device. Installing it is a downgrade with no warning.

**Fix.**

- Resolve the id first, then download that id, then store it. Stable: the tag from `releases/latest`, and the tarball `https://codeload.github.com/$REPO/tar.gz/refs/tags/$TAG` (or the tag's commit sha). Beta: the sha from `commits/beta`, and `tar.gz/$SHA`, not the moving branch name. `installed-id` is that same string, written only after the unpack succeeds.
- `set -o pipefail` on the installer pipe. `rocknixds-update install` should exit non-zero when `systemd-run` fails, and the unit should POST `/notify` if `install.sh` exits non-zero so a failed update is visible after the menu comes back.
- In the menu, if the channel's id simply differs, say which way it goes. A beta sha and a stable tag are not ordered; keep the previous channel's id and show "Switch to stable and install &lt;tag&gt;" instead of "update" whenever the installed id is a 40-hex sha and the channel is stable.

`themes.allow` is fail-open, in the same lockdown. `es-rgds-lockdown.patch` treats an empty allow-list as "no filter":

```cpp
if (allowed.empty() || it->first == selectedSet->first || std::find(...) != allowed.end())
    themeList.push_back(it->first);
```

`readAllText` of a missing `themes.allow` is empty, so every installed theme is offered, including ones the lockdown exists to keep off the dual-screen layout. A half-finished install (theme and patched ES in place, crash before `themes.allow` is written) boots into that. Split the cases: file missing or unreadable while locked → only `dii-ess-aye` plus the current theme; file present → only its lines, plus the current theme so a theme that was removed from the list does not get dropped on the floor.

The cooling profile is hidden by the same patch and is not on the list in the README (layout, governors, GPU driver, video mode, rotation, DTB overlays, developer options, factory reset, emulator reset, DS emulator choice). It does not break the panels. Leave it visible so a hot session can still be set to aggressive without `unlocked`.

## 3. Install and uninstall throw away per-game emulator choices, and can delete canvas-ds before the new copy exists

The lockdown deletes per-game DS emulator/core lines unconditionally:

```sh
sed -i '/^nds\(\[.*\]\)\{0,1\}\.\(emulator\|core\)=/d' $SYSCFG
```

`system.cfg` is backed up only inside the hires block (`--no-hires` skips it). The selective uninstall restores `nds.hires_3d` and nothing else, so those lines are gone even when a backup exists. `--restore-files` puts the whole file back, which also reverts the RetroAchievements login and every other change since install.

**Fix.** `backup_once $SYSCFG` before the sed, on every install. On uninstall, copy back only lines matching that same regex from the backup, the way `nds.hires_3d` is already restored. While there, delete the `resume on quit` and `power profile` feature blocks from `es_features.cfg`; the uninstall sed only strips `ds-*` shader choices, so stock ES keeps two options that no longer do anything.

canvas-ds is replaced with `rm -rf $C` and then `mv`. `backup_once` runs only for a copy that this installer did not download. If `mv` fails (unexpected archive layout, full disk), a theme this installer had already installed is gone and the rest of the install continues: under `set -e`, a failing command on the left of `&&` does not abort. Unpack to `$C.new`, and only then `rm -rf $C && mv $C.new $C`.

`theme-changed.sh` always uses the unit name `rocknixds-theme-restart`. A second theme change inside the ~60 s wait fails to start, and the first restart proceeds with the older name. Use a unique unit (`rocknixds-theme-restart-$NEW` is not unique either if the user bounces back; a timestamp is).

## 4. Media tool: the display-name fallback never runs, and a new RA id can be reported when ES refused it

```python
match, score = best_match(stem, lr.names(kind)) or best_match(name, lr.names(kind))
```

`best_match` returns a tuple. `(None, 0)` is truthy, so the `or` never calls the second match. A ROM whose file name is not a libretro name, but whose ES title is, stays "no match" for the box, the screenshot and the title screen. This predates the 1.5 retail-ranking change; that change never gets a chance to run on the display name. The retail-vs-demo sort itself is right: exact matches sort best-first, and the fuzzy dict keeps the later entry after a reverse sort, which is the better rank.

**Fix.**

```python
match, score = best_match(stem, lr.names(kind))
if not match:
    match, score = best_match(name, lr.names(kind))
```

`fill_cheevos_ids` POSTs `cheevosHash` / `cheevosId` and then sets `g["cheevosId"]` without looking at the HTTP status `push_meta` returns. `ra-fetch.py` does not read that local dict; it asks ES again. A rejected POST is logged as "RetroAchievements game &lt;id&gt;" and the strip is still missing. Keep the id only when the status is 200, and log the status otherwise.

The on-device hash matches rcheevos for a full retail header (first 0x160 bytes, ARM9, ARM7, 0xA00 of icon; SuperCard header skipped; ARM9+ARM7 over 16 MB rejected). It does not match the short-icon case: rcheevos zero-pads a short icon/title read out to 0xA00, and the script hashes only the bytes it got. Homebrew and trimmed dumps then disagree with ES's own hash, which is the check the tool is trying to reproduce. Pad that third read:

```python
blob = f.read(n)
if n == 0xA00 and len(blob) < n:
    blob += b"\0" * (n - len(blob))
m.update(blob)
```

`NON_RETAIL` still lets `(Hack)`, `(Translated)`, `(Overdump)` and `(Virtual Console)` sort with retail. Same treatment as Demo/Kiosk/Beta, or the next near-miss name will grab a hack's box the way Mario Kart grabbed the kiosk demo.

## 5. The monitor fights the idle menu, and it does not record what 0.3 added

`rgds-monitor.py` says the device side only reads files, and that this is why measuring doesn't change the measurement. The collector calls `http://localhost:1234/runningGame` on every sample where the pid is not DraStic. That is a request a second into ES for the whole time the menu is up. A launch on that API is main-thread work (the 1.4 power saver exists because those waited out the idle sleep); a plain GET may or may not be, and either way the log is no longer "files only." It also keeps a session from ending when ES still reports the game after DraStic has quit, which undoes the 6 s grace from the other direction.

**Fix.** Resolve the running game from the process table only. For a non-DS emulator, hit `/runningGame` on a slow period (a few seconds), not every sample, and don't let that path run during an idle-menu measurement. Keep the 6 s handover grace; it is the right fix for the gap where neither ES nor DraStic is visible.

The 0.3 log line is the one the beta needs, and the parser only keeps `present/s`, `dropped`, and `max-iv`:

```text
[dsflip] present/s=… dropped=… max-iv top=… bot=… us … drop-src=… drop-q=… drop-buf=… repeat=…
[queue] depth N, … | DraStic held …
```

`dropped` mixes source overruns, queue drops and buffer drops. `repeat` is the refreshes that showed no new frame, which is the hitch the power-profile measurements are about. `max-iv` parsing does match this line (`max-iv` is its own token, then `top=` and `bot=` in microseconds). Extend `parse_log` with `repeat`, `drop-src`, `drop-q`, `drop-buf`, and the `[queue]` hold count, and plot repeat next to fps. That is the chart that says whether balanced is holding DraStic or dropping frames.

Two smaller monitor bugs:

- A new session calls `st.clear()` after `parse_log` and before `Session.add`, so the first `present/s` line of every session is missing from the summary and the sparkline. Clear at the start of `handle`, or add the sample before clearing.
- A full SSE queue removes that subscriber and never puts it back. The page stays open and freezes. Drop the oldest queued message instead (`get_nowait` then `put_nowait`).

`Web.sessions` reads the last 4096 bytes and JSON-parses the tail. A sample line is the whole log batch; over 4096 bytes, an in-progress session disappears from the table (`continue` on `ValueError`). Seek back to the previous newline, then parse.

## 6. Smaller fixes

**ds-fsr at 2×.** [filters-1.5.md](filters-1.5.md) already measured it: at 2× the picture matches ds-crisp and the GPU time is 8–9× higher; at 1× it is the sharp filter closest to a real 2× render. `session.sh` still gives ds-fsr `performance` and the full GPU clock. In the shader, take the ds-crisp path when the scale is 2×, and name the ES choice "ds-fsr (for 1×)" so the default hires path stops paying for it.

**`hgpower.sh` with `GAME=b2`.** There is no `set -e`. A missing `Pokemon Black Version 2 (DSi Enhanced).nds`, or a savestate that is not `<that name>_0.dss`, prints a `cp` error and keeps going, then measures whatever `B2test.nds` already was or a DraStic that failed to load. Exit 2 when the source ROM or the slot-0 state is missing, before anything is copied. The "a game is running" check is doing the right job; it is still a check-then-act race with someone launching in ES, which is acceptable if the script fails closed when `dsflip-game` is active at `kmsrun` start too.

**README, "What libdsflip does".** The presenter is still described as a one-frame queue only. Profiles now set 1, 2 or 3, and `DSFLIP_QUEUE_WAIT` holds instead of dropping. Point that paragraph at the power-profile section so the two don't disagree.

**`tools/power.sh` / `hgpower.sh` GPU stanza.** The comment says to keep it in sync with `session.sh`, and it does not pass `DSFLIP_QUEUE` or the profile's CPU max. A profile measurement has to pass those variables by hand today and it is easy to measure "balanced" without the queue the profile actually sets. Default `hgpower.sh` to balanced's queue and cap unless `CPUMAX` / `DSFLIP_QUEUE` are passed.

## What looks sound

Checked and not a bug: the ES feature key really is `power_profile` / `resume_on_quit` when `value` is omitted; `DSFLIP_CPU_MAX` is kHz, so 1416000 / 1104000 are the right numbers; resume's SIGUSR1 path exits 137 and session.sh already treats that as a clean quit; the retail-before-demo sort on an exact normalised name is the correct direction; canvas-ds `0.32 × 1920 = 614` really does keep a centred 8:3 shot inside the top 640 px (a 0.35 width centred at 0.167 spills both off the left edge and onto the bottom panel), and the five `0.35 0.6` sites in `aspect-ratio-4-3.xml` at `ab3ba47ab8` are the screenshot and its video, not the logo; the DS emulator list is rewritten to drastic/drastic-sa on each boot; the update timer notifies once per id and does not mark the id notified when ES is down.
