# Hand-off: finishing 1.5.13 on the handheld

What was prepared without a handheld, and exactly what a person (or an AI) with an RG DS or RG DS Plus on the desk
runs to finish each item. ssh in as `root` (password `rocknix`); the device paths below are the installed ones.
Each item ends with its acceptance test. Update this file as items close.

Install the branch under test on the device first:

```sh
curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | RGDS_BRANCH=claude/tender-volta-a9nkpk sh
```

(or `RGDS_SRC=/path/to/checkout sh install.sh` from a copy on the device). Logs that matter:
`/storage/.config/drastic/dsflip/dsflip.log` (the engine's log of the last game), `last-session.log` (the launcher's),
`/var/log/es_log.txt` (EmulationStation).

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
