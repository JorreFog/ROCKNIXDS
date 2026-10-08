"""Apply recommended settings (#45): with a game's (or the DS system's) nds.recommended=1, the launcher reads the
picture and speed settings from ROCKNIXDS's defaults and recommended.cfg instead of the game's own; the game's
other settings stay its own."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

LIB = ROOT / "dsflip" / "device" / "nds-settings.sh"
TABLE = ROOT / "dsflip" / "device" / "recommended.cfg"
COD = "Call of Duty - World at War (USA).nds"


class RecommendedTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cfg = self.root / "system.cfg"
        self.table = self.root / "recommended.cfg"
        self.table.write_text("# a comment|power_profile=battery\n"
                              "zelda*|resolution3d=3x follow_3d_renderer=1\n"
                              "ZELDA - SPIRIT*|resolution3d=2x\n")

    def tearDown(self):
        self.tmp.cleanup()

    def get(self, game, key, table=None, fn="nds_get"):
        env = dict(os.environ, NDS_CFG=str(self.cfg), NDS_RECOMMENDED=str(table or self.table))
        r = subprocess.run(["sh", "-c", '. "$0"; %s "$1" "$2"; echo "rc=$?"' % fn, str(LIB), game, key],
                           env=env, capture_output=True, text=True, check=True)
        out = r.stdout.splitlines()
        return out[0] if len(out) > 1 else "", out[-1]

    def test_off_reads_the_game_then_the_system(self):
        self.cfg.write_text('nds.power_profile=battery\nnds["A.nds"].power_profile=performance\n')
        self.assertEqual(self.get("A.nds", "power_profile")[0], "performance")
        self.assertEqual(self.get("B.nds", "power_profile")[0], "battery")
        self.assertEqual(self.get("B.nds", "renderer")[0], "")

    def test_on_uses_the_defaults_and_the_table(self):
        self.cfg.write_text('nds.power_profile=battery\nnds.shader=ds-fsr\nnds.resume_on_quit=0\n'
                            'nds["Zelda - Spirit Tracks.nds"].recommended=1\nnds["Zelda - Spirit Tracks.nds"].renderer=drastic\n'
                            'nds["Zelda - Phantom Hourglass.nds"].recommended=1\n')
        st, ph = "Zelda - Spirit Tracks.nds", "Zelda - Phantom Hourglass.nds"
        self.assertEqual(self.get(st, "power_profile")[0], "balanced")
        self.assertEqual(self.get(st, "renderer")[0], "superdrastic")
        self.assertEqual(self.get(st, "shader")[0], "none")
        self.assertEqual(self.get(st, "hires_3d")[0], "1")
        self.assertEqual(self.get(st, "texture_filter")[0], "nearest")
        self.assertEqual(self.get(st, "resolution3d")[0], "2x")          # the later line wins
        self.assertEqual(self.get(ph, "resolution3d")[0], "3x")
        self.assertEqual(self.get(ph, "follow_3d_renderer")[0], "1")
        self.assertEqual(self.get(st, "follow_3d_renderer")[0], "1")     # the zelda* line, not overridden
        self.assertEqual(self.get(st, "resume_on_quit")[0], "0")         # not a picture or speed setting
        # a profile set by the table counts as the game's choice (3x then keeps it); a default doesn't
        self.assertEqual(self.get(ph, "resolution3d", fn="nds_chosen")[1], "rc=0")
        self.assertEqual(self.get(ph, "power_profile", fn="nds_chosen")[1], "rc=1")

    def test_system_wide_switch_and_the_shipped_table(self):
        self.cfg.write_text("nds.recommended=1\nnds.power_profile=battery\n")
        self.assertEqual(self.get(COD, "power_profile", table=TABLE)[0], "performance")
        self.assertEqual(self.get("Other.nds", "power_profile", table=TABLE)[0], "balanced")
        self.cfg.write_text('nds.recommended=1\nnds["Other.nds"].recommended=0\nnds.power_profile=battery\n')
        self.assertEqual(self.get("Other.nds", "power_profile", table=TABLE)[0], "battery")

    def test_shipped_table_parses(self):
        for line in TABLE.read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            pat, kvs = line.split("|")
            self.assertTrue(pat.strip())
            for kv in kvs.split():
                key, val = kv.split("=")
                self.assertIn(key, ("power_profile", "renderer", "resolution3d", "texture_filter", "shader",
                                    "hires_3d", "threaded_3d", "follow_3d_renderer"))
                self.assertTrue(val)


if __name__ == "__main__":
    unittest.main()
