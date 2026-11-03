"""What leaves the handheld after a game. perf-session.py builds the performance log on quit and, only when
the player allowed it, uploads it. A per-game No must beat a global Yes, a log from a test launch must stay
on the device, and a password, a GitHub token or a RetroAchievements name must not be in the file that would
be uploaded. The script's own selftest covers the upload conversation; it is run here with the clock in UTC,
because the queued file's name is a local timestamp of a fixed sample."""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT, load


SCRIPT = ROOT / "dsflip" / "device" / "perf-session.py"


def sample(t, fps=60, mhz=1000, ma=-3600, pct=80, temp=40, game=True, extra=None):
    row = {
        "t": t,
        "cpu_mhz": mhz,
        "bat": {"pct": pct, "status": "Discharging", "ma": ma},
        "temp": {"soc": temp},
        "log": [],
    }
    if game:
        row["game"] = {"system": "nds", "rom": "Heart Gold.nds"}
    if fps is not None:
        row["log"].append("[dsflip] present/s=%s dropped=0 commits=60" % fps)
    if extra:
        row["log"].extend(extra)
    return row


class PerfSessionSelftest(unittest.TestCase):
    def test_the_scripts_own_selftest_passes_with_the_clock_in_utc(self):
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin"),
            "TZ": "UTC",
            "HOME": os.environ.get("HOME", "/tmp"),
            "LANG": "C",
        }
        r = subprocess.run([sys.executable, str(SCRIPT), "selftest"], env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("perf-session selftest ok", r.stdout)


class PerfSessionPrivacyTest(unittest.TestCase):
    def setUp(self):
        self.perf = load("perf_session", "dsflip/device/perf-session.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.state = self.root / "state"
        self.state.mkdir()
        self.cfg = self.root / "system.cfg"
        self.perf.STATE = str(self.state)
        self.perf.CFG = str(self.cfg)
        self.perf.os.environ["ROCKNIXDS_PERF_SKIP_GITHUB_CHECK"] = "1"
        self.calls = []
        self._urlopen = self.perf.urllib.request.urlopen

        def refuse(*args, **kwargs):
            self.calls.append(args[0] if args else kwargs)
            raise AssertionError("the log tried to leave the machine")

        self.perf.urllib.request.urlopen = refuse

    def tearDown(self):
        self.perf.urllib.request.urlopen = self._urlopen
        self.tmp.cleanup()

    def test_nothing_is_uploaded_until_the_player_says_yes(self):
        self.assertFalse(self.perf.allowed("Heart Gold.nds"))  # no system.cfg yet: the question has not been asked
        self.cfg.write_text(
            "audio.volume=80\n"
            "nds.share_performance_logs=1\n"
            "nds.share_performance_logs=0\n"  # the last global line is the one in force, as the shell uses tail -n1
            'nds["Heart Gold.nds"].share_performance_logs=\n'  # blank: this game follows the global answer
            'nds["Secret.nds"].share_performance_logs=0\n'
            'nds["Public.nds"].share_performance_logs=yes\n'
        )
        self.assertFalse(self.perf.allowed("Heart Gold.nds"))
        self.assertFalse(self.perf.allowed("Secret.nds"))
        self.assertTrue(self.perf.allowed("Public.nds"))  # this game's Yes stands while the global answer is No
        self.cfg.write_text(
            "nds.share_performance_logs=0\n"
            "nds.share_performance_logs=1\n"
            'nds["Secret.nds"].share_performance_logs=0\n'
            'nds["Public.nds"].share_performance_logs=yes\n'
        )
        self.assertTrue(self.perf.allowed("Heart Gold.nds"))   # no line of its own: the last global line, Yes
        self.assertFalse(self.perf.allowed("Secret.nds"))      # this game's No beats that Yes
        self.assertTrue(self.perf.allowed("Public.nds"))

    def test_saving_the_answer_keeps_every_other_setting(self):
        self.cfg.write_text(
            "audio.volume=80\n"
            "nds.share_performance_logs=0\n"
            "nds.share_performance_logs=0\n"
            "wifi.enabled=1\n"
        )
        self.perf.write_share("1")
        text = self.cfg.read_text().splitlines()
        self.assertEqual(text, ["audio.volume=80", "nds.share_performance_logs=1", "wifi.enabled=1"])
        self.assertTrue(self.perf.allowed())

    def test_a_quit_log_drops_secrets_and_ignores_charging_current(self):
        rows = [
            sample(1000, fps=60, mhz=1000, ma=-3600, pct=80, temp=40,
                   extra=["password=hunter2", "api_key=abcdef", "[ra] logged in as playername",
                          "saw github_pat_shouldnotappear and ghp_abcdefghijklmnopqrstuvwxyz"]),
            sample(1002, fps=58, mhz=1000, ma=-3600, pct=78, temp=41),
            sample(1004, fps=None, mhz=2000, ma=9000, pct=90, temp=55, game=False),  # charging, no presents
        ]
        folder = self.root / "active"
        folder.mkdir()
        (folder / "samples.jsonl").write_text("not json\n" + "".join(json.dumps(r) + "\n" for r in rows))
        (folder / "meta.json").write_text(json.dumps({"profile": "balanced", "queue": "1", "rom": "ignored.nds"}))
        name, body = self.perf.build_session(str(folder))
        self.assertTrue(name.endswith("_Heart_Gold.nds.jsonl"), name)
        self.assertNotIn("/", name)
        for secret in ("hunter2", "abcdef", "playername", "github_pat_", "ghp_"):
            self.assertNotIn(secret, body)
        self.assertIn("[redacted]", body)
        summary = json.loads(body.strip().splitlines()[-1])
        self.assertTrue(summary["summary"])
        self.assertEqual(summary["game"]["rom"], "Heart Gold.nds")
        self.assertEqual(summary["seconds"], 4)
        self.assertEqual(summary["fps_avg"], 59.0)          # the charging second had no presents, so it is not a frame time
        self.assertEqual(summary["fps_min"], 58)
        self.assertEqual(summary["cpu_mhz_avg"], 1333)      # it does count toward the clock average
        self.assertEqual(summary["mah_drawn"], 2)           # +9000 mA while charging is not drawn from the battery
        self.assertEqual(summary["battery_pct"], [80, 90])
        self.assertEqual(summary["temp_max"]["soc"], 55)
        self.assertEqual(summary["profile"], "balanced")
        self.assertEqual(summary["queue"], 1)
        self.assertNotIn("/", self.perf.safe_name("../../etc/passwd"))
        self.assertEqual(self.perf.safe_name("../../etc/passwd"), ".._.._etc_passwd")
        self.assertEqual(self.perf.safe_name(""), "game")

    def test_sharing_off_keeps_the_log_on_the_device(self):
        self.cfg.write_text("nds.share_performance_logs=0\n")
        folder = self.root / "active"
        folder.mkdir()
        (folder / "samples.jsonl").write_text(json.dumps(sample(1000, extra=["password=hunter2"])) + "\n")
        self.perf.cmd_finish(str(folder))
        self.assertEqual(self.calls, [])
        self.assertFalse(folder.exists())
        logs = list((self.state / "logs").glob("*.jsonl"))
        self.assertEqual(len(logs), 1)
        self.assertNotIn("hunter2", logs[0].read_text())

    def test_a_test_launch_is_not_uploaded(self):
        flag = "/tmp/rocknixds-testing"
        existed = os.path.exists(flag)
        if not existed:
            os.close(os.open(flag, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644))
        try:
            self.perf.write_share("1")
            folder = self.root / "active"
            folder.mkdir()
            (folder / "samples.jsonl").write_text(json.dumps(sample(1000)) + "\n")
            self.perf.cmd_finish(str(folder))
            self.assertEqual(self.calls, [])
            self.assertTrue((self.state / "perf.log").read_text().find("not uploaded (test launch)") >= 0)
        finally:
            if not existed and os.path.exists(flag):
                os.remove(flag)

    def test_a_bad_device_or_file_name_is_not_queued(self):
        text = json.dumps({"summary": True}) + "\n"
        bad_file = self.state / "logs" / "not a name.jsonl"
        bad_file.parent.mkdir()
        bad_file.write_text(text)
        self.assertFalse(self.perf.try_upload(str(bad_file), text, {"summary": True, "device": "abc123def456"}))
        slipped = self.state / "logs" / "20231114-221320_Heart_Gold.nds.jsonl"
        slipped.write_text(text)
        self.assertFalse(self.perf.try_upload(str(slipped), text, {"summary": True, "device": "../etc"}))
        self.assertFalse(self.perf.github_has("../etc", "20231114-221320_Heart_Gold.nds.jsonl"))
        self.assertFalse(self.perf.github_has("abc123def456", "../x.jsonl"))
        self.assertEqual(self.calls, [])
        (self.state / "device-id").write_text("../etc/passwd\n")
        made = self.perf.device_id()
        self.assertIsNotNone(re.fullmatch(r"[0-9a-f]{16}", made))
        self.assertEqual((self.state / "device-id").read_text().strip(), made)
        (self.state / "device-id").write_text("abc123def456\n")
        self.assertEqual(self.perf.device_id(), "abc123def456")

    def test_old_logs_are_pruned_and_an_empty_session_leaves_nothing(self):
        logs = self.state / "logs"
        logs.mkdir()
        for i in range(42):
            (logs / ("202001%02d-000000_g.jsonl" % i)).write_text("{}\n")
        old = logs / "20200100-000000_g.jsonl"
        (logs / (old.name + ".uploaded")).write_text("x\n")
        (logs / (old.name + ".queued")).write_text("1\n")
        self.perf.prune(str(logs))
        left = sorted(p.name for p in logs.iterdir())
        self.assertEqual(len([n for n in left if n.endswith(".jsonl")]), 40)
        self.assertFalse(old.exists())
        self.assertNotIn(old.name + ".uploaded", left)
        self.assertNotIn(old.name + ".queued", left)
        self.assertTrue((logs / "20200141-000000_g.jsonl").exists())
        folder = self.root / "empty"
        folder.mkdir()
        (folder / "samples.jsonl").write_text("not json\n")
        self.perf.cmd_finish(str(folder))
        self.assertFalse(folder.exists())
        self.assertEqual(self.calls, [])
