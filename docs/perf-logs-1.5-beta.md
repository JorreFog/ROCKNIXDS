# The 1.5 beta performance logs

What the players' uploaded sessions (`device-logs` branch, `docs/data/device/`, imported up to 2026-10-02) say,
read with [`tools/perf-logs.py`](../tools/perf-logs.py). 23 sessions from 5 RG DS units, all on the RG DS beta
(`1.5-beta (beta)`). The RG DS Plus beta has no uploader, so none come from a Plus.

## Two bugs, both fixed for 1.5

**From beta 5 on, the logs are empty.** Every session from SuperDrastic 0.3.0-beta.3 (14 of 23, all five
units) stops logging 3-6 s in, after `[dsflip] the display stayed busy for 3 s -> passthrough`, while the game
runs on (one for 77 minutes). beta.3's in-game volume card starts `pactl subscribe` and, on each volume change,
`wpctl get-volume`, through popen(). Those shells inherited DraStic's `LD_PRELOAD`, so libdsflip started in each
of them. Each tried for 3 s to take the display the game already had, rotated `dsflip.log` away from the game,
and wrote "passthrough" over the session's verdict in `/tmp/dsflip-state`. If session.sh reads that verdict
before the game's own "ready", it stops the game with "couldn't take over the screens". The public SuperDrastic
(0.3.0-beta.2, "the RG DS Plus round", the Plus beta's) has a guard for this. The RG DS's beta.3 build was made
from a tree without it.

Fix (RG DS): `dsflip/device/preload-guard.so`, listed after libdsflip in `LD_PRELOAD`, removes `LD_PRELOAD`
from DraStic's environment before anything runs, so nothing DraStic starts loads libdsflip. The volume card
stays.

**One unit in four never ran the CPU governor.** Device `63957a` played 1.5 hours of GTA Chinatown Wars,
Professor Layton and Mario Kart. Its clock floated between 816 and 1992 MHz (64% of the time at 1992), and the
logs have no `[cpugov]` line at all, on balanced and on battery alike. cpugov sets the clock through
`scaling_max_freq`, which only pins it under the performance governor, and it switches itself off under any
other. The RG DS Plus nightly showed the same thing with its ondemand default. Fix (both): the session runs on
performance, and restore.sh puts the menu's governor back.

## What the sessions with data say (beta 2-4, SuperDrastic 0.3.0-beta.2 RG DS build)

| Unit | Profile | Shader | Game | Minutes | fps | Dropped/s | Repeated/s | CPU | Battery |
|---|---|---|---|---|---|---|---|---|---|
| e66e45 | battery (3-frame queue) | lcd3x | Pokemon Black 2 | 2.7 | 58.8-59.6 | 0.00-0.01 | 0.50-1.43 | 1104 MHz 73-88% | ~1000 mA |
| e66e45 | battery | lcd3x | Mario Kart DS | 3.2 | 59.78 | 0 | 0.34 | 1104 83%, 816 12% | ~1020 mA |
| e66e45 | battery | lcd3x | HeartGold | 4.0 | 59.84 | 0 | 0.21 | 1104 95% | ~990 mA |
| e66e45 | battery | lcd3x | Dragon Quest Monsters Joker | 18 | 59.74 | 0.03 | 0.37 | 1104 68%, 816 30% | ~965 mA |
| 63957a | balanced (cpugov off, see above) | none | GTA Chinatown Wars (x3) | 91 | 59.63-59.84 | 0 | 0.31-0.47 | 1992 64%, spread 816-1800 | ~700-720 mA |

- **Pacing is clean.** Almost no dropped frames (at most 0.03 a second), 0.2-0.5 repeated frames a second, and
  10-30 late latches an hour on the unit that logged them. The battery profile's deep queue holds 59.6-59.8 fps
  at 1104 MHz in Black 2, HeartGold, Mario Kart and DQM.
- **The battery profile's 816 MHz step bounced.** In 18 minutes of DQM, cpugov changed clock 101 times: 48
  climbs from 816 for one "heavy frame" and back down a few seconds later. That is what beta 5 changed (wait for
  a second heavy window before climbing). No log since then can confirm it, because of the bug above. The first
  logs from 1.5 will.
- **Battery current depends on the screens and the shader more than on the clock.** The unit at 1104 MHz with a
  shader drew ~1000 mA. The one at mostly 1992 MHz without a shader drew ~700 mA. These are different units, so
  brightness and battery differ too; neither number is a profile comparison.
- **"Below 59.5 fps" for 14-22% of seconds** is mostly the once-a-second counter catching 59 presents in a
  second, not visible stutter. The repeated-frame rate is the measure of hitches.

## For the next round of logs

- The RG DS Plus has no uploader yet. Its beta has its own measurements in the README (CPU placement,
  PipeWire at 48 kHz, the latch margin).
- `perf-session.py` keeps only the dsflip lines it sees after it starts, and the session's first lines
  (`remembered drops`, `ready`) were missing on one unit. Starting the sampler before DraStic, as session.sh
  does, should catch them. If they're still missing in 1.5 logs, start reading at the log's beginning instead.
