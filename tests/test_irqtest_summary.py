"""tools/irqtest-summary.py: interrupt rates per CPU from two /proc/interrupts snapshots, and a layout's runs
gathered by the layout irqab.sh recorded (not by the file name)."""
import json
import os
import tempfile
import unittest

from loadmod import load

S = load("irqtest_summary", "tools/irqtest-summary.py")

HEAD = "           CPU0       CPU1       CPU2       CPU3       \n"


def interrupts(timer, i2c, ipi):
    return (HEAD +
            f" 11: {timer[0]:10d} {timer[1]:10d} {timer[2]:10d} {timer[3]:10d}     GICv3  27 Level     arch_timer\n"
            f" 45: {i2c[0]:10d} {i2c[1]:10d} {i2c[2]:10d} {i2c[3]:10d}     GICv3  79 Level     fe5e0000.i2c\n"
            f" 60:          0          0          0          0     GICv3 100 Edge      unused\n"
            f"IPI0: {ipi[0]:10d} {ipi[1]:10d} {ipi[2]:10d} {ipi[3]:10d}       Rescheduling interrupts\n"
            "Err:          0\n")


def softirqs(timer):
    return ("                    CPU0       CPU1       CPU2       CPU3       \n"
            "          HI:          0          0          0          0\n"
            f"       TIMER: {timer[0]:10d} {timer[1]:10d} {timer[2]:10d} {timer[3]:10d}\n")


def write(p, s):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(s)


def run(out, tag, layout, i2c_cpu, drops):
    d = f"{out}/probe/{tag}.irq"
    write(f"{d}/layout", layout + "\n")
    write(f"{d}/before.uptime", "100.00\n")
    write(f"{d}/after.uptime", "110.00\n")
    i2c0, i2c1 = [0] * 4, [0] * 4
    i2c1[i2c_cpu] = 5000
    write(f"{d}/before.interrupts", interrupts([1000] * 4, i2c0, [0] * 4))
    write(f"{d}/after.interrupts", interrupts([3500, 3500, 2000, 1000], i2c1, [100, 0, 0, 0]))
    write(f"{d}/before.softirqs", softirqs([0] * 4))
    write(f"{d}/after.softirqs", softirqs([10, 20, 30, 40]))
    write(f"{d}/before.stat", "cpu  100 0 100 700 0 0 0 0 0 0\n")
    write(f"{d}/after.stat", "cpu  200 0 200 1400 0 50 50 0 0 0\n")
    write(f"{d}/before.idle", "cpu0 state0 WFI 0 0\ncpu0 state1 cpu-sleep 100 0\n")
    write(f"{d}/after.idle", "cpu0 state0 WFI 50 0\ncpu0 state1 cpu-sleep 600 0\n")
    if "-hg-" in tag:
        write(f"{out}/probe/{tag}.txt", f"...\nframes: 90 s, 59.95 presents/s, {drops:.2f} drops/s\n")
        write(f"{out}/probe/{tag}.json", json.dumps({"cpu_avg_mhz": 1500, "bat_ma": -900, "temp_c": [50, 60, 55],
                                                     "cpu_busy_pct": 140}))
        write(f"{out}/logs/hp-{tag}.log",
              "[dsflip] present/s=60.0 commits=60 dropped=0 busy=0 flips top=60 bot=60 max-iv top=16700 bot=16710 us\n"
              "[dsflip] present/s=59.0 commits=59 dropped=1 busy=0 flips top=59 bot=59 max-iv top=33400 bot=16710 us\n")
    else:
        write(f"{out}/logs/sg-{tag}.log", "".join(f"[dsflip] present/s={v} x\n" for v in [30, 60] + [50] * 40))


class Rates(unittest.TestCase):
    def test_per_cpu_and_sources(self):
        with tempfile.TemporaryDirectory() as out:
            run(out, "irq-1-hg-irq-3", "irq=3", 3, 0.1)
            x = S.irq_run(f"{out}/probe/irq-1-hg-irq-3.irq")
            self.assertEqual(x["secs"], 10.0)
            # timer 250/250/100/0 + i2c 500 on CPU3 + IPI 10 on CPU0
            self.assertEqual(x["per_cpu"], [260.0, 250.0, 100.0, 500.0])
            names = [s[2] for s in x["sources"]]
            self.assertEqual(names[:2], ["GICv3 27 Level arch_timer", "GICv3 79 Level fe5e0000.i2c"])
            self.assertNotIn("GICv3 100 Edge unused", names)
            self.assertEqual(x["soft"]["TIMER"], [1.0, 2.0, 3.0, 4.0])
            self.assertAlmostEqual(x["split"]["irq"], 5.0)
            self.assertEqual(x["idle"][("cpu0", "cpu-sleep")], 50.0)

    def test_layouts_grouped_from_their_record(self):
        with tempfile.TemporaryDirectory() as out:
            for r in (1, 2):
                run(out, f"irq-{r}-hg-base", "base", 0, 0.2 * r)
                run(out, f"irq-{r}-hg-irq-3_app-0-2", "irq=3,app=0-2", 3, 0.0)
                run(out, f"irq-{r}-sg-base", "base", 0, 0)
            runs = S.collect(out)
            self.assertEqual(sorted(runs), ["base", "irq=3,app=0-2"])
            self.assertEqual(len(runs["base"]["hg"]), 2)
            self.assertEqual(len(runs["base"]["sg"]), 2)
            hg = runs["base"]["hg"][0]
            self.assertEqual((hg["presents"], hg["mhz"], hg["temp"]), (59.95, 1500, 60))
            self.assertEqual(hg["late"], 30.0)          # 1 of 2 seconds late: 30 a minute
            sg = runs["base"]["sg"][0]
            self.assertEqual(sg["fps_last30"], 50.0)
            text = S.summary(out)
            self.assertIn("0.300 (0.200..0.400)", text)  # base's drops over the two rounds
            self.assertIn("irq=3,app=0-2", text)


if __name__ == "__main__":
    unittest.main()
