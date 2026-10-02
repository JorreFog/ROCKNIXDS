"""What --auto decides to scrape when the menu opens. A game missing art is retried after
a week, a RetroAchievements lookup that failed for the network is retried sooner, and a
strip is redrawn only after the game has been played again."""
import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from loadmod import load


NOW = 1_700_000_000


class Continued(Exception):
    pass


class MediaAutoTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("rocknixds_media_auto", "dii-ess-aye/scrape/rocknixds-media.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.mod.STATE = str(self.root / "media-state.json")
        self.mod.time.time = lambda: NOW
        self.continued = False

        def stop(local):
            self.continued = True
            raise Continued(local)

        self.mod.load_pil = stop

        class Fake(self.mod.Local):
            games_data = []

            def games(self):
                return [dict(g) for g in type(self).games_data]

        self.mod.Local = Fake
        self.Fake = Fake

    def tearDown(self):
        self.tmp.cleanup()

    def _game(self, **extra):
        g = {
            "id": "g1",
            "name": "Mario Kart DS",
            "path": "./Mario Kart DS (Europe).nds",
            "boxart": "x",
            "image": "x",
            "cartridge": "x",
        }
        g.update(extra)
        return g

    def _state(self, **st):
        base = {"tried": {}, "rahash": {}, "ra": {}}
        base.update(st)
        (self.root / "media-state.json").write_text(json.dumps(base))

    def _run(self, games, argv=None):
        self.continued = False
        self.Fake.games_data = games
        self.mod.sys.argv = argv or ["rocknixds-media.py", "--local", "--auto", "--out", str(self.root / "work")]
        buf = io.StringIO()
        with redirect_stdout(buf):
            try:
                self.mod.main()
            except Continued:
                pass
        return buf.getvalue()

    def _saved(self):
        return json.loads((self.root / "media-state.json").read_text())

    def test_nothing_missing_prunes_games_that_were_deleted(self):
        self._state(
            tried={"./Mario Kart DS (Europe).nds": 1, "./gone.nds": 2},
            rahash={"./gone.nds": 3},
            ra={"./Mario Kart DS (Europe).nds": "20260101T000000", "./gone.nds": "old"},
        )
        out = self._run([self._game(cheevosId=5, lastplayed="20260101T000000")])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)
        saved = self._saved()
        self.assertNotIn("./gone.nds", saved["tried"])
        self.assertNotIn("./gone.nds", saved["rahash"])
        self.assertNotIn("./gone.nds", saved["ra"])
        self.assertIn("./Mario Kart DS (Europe).nds", saved["ra"])

    def test_missing_thumbnail_alone_is_not_scraped(self):
        path = "./Mario Kart DS (Europe).nds"
        self._state(ra={path: ""})
        out = self._run([self._game(cheevosId=4, lastplayed="")])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)

    def test_missing_art_waits_a_week_then_tries_again(self):
        # ES omits the key when that media is missing. An empty string would still count as present.
        path = "./Mario Kart DS (Europe).nds"
        game = self._game(cheevosId=4, lastplayed="")
        del game["image"]
        self._state(tried={path: NOW - 86400}, ra={path: ""})
        out = self._run([game])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)

        self._state(tried={path: NOW - self.mod.RETRY - 1}, ra={path: ""})
        self._run([game])
        self.assertTrue(self.continued)

        self._state(ra={path: ""})
        self._run([game])
        self.assertTrue(self.continued)

    def test_a_new_rom_without_a_hash_is_looked_up_unless_it_was_just_tried(self):
        path = "./Mario Kart DS (Europe).nds"
        game = self._game()
        self._state(rahash={path: NOW - 86400})
        out = self._run([game])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)

        self._state(rahash={path: NOW - self.mod.RETRY - 1})
        self._run([game])
        self.assertTrue(self.continued)

    def test_a_strip_is_redrawn_after_a_later_play_and_not_before(self):
        path = "./Mario Kart DS (Europe).nds"
        self._state(ra={path: "20260101T000000"})
        out = self._run([self._game(cheevosId="9", lastplayed="20260101T000000")])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)

        self._run([self._game(cheevosId="9", lastplayed="20260102T000000")])
        self.assertTrue(self.continued)

        self.continued = False
        self._state()
        self._run([self._game(cheevosId=9, lastplayed="20260102T000000")])
        self.assertTrue(self.continued)

    def test_a_corrupt_state_file_is_treated_as_empty(self):
        # An empty memory has no strip timestamp, so a game ES already hashed is drawn again.
        # The same game with a remembered last-played date is left alone.
        path = "./Mario Kart DS (Europe).nds"
        (self.root / "media-state.json").write_text("{", encoding="utf-8")
        self._run([self._game(cheevosId=3, lastplayed="20260102T000000")])
        self.assertTrue(self.continued)

        self._state(ra={path: "20260102T000000"})
        out = self._run([self._game(cheevosId=3, lastplayed="20260102T000000")])
        self.assertIn("nothing missing", out)
        self.assertFalse(self.continued)

    def test_a_game_filter_does_not_drop_state_for_the_other_games(self):
        self._state(ra={
            "./Mario Kart DS (Europe).nds": "20260101T000000",
            "./Other (USA).nds": "20260101T000000",
        })
        other = self._game(id="g2", name="Other", path="./Other (USA).nds", cheevosId=1, lastplayed="20260101T000000")
        mario = self._game(cheevosId=2, lastplayed="20260101T000000")
        out = self._run([mario, other], ["rocknixds-media.py", "--local", "--auto", "--game", "Mario", "--out", str(self.root / "work")])
        self.assertIn("nothing missing", out)
        self.assertIn("./Other (USA).nds", self._saved()["ra"])


if __name__ == "__main__":
    unittest.main()
