"""es-features.sh writes DraStic's ES options. 1.5.5 nested the 3D renderer inside "share
performance logs", which ES never reads (it only walks direct <feature> children), and the
next boot's rewrite left a stray </feature> so the whole es_features.cfg failed to parse."""
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from loadmod import ROOT

SCRIPT = ROOT / "dsflip" / "device" / "es-features.sh"

BASE = """<?xml version="1.0"?>
<features>
  <emulator name="retroarch">
    <cores>
      <core name="nestopia">
        <features>
          <feature name="shader" value="shader">
            <choice name="none" value="none" />
          </feature>
        </features>
      </core>
    </cores>
  </emulator>
  <emulator name="drastic">
    <cores>
      <core name="drastic-sa">
        <features>
          <feature name="shader" value="shader">
            <choice name="none" value="none" />
          </feature>
          <feature name="microphone sensitivity" value="mic">
            <choice name="off" value="0" />
          </feature>
        </features>
      </core>
    </cores>
  </emulator>
</features>
"""

# What 1.5.5 wrote on the first boot: 3D options inside share performance logs.
NESTED = """<?xml version="1.0"?>
<features>
  <emulator name="drastic">
    <cores>
      <core name="drastic-sa">
        <features>
          <feature name="shader" value="shader">
            <choice name="none" value="none" />
          </feature>
          <feature name="share performance logs">
            <choice name="yes" value="1" />
            <choice name="no" value="0" />
            <feature name="3D renderer" value="renderer">
              <choice name="DraStic" value="drastic" />
              <choice name="Gengis Engine" value="superdrastic" />
            </feature>
            <feature name="3D texture filter" value="texture_filter">
              <choice name="nearest (DS)" value="nearest" />
            </feature>
          </feature>
          <feature name="microphone sensitivity" value="mic">
            <choice name="off" value="0" />
          </feature>
        </features>
      </core>
    </cores>
  </emulator>
</features>
"""

# The following boot: the skip stopped at the nested close and left an extra </feature>.
STRAY = NESTED.replace(
    "          </feature>\n          <feature name=\"microphone sensitivity\"",
    "          </feature>\n          </feature>\n          <feature name=\"microphone sensitivity\"",
    1,
)


def direct_features(text, core):
    root = ET.fromstring(text)
    for node in root.iter("core"):
        if node.get("name") == core:
            box = node.find("features")
            parent = box if box is not None else node
            return [(f.get("name"), [c.tag for c in list(f) if c.tag == "feature"]) for f in list(parent) if f.tag == "feature"]
    return None


class EsFeaturesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.user = self.root / "es_features.cfg"
        self.system = self.root / "system.cfg"
        self.system.write_text("<features></features>\n")
        self.state = self.root / "state"
        self.state.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self):
        env = os.environ.copy()
        env.update({
            "ESF_SYSTEM": str(self.system),
            "ESF_USER": str(self.user),
            "ESF_STATE": str(self.state),
            "ESS_USER": str(self.root / "missing-systems.cfg"),
        })
        subprocess.run(["sh", str(SCRIPT)], check=True, env=env)

    def assert_siblings(self, text):
        ET.fromstring(text)
        feats = dict(direct_features(text, "drastic-sa"))
        self.assertIn("3D renderer", feats)
        self.assertIn("3D texture filter", feats)
        self.assertIn("share performance logs", feats)
        self.assertEqual(feats["share performance logs"], [])
        self.assertEqual(feats["3D renderer"], [])
        self.assertEqual(feats["microphone sensitivity"], [])
        nestopia = dict(direct_features(text, "nestopia")) if "nestopia" in text else {}
        if nestopia:
            self.assertEqual(list(nestopia), ["shader"])

    def test_new_options_are_siblings_and_stable(self):
        self.user.write_text(BASE)
        self.run_script()
        first = self.user.read_text()
        self.assert_siblings(first)
        self.run_script()
        self.assertEqual(self.user.read_text(), first)

    def test_repairs_nested_1_5_5_output(self):
        self.user.write_text(NESTED)
        self.run_script()
        self.assert_siblings(self.user.read_text())
        again = self.user.read_text()
        self.run_script()
        self.assertEqual(self.user.read_text(), again)

    def test_repairs_stray_close_from_the_second_boot(self):
        self.assertIn("</feature>\n          </feature>", STRAY)
        with self.assertRaises(ET.ParseError):
            ET.fromstring(STRAY)
        self.user.write_text(STRAY)
        self.run_script()
        self.assert_siblings(self.user.read_text())
