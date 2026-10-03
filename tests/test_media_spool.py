"""--auto keeps what it pushes to ES until the end of the run: each push makes ES refresh the lists that show the
game, and a push after every picture of every game kept ES's own themes redrawing (issue 25)."""
import tempfile
import unittest
from pathlib import Path

from loadmod import load


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


if __name__ == "__main__":
    unittest.main()
