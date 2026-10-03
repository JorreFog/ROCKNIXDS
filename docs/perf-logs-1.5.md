# The 1.5 performance logs

What the players' uploaded sessions from ROCKNIXDS 1.5 say (`device-logs` branch, `docs/data/device/`, imported up
to 2026-10-03), read with [`tools/perf-logs.py`](../tools/perf-logs.py). 33 sessions from 5 RG DS units on
1.5 (stable and beta channel), all with SuperDrastic 0.3.0-beta.3, 1.5's RG DS build. The RG DS Plus has no uploader.

`perf-logs.py` now counts play only: a second in DraStic's menu (the bottom panel flips, the top doesn't) or under 10
presents (loading, a pause) is left out of the frame numbers. Before, a long stay in DraStic's menu read as a slow
game (Final Fantasy - The 4 Heroes of Light: 129 menu seconds against 97 of play).

## The finding: the CPU governor held games below full speed

| | Play (s) | Below 57 fps | ...with DraStic busy (a core or more) and the clock below its top |
|---|---|---|---|
| 1.5 | 1993 | 233 (11.7%) | 150 (64% of them) |
| 1.5 betas | 8306 | 240 (2.9%) | 177 (74% of them) |

beta.3's governor raises the clock on dropped frames and heavy frames only. A game that is simply slow drops nothing:
DraStic presents fewer frames and every refresh without one is a repeated frame. So it held clocks the game couldn't
run at. Examples:

- **Pokemon Platinum** with ds-crisp (2437b8, balanced): 60 fps at 1992 MHz, then "1992 -> 1800 MHz (light)", and
  44 fps for the rest of the session at 1800, DraStic's threads at 130-190% of a core. 87-90% of its play below
  57 fps in two such sessions.
- **Call of Duty - World at War** (f8b44a, balanced, no shader): 22-56 fps at 1608 MHz, then "1608 -> 1416 MHz
  (light)" with the busiest thread at 82%. 84% of its play below 57 fps.
- **Final Fantasy - The 4 Heroes of Light** and **Contact** (f8b44a): 50% and 32% of their play below 57 fps at
  1416 MHz.

The fix was already in SuperDrastic's main line (0.3.0-beta.2, "the RG DS Plus round", what the Plus runs): below
full speed with real work going on is a reason to step up and a strike against that clock, and no step down when all
of DraStic's threads together wouldn't fit the lower clock. beta.3 was built from a tree without it.

## Smaller things

- **The 816 MHz floor bounced.** Yoshi's Island (aaf6b4, quilez): 164 clock changes in 400 s of play, 816 MHz 62% of
  the time, 0.51 dropped frames a second. The main line's floor is 1104 MHz.
- **The power profiles' caps weren't caps.** beta.3 started every session at the menu's clock (usually 1992) and
  only stepped down on light windows, so battery-saver sessions ran at 1800-1992 MHz (2437b8: ~1000 mA). The main
  line's governor holds the bound, and a heavy game at a "balanced" 1416 MHz then runs slow all session.
- **RetroAchievements and zipped ROMs**: `hash generation failed` for `Pokemon - Platinum Version (USA) (Rev 1).zip`
  (10 sessions). SuperDrastic hashes the ROM file, and a zip isn't one. Not a performance problem; unchanged.

## What 1.5.1 changes

- **The RG DS runs SuperDrastic 0.3.0-beta.4**, the main line with the Plus round's governor (and 1.5.1's other
  fixes), like the Plus. It replaces beta.3 and its 816 MHz work.
- **The profiles' upper bounds are soft** (`DSFLIP_CPU_MAX_SOFT=1`, both handhelds): the governor goes past them only
  while the game is below full speed with real work going on, and steps back down when the lower clock fits.
  Simulated with the governor's own code (a game needing ~1700 MHz for 60 fps): balanced settles at 1800 MHz at full
  speed, where a hard 1416 MHz bound gave 50 fps; a light game still goes down to the floor, and a medium one stays
  at the bound. A bound that is meant to hold (`DSFLIP_CPU_MAX_SOFT=0`) holds from the first moment.

## For the next round of logs

- Confirm on 1.5.1: the share of slow play seconds with DraStic busy below the top clock should be near zero, and
  Platinum with a shader, Call of Duty and The 4 Heroes of Light should run at full speed on balanced.
- Battery current on balanced and battery saver, now that the bounds hold except for games that need more.
