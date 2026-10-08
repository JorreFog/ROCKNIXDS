"""Where DraStic's saves and savestates go (#42, #56). ROCKNIX's start_drastic.sh points DraStic's backup/ at
roms/nds and savestates/ at roms/savestates/nds before every launch; save-dirs.sh then points them where the DS
system's options say and moves DraStic's files along without ever losing one."""
import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

from loadmod import ROOT

SCRIPT = ROOT / "dsflip" / "device" / "save-dirs.sh"


class SaveDirsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.d = self.root / "drastic"
        self.roms = self.root / "roms"
        self.cfg = self.root / "system.cfg"
        self.state = self.root / "rocknixds"
        (self.d / "dsflip").mkdir(parents=True)
        (self.roms / "nds").mkdir(parents=True)
        (self.roms / "savestates" / "nds").mkdir(parents=True)
        self.cfg.write_text("")
        self.rocknix_launch()

    def tearDown(self):
        self.tmp.cleanup()

    def rocknix_launch(self):
        """What start_drastic.sh does before every launch: its own links, whatever was there."""
        for name, target in (("backup", self.roms / "nds"), ("savestates", self.roms / "savestates" / "nds")):
            p = self.d / name
            if p.is_symlink() or p.is_file():
                p.unlink()
            elif p.is_dir():
                import shutil
                shutil.rmtree(p)
            p.symlink_to(target)

    def run_script(self, *args):
        env = dict(os.environ, SAVEDIRS_DRASTIC=str(self.d), SAVEDIRS_ROMS=str(self.roms),
                   SAVEDIRS_CFG=str(self.cfg), SAVEDIRS_STATE=str(self.state))
        r = subprocess.run(["sh", str(SCRIPT), *args], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r

    def link(self, name):
        return Path(os.readlink(self.d / name))

    def test_unset_leaves_everything_alone(self):
        (self.roms / "nds" / "Game.dsv").write_text("save")
        custom = self.root / "mine"
        custom.mkdir()
        (self.d / "backup").unlink()
        (self.d / "backup").symlink_to(custom)            # a link changed by hand
        self.run_script()
        self.assertEqual(self.link("backup"), custom)
        self.assertEqual(self.link("savestates"), self.roms / "savestates" / "nds")
        self.assertTrue((self.roms / "nds" / "Game.dsv").exists())
        self.assertFalse(self.state.exists())

    def test_saves_folder_moves_saves_and_states_and_back(self):
        (self.roms / "nds" / "Game.dsv").write_text("save")
        (self.roms / "nds" / "Game.nds").write_text("rom")
        (self.roms / "savestates" / "nds" / "Game_0.dss").write_text("state")
        (self.roms / "savestates" / "nds" / "notes.txt").write_text("not DraStic's")
        self.cfg.write_text("nds.saves_dir=saves\nnds.states_dir=saves\n")
        self.run_script()
        saves = self.roms / "saves" / "nds"
        self.assertEqual(self.link("backup"), saves)
        self.assertEqual(self.link("savestates"), saves / "states")
        self.assertEqual((saves / "Game.dsv").read_text(), "save")
        self.assertEqual((saves / "states" / "Game_0.dss").read_text(), "state")
        self.assertTrue((self.roms / "nds" / "Game.nds").exists())           # the games stay
        self.assertFalse((self.roms / "nds" / "Game.dsv").exists())
        self.assertTrue((self.roms / "savestates" / "nds" / "notes.txt").exists())
        self.assertEqual((self.state / "saves-dir").read_text().strip(), str(saves))

        # the next launch: ROCKNIX's links again, ours again, nothing to move
        self.rocknix_launch()
        self.run_script()
        self.assertEqual(self.link("backup"), saves)

        # back to the default: from the recorded folder to ROCKNIX's
        self.cfg.write_text("")
        self.rocknix_launch()
        self.run_script()
        self.assertEqual(self.link("backup"), self.roms / "nds")
        self.assertEqual((self.roms / "nds" / "Game.dsv").read_text(), "save")
        self.assertEqual((self.roms / "savestates" / "nds" / "Game_0.dss").read_text(), "state")
        self.assertFalse((self.state / "saves-dir").exists())
        self.assertFalse((self.state / "states-dir").exists())

    def test_a_name_in_both_places_keeps_the_newer_and_the_older(self):
        saves = self.roms / "saves" / "nds"
        saves.mkdir(parents=True)
        old, new = saves / "Game.dsv", self.roms / "nds" / "Game.dsv"
        old.write_text("older")
        new.write_text("newer")
        t = time.time()
        os.utime(old, (t - 100, t - 100))
        os.utime(new, (t, t))
        (self.roms / "nds" / "Other.dsv").write_text("older other")
        (saves / "Other.dsv").write_text("newer other")
        os.utime(self.roms / "nds" / "Other.dsv", (t - 100, t - 100))
        (self.roms / "nds" / "Same.dsv").write_text("same")
        (saves / "Same.dsv").write_text("same")
        self.cfg.write_text("nds.saves_dir=saves\n")
        self.run_script()
        self.assertEqual((saves / "Game.dsv").read_text(), "newer")
        kept = [p for p in saves.iterdir() if p.name.startswith("Game.dsv.older-")]
        self.assertEqual([p.read_text() for p in kept], ["older"])
        self.assertEqual((saves / "Other.dsv").read_text(), "newer other")
        left = [p for p in (self.roms / "nds").iterdir() if p.name.startswith("Other.dsv.older-")]
        self.assertEqual([p.read_text() for p in left], ["older other"])
        self.assertFalse((self.roms / "nds" / "Same.dsv").exists())
        self.assertEqual((saves / "Same.dsv").read_text(), "same")

    def test_a_chosen_option_takes_a_hand_made_link_along(self):
        custom = self.root / "mine"
        custom.mkdir()
        (custom / "Game.dsv").write_text("save")
        (self.d / "backup").unlink()
        (self.d / "backup").symlink_to(custom)
        self.cfg.write_text("nds.saves_dir=roms\n")
        self.run_script()
        self.assertEqual(self.link("backup"), self.roms / "nds")
        self.assertEqual((self.roms / "nds" / "Game.dsv").read_text(), "save")

    def test_a_real_folder_is_emptied_and_kept_with_other_files(self):
        (self.d / "backup").unlink()
        (self.d / "backup").mkdir()
        (self.d / "backup" / "Game.dsv").write_text("save")
        (self.d / "backup" / "readme").write_text("x")
        self.cfg.write_text("nds.saves_dir=saves\n")
        self.run_script()
        self.assertEqual((self.roms / "saves" / "nds" / "Game.dsv").read_text(), "save")
        self.assertTrue((self.d / "backup").is_dir() and not (self.d / "backup").is_symlink())
        (self.d / "backup" / "readme").unlink()
        self.run_script()
        self.assertEqual(self.link("backup"), self.roms / "saves" / "nds")

    def test_stock_puts_everything_back(self):
        (self.roms / "savestates" / "nds" / "Game_1.dss").write_text("state")
        self.cfg.write_text("nds.states_dir=saves\n")
        self.run_script()
        self.assertFalse((self.roms / "savestates" / "nds" / "Game_1.dss").exists())
        self.run_script("--stock")
        self.assertEqual(self.link("savestates"), self.roms / "savestates" / "nds")
        self.assertEqual((self.roms / "savestates" / "nds" / "Game_1.dss").read_text(), "state")
        self.assertFalse((self.state / "states-dir").exists())


if __name__ == "__main__":
    unittest.main()
