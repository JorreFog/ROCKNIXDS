"""ES gamelist updates for a DS session. libdsflip stops ES, so this script is the only
record of play count, last played and time played (issue #3)."""
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from loadmod import load


WHEN = "20261001T100302"


class PlaystatsTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("playstats", "dsflip/device/playstats.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.recovery = self.root / "recovery"
        self.settings = self.root / "es_settings.cfg"
        self.sysroot = self.root / "roms" / "nds"
        self.sysroot.mkdir(parents=True)
        self.mod.RECOVERY = str(self.recovery)
        self.mod.ES_SETTINGS = str(self.settings)
        self._strftime = self.mod.time.strftime
        self.mod.time.strftime = self._frozen

    def tearDown(self):
        self.mod.time.strftime = self._strftime
        self.tmp.cleanup()

    def _frozen(self, fmt, *args):
        self.assertEqual(fmt, "%Y%m%dT%H%M%S")
        return WHEN

    def _gamelist(self, games):
        lines = ['<?xml version="1.0"?>', "<gameList>"]
        for g in games:
            lines.append("\t<game>")
            for key, value in g.items():
                lines.append("\t\t<%s>%s</%s>" % (key, value, key))
            lines.append("\t</game>")
        lines.append("</gameList>")
        path = self.sysroot / "gamelist.xml"
        path.write_text("\n".join(lines) + "\n")
        return path

    def _run(self, rom, secs):
        self.mod.sys.argv = ["playstats.py", str(rom), str(secs)]
        self.mod.main()

    def _recovery(self, rom):
        rel = Path(rom).relative_to(self.sysroot)
        return self.recovery / "nds" / (str(rel.with_suffix(".xml")))

    def _read(self, rom):
        root = ET.parse(self._recovery(rom)).getroot()
        games = root.findall("game")
        self.assertEqual(len(games), 1)
        g = games[0]
        return root, {c.tag: c.text for c in g}

    def test_new_game_records_count_time_and_date(self):
        rom = self.sysroot / "Heart Gold.nds"
        rom.write_bytes(b"")
        self._run(rom, 15)
        root, g = self._read(rom)
        self.assertEqual(root.get("parentHash"), "0")
        self.assertEqual(g["path"], "./Heart Gold.nds")
        self.assertEqual(g["name"], "Heart Gold")
        self.assertEqual(g["playcount"], "1")
        self.assertEqual(g["gametime"], "15")
        self.assertEqual(g["lastplayed"], WHEN)
        self.assertFalse((self.sysroot / "gamelist.xml").exists())
        self.assertFalse(self._recovery(rom).with_suffix(".xml.tmp").exists())

    def test_short_session_counts_a_play_but_not_time(self):
        rom = self.sysroot / "Game.nds"
        gl = self._gamelist([{"path": "./Game.nds", "name": "Game", "playcount": "2", "gametime": "100"}])
        before = gl.read_bytes()
        self._run(rom, 9)
        root, g = self._read(rom)
        self.assertEqual(root.get("parentHash"), str(len(before)))
        self.assertEqual(g["playcount"], "3")
        self.assertEqual(g["gametime"], "100")
        self.assertEqual(g["lastplayed"], WHEN)
        self.assertEqual(gl.read_bytes(), before)
        self._run(rom, "10")
        _, g = self._read(rom)
        self.assertEqual(g["playcount"], "4")
        self.assertEqual(g["gametime"], "110")

    def test_fractional_seconds_truncate_before_the_ten_second_rule(self):
        rom = self.sysroot / "Game.nds"
        self._gamelist([{"path": "./Game.nds", "name": "Game", "playcount": "0", "gametime": "5"}])
        self._run(rom, "9.9")
        _, g = self._read(rom)
        self.assertEqual(g["playcount"], "1")
        self.assertEqual(g["gametime"], "5")
        self._run(rom, "10.9")
        _, g = self._read(rom)
        self.assertEqual(g["gametime"], "15")

    def test_recovery_with_the_current_gamelist_wins(self):
        rom = self.sysroot / "Game.nds"
        gl = self._gamelist([{"path": "./Game.nds", "name": "Game", "playcount": "2", "gametime": "10"}])
        size = gl.stat().st_size
        rec = self._recovery(rom)
        rec.parent.mkdir(parents=True)
        rec.write_text(
            '<?xml version="1.0"?>\n<gameList parentHash="%d">\n\t<game>\n'
            '\t\t<path>./Game.nds</path>\n\t\t<playcount>7</playcount>\n\t\t<gametime>40</gametime>\n'
            '\t</game>\n</gameList>\n' % size
        )
        self._run(rom, 15)
        _, g = self._read(rom)
        self.assertEqual(g["playcount"], "8")
        self.assertEqual(g["gametime"], "55")
        self.assertEqual(gl.stat().st_size, size)

    def test_stale_recovery_is_ignored(self):
        rom = self.sysroot / "Game.nds"
        gl = self._gamelist([
            {"path": "./Other.nds", "name": "Other", "playcount": "4"},
            {"path": "./Game.nds", "name": "Game", "playcount": "2", "gametime": "10"},
        ])
        before = gl.read_bytes()
        rec = self._recovery(rom)
        rec.parent.mkdir(parents=True)
        rec.write_text(
            '<?xml version="1.0"?>\n<gameList parentHash="1">\n\t<game>\n'
            '\t\t<path>./Game.nds</path>\n\t\t<playcount>99</playcount>\n\t\t<gametime>999</gametime>\n'
            '\t</game>\n</gameList>\n'
        )
        self._run(rom, 12)
        root, g = self._read(rom)
        self.assertEqual(len(root.findall("game")), 1)
        self.assertEqual(g["playcount"], "3")
        self.assertEqual(g["gametime"], "22")
        self.assertNotIn("Other", g["path"])
        self.assertEqual(gl.read_bytes(), before)

    def test_bad_numbers_and_a_broken_recovery_fall_back(self):
        rom = self.sysroot / "folder" / "Game.nds"
        rom.parent.mkdir()
        gl = self._gamelist([{"path": "./folder/Game.nds", "name": "Game", "playcount": "nope", "gametime": "12s"}])
        rec = self._recovery(rom)
        rec.parent.mkdir(parents=True)
        rec.write_text("<gameList parentHash=\"%d\"><game>" % gl.stat().st_size)
        self._run(rom, 12)
        _, g = self._read(rom)
        self.assertEqual(g["path"], "./folder/Game.nds")
        self.assertEqual(g["playcount"], "1")
        self.assertEqual(g["gametime"], "12")
        self.assertEqual(self._recovery(rom), self.recovery / "nds" / "folder" / "Game.xml")

    def test_save_disabled_or_a_path_outside_roms_writes_nothing(self):
        rom = self.sysroot / "Game.nds"
        self._gamelist([{"path": "./Game.nds", "name": "Game", "playcount": "1"}])
        self.settings.write_text(
            '<setting name="AudioVolume" value="80" />\n'
            '<setting name="SaveGamelistsOnExit" value="false" />\n'
        )
        self._run(rom, 30)
        self.assertFalse(self.recovery.exists())
        self.settings.write_text('<setting name="SaveGamelistsOnExit" value="true" />\n')
        outside = self.root / "games" / "nds" / "Game.nds"
        outside.parent.mkdir(parents=True)
        self._run(outside, 30)
        self.assertFalse(self.recovery.exists())
        self._run(rom, 30)
        _, g = self._read(rom)
        self.assertEqual(g["playcount"], "2")


if __name__ == "__main__":
    unittest.main()
