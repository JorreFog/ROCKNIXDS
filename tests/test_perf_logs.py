"""Which seconds of a performance log count as play. A stay in DraStic's menu (the bottom panel flips, the top
doesn't) or a second under 10 presents used to be read as a slow game. busy<top is only the slow play seconds
where DraStic was busy and the clock was below the hardware top, 1990 MHz when the log never recorded one."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT


def present(fps, dropped=0, top=60, bot=60, src=0, q=0, buf=0, repeat=0, dup=0, with_dup=True):
    dup_s = (" dup=%s" % dup) if with_dup else ""
    return ("[dsflip] present/s=%s commits=60 dropped=%s busy=0 flips top=%s bot=%s "
            "max-iv top=16688 bot=16687 us touch=0 drop-src=%s drop-q=%s drop-buf=%s%s repeat=%s"
            % (fps, dropped, top, bot, src, q, buf, dup_s, repeat))


def sample(log, mhz, game_cpu=40, ma=-400, status="Discharging", temp=50, hw=None, extra_log=()):
    d = {
        "game": {"rom": "Pokemon - Platinum Version (USA).nds", "shader": "ds-crisp"},
        "cpu_mhz": mhz,
        "game_cpu": game_cpu,
        "bat": {"ma": ma, "status": status},
        "temp": {"cpu": temp},
        "log": [log, *extra_log] if log else list(extra_log),
    }
    if hw is not None:
        d["cpu_hw_max_mhz"] = hw
    return d


class PerfLogsTest(unittest.TestCase):
    def test_menu_and_loading_stay_out_of_the_frame_numbers(self):
        root = Path(tempfile.mkdtemp())
        dev = root / "2437b8ffff"
        dev.mkdir()
        rows = [
            # menu even though almost nothing was presented: the bottom panel moved, the top did not
            sample(present(2.9, top=0, bot=3), 1992, game_cpu=2, temp=40),
            sample(present(0.0, top=0, bot=0), 1608, game_cpu=1, temp=41),
            # a full-speed second, and a line from before dup= was logged, still counts
            sample(present(60.0, with_dup=False), 1992, game_cpu=80, temp=55),
            sample(present(44.0, dropped=2, repeat=6, src=1, q=2, buf=3), 1800, game_cpu=130, hw=1992, temp=60,
                   extra_log=("[cpugov] 1992 -> 1800 MHz (light)", "[late latch]")),
            sample(present(50.0), 1992, game_cpu=150, hw=1992, temp=58,
                   extra_log=("[cpugov] 1800 -> 1992 MHz",)),
            sample(present(40.0), 1416, game_cpu=90, hw=1992),          # slow, but DraStic was not a core's worth
            sample(present(30.0), 1992, game_cpu=120),                  # no hardware max: 1992 is not below 1990
            sample(present(20.0), 1800, game_cpu=100),                  # 100% of a core is busy; 1800 is below 1990
            sample(None, 816, ma=-900, status="Charging", temp=62,
                   extra_log=("[dsflip] present/s=12.0 dropped=1 flips top=12 bot=12",)),  # truncated: not play
            {"summary": True, "rocknixds": "1.5.1", "profile": "balanced", "queue": 2,
             "game": {"rom": "Pokemon - Platinum Version (USA).nds", "shader": "ds-crisp"}},
        ]
        (dev / "20261003-120000_plat.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
        # a summary with no samples is not a session
        empty = root / "skipme"
        empty.mkdir()
        (empty / "20260101-010101_x.jsonl").write_text(json.dumps({"summary": True, "game": {"rom": "nope"}}) + "\n")
        edges = root / "edge01zz"
        edges.mkdir()
        edge_rows = [
            sample(present(10.0, top=10, bot=10), 1000, game_cpu=200, hw=1992, ma=None),
            sample(present(9.9, top=9, bot=9), 1000, game_cpu=200, hw=1992),
            sample(present(57.0, top=57, bot=57), 1000, game_cpu=200, hw=1992),
            sample(present(56.9, top=56, bot=56), 1000, game_cpu=200, hw=1992),
            sample(present(0.5, top=0, bot=1), 1000, game_cpu=1),
            {"summary": True, "rocknixds": "edge", "profile": "battery", "queue": 3,
             "game": {"rom": "Edges.nds", "shader": "none"}},
        ]
        # ma=None still builds a bat dict; drop it so a missing battery isn't treated as discharge
        edge_rows[0].pop("bat")
        (edges / "20260101-010101_e.jsonl").write_text("".join(json.dumps(r) + "\n" for r in edge_rows))
        nosum = root / "nosum1"
        nosum.mkdir()
        (nosum / "20260202-000000_n.jsonl").write_text(json.dumps(
            sample(present(59.9), 1416, game_cpu=10, hw=1992)) + "\n")

        out = self._run(root)
        self.assertFalse(any(ln.startswith("skipme") for ln in out.splitlines()), out)
        plat = self._line(out, "2437b8")
        self.assertIn("1.5.1", plat)
        self.assertIn("balanced", plat)
        self.assertIn("ds-crisp", plat)
        self.assertNotIn("(no summary)", plat)
        # play, menu, idle, fps, <59.5%, drop/s, rep/s, src, q, buf, MHz, mA, T, gov, late, busy<top, clocks
        self.assertIn(
            "    6    1    1 40.67   83.3  0.333  1.00  0.17  0.33  0.50  1712   400   62   2    1      40%  "
            "816:11,1416:11,1608:11,1800:22,1992:44",
            plat)
        edge = self._line(out, "edge01")
        self.assertIn("    3    1    1", edge)
        self.assertIn("100%", edge)
        self.assertNotIn("40%", edge)
        bare = self._line(out, "nosum1")
        self.assertIn("(no summary)", bare)
        self.assertIn("Pokemon - Platinum Version (U", bare)

    def _run(self, root):
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "perf-logs.py"), str(root)],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        return r.stdout

    def _line(self, out, dev):
        lines = [ln for ln in out.splitlines() if ln.startswith(dev)]
        self.assertEqual(len(lines), 1, out)
        return lines[0]


if __name__ == "__main__":
    unittest.main()
