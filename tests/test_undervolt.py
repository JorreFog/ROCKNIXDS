"""The undervolt tool (undervolt/): uvdtb.py's reading and in-place patching of the RG DS's OPP voltages, and
rocknixds-undervolt's trial life cycle (apply, the boot guard, test, confirm, off) on a fake /flash.

tests/data/*.dtb are the RG DS and RG DS Plus device trees as ROCKNIX ships them (its rk3568-anbernic-rg-ds*.dts
on mainline 7.1's rk3568.dtsi, built with cpp + dtc), so the OPP tables are the real ones."""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT, load

uvdtb = load("uvdtb", "undervolt/device/uvdtb.py")
DATA = ROOT / "tests" / "data"
DTBS = [DATA / "rk3568-anbernic-rg-ds.dtb", DATA / "rk3568-anbernic-rg-ds-plus.dtb"]
TRIAL_ARG = "cpufreq.default_governor=powersave"


class UvdtbTest(unittest.TestCase):
    def test_reads_rocknix_tables_as_stock(self):
        for p in DTBS:
            fdt = uvdtb.Fdt(p.read_bytes())
            self.assertEqual(uvdtb.voltages(fdt), uvdtb.STOCK, p.name)
            # the max cell is 1150 mV on the CPU, 1000 on the GPU
            t = uvdtb.tables(fdt)
            self.assertEqual({c[2] for _, c in t["cpu"].values()}, {1150000})
            self.assertEqual({c[2] for _, c in t["gpu"].values()}, {1000000})

    def test_l3_changes_only_voltage_cells(self):
        data = DTBS[0].read_bytes()
        out = uvdtb.patch(data, uvdtb.parse_spec(["l3"]))
        self.assertEqual(len(out), len(data))
        v = uvdtb.voltages(uvdtb.Fdt(out))
        self.assertEqual(v["cpu"], uvdtb.PRESETS["l3"]["cpu"])
        self.assertEqual(v["gpu"], uvdtb.STOCK["gpu"])         # a CPU preset leaves the GPU alone
        # every changed byte is inside a CPU opp-microvolt's first two cells
        fdt = uvdtb.Fdt(data)
        allowed = set()
        for _, (path, _) in uvdtb.tables(fdt)["cpu"].items():
            off, _ = fdt.nodes[path]["opp-microvolt"]
            allowed.update(range(off, off + 8))
        changed = {i for i in range(len(data)) if data[i] != out[i]}
        self.assertTrue(changed)
        self.assertLessEqual(changed, allowed)
        # max stays
        for _, (_, c) in uvdtb.tables(uvdtb.Fdt(out))["cpu"].items():
            self.assertEqual(c[2], 1150000)

    def test_spec(self):
        w = uvdtb.parse_spec(["l2", "gpu=l3", "cpu1992=1087.5"])
        self.assertEqual(w["cpu"][1992], 1087.5)
        self.assertEqual(w["cpu"][1800], 1075)
        self.assertEqual(w["gpu"], uvdtb.PRESETS["l3"]["gpu"])
        out = uvdtb.patch(DTBS[1].read_bytes(), w)
        self.assertEqual(uvdtb.voltages(uvdtb.Fdt(out))["cpu"][1992], 1087.5)
        self.assertEqual(uvdtb.parse_spec([]), uvdtb.STOCK)
        for bad in (["cpu1992=1200"],                 # over stock
                    ["cpu1608=990"],                  # not a 12.5 mV step
                    ["cpu408=787.5"],                 # under the CPU floor
                    ["gpu200=812.5"],                 # under vdd_gpu's minimum
                    ["cpu1800=1000", "cpu1992=987.5"],  # a faster clock under a slower one
                    ["cpu1700=1000"], ["l9"], ["cpu1992=1000", "l3"]):
            with self.assertRaises(uvdtb.DtbError, msg=bad):
                uvdtb.validate(uvdtb.parse_spec(bad))

    def test_refuses_unknown_dtb(self):
        with self.assertRaises(uvdtb.DtbError):
            uvdtb.Fdt(b"\0" * 64)
        data = bytearray(DTBS[0].read_bytes())
        fdt = uvdtb.Fdt(bytes(data))
        path, _ = uvdtb.tables(fdt)["cpu"][1992]
        off, _ = fdt.nodes[path]["opp-hz"]
        struct.pack_into(">Q", data, off, 2208000000)           # an OPP this tool doesn't know
        with self.assertRaises(uvdtb.DtbError):
            uvdtb.tables(uvdtb.Fdt(bytes(data)))

    def test_dtc_still_reads_it(self):
        if not shutil.which("dtc"):
            self.skipTest("no dtc")
        out = uvdtb.patch(DTBS[0].read_bytes(), uvdtb.parse_spec(["l3", "gpu=l3"]))
        with tempfile.NamedTemporaryFile(suffix=".dtb") as f:
            f.write(out)
            f.flush()
            dts = subprocess.run(["dtc", "-q", "-I", "dtb", "-O", "dts", f.name], check=True,
                                 capture_output=True, text=True).stdout
        self.assertIn("opp-microvolt = <0x10c8e0 0x10c8e0 0x118c30>;", dts)     # 1992: 1100 1100 1150 mV


class CliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        r = Path(self.tmp.name)
        self.flash, self.state, self.sys, self.bin = r / "flash", r / "state", r / "sys", r / "bin"
        (self.flash / "device_trees").mkdir(parents=True)
        (self.flash / "extlinux").mkdir()
        self.dtb = self.flash / "device_trees" / "rk3568-anbernic-rg-ds.dtb"
        shutil.copy(DTBS[0], self.dtb)
        self.ext = self.flash / "extlinux" / "extlinux.conf"
        self.ext.write_text("LABEL ROCKNIX\n  LINUX /KERNEL\n  FDT /device_trees/rk3568-anbernic-rg-ds.dtb\n"
                            "  APPEND boot=UUID=1234 disk=UUID=5678 quiet console=tty0\n")
        # the tool, next to a uvstress stand-in (the real one is arm64) that passes unless UVSTRESS_FAIL is set
        self.bin.mkdir()
        for f in ("rocknixds-undervolt", "uvdtb.py"):
            shutil.copy(ROOT / "undervolt" / "device" / f, self.bin / f)
        (self.bin / "uvstress").write_text('#!/bin/sh\necho "uvstress $*" >> "$UV_STATE/stress.log"\n'
                                           '[ -z "$UVSTRESS_FAIL" ]\n')
        os.chmod(self.bin / "uvstress", 0o755)
        # cpufreq policy0 (scaling_cur_freq follows the pinned minimum) and vdd_cpu
        pol = self.sys / "devices/system/cpu/cpufreq/policy0"
        pol.mkdir(parents=True)
        for k, v in {"scaling_governor": "performance", "scaling_min_freq": "408000", "scaling_max_freq": "1992000",
                     "cpuinfo_min_freq": "408000",
                     "scaling_available_frequencies": "408000 600000 816000 1104000 1416000 1608000 1800000 1992000"
                     }.items():
            (pol / k).write_text(v + "\n")
        os.symlink("scaling_min_freq", pol / "scaling_cur_freq")
        self.reg = self.sys / "class/regulator/regulator.5"
        self.reg.mkdir(parents=True)
        (self.reg / "name").write_text("vdd_cpu\n")
        self.original = self.dtb.read_bytes()

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *args, ok=True, **env):
        e = dict(os.environ, UV_FLASH=str(self.flash), UV_STATE=str(self.state), UV_SYS=str(self.sys), **env)
        p = subprocess.run(["sh", str(self.bin / "rocknixds-undervolt"), *args], env=e, capture_output=True, text=True)
        if ok:
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        else:
            self.assertNotEqual(p.returncode, 0, p.stdout + p.stderr)
        return p.stdout + p.stderr

    def volts(self):
        return uvdtb.voltages(uvdtb.Fdt(self.dtb.read_bytes()))

    def trial(self):
        p = self.state / "trial"
        return p.read_text().strip() if p.exists() else None

    def test_apply_writes_trial_with_backups(self):
        self.run_tool("apply", "l3")
        self.assertEqual(self.volts()["cpu"], uvdtb.PRESETS["l3"]["cpu"])
        self.assertEqual((self.flash / "rocknixds-undervolt" / "stock.dtb").read_bytes(), self.original)
        self.assertEqual((self.flash / "rocknixds-undervolt" / "previous.dtb").read_bytes(), self.original)
        self.assertIn("stock.dtb", (self.flash / "rocknixds-undervolt" / "RECOVERY.txt").read_text())
        ext = self.ext.read_text()
        self.assertIn("console=tty0 " + TRIAL_ARG + "\n", ext)
        self.assertEqual(ext.count(TRIAL_ARG), 1)
        self.assertEqual(self.trial(), "0")

    def test_bad_spec_touches_nothing(self):
        self.run_tool("apply", "cpu1992=900", ok=False)        # under 1800 MHz's 1150
        self.run_tool("apply", "cpu1992=1200", ok=False)
        self.assertEqual(self.dtb.read_bytes(), self.original)
        self.assertNotIn(TRIAL_ARG, self.ext.read_text())
        self.assertFalse((self.flash / "rocknixds-undervolt").exists())

    def test_guard_reverts_an_unconfirmed_trial(self):
        self.run_tool("apply", "l3")
        self.run_tool("guard")                      # the trial's first boot
        self.assertEqual(self.trial(), "1")
        self.assertEqual(self.volts()["cpu"], uvdtb.PRESETS["l3"]["cpu"])
        self.run_tool("guard")                      # it booted again without passing: back to before apply
        self.assertIsNone(self.trial())
        self.assertEqual(self.dtb.read_bytes(), self.original)
        self.assertNotIn(TRIAL_ARG, self.ext.read_text())
        self.assertIn("restarted before its test passed", (self.state / "reverted").read_text())
        self.run_tool("guard")                      # nothing pending: nothing happens
        self.assertEqual(self.dtb.read_bytes(), self.original)

    def test_guard_reverts_to_the_previous_undervolt(self):
        self.run_tool("apply", "l1")
        self.run_tool("confirm")
        l1 = self.dtb.read_bytes()
        self.run_tool("apply", "l3")
        self.assertEqual((self.flash / "rocknixds-undervolt" / "stock.dtb").read_bytes(), self.original)
        self.run_tool("guard")
        self.run_tool("guard")
        self.assertEqual(self.dtb.read_bytes(), l1)

    def test_second_apply_keeps_the_tested_previous(self):
        self.run_tool("apply", "l1")
        self.run_tool("apply", "l3")                # before rebooting into l1: l1 was never tested
        self.assertEqual((self.flash / "rocknixds-undervolt" / "previous.dtb").read_bytes(), self.original)
        self.run_tool("guard")
        self.run_tool("guard")
        self.assertEqual(self.dtb.read_bytes(), self.original)

    def test_test_passes_and_confirms(self):
        # one lowered clock: the stand-in regulator reads one value
        self.run_tool("apply", "cpu1104=875")
        self.assertIn("reboot first", self.run_tool("test", ok=False))
        self.run_tool("guard")
        (self.reg / "microvolts").write_text("875000\n")       # what the kernel sets at 1104 MHz
        out = self.run_tool("test", "1")
        self.assertIn("PASSED", out)
        self.assertIsNone(self.trial())
        self.assertNotIn(TRIAL_ARG, self.ext.read_text())
        self.assertEqual(self.volts()["cpu"][1104], 875)
        stress = (self.state / "stress.log").read_text().split("\n")
        self.assertEqual(stress[:3], ["uvstress -t 60", "uvstress -b -t 60", "uvstress -b -t 60"])
        pol = self.sys / "devices/system/cpu/cpufreq/policy0"
        self.assertEqual((pol / "scaling_governor").read_text().strip(), "performance")     # put back
        self.assertEqual((pol / "scaling_min_freq").read_text().strip(), "408000")
        self.assertEqual((pol / "scaling_max_freq").read_text().strip(), "1992000")

    def test_failed_test_reverts(self):
        self.run_tool("apply", "cpu1104=850")
        self.run_tool("guard")
        (self.reg / "microvolts").write_text("850000\n")
        out = self.run_tool("test", "1", ok=False, UVSTRESS_FAIL="1")
        self.assertIn("FAILED at 1104 MHz", out)
        self.assertEqual(self.dtb.read_bytes(), self.original)
        self.assertIsNone(self.trial())
        self.assertNotIn(TRIAL_ARG, self.ext.read_text())

    def test_wrong_voltage_in_use_changes_nothing(self):
        self.run_tool("apply", "cpu1104=850")
        self.run_tool("guard")
        (self.reg / "microvolts").write_text("900000\n")       # still the old DTB
        out = self.run_tool("test", "1", ok=False)
        self.assertIn("reboot so the new DTB is in use", out)
        self.assertEqual(self.trial(), "1")
        self.assertEqual(self.volts()["cpu"][1104], 850)

    def test_off(self):
        self.run_tool("apply", "l3", "gpu=l3")
        self.run_tool("confirm")
        self.run_tool("off")
        self.assertEqual(self.dtb.read_bytes(), self.original)
        self.assertNotIn(TRIAL_ARG, self.ext.read_text())


if __name__ == "__main__":
    unittest.main()
