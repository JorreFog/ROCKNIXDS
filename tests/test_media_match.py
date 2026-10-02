"""Which art a ROM gets. Mario Kart DS and Metroid Prime Hunters were given the kiosk
demo's box because every bracketed name collapsed to the same title and "(USA) (Demo)"
sorted first. Retail releases come first, then the ROM's own region."""
import hashlib
import io
import json
import os
import shlex
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

from loadmod import load


MARIO = [
    "Mario Kart DS (USA) (Demo)",
    "Mario Kart DS (USA, Australia)",
    "Mario Kart DS (Europe) (En,Fr,De,Es,It)",
    "Mario Kart DS (Japan)",
]


def nds_rom(arm9, arm7, icon, header_patch=None, supercard=False):
    header = bytearray(512)
    arm9_off = 0x200
    arm7_off = arm9_off + len(arm9)
    icon_off = arm7_off + len(arm7)
    header[0x20:0x24] = arm9_off.to_bytes(4, "little")
    header[0x2c:0x30] = len(arm9).to_bytes(4, "little")
    header[0x30:0x34] = arm7_off.to_bytes(4, "little")
    header[0x3c:0x40] = len(arm7).to_bytes(4, "little")
    header[0x68:0x6c] = icon_off.to_bytes(4, "little")
    if header_patch:
        for off, val in header_patch.items():
            header[off] = val
    body = bytes(header) + arm9 + arm7 + icon
    if not supercard:
        return body
    wrapper = bytearray(512)
    wrapper[0:4] = b"\x2e\x00\x00\xea"
    wrapper[0xb0:0xb4] = b"\x44\x46\x96\x00"
    return bytes(wrapper) + body


class MediaMatchTest(unittest.TestCase):
    def setUp(self):
        self.mod = load("rocknixds_media", "dii-ess-aye/scrape/rocknixds-media.py")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_norm_strips_regions_punctuation_and_accents(self):
        self.assertEqual(self.mod.norm("Mario Kart DS (USA) (Demo)"), "mario kart ds")
        self.assertEqual(self.mod.norm("Mario Kart DS (USA, Australia)"), "mario kart ds")
        self.assertEqual(self.mod.norm("Pokémon: The Version"), "pokemon")
        self.assertEqual(self.mod.norm("Mario & Luigi: Bowser's Inside Story"), "mario and luigi bowser s inside story")
        self.assertEqual(self.mod.norm("The Legend of Zelda: Phantom Hourglass [b]"), "legend of zelda phantom hourglass")

    def test_own_region_reads_the_rom_tag(self):
        self.assertEqual(self.mod.own_region("Game (Europe) (En,Fr,De,Es,It)"), "Europe")
        self.assertEqual(self.mod.own_region("Game (E)"), "Europe")
        self.assertEqual(self.mod.own_region("Game (U)"), "USA")
        self.assertEqual(self.mod.own_region("Game (J)"), "Japan")
        self.assertEqual(self.mod.own_region("Game (USA, Europe)"), "USA")
        self.assertEqual(self.mod.own_region("Game (World)"), "World")
        self.assertIsNone(self.mod.own_region("Game (En,Fr,De,Es,It)"))
        self.assertIsNone(self.mod.own_region("Game (Demo)"))

    def test_retail_beats_a_demo_that_sorts_first(self):
        match, score = self.mod.best_match("Mario Kart DS (Europe)", MARIO)
        self.assertEqual(score, 1.0)
        self.assertEqual(match, "Mario Kart DS (Europe) (En,Fr,De,Es,It)")
        match, score = self.mod.best_match("Mario Kart DS (USA)", MARIO)
        self.assertEqual(match, "Mario Kart DS (USA, Australia)")
        match, _ = self.mod.best_match("Mario Kart DS (J)", MARIO)
        self.assertEqual(match, "Mario Kart DS (Japan)")

    def test_a_regionless_rom_prefers_usa_then_world_and_still_skips_demos(self):
        names = [
            "Game (USA) (Demo)",
            "Game (Europe)",
            "Game (World)",
            "Game (USA)",
        ]
        match, score = self.mod.best_match("Game", names)
        self.assertEqual(score, 1.0)
        self.assertEqual(match, "Game (USA)")

    def test_demo_is_used_only_when_nothing_retail_matches(self):
        names = ["Mario Kart DS (USA) (Kiosk Demo)", "Something Else (USA)"]
        match, score = self.mod.best_match("Mario Kart DS (USA)", names)
        self.assertEqual(score, 1.0)
        self.assertIn("Demo", match)
        match, score = self.mod.best_match("Totally Unrelated Title", names)
        self.assertIsNone(match)
        self.assertEqual(score, 0)

    def test_non_retail_tags(self):
        retail = "Game (USA) (Rev 1)"
        for tag in ("Demo", "Kiosk", "Beta", "Proto", "Prototype", "Sample", "Preview",
                    "Debug", "Pirate", "Unl", "Aftermarket", "Program", "Competition",
                    "Taikenban", "Trial"):
            self.assertIsNotNone(self.mod.NON_RETAIL.search("Game (USA) (%s)" % tag), tag)
        self.assertIsNone(self.mod.NON_RETAIL.search(retail))

    def test_libretro_listing_is_decoded_and_a_failed_refresh_keeps_the_old_one(self):
        cache = self.root / "cache"
        cache.mkdir()
        listing = cache / "lr-Named_Boxarts.html"
        listing.write_text(
            '<a href="Mario%20Kart%20DS%20%28USA%29.png">x</a>\n'
            '<a href="A&amp;B.png">y</a>\n'
            '<a href="notes.txt">z</a>\n',
            encoding="utf-8",
        )
        now = self.mod.time.time()
        os.utime(listing, (now, now))
        calls = []

        def boom(url, timeout=40):
            calls.append(url)
            raise OSError("offline")

        self.mod.http = boom
        lr = self.mod.Libretro(str(cache))
        self.assertEqual(lr.names("Named_Boxarts"), ["Mario Kart DS (USA)", "A&B"])
        self.assertEqual(calls, [])

        week = self.mod.LR_LIST_DAYS * 86400 + 5
        os.utime(listing, (now - week, now - week))
        lr2 = self.mod.Libretro(str(cache))
        self.assertEqual(lr2.names("Named_Boxarts"), ["Mario Kart DS (USA)", "A&B"])
        self.assertTrue(calls)
        self.assertFalse((cache / "lr-Named_Boxarts.html.new").exists())

        def fresh(url, timeout=40):
            return b'<a href="New%20Game%20%28Europe%29.png"></a>'

        self.mod.http = fresh
        lr3 = self.mod.Libretro(str(cache))
        self.assertEqual(lr3.names("Named_Boxarts"), ["New Game (Europe)"])

    def test_a_missing_listing_still_raises_when_the_download_fails(self):
        cache = self.root / "empty"
        cache.mkdir()

        def boom(url, timeout=40):
            raise OSError("offline")

        self.mod.http = boom
        with self.assertRaises(OSError):
            self.mod.Libretro(str(cache)).names("Named_Boxarts")

    def test_portrait_snaps_are_placed_side_by_side(self):
        self.mod.load_pil(False)
        tall = Image.new("RGBA", (4, 6))
        tall.paste((255, 0, 0, 255), (0, 0, 4, 3))
        tall.paste((0, 0, 255, 255), (0, 3, 4, 6))
        out = self.mod.side_by_side(tall)
        self.assertEqual(out.size, (8, 3))
        self.assertEqual(out.getpixel((0, 0)), (255, 0, 0, 255))
        self.assertEqual(out.getpixel((4, 0)), (0, 0, 255, 255))
        wide = Image.new("RGBA", (8, 4), (1, 2, 3, 4))
        self.assertEqual(self.mod.side_by_side(wide).size, (8, 4))

    def test_png_bytes_drop_the_icc_profile(self):
        img = Image.new("RGBA", (2, 2), (1, 2, 3, 255))
        img.info["icc_profile"] = b"\x00" * 32
        data = self.mod.png_bytes(img)
        self.assertTrue(data.startswith(b"\x89PNG"))
        self.assertNotIn(b"iCCP", data)
        self.assertNotIn("icc_profile", img.info)

    def test_cart_scan_prefers_the_rom_region_among_cutouts(self):
        self.mod.load_pil(False)
        cache = self.root / "cache"
        lb = cache / "lb"
        lb.mkdir(parents=True)
        Image.new("RGBA", (8, 8), (10, 20, 30, 255)).save(lb / "eu.png")
        Image.new("RGBA", (8, 8), (9, 9, 9, 255)).save(lb / "na.png")
        index = {"Mario Kart DS": [
            ["North America", "images/na.png"],
            ["Europe", "images/eu.png"],
        ]}
        img, why = self.mod.cart_image(index, "Mario Kart DS (Europe)", str(cache))
        self.assertEqual(img.size, (round(self.mod.CARD_H * self.mod.CARD_ASPECT), self.mod.CARD_H))
        self.assertIn("Europe", why)
        self.assertNotIn("North America", why)
        img, why = self.mod.cart_image({}, "No Such Game", str(cache))
        self.assertIsNone(img)
        self.assertEqual(why, "no LaunchBox entry")

        def boom(url, timeout=40):
            raise OSError("offline")

        self.mod.http = boom
        img, why = self.mod.cart_image({"Mario Kart DS": [["Europe", "missing/nope.png"]]}, "Mario Kart DS (Europe)", str(cache))
        self.assertIsNone(img)
        self.assertTrue(why.endswith("download failed"))

    def test_ra_hash_skips_a_supercard_header_and_ignores_bytes_outside_the_hash(self):
        arm9, arm7, icon = b"\x01" * 16, b"\x02" * 16, b"\x03" * 0xA00
        plain = self.root / "plain.nds"
        wrapped = self.root / "wrapped.nds"
        plain.write_bytes(nds_rom(arm9, arm7, icon))
        wrapped.write_bytes(nds_rom(arm9, arm7, icon, supercard=True))
        outside = bytearray(plain.read_bytes())
        outside[0x180] = 0xAB
        extra = self.root / "extra.nds"
        extra.write_bytes(bytes(outside) + b"\xff")
        touched = bytearray(plain.read_bytes())
        touched[0x10] = 0xCD
        header = self.root / "header.nds"
        header.write_bytes(touched)
        icon_byte = bytearray(plain.read_bytes())
        icon_byte[-1] ^= 0xFF
        icon_file = self.root / "icon.nds"
        icon_file.write_bytes(icon_byte)
        huge = self.root / "huge.nds"
        huge.write_bytes(nds_rom(b"", b"", b"", header_patch={}))
        raw = bytearray(huge.read_bytes())
        raw[0x2c:0x30] = (16 << 20).to_bytes(4, "little")
        raw[0x3c:0x40] = (1).to_bytes(4, "little")
        huge.write_bytes(raw)
        missing = self.root / "missing.nds"

        got = self._hashes([str(p) for p in (plain, wrapped, extra, header, icon_file, huge, missing)])
        self.assertEqual(got[str(plain)], got[str(wrapped)])
        self.assertEqual(got[str(plain)], got[str(extra)])
        self.assertNotEqual(got[str(plain)], got[str(header)])
        self.assertNotEqual(got[str(plain)], got[str(icon_file)])
        self.assertTrue(got[str(plain)])
        self.assertRegex(got[str(plain)], r"^[0-9a-f]{32}$")
        self.assertIn("16 MB", got[str(huge)])
        self.assertTrue(got[str(missing)].startswith("error:"))

    def test_fill_cheevos_ids_pushes_upper_hash_and_retries_only_lookup_failures(self):
        good = self.root / "good.nds"
        good.write_bytes(nds_rom(b"\x11" * 8, b"\x22" * 8, b"\x33" * 0xA00))
        bad = self.root / "bad.nds"
        raw = bytearray(nds_rom(b"", b"", b""))
        raw[0x2c:0x30] = (1 << 20).to_bytes(4, "little")
        raw[0x3c:0x40] = (16 << 20).to_bytes(4, "little")
        bad.write_bytes(raw)
        pushed = []
        looked = []

        class Dev:
            def run(self, cmd, data=None, binary=False):
                quoted = shlex.split(cmd)
                r = subprocess.run([quoted[0], quoted[1], quoted[2]], input=data, capture_output=True, check=True)
                return r.stdout.decode()

            def push_meta(self, gid, meta):
                pushed.append((gid, meta))
                return "200"

        dev = Dev()

        def fake_http(url, timeout=40):
            looked.append(url)
            if "fail" in url:
                raise OSError("offline")
            game_id = 0 if "zero" in url else 42
            return json.dumps({"GameID": game_id}).encode()

        self.mod.http = fake_http
        known = {"id": "known", "name": "Known", "path": "/storage/roms/nds/Known.nds", "cheevosId": "9"}
        self.assertEqual(self.mod.fill_cheevos_ids(dev, [known]), set())
        self.assertEqual(looked, [])

        fresh = {"id": "g1", "name": "Good", "path": str(good), "cheevosId": 0}
        broken = {"id": "g2", "name": "Huge", "path": str(bad), "cheevosId": ""}
        failed = self.mod.fill_cheevos_ids(dev, [fresh, broken])
        self.assertEqual(failed, set())
        self.assertEqual(len(pushed), 1)
        gid, meta = pushed[0]
        self.assertEqual(gid, "g1")
        self.assertEqual(meta["cheevosId"], "42")
        self.assertEqual(meta["cheevosHash"], meta["cheevosHash"].upper())
        self.assertEqual(len(meta["cheevosHash"]), 32)
        self.assertEqual(fresh["cheevosId"], 42)
        self.assertIn(meta["cheevosHash"].lower(), looked[0])

        pushed.clear()
        looked.clear()
        unknown = {"id": "g3", "name": "Unknown", "path": str(good), "cheevosId": None}
        self.mod.http = lambda url, timeout=40: json.dumps({"GameID": 0}).encode()
        self.assertEqual(self.mod.fill_cheevos_ids(dev, [unknown]), set())
        self.assertEqual(pushed, [])
        self.assertIsNone(unknown["cheevosId"])

        offline = {"id": "g4", "name": "Offline", "path": str(good)}
        self.mod.http = lambda url, timeout=40: (_ for _ in ()).throw(OSError("down"))
        self.assertEqual(self.mod.fill_cheevos_ids(dev, [offline]), {"g4"})
        self.assertNotIn("cheevosId", offline)

    def test_ra_hash_pads_a_short_icon_block_like_rcheevos(self):
        import hashlib
        arm9, arm7, icon = b"\x01" * 16, b"\x02" * 16, b"\x03" * 0x40
        short = self.root / "short.nds"
        short.write_bytes(nds_rom(arm9, arm7, icon))
        raw = short.read_bytes()
        u = lambda o: int.from_bytes(raw[o:o + 4], "little")
        want = hashlib.md5(raw[:0x160] + raw[u(0x20):u(0x20) + u(0x2c)] + raw[u(0x30):u(0x30) + u(0x3c)]
                           + raw[u(0x68):].ljust(0xA00, b"\0")).hexdigest()
        self.assertEqual(self._hashes([str(short)])[str(short)], want)

    def test_fill_cheevos_ids_keeps_no_id_es_refused(self):
        good = self.root / "good.nds"
        good.write_bytes(nds_rom(b"\x11" * 8, b"\x22" * 8, b"\x33" * 0xA00))

        class Dev:
            def run(self, cmd, data=None, binary=False):
                quoted = shlex.split(cmd)
                return subprocess.run(quoted[:3], input=data, capture_output=True, check=True).stdout.decode()

            def push_meta(self, gid, meta):
                return "500"

        self.mod.http = lambda url, timeout=40: json.dumps({"GameID": 42}).encode()
        game = {"id": "g1", "name": "Good", "path": str(good), "cheevosId": 0}
        self.assertEqual(self.mod.fill_cheevos_ids(Dev(), [game]), {"g1"})
        self.assertEqual(game["cheevosId"], 0)

    def test_gamelist_entry_prefers_a_recovery_file_that_still_matches(self):
        sysroot = self.root / "roms" / "nds"
        sysroot.mkdir(parents=True)
        rom = sysroot / "Heart Gold.nds"
        rom.write_bytes(b"")
        gamelist = sysroot / "gamelist.xml"
        gamelist.write_text(
            "<gameList><game><path>./Heart Gold.nds</path><name>From list</name>"
            "<cheevosId>1</cheevosId></game></gameList>"
        )
        rec_dir = self.root / "recovery" / "nds"
        rec_dir.mkdir(parents=True)
        self.mod.RECOVERY = str(self.root / "recovery")
        rec = rec_dir / "Heart Gold.xml"
        rec.write_text(
            '<gameList parentHash="%d"><game><path>./Heart Gold.nds</path><name>From recovery</name>'
            "<cheevosId>2</cheevosId><wheel>./media/wheel.png</wheel></game></gameList>" % gamelist.stat().st_size
        )
        entry, root = self.mod.gamelist_entry(str(rom))
        self.assertEqual(root, str(sysroot))
        self.assertEqual(entry.findtext("name"), "From recovery")

        rec.write_text(
            '<gameList parentHash="999"><game><path>./Heart Gold.nds</path><name>Stale</name></game></gameList>'
        )
        entry, _ = self.mod.gamelist_entry(str(rom))
        self.assertEqual(entry.findtext("name"), "From list")

        self.assertEqual(self.mod.gamelist_entry(str(self.root / "nope.nds")), (None, None))
        other = sysroot / "Other.nds"
        other.write_bytes(b"")
        self.assertEqual(self.mod.gamelist_entry(str(other))[0], None)

    def test_ra_rom_stops_when_the_strip_cannot_be_redrawn_yet(self):
        sysroot = self.root / "roms" / "nds"
        sysroot.mkdir(parents=True)
        rom = sysroot / "Game.nds"
        rom.write_bytes(b"")
        gamelist = sysroot / "gamelist.xml"
        gamelist.write_text("<gameList></gameList>")
        self.mod.RECOVERY = str(self.root / "recovery")
        messages = []
        self.mod.log = lambda *a: messages.append(" ".join(str(x) for x in a))
        self.mod.ra_rom(str(rom))
        self.assertIn("not in the gamelist", messages[-1])

        gamelist.write_text(
            "<gameList><game><path>./Game.nds</path><name>Game</name><cheevosId>0</cheevosId></game></gameList>"
        )
        self.mod.ra_rom(str(rom))
        self.assertIn("no RetroAchievements set", messages[-1])

        gamelist.write_text(
            "<gameList><game><path>./Game.nds</path><name>Game</name><cheevosId>4</cheevosId>"
            "<wheel>./missing-strip.png</wheel></game></gameList>"
        )
        self.mod.ra_rom(str(rom))
        self.assertIn("missing", messages[-1])

    def test_state_round_trip_and_a_corrupt_file_starts_empty(self):
        path = self.root / "media-state.json"
        self.mod.STATE = str(path)
        self.assertEqual(self.mod.load_state(), {"tried": {}, "rahash": {}, "ra": {}})
        path.write_text("{", encoding="utf-8")
        self.assertEqual(self.mod.load_state(), {"tried": {}, "rahash": {}, "ra": {}})
        self.mod.save_state({"tried": {"./a.nds": 1}, "rahash": {}, "ra": {"./a.nds": "20260101T000000"}})
        self.assertFalse(path.with_name("media-state.json.new").exists())
        self.assertEqual(json.loads(path.read_text()), {
            "tried": {"./a.nds": 1}, "rahash": {}, "ra": {"./a.nds": "20260101T000000"},
        })

    def test_pillow_wheel_is_the_newest_build_this_glibc_can_load(self):
        tag = "cp%d%d" % self.mod.sys.version_info[:2]
        self.mod.platform.machine = lambda: "aarch64"
        self.mod.platform.libc_ver = lambda: ("glibc", "2.41")

        def wheel(name, payload=b"ok"):
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, "w") as z:
                z.writestr("PIL/marker.txt", payload)
            data = buf.getvalue()
            return data, hashlib.sha256(data).hexdigest(), name

        good_data, good_sum, good_name = wheel(f"pillow-11.0.0-{tag}-{tag}-manylinux_2_28_aarch64.whl", b"new")
        old_data, old_sum, old_name = wheel(f"pillow-10.4.0-{tag}-{tag}-manylinux_2_17_aarch64.whl", b"old")
        yanked_data, yanked_sum, yanked_name = wheel(f"pillow-12.0.0-{tag}-{tag}-manylinux_2_28_aarch64.whl", b"yanked")
        too_new_data, too_new_sum, too_new_name = wheel(f"pillow-11.3.0-{tag}-{tag}-manylinux_2_42_aarch64.whl")
        files = {
            good_name: good_data,
            old_name: old_data,
            yanked_name: yanked_data,
            too_new_name: too_new_data,
        }
        meta = {"releases": {
            "11.0.0": [{"filename": good_name, "url": "https://files.example/" + good_name,
                        "digests": {"sha256": good_sum}}],
            "10.4.0": [{"filename": old_name, "url": "https://files.example/" + old_name,
                        "digests": {"sha256": old_sum}}],
            "12.0.0": [{"filename": yanked_name, "yanked": True, "url": "https://files.example/" + yanked_name,
                        "digests": {"sha256": yanked_sum}}],
            "13.0.0rc1": [{"filename": f"pillow-13.0.0rc1-{tag}-{tag}-manylinux_2_28_aarch64.whl",
                           "url": "https://files.example/rc", "digests": {"sha256": "0" * 64}}],
            "11.3.0": [{"filename": too_new_name, "url": "https://files.example/" + too_new_name,
                        "digests": {"sha256": too_new_sum}}],
        }}

        def fake_http(url, timeout=40):
            if url.endswith("/json"):
                return json.dumps(meta).encode()
            return files[url.rsplit("/", 1)[-1]]

        self.mod.http = fake_http
        dest = self.root / "pylib"
        self.mod.install_pillow(str(dest))
        self.assertEqual((dest / "PIL" / "marker.txt").read_bytes(), b"new")
        self.assertEqual((dest / ".wheel").read_text().strip(), good_name)

        meta["releases"]["11.0.0"][0]["digests"]["sha256"] = "f" * 64
        with self.assertRaises(RuntimeError) as caught:
            self.mod.install_pillow(str(self.root / "bad"))
        self.assertIn("checksum", str(caught.exception))
        self.assertFalse((self.root / "bad").exists())

        self.mod.http = lambda url, timeout=40: b"{}"
        with self.assertRaises(RuntimeError) as caught:
            self.mod.install_pillow(str(self.root / "none"))
        self.assertIn("aarch64", str(caught.exception))

        # The other install lands between the delete and the rename: keep theirs, drop our temp tree.
        self.mod.http = fake_http
        meta["releases"]["11.0.0"][0]["digests"]["sha256"] = good_sum
        real_rmtree = self.mod.shutil.rmtree

        def other_wins(path, ignore_errors=False):
            real_rmtree(path, ignore_errors=ignore_errors)
            if os.path.abspath(path) == os.path.abspath(dest):
                os.makedirs(dest, exist_ok=True)
                (dest / "winner").write_text("other")

        self.mod.shutil.rmtree = other_wins
        try:
            self.mod.install_pillow(str(dest))
        finally:
            self.mod.shutil.rmtree = real_rmtree
        self.assertEqual((dest / "winner").read_text(), "other")
        self.assertFalse((dest / "PIL" / "marker.txt").exists())
        self.assertFalse(any(p.name.startswith("pylib.new") for p in self.root.iterdir()))

    def _hashes(self, paths):
        r = subprocess.run(["python3", "-c", self.mod.RA_HASH], input=json.dumps(paths).encode(),
                           capture_output=True, check=True)
        return json.loads(r.stdout)


if __name__ == "__main__":
    unittest.main()
