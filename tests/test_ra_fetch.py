"""RetroAchievements counts for the media strip. RA's warning pseudo-achievement must not
count, one game's error must not drop the rest of the list, and --auto's id filter must
not fetch games it did not name."""
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path

from loadmod import load


def body(payload):
    class Response(io.BytesIO):
        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

    return Response(json.dumps(payload).encode())


class RaFetchTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("ra_fetch", "dii-ess-aye/scrape/ra-fetch.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name)
        self.sleeps = []
        self.mod.time.sleep = self.sleeps.append

    def tearDown(self):
        self.tmp.cleanup()

    def test_cfg_reads_the_first_exact_key(self):
        cfg = self.out / "system.cfg"
        cfg.write_text(
            "global.retroachievements.username.extra=nope\n"
            "global.retroachievements.username=alice\n"
            "global.retroachievements.username=old\n"
            "global.retroachievements.token=sek=ret\n"
        )
        self.mod.CFG = str(cfg)
        self.assertEqual(self.mod.cfg("global.retroachievements.username"), "alice")
        self.assertEqual(self.mod.cfg("global.retroachievements.token"), "sek=ret")
        self.assertEqual(self.mod.cfg("global.retroachievements.missing"), "")

    def test_warning_achievements_are_not_counted(self):
        calls = []

        def fake_call(**params):
            calls.append(params)
            if params["r"] == "patch":
                return {"PatchData": {"Title": "HeartGold", "Achievements": [
                    {"ID": 1, "Flags": 3, "Points": 10},
                    {"ID": 2, "Flags": 5, "Points": 50},
                    {"ID": self.mod.WARNING_ID - 1, "Flags": 3, "Points": 4},
                    {"ID": self.mod.WARNING_ID, "Flags": 3, "Points": 1},
                    {"ID": self.mod.WARNING_ID + 50, "Flags": 3, "Points": 9},
                ]}}
            return {"UserUnlocks": [1, self.mod.WARNING_ID, 99]}

        self.mod.call = fake_call
        info = self.mod.fetch("alice", "sek", 9, "HeartGold", str(self.out))
        self.assertEqual(info["title"], "HeartGold")
        self.assertEqual(info["total"], 2)
        self.assertEqual(info["points"], 14)
        self.assertEqual(info["unlocked"], 1)
        self.assertEqual(info["unlocked_points"], 10)
        self.assertNotIn("icon_error", info)
        self.assertEqual(calls[1]["r"], "unlocks")
        self.assertEqual(calls[1]["h"], 0)
        self.assertEqual(calls[1]["g"], 9)

    def test_missing_unlocks_and_points_are_zero(self):
        def fake_call(**params):
            if params["r"] == "patch":
                return {"PatchData": {"Achievements": [{"ID": 7, "Flags": 3}]}}
            return {}

        self.mod.call = fake_call
        info = self.mod.fetch("a", "b", 1, "Game", str(self.out))
        self.assertEqual(info["total"], 1)
        self.assertEqual(info["points"], 0)
        self.assertEqual(info["unlocked"], 0)
        self.assertEqual(info["unlocked_points"], 0)

    def test_icon_failure_keeps_the_counts(self):
        def fake_call(**params):
            if params["r"] == "patch":
                return {"PatchData": {
                    "Achievements": [{"ID": 1, "Flags": 3, "Points": 5}],
                    "ImageIconURL": "https://example.invalid/icon.png",
                }}
            return {"UserUnlocks": [1]}

        def boom(*args, **kwargs):
            raise OSError("no network")

        self.mod.call = fake_call
        self.mod.urllib.request.urlopen = boom
        info = self.mod.fetch("a", "b", 3, "Game", str(self.out))
        self.assertEqual(info["unlocked"], 1)
        self.assertEqual(info["points"], 5)
        self.assertEqual(info["icon_error"], "no network")
        self.assertFalse((self.out / "3.png").exists())

    def test_one_game_error_does_not_drop_the_others(self):
        cfg = self.out / "system.cfg"
        cfg.write_text("global.retroachievements.username=alice\nglobal.retroachievements.token=sek\n")
        self.mod.CFG = str(cfg)
        games = [
            {"id": "a", "name": "Good", "cheevosId": 11},
            {"id": "b", "name": "Bad", "cheevosId": "2"},
            {"id": "c", "name": "None"},
            {"id": "d", "name": "Zero", "cheevosId": 0},
        ]

        def urlopen(url, *args, **kwargs):
            self.assertIn("systems/nds/games", url)
            return body(games)

        def fetch(user, token, gid, name, out):
            self.assertEqual((user, token), ("alice", "sek"))
            if gid == 2:
                raise RuntimeError("server said no")
            return {"name": name, "ra": gid, "unlocked": 1, "total": 4, "points": 25}

        self.mod.urllib.request.urlopen = urlopen
        self.mod.fetch = fetch
        self.mod.sys.argv = ["ra-fetch.py", str(self.out / "ra")]
        self.mod.main()
        data = json.loads((self.out / "ra" / "ra.json").read_text())
        self.assertEqual(data["a"]["unlocked"], 1)
        self.assertEqual(data["b"]["error"], "server said no")
        self.assertEqual(data["b"]["ra"], 2)
        self.assertIsNone(data["c"]["ra"])
        self.assertIsNone(data["d"]["ra"])

    def test_named_ids_are_the_only_games_fetched(self):
        cfg = self.out / "system.cfg"
        cfg.write_text("global.retroachievements.username=alice\nglobal.retroachievements.token=sek\n")
        self.mod.CFG = str(cfg)
        games = [
            {"id": "a", "name": "Skipped", "cheevosId": 11},
            {"id": "b", "name": "Wanted", "cheevosId": 22},
            {"id": "c", "name": "Also skipped", "cheevosId": 33},
        ]
        fetched = []

        def urlopen(url, *args, **kwargs):
            return body(games)

        def fetch(user, token, gid, name, out):
            fetched.append((gid, name))
            return {"name": name, "ra": gid, "unlocked": 0, "total": 1, "points": 5}

        self.mod.urllib.request.urlopen = urlopen
        self.mod.fetch = fetch
        self.mod.sys.argv = ["ra-fetch.py", str(self.out / "ra"), "b"]
        self.mod.main()
        data = json.loads((self.out / "ra" / "ra.json").read_text())
        self.assertEqual(list(data), ["b"])
        self.assertEqual(fetched, [(22, "Wanted")])

    def test_missing_account_stops_before_any_request(self):
        cfg = self.out / "system.cfg"
        cfg.write_text("global.retroachievements.username=\n")
        self.mod.CFG = str(cfg)
        self.mod.sys.argv = ["ra-fetch.py", str(self.out / "ra")]
        with self.assertRaises(SystemExit):
            self.mod.main()
        self.assertFalse((self.out / "ra" / "ra.json").exists())

    def test_call_retries_rate_limits_and_surfaces_a_refusal(self):
        responses = [
            urllib.error.HTTPError("https://retroachievements.org/dorequest.php", 429, "slow", None, None),
            urllib.error.HTTPError("https://retroachievements.org/dorequest.php", 503, "down", None, None),
            body({"Success": True, "ok": 1}),
        ]

        def urlopen(*args, **kwargs):
            item = responses.pop(0)
            if isinstance(item, Exception):
                raise item
            return item

        self.mod.urllib.request.urlopen = urlopen
        self.assertEqual(self.mod.call(r="patch", u="a", t="b", g=1)["ok"], 1)
        self.assertEqual(self.sleeps, [2, 4])

        self.mod.urllib.request.urlopen = lambda *a, **k: body({"Success": False, "Error": "bad token"})
        with self.assertRaises(RuntimeError) as caught:
            self.mod.call(r="patch")
        self.assertIn("bad token", str(caught.exception))

        def denied(*args, **kwargs):
            raise urllib.error.HTTPError("https://retroachievements.org/dorequest.php", 401, "no", None, None)

        self.sleeps.clear()
        self.mod.urllib.request.urlopen = denied
        with self.assertRaises(urllib.error.HTTPError):
            self.mod.call(r="patch")
        self.assertEqual(self.sleeps, [])

        def always_limited(*args, **kwargs):
            raise urllib.error.HTTPError("https://retroachievements.org/dorequest.php", 429, "slow", None, None)

        self.mod.urllib.request.urlopen = always_limited
        with self.assertRaises(urllib.error.HTTPError):
            self.mod.call(r="patch")
        self.assertEqual(self.sleeps, [2, 4, 8])


if __name__ == "__main__":
    unittest.main()
