# ROCKNIXDS 1.5 plan

Carried over from 1.4 (see [plan-1.4.md](plan-1.4.md) and the [optimization report](optimization-1.4.md)).

## 1. Starting and quitting games (first)

1.4 starts a game as fast as 1.3 (first frame 3.47 s after the launch request, 1.3: 3.44 s), but the way back to the
menu is ~0.8 s slower: ES answers its API 3.51 s after the game ends (1.3: 2.65 s) and is visible at 5.26 s (1.3:
4.47 s). Known so far: it isn't the menu governor hook (ES starts in ~1.7 s with or without it), the CPU clock limit
(session.sh lifts it the moment DraStic exits) or the PipeWire rate reset (~20 ms). Next: time each step of
restore.sh and ES's start on 1.3's and 1.4's ES binaries (`tools/switchtime.sh`), then look at the launch chain
itself (ROCKNIX's runemu.sh and start_drastic.sh take ~2.3 s before our unit starts).

## 2. Power, continued

- **ROCKNIX's `powerstate` service**: a bash loop polling every 2 s with `cat`, `awk` on the battery's whole uevent
  (I2C reads) and `sleep`, ~3% of a core, more on battery (it looks settings up with `awk` on every pass after the
  first 40 s). Same treatment as `battery-led-status`: a fork-free copy behind a systemd drop-in that hands back to
  ROCKNIX's script if that ever changes (md5 79dcb5ee1f43876d5c1dff429f87b36b), including `ledcontrol discharging`
  done in-process when the battery is above 97%.
- **The CPU governor's first minute**: it finds its level by trying lower clocks, and each clock that turns out too
  low costs 2-4 frames once (a 30 s smoke test showed 0.20 drops/s, a 90 s run 0.02-0.07). Ideas: descend more
  slowly, start from the last session's level for the same game, or learn per game.
- **Skipping unchanged frames with a shader**: needs a way to know a frame didn't change without reading DraStic's
  uncached buffers back.
- **ds-fsr** (5.3 ms per panel at 800 MHz, 4.3 in the 1.4 comparison session): a two-pass version that analyses each
  source pixel once.
- **Other systems**: ROCKNIX's per-system governors for RetroArch cores, measured with free homebrew ROMs.

## 3. Parked branches

- `standalone-wip`: libdsflip as a package for other firmwares (built, not tested on the device).
- `plus-alpha`: the RG DS Plus port (pre-release v1.4-plus-alpha.1, untested on hardware); rebase onto 1.4 when the
  hardware is at hand.
