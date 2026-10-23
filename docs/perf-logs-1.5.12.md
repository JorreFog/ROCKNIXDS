# The 1.5.1 to 1.5.12 performance logs

What the players' uploaded sessions say (`device-logs` branch, `docs/data/device/`, imported up to 2026-10-08),
read with [`tools/perf-logs.py`](../tools/perf-logs.py)'s rules (play seconds only: DraStic's menu and seconds under
10 presents are left out). 319 sessions from 41 RG DS units, 20.6 hours of play, ROCKNIXDS 1.5 betas to 1.6.1.
1.6.1 has one 4-second session, so these logs say nothing about it yet.

## By version

| | Sessions | Units | Play (s) | fps | Below 57 fps | ...busy, clock below its top | ...at the top clock | Dropped/s | Repeated/s |
|---|---|---|---|---|---|---|---|---|---|
| 1.5 betas | 39 | 6 | 8480 | 59.47 | 2.9% | 178 | 20 | 0.008 | 0.75 |
| 1.5 | 32 | 6 | 5573 | 58.54 | 8.2% | 325 | 40 | 0.045 | 1.86 |
| 1.5.1 | 63 | 14 | 16914 | 58.65 | 8.1% | 6 | 1320 | 0.066 | 1.74 |
| 1.5.2 | 15 | 7 | 1980 | 59.20 | 3.7% | 2 | 51 | 0.224 | 1.19 |
| 1.5.5 | 10 | 7 | 1816 | 58.92 | 5.6% | 0 | 93 | 0.463 | 1.65 |
| 1.5.7 | 51 | 11 | 11245 | 58.38 | 9.1% | 0 | 992 | 0.120 | 1.96 |
| 1.5.9 | 27 | 10 | 8315 | 57.04 | 14.3% | 2 | 1149 | 0.212 | 3.26 |
| 1.5.12 | 78 | 17 | 19738 | 50.88 | 36.9% | 4 | 7194 | 0.106 | 9.60 |
| 1.5.12 without f1aa93 | 66 | 16 | 10141 | 58.74 | 6.2% | 3 | — | 0.079 | — |

(1.5.6 and 1.5.8: 3 sessions, 149 s in all, left out.)

The 1.5 and 1.5.9 figures take in many seconds from a few units playing heavy games. 1.5.12's figures take in one
unit, f1aa93, almost entirely (below). Without f1aa93, every release since 1.5.1 sits at 58.4-59.2 fps.

## Confirmed: 1.5.1's governor fix works

1.5's report asked for this check: of the slow play seconds on 1.5.1 and later, how many had DraStic busy (a core or
more) while the clock sat below its top? The answer is 14 out of 11,000+ (1.5: 325 of 459; 1.5 betas: 178 of 245).
Slow seconds now happen at 1992 MHz. The governor isn't holding games back any more. What's left is games that are
heavier than the RG DS can run.

The soft profile bounds behave as designed. Battery-saver sessions running Platinum with lcd3x on 2437b8 go up to
1700-1992 MHz when the game needs it. Light games stay low: full-speed balanced sessions on 1.5.2+ have a median of
1438 MHz.

## The slow play left is CPU-bound, and two heavy setups cause most of it

- **Pokemon HeartGold at 2x, no shader, on f1aa93 (1.5.12, balanced): 9,600 s of play at ~40 fps.** This is 6,700 of
  1.5.12's 7,300 slow seconds. The clock is at 1992 MHz the whole time. DraStic uses 117-123% of a core, and the
  busiest core is pinned at ~100%. Its present intervals all fall in the 24+ ms range (two refreshes per frame). The
  same unit ran HeartGold at a steady 60 fps (game CPU 68%) in a 93 s session (20261006-030421) eight minutes before
  a 35 fps one. So the speed depends on the scene: some HeartGold scenes at 2x need more than one A55 core at 1992
  MHz. Other units' HeartGold sessions at 2x are short (22-36 s) and also drop to 30-55 fps in their last quarter
  (8b19a7). At 1x, HeartGold runs at 59 fps (df2c08, 387 s).
- **Pokemon Platinum with lcd3x on 2437b8: 20-77% of play below 57 fps from 1.5 through 1.5.9**, at 1700-1992 MHz
  with DraStic at 90-145%. This unit also produced most of the logs' `[fence]` events (commits that showed an
  unfinished GPU frame: 3,233 in one 1.5.9 session). The GPU is at its 400 MHz top throughout, so lcd3x costs GPU
  time on top of the CPU load. On 1.5.12, a 715 s session on the same unit, game and shader ran at 59.6 fps (1%
  slow). That is one session, so confirm it before crediting a release with the fix.

Queue depth 3 looks slow (16-46% of play below 57 fps). Nearly all of those sessions are 2437b8's Platinum with
lcd3x. Without that unit, depth-3 play is 3.1% slow, so the queue depth isn't the cause.

By resolution, without f1aa93: 1x play is 3.3% slow (59.48 fps) and 2x play is 8.9% slow (58.34 fps), with 2.0
repeated frames a second at 2x against 0.8 at 1x. 2x is now the default for most of the logs (269 of 319 sessions).

## Smaller things

- **Queued drops went up with depth 1** (1.5.5's half-latency queue). Since 1.5.2, nearly every dropped frame is a queue
  drop (`drop-q`). Source and buffer drops are close to zero. Depth-1 play drops 0.10-0.57 frames a second, against
  0.005-0.05 at depth 2 on 1.5-1.5.1. The worst sessions use sharp-shimmerless or ds-grid-2x at 2x: Sonic Colors
  (1be1e3, 1.5.9) at 2.3 and 8.3 drops/s, and The 4 Heroes of Light (e12f3c, 1.5.5, performance) at 1.6/s. That's
  low enough to trade for the latency, but these shaders at 2x are the setups to look at.
- **Audio underruns went up on 1.5.12**: 96 an hour of play, against 22-58 an hour before. Most come from f1aa93
  playing at 40 fps, where underruns are expected. Without that unit, the rate is 37 an hour, in line with earlier
  releases.
- **Late latches and GPU-unfinished commits went down on 1.5.12**: 64 late an hour (113-127 on 1.5.1-1.5.9) and 284
  fence events an hour (830-1,600). Without f1aa93, both still hold. Part of the fence drop is 2437b8's Platinum
  running clean.
- **Temperature isn't a factor.** The highest CPU temperature in any session is 68 °C, and the median of the
  sessions' highest temperatures is 52-54 °C on every release. Nothing points to thermal throttling.
- **Battery current can't separate the profiles in these logs.** Full-speed sessions of at least 120 s have medians
  of 825 mA balanced (30 sessions) and 816 mA performance (4) on 1.5.2+. Battery saver has 2 sessions. The heavy
  games on battery saver (2437b8) raise its average to 1,039 mA. That reflects the game being played, not the
  profile.
- **Governor changes are down**: 127 clock changes an hour on 1.5.12, against 150-250 on 1.5.1-1.5.9.

## What changes with this report

- **The logs record the device model, the 3D renderer, the 3D resolution and whether the profile's CPU bound is
  soft** (`dsflip/device/perf-session.py`): the summary gets `model` (from `/proc/device-tree/model`) and
  `cpu_max_soft`. Each sample's `game` gets `renderer` (`gengis` or `drastic`) and, for Gengis Engine, `res3d`
  (`2x` or `3x`), all read from DraStic's environment. Until now the resolution was only in the governor's
  remembered-drops line, which 30 sessions don't have, and the RG DS and RG DS Plus couldn't be told apart.
- **`tools/perf-logs.py`** shows the resolution and ends with one line per version (play, fps, slow play split into
  busy below the top clock, at the top clock, and the rest). That's this report's version table.
- **SuperDrastic: 3x gets its own governor memory** (`src/cpugov.c`, `cpugov/<rom>.<shader>.3x`). Up to
  0.5.0-beta.1 the file name went by DraStic's screen width only, and 3x's frame is 2x's size. So 1.6's 3x and 2x
  shared one file: a heavy 3x session's strikes would keep the same game's 2x sessions at clocks they don't need, and
  2x's clean record would start 3x too low. The key uses the scale Gengis Engine actually draws at. It falls back to
  2x when the 3x hook fails, and 1x and 2x keep their existing files.

## For the next round of logs

- **1.6 / 1.6.1**: not measured yet (one 4 s session). 1.6 adds a 3x resolution. Given HeartGold at 2x, expect 3x
  to be CPU-bound in heavy 3D scenes. The 3x sessions' slow share at the top clock is the number to watch.
- **HeartGold at 2x**: find out whether the ~38-44 fps scenes run at 60 at 1x on the same unit. If they do, consider
  remembering heavy games per ROM and suggesting 1x for them (the per-ROM data already exists: `remembered drops
  for ….none.2x`).
- **Platinum with lcd3x**: confirm 1.5.12's 59.6 fps on more than one session.
- **Depth-1 queue drops with sharp-shimmerless and ds-grid-2x at 2x.**
- The logs carry no device model. An RG DS Plus uploader (or a `model` field) would let the two handhelds be
  compared.
