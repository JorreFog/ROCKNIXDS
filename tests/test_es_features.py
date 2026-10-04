"""es-features.sh: the DraStic menu options, and the NDS system locked to drastic-sa.

The 3D renderer and texture filter carry value attributes (renderer, texture_filter) so ES
stores nds.renderer and nds.texture_filter, which session.sh reads. They are siblings of
"share performance logs", not nested in it. A 3D resolution option left from an older
install is removed: 1.5.5 does not offer 3x. Re-running changes nothing. A file the
installer created is rebuilt when ROCKNIX's copy changes; a player's own file is only
patched. --unlock-nds puts ROCKNIX's emulator list back.
"""
import hashlib
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from loadmod import ROOT

SCRIPT = ROOT / "dsflip" / "device" / "es-features.sh"

FEATURES = """<?xml version="1.0"?>
<features>
  <core name="other">
    <feature name="shader">
      <choice name="keep" value="keep" />
    </feature>
  </core>
  <core name="drastic-sa">
    <feature name="resume on quit">
      <choice name="on" value="1" />
    </feature>
    <feature name="shader">
      <choice name="none" value="none" />
      <choice name="old crisp" value="ds-crisp" />
    </feature>
    <feature name="3D resolution">
      <choice name="3x" value="3" />
    </feature>
    <feature name="3D renderer" value="wrong">
      <choice name="old" value="old" />
    </feature>
    <feature name="microphone sensitivity">
      <choice name="low" value="10" />
    </feature>
  </core>
</features>
"""

SHADER_VALUES = [
    "none", "ds-crisp", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-grid-2x", "ds-fsr", "ds-integer",
]


def choices(feature):
    return [(c.get("name"), c.get("value")) for c in feature.findall("choice")]


def direct_features(core):
    return [el for el in list(core) if el.tag == "feature"]


class EsFeaturesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sys = self.root / "sys.cfg"
        self.user = self.root / "user.cfg"
        self.state = self.root / "state"
        self.state.mkdir()
        self.ess = self.root / "es_systems.cfg"
        self.ess_sys = self.root / "es_systems.stock.cfg"

    def tearDown(self):
        self.tmp.cleanup()

    def run_sh(self, *args):
        env = os.environ.copy()
        env.update({
            "ESF_SYSTEM": str(self.sys),
            "ESF_USER": str(self.user),
            "ESF_STATE": str(self.state),
            "ESS_USER": str(self.ess),
            "ESS_SYSTEM": str(self.ess_sys),
        })
        return subprocess.run(["sh", str(SCRIPT), *args], env=env, capture_output=True, text=True, check=False)

    def core(self, path, name):
        tree = ET.parse(path)
        for el in tree.iter("core"):
            if el.get("name") == name:
                return el
        self.fail(f"no core {name}")

    def test_options_are_siblings_and_a_second_run_is_a_noop(self):
        self.sys.write_text("<features/>\n")
        self.user.write_text(FEATURES)
        first = self.run_sh()
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("updated", first.stdout)
        self.assertNotIn("from ROCKNIX's copy", first.stdout)

        drastic = direct_features(self.core(self.user, "drastic-sa"))
        names = [f.get("name") for f in drastic]
        self.assertEqual(names, [
            "shader", "resume on quit", "power profile", "share performance logs",
            "3D renderer", "3D texture filter", "microphone sensitivity",
        ])
        by_name = {f.get("name"): f for f in drastic}
        for feat in by_name.values():
            self.assertEqual(feat.findall("feature"), [], feat.get("name"))

        self.assertEqual([v for _, v in choices(by_name["shader"])], SHADER_VALUES)
        self.assertEqual(choices(by_name["resume on quit"]), [("on", "1"), ("off", "0")])
        self.assertEqual(choices(by_name["power profile"]), [
            ("balanced", "balanced"), ("performance", "performance"), ("battery saver", "battery"),
        ])
        self.assertEqual(choices(by_name["share performance logs"]), [("yes", "1"), ("no", "0")])
        self.assertEqual(by_name["3D renderer"].get("value"), "renderer")
        self.assertEqual(choices(by_name["3D renderer"]), [("DraStic", "drastic"), ("Gengis Engine", "superdrastic")])
        self.assertEqual(by_name["3D texture filter"].get("value"), "texture_filter")
        self.assertEqual(choices(by_name["3D texture filter"]), [
            ("nearest (DS)", "nearest"), ("bilinear", "bilinear"), ("sharp bilinear", "sharp"),
        ])
        self.assertEqual(choices(by_name["microphone sensitivity"]), [("low", "10")])
        self.assertNotIn("3D resolution", names)
        self.assertNotIn('value="old"', self.user.read_text())
        self.assertNotIn('value="3"', self.user.read_text())

        other = direct_features(self.core(self.user, "other"))
        self.assertEqual([f.get("name") for f in other], ["shader"])
        self.assertEqual(choices(other[0]), [("keep", "keep")])

        text = self.user.read_text()
        second = self.run_sh()
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertNotIn("updated", second.stdout)
        self.assertEqual(self.user.read_text(), text)

    def test_no_shader_option_leaves_the_file(self):
        self.sys.write_text("<features/>\n")
        original = "<features><core name=\"drastic-sa\"><feature name=\"other\"/></core></features>\n"
        self.user.write_text(original)
        out = self.run_sh()
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("no drastic-sa shader option", out.stdout)
        self.assertEqual(self.user.read_text(), original)

    def test_a_player_file_is_patched_until_rocknix_changes(self):
        def cfg(choice, value):
            return (
                "<?xml version=\"1.0\"?>\n<features>\n  <core name=\"drastic-sa\">\n"
                "    <feature name=\"shader\">\n"
                f"      <choice name=\"{choice}\" value=\"{value}\" />\n"
                "    </feature>\n  </core>\n</features>\n"
            )
        self.sys.write_text(cfg("stock", "stock"))
        self.user.write_text(cfg("mine", "mine"))
        (self.state / ".esf-created").write_text("1")
        digest = hashlib.md5(self.sys.read_bytes()).hexdigest()
        (self.state / ".esf-system-md5").write_text(digest + "\n")

        kept = self.run_sh()
        self.assertEqual(kept.returncode, 0, kept.stderr)
        self.assertNotIn("from ROCKNIX's copy", kept.stdout)
        text = self.user.read_text()
        self.assertIn('value="mine"', text)
        self.assertNotIn('value="stock"', text)
        self.assertEqual(self.core(self.user, "drastic-sa").find("feature[@name='3D renderer']").get("value"), "renderer")

        self.sys.write_text(self.sys.read_text().replace("stock", "stock2"))
        rebuilt = self.run_sh()
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stderr)
        self.assertIn("from ROCKNIX's copy", rebuilt.stdout)
        new = self.user.read_text()
        self.assertIn('value="stock2"', new)
        self.assertNotIn('value="mine"', new)
        self.assertIn('value="mine"', Path(str(self.user) + ".rocknixds-old").read_text())
        self.assertEqual((self.state / ".esf-system-md5").read_text().split()[0],
                         hashlib.md5(self.sys.read_bytes()).hexdigest())
        again = self.run_sh()
        self.assertNotIn("updated", again.stdout)
        self.assertEqual(self.user.read_text(), new)

    def test_nds_emulators_lock_to_drastic_and_unlock(self):
        systems = """<?xml version="1.0"?>
<systemList>
  <system>
    <name>nds</name>
    <emulators>
      <emulator name="melonds"><cores><core>melonds-user</core></cores></emulator>
    </emulators>
  </system>
  <system>
    <name>gba</name>
    <emulators>
      <emulator name="mgba"><cores><core>mgba-keep</core></cores></emulator>
    </emulators>
  </system>
</systemList>
"""
        stock = systems.replace("melonds-user", "melonds-sys")
        self.ess.write_text(systems)
        self.ess_sys.write_text(stock)
        locked = self.run_sh()
        self.assertEqual(locked.returncode, 0, locked.stderr)
        self.assertIn("locked", locked.stdout)
        nds, gba = self._emulators(self.ess)
        self.assertEqual([e.get("name") for e in nds], ["drastic"])
        self.assertEqual([c.text for c in nds[0].iter("core")], ["drastic-sa"])
        self.assertEqual(nds[0].find("cores/core").get("default"), "true")
        self.assertEqual([c.text for c in gba[0].iter("core")], ["mgba-keep"])
        self.assertNotIn("melonds-user", self.ess.read_text())

        unlocked = self.run_sh("--unlock-nds")
        self.assertEqual(unlocked.returncode, 0, unlocked.stderr)
        self.assertIn("unlocked", unlocked.stdout)
        nds, gba = self._emulators(self.ess)
        self.assertEqual([c.text for c in nds[0].iter("core")], ["melonds-sys"])
        self.assertEqual([c.text for c in gba[0].iter("core")], ["mgba-keep"])

    def _emulators(self, path):
        tree = ET.parse(path)
        found = {}
        for system in tree.iter("system"):
            name = system.findtext("name")
            if name in ("nds", "gba"):
                found[name] = system.find("emulators").findall("emulator")
        return found["nds"], found["gba"]


if __name__ == "__main__":
    unittest.main()
