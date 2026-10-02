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
4. Push the tag **`v1.5-plus`** at `plus-beta`'s head; the release workflow publishes it (not a pre-release).
5. Then push the tag **`v1.5`** at `main`'s head; the workflow publishes it and it becomes "latest".
6. On one handheld of each kind, run the README command and check that *Updates & downloads > ROCKNIXDS* says
   "up to date". Then switch a beta handheld to stable and check that it is offered its own release.

Before main's installer finds `v1.5`, it still installs `v1.4` (the newest RG DS release), and a Plus is told there
is no Plus release yet. So the merges can land before the releases are published.

## Release notes

[`docs/releases/v1.5.md`](releases/v1.5.md) and [`docs/releases/v1.5-plus.md`](releases/v1.5-plus.md). Pushing a
`v*` tag runs [`.github/workflows/release.yml`](../.github/workflows/release.yml), which publishes the release with
that tag's notes file and the Pixel images attached. A `-plus` tag never becomes "latest".

## Known before release

- **SuperDrastic**: the two handhelds run different SuperDrastic builds. The RG DS's 0.3.0-beta.3 (the 816 MHz work)
  was built from a tree that isn't in the SuperDrastic repository. It also lacks the Plus round's fixes: the
  preload guard, integer viewports for both panels, and all threads counted by the clock governor. ROCKNIXDS works
  around the missing guard with `dsflip/device/preload-guard.so`. One SuperDrastic 0.3.0 with both lines, published
  as a release, should replace both before 1.6.
- **Not tested on a handheld**: the 1.5 fixes in this round, ROCKNIXDS Pixel, and the cross-built ES binary
  (`tools/build-es.sh`) were tested on a PC: host tests, the real ES under Xvfb, and the media tool end to end with
  stand-ins. Each needs one pass on an RG DS and an RG DS Plus before the tags.
