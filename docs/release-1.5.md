# Releasing 1.5: two releases, one updater

1.5 is released twice from two trees. Each tree is the 1.5 betas plus the 1.5 fixes:

| Release | Tag | Tree | SuperDrastic | Who gets it |
|---|---|---|---|---|
| ROCKNIXDS 1.5 | `v1.5` | `beta` + the 1.5 fixes, merged into `main` | 0.3.0-beta.3 (shipped in `dsflip/`) | RG DS |
| ROCKNIXDS 1.5 for the RG DS Plus | `v1.5-plus` | `plus-beta` + the 1.5 fixes | 0.3.0-beta.2 (the published release) | RG DS Plus |

A tag ending in `-plus` is the Plus's; every other tag is the RG DS's. Pre-releases are betas. The installer and the
updater only look at stable (non-pre-release) releases for the stable channel.

## How each install gets to the right release

`install.sh` first works out the exact ref for the handheld it runs on. It then runs that ref's own `install.sh`
with `RGDS_REF`, and that installer downloads exactly that ref and records it in `installed-id`.

| Starting point | What runs | Result |
|---|---|---|
| 1.4 or earlier on an RG DS (no updater): the README's `curl …/main/install.sh \| sh` | main's installer | newest RG DS release: `v1.5` |
| RG DS Plus alpha (no updater): the same command | main's installer | newest `-plus` release: `v1.5-plus` |
| RG DS 1.5 beta, *stable* channel | the beta's updater: newest release (`releases/latest`), then main's installer | `v1.5` |
| RG DS 1.5 beta, *beta* channel | the beta's updater: `beta`'s head, then beta's installer | `beta` at that commit |
| RG DS Plus beta, *beta* channel (the Plus installer set it) | the beta's updater: `plus-beta`'s head, then plus-beta's installer | `plus-beta` at that commit |
| RG DS Plus beta, switched to *stable* | the beta's updater offers `releases/latest`, then main's installer | `v1.5-plus` (main's installer sends a Plus to its own release) |
| 1.5 (either), any channel | 1.5's updater: this handheld's newest release or beta commit, that ref's installer | that ref |

The 1.5 betas' updaters compare `releases/latest` with what is installed. Publish **`v1.5-plus` first and `v1.5`
second**, so that `releases/latest` is the RG DS's release: an RG DS beta on stable then sees exactly `v1.5`. A Plus
on stable sees "1.5" too and gets `v1.5-plus` from main's installer. From then on, 1.5's own updater only looks at
the Plus's releases on a Plus.

## Steps

1. RG DS: merge `claude/wizardly-cori-486rvt` (based on `beta`, with `main` merged in) into `beta` and `main`.
   `VERSION` is `1.5`.
2. RG DS Plus: merge the Plus release branch (based on `plus-beta`; `VERSION` is `1.5-plus`) into `plus-beta`.
3. Check CI on both (`check` and `unit tests`).
4. Publish the release **`v1.5-plus`** from `plus-beta` (not a pre-release), with the notes below.
5. Publish the release **`v1.5`** from `main` (not a pre-release), with the notes below. It becomes "latest".
6. On one handheld of each kind, run the README command and check that *Updates & downloads > ROCKNIXDS* says
   "up to date". Then switch a beta handheld to stable and check that it is offered its own release.

Before main's installer finds `v1.5`, it still installs `v1.4` (the newest RG DS release), and a Plus is told there
is no Plus release yet. So the merges can land before the releases are published.

## Release notes: v1.5 (RG DS)

> **ROCKNIXDS 1.5: a new pixel theme, resume where you quit, power profiles, art on the device**
>
> For the Anbernic RG DS. RG DS Plus owners: take **v1.5-plus** (the installer and the updater do that for you).
>
> - ROCKNIXDS Pixel: a new theme drawn by its own engine. A pixel-art shelf of systems, games as cartridges,
>   a ready screen and the stats of every system and game, with springy, stepped pixel animations.
> - Dark and light ROCKNIXDS themes, and canvas-ds as a second dual-screen theme.
> - The exit hotkey saves your place, and the next start resumes there (*resume on quit*, on by default).
> - Power profiles per system or game: balanced (816–1416 MHz), performance, battery saver.
> - The CPU clock remembers each game. DS games always run on the governor it needs. No more stutter storms.
> - Game art and RetroAchievements strips are fetched and kept up to date on the handheld itself.
> - Updates from the menu, each handheld to its own release. Settings that break ROCKNIXDS are hidden.
> - Optional performance logs (asked once), an in-game volume card, and a faster way back to the menu.
>
> Install or upgrade: `curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh`
> (from 1.5 on, *Updates & downloads > ROCKNIXDS*). Built for ROCKNIX 20260901.

## Release notes: v1.5-plus (RG DS Plus)

> **ROCKNIXDS 1.5 for the RG DS Plus**
>
> The first stable ROCKNIXDS for the Anbernic RG DS Plus: everything in 1.5, sized for the Plus's 1024x768 panels.
>
> - ROCKNIXDS Pixel at 1.6x, the dark and light ROCKNIXDS themes and canvas-ds on both panels.
> - DS games at full speed on the Plus: DraStic's emulation thread on a CPU of its own, its helper threads spread
>   over the others, a later latch for the Plus's bottom panel, and PipeWire at the 48 kHz its speaker amp runs.
> - Resume on quit, power profiles (battery saver runs 1104–1416 MHz on the Plus), the per-game CPU clock memory.
> - Game art and RetroAchievements strips on the device, updates from the menu, the volume indicator in the menus.
>
> Install or upgrade: `curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh`
> (the same command as the RG DS: it installs this release on a Plus). Plus beta testers stay on the beta channel
> unless they switch to stable in *Updates & downloads > ROCKNIXDS*.

## Known before release

- **SuperDrastic**: the two handhelds run different SuperDrastic builds. The RG DS's 0.3.0-beta.3 (the 816 MHz work)
  was built from a tree that isn't in the SuperDrastic repository. It also lacks the Plus round's fixes: the
  preload guard, integer viewports for both panels, and all threads counted by the clock governor. ROCKNIXDS works
  around the missing guard with `dsflip/device/preload-guard.so`. One SuperDrastic 0.3.0 with both lines, published
  as a release, should replace both before 1.6.
- **Not tested on a handheld**: the 1.5 fixes in this round, ROCKNIXDS Pixel, and the cross-built ES binary
  (`tools/build-es.sh`) were tested on a PC: host tests, the real ES under Xvfb, and the media tool end to end with
  stand-ins. Each needs one pass on an RG DS and an RG DS Plus before the tags.
