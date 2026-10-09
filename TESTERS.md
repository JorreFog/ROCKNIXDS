# ROCKNIXDS private test build 2 (2026-10-09)

**For testers only.** This is not a release: it isn't on the Releases page and the menu's updater won't offer it.
Please don't post the install command publicly.

## Install

ssh into the handheld as `root` (password `rocknix`), with no game running, and run the command you were given:

```sh
curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/<commit>/testers.sh | sh
```

`testers.sh` picks the build for your handheld (RG DS or RG DS Plus) and runs that build's own installer. Your
games, saves and settings stay. Afterwards the version reads `... (private-test-2)`.

**Back to the public release:** UPDATES & DOWNLOADS > ROCKNIXDS in the menu offers it as an update, or over ssh:

```sh
curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh
```

## New in build 2

- **Döda Kvarter 0.3.0** (Kert Barlsson on the radio, the story, new music) for testers: on the RG DS the Store's Updates
  tab and the game's *Settings > Game updates* both offer it (the RG DS installs with 0.2.0, so the update can be
  tried). The RG DS Plus build already has 0.3.0.
- **The Store reaches the network again:** it looked for its app list on the public main branch, where the Store
  isn't yet, and said it was offline.
- Döda Kvarter's *Game updates* says "up to date" instead of "offline" when there's nothing newer, and the Store says
  "No release yet" instead of "No network" for an app without a release.

## What's in it

| | RG DS | RG DS Plus |
|---|---|---|
| Built from | `banana` (codename Banana, PR #60) + fixes | `plus-beta` (1.6.1-plus, Döda Kvarter 0.3) |
| SuperDrastic | 0.5.0-beta.1-rocknixds.9-test.1 | 0.5.0-beta.1-rocknixds.9-test.1 |
| Faster 3x (below) | yes | yes |
| 3x stutter fix (below) | yes | yes |
| Banana: base ROCKNIX from the menu, saves folders, recommended settings, Art Book Next, sound check at boot, ROCKNIXDS Store, perf logs with model and 3D resolution | yes | no (the RG DS line only for now) |
| Base ROCKNIX list fix (below) | yes | not needed |

- **Faster 3x.** The 3D work at 3x costs about 30% less CPU: on the RG DS the 3D stress test at level 4 went from
  49.7 to 60 fps and level 6 from 41 to 59; on the Plus, Metroid Prime Hunters from 56.9 to 59 fps at 1416 MHz.
  It also fixes textures drawn askew at 3x on some large polygons (seen on Mario Kart DS's cliffs).
  2x looks exactly as before (checked frame by frame in six games).
- **3x stutter fix.** At 3x the game now keeps the frame queue's 20 ms wait. HeartGold at 3x on the Plus: repeated
  frames went from 0.8-1.2 a second to about 0.3.
- **3x keeps its own clock memory**, so 2x and 3x sessions of the same game no longer push each other's CPU clock.
- **Base ROCKNIX list fix (RG DS).** UPDATES & DOWNLOADS > ROCKNIX (BASE SYSTEM) now lists ROCKNIX's monthly releases
  and its daily nightlies; before, the list was empty.

## Please check

1. **3x in your own games** (Nintendo DS > 3D resolution: 3x): smoothness, and anything drawn wrong.
2. **2x as usual:** anything that looks different from before is a bug.
3. **Starting and quitting games:** a game that never shows a picture, or doesn't quit.
4. RG DS: the Banana checks in `BANANA.md`, mostly the ones that need a person: RetroAchievements progress bars
   (#51), the on-screen keyboard's hints (#30), last played / play count (#50), brightness sliders (#49), blowing
   into the microphone (#26), the Store on the screens.

Send back what you saw, and these logs: `/storage/.config/drastic/dsflip/dsflip.log`, `last-session.log` in the
same folder, `/var/log/es_log.txt`.

Known: the Store can't update itself yet (there's no release of it); it says "No release yet" for that.
