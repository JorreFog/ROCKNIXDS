# Codename Banana

**Banana** is the name for the work in progress after 1.6.1. It is everything on the list below, put together on one
branch, `banana`, so it can be installed and tested on a handheld in one go. When the owner says *"test codename
banana on the device"*, they mean: install the `banana` branch on the handheld and work through the checks below.

The undervolting work (PR #54) is **not** part of Banana.

## Install it

```sh
curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | RGDS_BRANCH=banana sh
```

Or from a checkout of `banana` copied to the handheld: `RGDS_SRC=/path/to/checkout sh install.sh`. ssh in as `root`
(password `rocknix`). `banana` is built on `main` (the RG DS line, 1.6.1), so test on an **RG DS** first. On an RG DS
Plus, check `docs/handoff-local.md` section H (where the two lines still differ) before you install.

The logs to send back are the same as always: `/storage/.config/drastic/dsflip/dsflip.log`, `last-session.log`,
`/var/log/es_log.txt`, `/storage/.config/emulationstation/es-mem.log`, plus the logs named in each check.

## What's in Banana

| Part | PR | Branch | In `banana`? |
|---|---|---|---|
| Open issues after 1.6.1, base ROCKNIX nightly from the menu, SuperDrastic `.8` | ROCKNIXDS #58 | `claude/open-issues-rocknix-nightly-1l4s4w` | yes |
| ROCKNIXDS Store | ROCKNIXDS #55 | `claude/app-store` | yes |
| Perf logs record model, renderer and 3D resolution; the 1.5.1-1.5.12 report | ROCKNIXDS #57 | `claude/perf-logs-1.5.12` | yes |
| Governor: 3x gets its own remembered-drops file | SuperDrastic #6 | `claude/cpugov-3x-memory` (SuperDrastic) | **no**: needs a SuperDrastic package |
| Gengis Engine optimization | none yet | none yet | not yet |
| CPU scheduling: IRQs off DraStic's core, limit deep CPU idle (`tools/irqtest.sh`) | none yet | none yet | not yet |

## The checks

1. **Open issues + base ROCKNIX (#58).** These are the steps in `docs/handoff-local.md` section J (1-6): the base
   ROCKNIX menu and its `os-*` round trips, save folders (#42/#56), *Apply recommended settings* (#45), Art Book Next
   (#48), the in-game menu's sound and tip switches, sound at boot (#53). Also check by eye:
   - **#51:** RetroAchievements progress bars don't overlap their labels.
   - **#30:** the on-screen keyboard's button hints sit under the keyboard, on its panel.
   - **#50:** after playing a DS game, *last played*, play count and time update in the menu.
   - **#47:** no lines in 3D at *3D resolution 3x* (`nds.resolution3d=3x`).
   - **#49:** in the in-game menu, a brightness slider for each screen, and the brightness card on Menu + volume.
   - **#26:** a blow on the microphone is detected in a game that uses it.
2. **Store (#55).** The Store tile (a green bag) is on the menu's shelf and opens on both panels. Touch and buttons
   work in the Games / Apps / Installed tabs, and the Updates tab lists apps with a newer version and what's new in it. Install, update and remove an app; the Store refuses to install or
   remove an app while it is running. ES comes back with the right tiles afterwards. Not expected to work yet: Döda Kvarter
   shows "SOON" on a handheld that doesn't have it, and the Store's self-update is unavailable (no `store-v0.1.0`
   release yet).
3. **Perf logs (#57).** Play one DS game at 2x and one at 3x. Each session's perf log records the device model, the
   3D renderer (Gengis Engine or DraStic's own) and the 3D resolution. `tools/perf-logs.py` on a PC shows the
   resolution and ends with a per-version summary table.
4. **Every time:** a game starts and quits cleanly, resume on quit works, and nothing freezes (as in section A).

Write the results into `docs/handoff-local.md` under a new heading, `## Results on an RG DS, <date> (Banana)`, like
the sections already there.

## Keeping `banana` up to date

`banana` is `main` with each "yes" branch above merged in. When one of those branches moves, or a new part gets a
branch, merge it into `banana` (`git merge origin/<branch>`), run `sh tests/run.sh`, update the table above and push.
Nothing is released from `banana`. Each part still goes to `main` through its own PR.
