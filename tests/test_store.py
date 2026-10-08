"""The ROCKNIXDS Store's package manager (store/device/rocknixds-store) on a handheld in a folder: installs, updates
over what ROCKNIXDS's own installers put there, keeps what an app keeps, refuses damaged packages, and gives an app
ROCKNIXDS doesn't know its tile (an ES system and a line in ROCKNIXDS Pixel's systems.cfg) and takes it away again."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import importlib.machinery
import importlib.util

from loadmod import ROOT

sys.path.insert(0, str(ROOT / "store" / "tests"))
from sandbox import CLI, Sandbox, tar_package  # noqa: E402

_loader = importlib.machinery.SourceFileLoader("rocknixds_store", str(CLI))   # no .py: load it as Python anyway
store = importlib.util.module_from_spec(importlib.util.spec_from_loader("rocknixds_store", _loader))
_loader.exec_module(store)


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sb = Sandbox(self.tmp.name)
        self.sb.publish_dodakvarter("0.2.0")
        self.sb.publish_dodakvarter("0.3.0")
        self.sb.publish_bank("0.1.0")
        self.sb.publish_hello("1.0.0")

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, ok=True):
        r = subprocess.run([sys.executable, str(CLI)] + list(args), env=self.sb.env(), capture_output=True, text=True)
        last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
        if ok:
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r, last

    def states(self):
        r, _ = self.run_cli("list")
        return {l.split("\t")[1]: l.split("\t") for l in r.stdout.splitlines() if l.startswith("APP\t")}

    @property
    def cfg(self):
        return self.sb.storage / ".config"

    # ---- the catalog and what's out ----
    def test_refresh_lists_newest_versions_and_states(self):
        self.sb.legacy_dodakvarter("0.2.0")
        _, last = self.run_cli("refresh")
        self.assertEqual(last, "OK")
        s = self.states()
        self.assertEqual(s["dodakvarter"][5:8], ["0.2.0", "0.3.0", "update"])
        self.assertEqual(s["bank"][5:9], ["", "0.1.0", "install", "24156930"])
        self.assertEqual(s["hello"][7], "install")
        self.assertEqual(s["store"][7], "unavailable")       # no store-v release in the sandbox

    def test_refresh_offline_keeps_what_it_knew(self):
        self.run_cli("refresh")
        env_cat = self.sb.catalog_path
        env_cat.rename(env_cat.with_suffix(".gone"))
        _, last = self.run_cli("refresh")
        self.assertEqual(last, "OFFLINE")
        self.assertEqual(self.states()["bank"][6], "0.1.0")

    def test_refresh_converts_pictures_for_the_app(self):
        self.run_cli("refresh")
        for app, name, size in (("bank", "icon.raw", (32, 32)), ("bank", "shot.raw", (160, 120)),
                                ("dodakvarter", "shot.raw", (160, 120))):
            b = (self.sb.data / "media" / app / name).read_bytes()
            self.assertEqual(b[:8], b"RNDSIMG1")
            w, h = int.from_bytes(b[8:10], "little"), int.from_bytes(b[10:12], "little")
            self.assertEqual((w, h), size)
            self.assertEqual(len(b), 12 + w * h * 4)

    def test_png_decoder_matches_pillow(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("no Pillow")
        pics = [ROOT / "bank/device/media/bank-marquee.png", ROOT / "dodakvarter/device/media/dodakvarter-image.png",
                ROOT / "dii-ess-aye/themes/rocknixds-pixel-dark/rnds/icons/bank.png",
                ROOT / "dii-ess-aye/themes/rocknixds-pixel-dark/rnds/icons/store.png"]
        for p in pics:
            w, h, px = store.png_decode(p.read_bytes())
            im = Image.open(p).convert("RGBA")
            self.assertEqual((w, h), im.size)
            want = [a << 24 | r << 16 | g << 8 | b for r, g, b, a in im.getdata()]
            self.assertEqual(px, want, p.name)

    # ---- installs ----
    def test_install_new_app_gets_its_own_tile(self):
        self.run_cli("refresh")
        _, last = self.run_cli("install", "hello")
        self.assertEqual(last, "DONE 1.0.0")
        d = self.cfg / "rocknixds" / "hello"
        self.assertTrue((d / "hello").stat().st_mode & 0o100)
        self.assertEqual((d / "VERSION").read_text().strip(), "1.0.0")
        self.assertTrue((d / "setup-ran").exists())            # its post_install ran
        tile = self.cfg / "rocknixds" / "apps" / "hello"
        self.assertTrue((tile / "Hello.sh").exists())
        gl = (tile / "gamelist.xml").read_text()
        self.assertIn("<name>Hello Handheld</name>", gl)
        self.assertIn("./images/hello-image.png", gl)
        es = (self.sb.es / "es_systems_rocknixds-store.cfg").read_text()
        self.assertIn("<name>hello</name>", es)
        self.assertIn("<path>/storage/.config/rocknixds/apps/hello</path>", es)
        theme = (self.sb.theme / "systems.cfg").read_text()
        self.assertIn(store.THEME_MARK_BEGIN, theme)
        self.assertRegex(theme, r"\nhello +store-hello\.png +#b07ef0 +Someone Else \[app\]\n")
        self.assertTrue((self.sb.theme / "icons" / "store-hello.png").exists())
        self.assertEqual(self.states()["hello"][7], "installed")

    def test_known_apps_use_rocknixds_own_system_and_theme_line(self):
        self.run_cli("refresh")
        self.run_cli("install", "bank")
        self.assertFalse((self.sb.es / "es_systems_rocknixds-store.cfg").exists())
        self.assertNotIn(store.THEME_MARK_BEGIN, (self.sb.theme / "systems.cfg").read_text())
        tile = self.cfg / "rocknixds" / "apps" / "bank"
        self.assertTrue((tile / "ROCKNIXDS Bank.sh").exists())
        self.assertTrue((tile / "images" / "bank-thumb.png").exists())
        self.assertTrue((self.cfg / "rocknixds" / "bank" / "rocknixds-bank").exists())

    def test_update_over_rocknixds_install_keeps_data(self):
        d = self.sb.legacy_dodakvarter("0.2.0")
        self.run_cli("refresh")
        _, last = self.run_cli("update")
        self.assertEqual(last, "DONE 0.3.0")
        self.assertEqual((d / "VERSION").read_text().strip(), "0.3.0")
        self.assertIn("game 0.3.0", (d / "dodakvarter").read_text())
        self.assertEqual((d / "data" / "scores.txt").read_text(), "JRF 30\n")
        self.assertIn("<name>Döda Kvarter</name>", (self.cfg / "rocknixds/apps/dodakvarter/gamelist.xml").read_text())
        self.assertFalse((self.sb.storage / "roms/ports/Doda Kvarter.sh").exists())
        _, last = self.run_cli("install", "dodakvarter")          # nothing newer: nothing done
        self.assertEqual(last, "DONE 0.3.0")

    def test_install_pinned_older_version(self):
        self.sb.legacy_dodakvarter("0.3.0")
        _, last = self.run_cli("install", "dodakvarter", "--version", "0.2.0")
        self.assertEqual(last, "DONE 0.2.0")

    def test_damaged_download_changes_nothing(self):
        d = self.sb.legacy_dodakvarter("0.2.0")
        pkg = self.sb.dl / "dodakvarter-v0.3.0" / "dodakvarter-0.3.0-aarch64.tar.gz"
        pkg.write_bytes(pkg.read_bytes()[:-10] + b"0123456789")
        self.run_cli("refresh")
        r, last = self.run_cli("install", "dodakvarter", ok=False)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(last, "FAIL Download damaged")
        self.assertEqual((d / "VERSION").read_text().strip(), "0.2.0")
        self.assertEqual((d / "dodakvarter").read_text(), "old game\n")

    def test_package_with_wrong_version_or_unsafe_paths_is_refused(self):
        self.sb.publish_hello("1.1.0", extra={"VERSION": b"9.9.9\n"})
        self.run_cli("refresh")
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertEqual(last, "FAIL Wrong version inside")
        p = self.sb.dl / "v1.2.0" / "hello-1.2.0-aarch64.tar.gz"
        import io
        import tarfile
        p.parent.mkdir(parents=True)
        with tarfile.open(p, "w:gz") as t:
            ti = tarfile.TarInfo("../../evil")
            ti.size = 1
            t.addfile(ti, io.BytesIO(b"x"))
        import hashlib
        Path(str(p) + ".sha256").write_text(hashlib.sha256(p.read_bytes()).hexdigest() + "  x\n")
        _, last = self.run_cli("install", "hello", "--version", "1.2.0", ok=False)
        self.assertEqual(last, "FAIL Package damaged")
        self.assertFalse((Path(self.tmp.name) / "evil").exists())

    def test_wrong_handheld_and_old_rocknixds_are_refused(self):
        self.sb.write_catalog(extra=[
            {"id": "plusonly", "name": "Plus only", "devices": ["rgds-plus"],
             "release": {"url": "x.tar.gz", "version": "1.0", "sha256": "00"}},
            {"id": "future", "name": "Future", "requires": {"rocknixds": "2.0"},
             "release": {"url": "x.tar.gz", "version": "1.0", "sha256": "00"}}])
        self.run_cli("refresh")
        s = self.states()
        self.assertEqual(s["plusonly"][7], "unsupported")
        self.assertEqual(s["future"][7], "needs-2.0")
        _, last = self.run_cli("install", "plusonly", ok=False)
        self.assertEqual(last, "FAIL Not made for this handheld")
        _, last = self.run_cli("install", "future", ok=False)
        self.assertEqual(last, "FAIL Needs ROCKNIXDS 2.0")

    def test_pinned_release_with_its_sha256(self):
        p = Path(self.tmp.name) / "pinned" / "pin-1.0.tar.gz"
        sha = tar_package(str(p), "pin", {"pin.sh": b"#!/bin/sh\n", "Pin.sh": b"#!/bin/sh\n"})
        self.sb.write_catalog(extra=[{"id": "pin", "name": "Pinned", "menu": {"entry": "Pin.sh"},
                                      "package": {"check": "pin.sh"},
                                      "release": {"url": p.as_uri(), "version": "1.0", "sha256": sha}}])
        self.run_cli("refresh")
        _, last = self.run_cli("install", "pin")
        self.assertEqual(last, "DONE 1.0")
        self.assertIn("<name>pin</name>", (self.sb.es / "es_systems_rocknixds-store.cfg").read_text())

    def test_template_app_installs(self):
        """store/template/myapp, packaged by store/tools/package-app.sh, as a new developer's first app"""
        out = Path(self.tmp.name) / "tpl"
        subprocess.run(["sh", str(ROOT / "store/tools/package-app.sh"), str(ROOT / "store/template/myapp"), str(out)],
                       check=True, capture_output=True)
        p = out / "myapp-1.0.0-aarch64.tar.gz"
        sha = (out / "myapp-1.0.0-aarch64.tar.gz.sha256").read_text().split()[0]
        self.sb.write_catalog(extra=[{"id": "myapp", "name": "My App", "kind": "app", "developer": "You",
                                      "release": {"url": p.as_uri(), "version": "1.0.0", "sha256": sha}}])
        _, last = self.run_cli("install", "myapp")
        self.assertEqual(last, "DONE 1.0.0")
        self.assertTrue((self.cfg / "rocknixds/apps/myapp/My App.sh").exists())
        self.assertIn("store-myapp.png", (self.sb.theme / "systems.cfg").read_text())

    # ---- remove, relink ----
    def test_remove_takes_tile_away_and_keeps_saves(self):
        self.run_cli("refresh")
        self.run_cli("install", "hello")
        d = self.cfg / "rocknixds" / "hello"
        (d / "saves").mkdir()
        (d / "saves" / "slot1").write_text("mine")
        _, last = self.run_cli("remove", "hello")
        self.assertEqual(last, "DONE")
        self.assertEqual(sorted(p.name for p in d.iterdir()), ["saves"])
        self.assertFalse((self.cfg / "rocknixds" / "apps" / "hello").exists())
        self.assertFalse((self.sb.es / "es_systems_rocknixds-store.cfg").exists())
        self.assertNotIn(store.THEME_MARK_BEGIN, (self.sb.theme / "systems.cfg").read_text())
        self.assertFalse((self.sb.theme / "icons" / "store-hello.png").exists())
        self.assertEqual(self.states()["hello"][7], "install")

    def test_store_does_not_remove_itself(self):
        _, last = self.run_cli("remove", "store", ok=False)
        self.assertIn("doesn't remove itself", last)

    def test_relink_after_rocknixds_update_puts_tiles_back(self):
        self.run_cli("refresh")
        self.run_cli("install", "hello")
        # a ROCKNIXDS update puts the theme back as it ships, and its own ES list
        (self.sb.theme / "systems.cfg").write_text(
            (ROOT / "dii-ess-aye/themes/rocknixds-pixel-dark/rnds/systems.cfg").read_text())
        (self.sb.theme / "icons" / "store-hello.png").unlink()
        self.run_cli("relink")
        theme = (self.sb.theme / "systems.cfg").read_text()
        self.assertEqual(theme.count(store.THEME_MARK_BEGIN), 1)
        self.assertIn("store-hello.png", theme)
        self.assertTrue((self.sb.theme / "icons" / "store-hello.png").exists())
        self.run_cli("relink")                                    # twice: one block still
        self.assertEqual((self.sb.theme / "systems.cfg").read_text(), theme)


class StoreWithoutTilesTest(unittest.TestCase):
    """ROCKNIXDS before 1.6 (no es_systems_rocknixds.cfg): apps go to Ports, no systems are written"""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sb = Sandbox(self.tmp.name, tiles=False)
        self.sb.publish_hello("1.0.0")

    def tearDown(self):
        self.tmp.cleanup()

    def test_install_goes_to_ports(self):
        r = subprocess.run([sys.executable, str(CLI), "install", "hello"], env=self.sb.env(), capture_output=True, text=True)
        self.assertTrue(r.stdout.strip().endswith("DONE 1.0.0"), r.stdout)
        ports = self.sb.storage / "roms" / "ports"
        self.assertTrue((ports / "Hello.sh").exists())
        self.assertIn("Hello Handheld", (ports / "gamelist.xml").read_text())
        self.assertFalse((self.sb.es / "es_systems_rocknixds-store.cfg").exists())
        self.assertNotIn(store.THEME_MARK_BEGIN, (self.sb.theme / "systems.cfg").read_text())


class CatalogTest(unittest.TestCase):
    """The catalog the Store fetches from main: every app's layout is one the package manager accepts, and every
    picture it names is in the repository."""
    def test_catalog(self):
        c = json.loads((ROOT / "store" / "catalog.json").read_text())
        ids = set()
        for a in c["apps"]:
            self.assertRegex(a["id"], store.ID_RE.pattern)
            self.assertNotIn(a["id"], ids)
            ids.add(a["id"])
            store.layout(a)
            for ref in [a.get("icon")] + a.get("screenshots", []):
                if ref:
                    self.assertTrue((ROOT / "store" / ref).resolve().exists(), ref)
            self.assertRegex(a.get("accent", "#000000"), r"^#[0-9a-f]{6}$")
        self.assertTrue({"dodakvarter", "bank", "store"} <= ids)


if __name__ == "__main__":
    unittest.main()
