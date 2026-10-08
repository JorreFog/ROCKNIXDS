# Undervolting the RG DS on ROCKNIX

`rocknixds-undervolt` lowers the CPU (and, if asked, GPU) voltages in the RG DS / RG DS Plus device tree, stress-tests
them with a test that checks its own results, and keeps them only if they pass. It is the same idea as
[u/aumVilohm's GammaOS undervolt guide](https://www.reddit.com/r/SBCGaming/s/xCjQufKtlD) ("RGDS+ undervolt tutorial (Advanced
tinkering)"): lower the OPP voltages in the DTB, test each clock, back off where it fails. On ROCKNIX there is no
`boot.img`, no RSCE checksum and no second DTB copy to keep in sync. The DTB is a plain file on the FAT partition
(`/flash/device_trees/rk3568-anbernic-rg-ds*.dtb`), so the whole job can run on the handheld.

> [!WARNING]
> **Advanced tinkering, at your own risk.** Too little voltage causes wrong calculations, crashes and resets. The
> tool backs everything up, and it reverts a trial that crashes, but it can't promise that nothing goes wrong. Have a
> card reader within reach. Don't copy someone else's voltages: every chip is different.

## Why ROCKNIX has more headroom than GammaOS had

Rockchip's vendor kernel measures each RK3568 at boot (a PVTM ring oscillator and leakage fuses) and puts it in a
voltage bin, L0 (worst) to L3 (best). Then it runs the chip at that bin's voltages. Mainline Linux, and so ROCKNIX, has no
binning, and its `rk3568.dtsi` OPP table is Rockchip's **L0** table. So every RG DS on ROCKNIX runs at worst-case
voltages, even a chip that Anbernic's own firmware would run 100 mV lower.

CPU, mV (`opp-microvolt`, vendor `rk3568.dtsi` from rockchip-linux/kernel develop-5.10; mainline 7.1 `rk3568.dtsi`):

| MHz | ROCKNIX (= L0) | L1 | L2 | L3 (Anbernic stock on an L3 chip) | GammaOS 1.4.4 | The guide's unit |
|---:|---:|---:|---:|---:|---:|---:|
| 408–816 | 850 | 850 | 850 | 850 | 825 | 825 |
| 1104 | **900** | 850 | 850 | 850 | 825 | 825 |
| 1416 | **1025** | 975 | 950 | 925 | 900 | 825 |
| 1608 | **1100** | 1050 | 1025 | 1000 | 975 | 875 |
| 1800 | **1150** | 1100 | 1075 | 1050 | 1000 | 900 |
| 1992 | **1150** | 1150 | 1125 | 1100 | 1050 | 950 |

GPU, mV:

| MHz | ROCKNIX (= L0) | L1 | L2 | L3 |
|---:|---:|---:|---:|---:|
| 200–400 | 850 | 850 | 850 | 850 |
| 600 | **900** | 875 | 850 | 850 |
| 700 | **950** | 925 | 900 | 875 |
| 800 | **1000** | 975 | 950 | 925 |

DS games on ROCKNIXDS spend their time at 1416–1992 MHz, which is where ROCKNIX's voltages sit furthest above the
better bins. Dynamic power goes with the square of the voltage. 1992 MHz at 1100 mV instead of 1150 is about 8.5% less
CPU power, and at 1050 mV it is about 17% less. The guide reports two extra hours of play from its (much lower) values.
The GPU matters less here: the default DS settings put no load on it, and it idles at 200–400 MHz, which no preset changes.

## How it works

```
/storage/.config/rocknixds/undervolt/rocknixds-undervolt status          # what's in use now
/storage/.config/rocknixds/undervolt/rocknixds-undervolt plan l3         # what L3 would write
/storage/.config/rocknixds/undervolt/rocknixds-undervolt apply l3        # write it as a trial
reboot
/storage/.config/rocknixds/undervolt/rocknixds-undervolt test 10         # 10 minutes per lowered clock
```

`SPEC` is a CPU preset (`l1`, `l2`, `l3`, or `stock`), then `gpu=l1|l2|l3` if you want the GPU too, then single
clocks (`cpu1992=1050 cpu1800=1012.5`). Values must be 12.5 mV steps. A value can never be over stock or under 800 mV
(825 for the GPU, its regulator's minimum). A faster clock can never get less voltage than a slower one.

- **apply** checks that the DTB is one it knows: exactly the RG DS's OPPs, with the layout it expects. It changes only
  the target and min cells of each `opp-microvolt`, in place, so the DTB doesn't change size. It copies the DTB to
  `/flash/rocknixds-undervolt/` first: `stock.dtb` (ROCKNIX's own) and `previous.dtb` (from before this apply).
  `RECOVERY.txt` there explains the recovery from a computer. It also adds `cpufreq.default_governor=powersave` to the
  boot line, so a trial boots at 408 MHz instead of ROCKNIX's default (performance, 1992 MHz). That holds until the
  boot guard has run.
- **The boot guard** (`rocknixds-undervolt-guard.service`) runs early at every boot and does nothing unless a trial is
  pending. If a trial boots a second time without passing `test` (it crashed, froze, or was restarted), the guard
  writes `previous.dtb` back and restarts once. A crash during the test therefore undoes itself.
- **test** pins the CPU at each lowered clock, slowest first. It checks that the clock holds and that `vdd_cpu`
  really reads the new voltage (the guide's "check the ACTUAL voltage"). Then it runs `uvstress` on all four cores for
  the given minutes, plus a minute of bursts. Finally it lets schedutil move the clock under bursty load (the guide's
  "slow load transitions"). `uvstress` runs integer, NEON and double-precision work whose results must be identical
  every round on every core. A single wrong bit fails the test, the old voltages go back, and the tool names the clock
  that failed. Raise that clock two steps (25 mV) above the failing value, like the guide says, and test again.
  A pass ends the trial.
- **off** writes the stock voltages back. Uninstalling ROCKNIXDS does the same.

A ROCKNIX update replaces the DTB, so the voltages go back to stock. Run `apply` again after one.

## What it can't do yet

- **Find the chip's bin.** The vendor kernel reads it from the PVTM block, which mainline doesn't expose. Start at
  `l1` or `l2`, test, then go lower. Most chips will take L3 or less, but some are L0.
- **Check the GPU's results.** `uvstress` runs on the CPU. `gpu=` changes are opt-in and untested by the tool. Play
  with the *ds-fsr* shader (the GPU at full clock) for a while to test them.
- **Run from the menu.** For now it's an ssh tool.

## Files

| File | What it is |
|---|---|
| `device/rocknixds-undervolt` | The tool (POSIX sh). `UV_FLASH`, `UV_STATE` and `UV_SYS` point it at a fake tree for the host tests |
| `device/uvdtb.py` | Reads and patches the OPP tables in a DTB with no `dtc` (python3 only, as on ROCKNIX) |
| `device/uvstress.c`, `device/uvstress` | The self-checking stress test: static arm64, no libc. The build line is in the source |
| `device/rocknixds-undervolt-guard.service` | The boot guard |
| `../tests/test_undervolt.py` | Host tests on ROCKNIX's real RG DS and RG DS Plus DTBs (`tests/data/`) |
