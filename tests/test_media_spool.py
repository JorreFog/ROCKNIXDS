"""--auto keeps what it pushes to ES until the end of the run: each push makes ES refresh the lists that show the
game, and a push after every picture of every game kept ES's own themes redrawing (issue 25). A picture ES
refuses at the flush is tried again next time, not left on the week-long wait."""
import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from loadmod import load


NOW = 1_700_000_000


class MediaSpoolTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("rocknixds_media_spool", "dii-ess-aye/scrape/rocknixds-media.py")
        self.tmp = tempfile.TemporaryDirectory()
        sent = self.sent = []

        class Fake(self.mod.Local):
            answer = "ok"

            def send_media(self, gid, mtype, data):
                sent.append((gid, mtype, data))
                return type(self).answer

        self.dev = Fake(False)
        self.dev.spool = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_pushes_wait_for_the_flush(self):
        self.assertEqual(self.dev.push_media("g1", "boxart", b"a"), "ok")
        self.assertEqual(self.dev.push_media("g2", "wheel", b"b"), "ok")
        self.assertEqual(self.sent, [])
        self.assertEqual(self.dev.flush(), [])
        self.assertEqual(self.sent, [("g1", "boxart", b"a"), ("g2", "wheel", b"b")])
        self.assertEqual(list(Path(self.tmp.name).iterdir()), [])       # the spooled files are gone

    def test_what_es_refuses_is_reported(self):
        self.dev.push_media("g1", "cartridge", b"a")
        type(self.dev).answer = "failed (ES answered 404)"
        self.assertEqual(self.dev.flush(), [("g1", "cartridge")])

    def test_without_a_spool_it_pushes_at_once(self):
        self.dev.spool = None
        self.dev.push_media("g1", "boxart", b"a")
        self.assertEqual(self.sent, [("g1", "boxart", b"a")])


class MediaSpoolRetryTest(unittest.TestCase):
    """A spooled push answers "ok" before ES has the picture. If ES then refuses it, the week-long
    "nothing matched" wait must not stick: the game is scraped again the next time the menu opens."""

    def setUp(self):
        self.mod = load("rocknixds_media_spool_retry", "dii-ess-aye/scrape/rocknixds-media.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out = self.root / "out"
        cache = self.out / "cache"
        box = cache / "Named_Boxarts"
        box.mkdir(parents=True)
        (cache / "dsi_font.otf").write_bytes(b"font")
        name = "Mario Kart DS (Europe)"
        (cache / "lr-Named_Boxarts.html").write_text('<a href="%s.png">' % name.replace(" ", "%20"))
        (cache / "lr-Named_Snaps.html").write_text("<html></html>")
        from PIL import Image
        Image.new("RGBA", (4, 4), (1, 2, 3, 255)).save(box / (name + ".png"))
        self.mod.STATE = str(self.root / "media-state.json")
        self.mod.time.time = lambda: NOW
        self.sent = []

        sent = self.sent

        class Fake(self.mod.Local):
            answer = "ok"

            def games(self):
                return [{
                    "id": "g1",
                    "name": "Mario Kart DS",
                    "path": "./Mario Kart DS (Europe).nds",
                    "boxart": "x",
                    "boxback": "x",
                    "cartridge": "x",
                    "titleshot": "x",
                }]

            def es_up(self):
                return True

            def send_media(self, gid, mtype, data):
                sent.append((gid, mtype, data))
                return type(self).answer

            def run(self, cmd, data=None, binary=False):
                return "200"

        self.mod.Local = Fake
        self.Fake = Fake

        def no_network(*a, **k):
            raise AssertionError("network %s" % (a[0] if a else ""))

        self.mod.http = no_network

    def tearDown(self):
        self.tmp.cleanup()

    def _run(self):
        self.mod.sys.argv = ["rocknixds-media.py", "--local", "--auto", "--no-ra", "--out", str(self.out)]
        buf = io.StringIO()
        with redirect_stdout(buf):
            self.mod.main()
        return buf.getvalue(), json.loads((self.root / "media-state.json").read_text())

    def test_a_refused_picture_is_tried_again_instead_of_waiting_a_week(self):
        self.Fake.answer = "failed (ES answered 404)"
        out, st = self._run()
        self.assertEqual([m for _, m, _ in self.sent], ["thumbnail"])
        self.assertIn("not taken", out)
        self.assertNotIn("./Mario Kart DS (Europe).nds", st["tried"])

    def test_an_accepted_run_keeps_the_week_wait_for_what_did_not_match(self):
        out, st = self._run()
        self.assertNotIn("not taken", out)
        self.assertEqual(self.sent[0][0], "g1")
        self.assertEqual(st["tried"]["./Mario Kart DS (Europe).nds"], NOW)


if __name__ == "__main__":
    unittest.main()
