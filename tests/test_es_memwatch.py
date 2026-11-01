"""es-memwatch.sh restarts EmulationStation when it grows past half the handheld's memory.

A restart kills whatever ES is running, including RetroArch. The 2026-10-05 valve restarted
whenever a DS game was not in its own unit; a game inside ES (HTTP 200 from /runningGame) has
to hold the restart, and no answer at all still has to restart. Host tests source the script
with ROCKNIXDS_MEMWATCH_FUNCS=1 and call those decisions.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

SCRIPT = ROOT / "dii-ess-aye" / "overlay" / "scripts" / "es-memwatch.sh"


def sh(body, env=None):
    e = dict(os.environ)
    e["ROCKNIXDS_MEMWATCH_FUNCS"] = "1"
    if env:
        e.update(env)
    r = subprocess.run(["bash", "-c", body], capture_output=True, text=True, env=e)
    if r.returncode != 0:
        raise AssertionError(r.stdout + r.stderr)
    return r.stdout.strip()


class MemwatchTest(unittest.TestCase):
    def limit(self, total, override=None):
        env = {} if override is None else {"ROCKNIXDS_ES_MEMLIMIT_MB": override}
        return int(sh('. "%s"; memwatch_limit_kb %s' % (SCRIPT, total), env))

    def restart(self, rss, limit, active, code):
        out = sh('. "%s"; if memwatch_should_restart %s %s %s %s; then echo yes; else echo no; fi'
                 % (SCRIPT, rss, limit, active or '""', code or '""'))
        return out == "yes"

    def test_limit_is_half_the_memory_unless_overridden(self):
        # 975 MB is 998400 kB; half is 487 MB, the figure in the script's comment.
        self.assertEqual(self.limit(998400), 487 * 1024)
        self.assertEqual(self.limit(2047), 0)          # under 2 MB of MemTotal: nothing to cut in half
        self.assertEqual(self.limit(998400, "0"), 0)   # 0: the valve is off
        self.assertEqual(self.limit(998400, "100"), 100 * 1024)

    def test_restart_spares_a_game_and_a_dead_menu_still_restarts(self):
        limit = 487 * 1024
        over = limit + 1
        self.assertFalse(self.restart(over, limit, "", "200"))       # RetroArch inside ES
        self.assertFalse(self.restart(over, limit, "active", "201")) # a DS game in its own unit
        self.assertFalse(self.restart(limit, limit, "", "201"))      # exactly the limit stays
        self.assertFalse(self.restart(over, 0, "", "201"))           # valve off
        self.assertTrue(self.restart(over, limit, "", "201"))        # menu, nothing running
        self.assertTrue(self.restart(over, limit, "", "000"))        # curl could not connect
        self.assertTrue(self.restart(over, limit, "", ""))           # ES did not answer

    def test_a_16mb_move_or_an_hour_is_logged_and_a_drop_counts(self):
        self.assertEqual(sh('. "%s"; memwatch_delta_kb 200000 10000' % SCRIPT), "190000")
        self.assertEqual(sh('. "%s"; memwatch_delta_kb 10000 200000' % SCRIPT), "190000")
        def logged(delta, now, last):
            out = sh('. "%s"; if memwatch_should_log %s %s %s; then echo yes; else echo no; fi'
                     % (SCRIPT, delta, now, last))
            return out == "yes"
        self.assertFalse(logged(16383, 100, 100))
        self.assertTrue(logged(16384, 100, 100))
        self.assertFalse(logged(0, 3599, 0))
        self.assertTrue(logged(0, 3600, 0))

    def test_the_log_rotates_past_256kb_and_keeps_the_new_line(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "es-mem.log"
            log.write_bytes(b"x" * 262144)
            sh('. "%s"; say "still here"' % SCRIPT, {"ROCKNIXDS_ES_MEMLOG": str(log)})
            self.assertFalse((Path(d) / "es-mem.log.1").exists())
            self.assertIn("still here", log.read_text())
            log.write_bytes(b"y" * 262145)
            sh('. "%s"; say "after rotate"' % SCRIPT, {"ROCKNIXDS_ES_MEMLOG": str(log)})
            old = (Path(d) / "es-mem.log.1").read_bytes()
            self.assertEqual(old[:1], b"y")
            self.assertEqual(len(old), 262145)
            self.assertIn("after rotate", log.read_text())
            self.assertNotIn("y", log.read_text())
