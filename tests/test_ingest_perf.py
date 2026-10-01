"""The performance-log importer writes whatever it accepts onto the device-logs branch.
Path checks, the ntfy host allowlist, and which failures are retried are the parts a
regression would turn into a bad commit."""
import tempfile
import unittest
import urllib.error
from pathlib import Path

from loadmod import load


OK = '{"t": 1, "game": {"rom": "a.nds"}}\n{"summary": true, "game": {"rom": "a.nds"}}\n'
NAME = "20231114-221320_Heart_Gold.nds.jsonl"
DEV = "abc123def456"


def msg(mid, device=DEV, filename=NAME, url="https://ntfy.sh/file/ok.json"):
    return {"id": mid, "title": device, "attachment": {"name": filename, "url": url}}


class IngestPerfTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("ingest_perf", ".github/ingest-perf.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def ingested(self):
        path = Path(self.root) / "docs" / "data" / "device" / ".ingested"
        if not path.exists():
            return set()
        return {line for line in path.read_text().splitlines() if line}

    def test_existing_selftest(self):
        self.mod.selftest()

    def test_body_must_end_in_a_real_summary(self):
        self.assertTrue(self.mod.body_ok("\n" + OK + "\n"))
        self.assertFalse(self.mod.body_ok('{"summary": true}\n{"t": 1}\n'))
        self.assertFalse(self.mod.body_ok('{"summary": "true"}\n'))
        self.assertFalse(self.mod.body_ok('{"summary": 1}\n'))
        self.assertFalse(self.mod.body_ok(""))
        self.assertFalse(self.mod.body_ok("   \n"))
        self.assertFalse(self.mod.body_ok("x" * (self.mod.MAX + 1)))

    def test_relpath_rejects_traversal_and_odd_ids(self):
        self.assertEqual(self.mod.relpath(DEV, NAME), "docs/data/device/%s/%s" % (DEV, NAME))
        self.assertEqual(self.mod.relpath("a" * 8, NAME), "docs/data/device/%s/%s" % ("a" * 8, NAME))
        self.assertEqual(self.mod.relpath("a" * 32, NAME), "docs/data/device/%s/%s" % ("a" * 32, NAME))
        for device, filename in [
            ("a" * 7, NAME),
            ("a" * 33, NAME),
            ("ABC123def456", NAME),
            ("../etc", NAME),
            (DEV + "/x", NAME),
            (DEV, "../x.jsonl"),
            (DEV, "20231114-221320_a/b.jsonl"),
            (DEV, "20231114-221320_a.jsonl\n"),
            (DEV, "not a log"),
            ("", NAME),
            (DEV, ""),
        ]:
            self.assertIsNone(self.mod.relpath(device, filename), (device, filename))

    def test_bad_names_are_not_fetched_or_written(self):
        called = []

        def fake(url):
            called.append(url)
            return OK

        n = self.mod.import_messages(self.root, [
            msg("t1", device="../etc", url="https://evil.example/x"),
            msg("t2", filename="../x.jsonl", url="https://evil.example/y"),
            msg("t3", device="NOPE", url="http://127.0.0.1/x"),
        ], fake)
        self.assertEqual(n, 0)
        self.assertEqual(called, [])
        self.assertEqual(self.ingested(), {"t1", "t2", "t3"})
        self.assertFalse((Path(self.root) / "docs" / "data" / "device" / "etc").exists())

    def test_dead_or_useless_sessions_are_not_retried(self):
        def fake(url):
            if url.endswith("missing.json"):
                raise urllib.error.HTTPError(url, 404, "gone", None, None)
            if url.endswith("gone.json"):
                raise urllib.error.HTTPError(url, 410, "gone", None, None)
            if url.endswith("junk.json"):
                return '{"t": 1}\n'
            raise AssertionError(url)

        messages = [
            msg("miss", url="https://ntfy.sh/file/missing.json"),
            msg("gone410", filename="20231114-221321_b.nds.jsonl", url="https://ntfy.sh/file/gone.json"),
            msg("junk", filename="20231114-221322_c.nds.jsonl", url="https://ntfy.sh/file/junk.json"),
        ]
        self.assertEqual(self.mod.import_messages(self.root, messages, fake), 0)
        self.assertEqual(self.ingested(), {"miss", "gone410", "junk"})
        self.assertEqual(list(Path(self.root).rglob("*.jsonl")), [])

        def again(url):
            raise AssertionError("fetched again: " + url)

        self.assertEqual(self.mod.import_messages(self.root, messages, again), 0)

    def test_a_timeout_is_retried_next_run(self):
        def timeout(url):
            raise TimeoutError("timed out")

        n = self.mod.import_messages(self.root, [msg("later", url="https://ntfy.sh/file/later.json")], timeout)
        self.assertEqual(n, 0)
        self.assertNotIn("later", self.ingested())
        n = self.mod.import_messages(self.root, [msg("later", url="https://ntfy.sh/file/later.json")], lambda url: OK.rstrip("\n"))
        self.assertEqual(n, 1)
        written = Path(self.root) / "docs" / "data" / "device" / DEV / NAME
        text = written.read_text()
        self.assertTrue(text.endswith("\n"))
        self.assertIn('"summary": true', text)

    def test_existing_file_is_not_replaced(self):
        dest = Path(self.root) / "docs" / "data" / "device" / DEV / NAME
        dest.parent.mkdir(parents=True)
        dest.write_text("OLD\n")
        n = self.mod.import_messages(self.root, [msg("have")], lambda url: OK + "NEW\n")
        self.assertEqual(n, 0)
        self.assertEqual(dest.read_text(), "OLD\n")
        self.assertIn("have", self.ingested())

    def test_other_hosts_are_refused_before_any_connection(self):
        for url in ("http://ntfy.sh/file", "https://evil.example/file", "https://ntfy.sh.evil.example/file", "file:///tmp/x"):
            with self.assertRaises(ValueError):
                self.mod.fetch(url)
        n = self.mod.import_messages(self.root, [
            msg("evil", url="https://evil.example/file"),
        ], self.mod.fetch)
        self.assertEqual(n, 0)
        self.assertIn("evil", self.ingested())

    def test_a_run_stops_after_thirty_and_keeps_the_rest_for_next_time(self):
        messages = []
        for i in range(31):
            messages.append(msg(
                "m%02d" % i,
                filename="20231114-2213%02d_a.nds.jsonl" % i,
                url="https://ntfy.sh/file/%02d.json" % i,
            ))
        n = self.mod.import_messages(self.root, messages, lambda url: OK)
        self.assertEqual(n, 30)
        self.assertNotIn("m30", self.ingested())
        self.assertEqual(len(list((Path(self.root) / "docs" / "data" / "device" / DEV).glob("*.jsonl"))), 30)
        n = self.mod.import_messages(self.root, messages, lambda url: OK)
        self.assertEqual(n, 1)
        self.assertIn("m30", self.ingested())

    def test_remember_keeps_ids_already_on_disk(self):
        folder = Path(self.root) / "docs" / "data" / "device"
        folder.mkdir(parents=True)
        (folder / ".ingested").write_text("older\n")
        self.mod.import_messages(self.root, [msg("newer", filename="20231114-010101_b.nds.jsonl")], lambda url: OK)
        self.assertEqual(self.ingested(), {"older", "newer"})
        text = (folder / ".ingested").read_text()
        self.assertEqual(text, "newer\nolder\n")


if __name__ == "__main__":
    unittest.main()
