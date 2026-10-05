"""es-features.sh writes DraStic's menu options. The RG DS's 1.5.5 nested its 3D options inside
"share performance logs". EmulationStation only reads a core's direct <feature> children and
each feature's own <choice> children, so they never appeared. The next boot stopped removing
that feature at the first </feature> and left a stray close, so the whole es_features.cfg failed
to parse and every DraStic setting disappeared (#36, #37). The Plus used the same one-level skip;
this is the RG DS line's test with the Plus's options: no "share performance logs" and no
"3D resolution"."""
import hashlib
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
          <feature name="nested-parent" value="np">
            <feature name="nested-child" value="nc">
              <choice name="a" value="a" />
            </feature>
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
            <choice name="low" value="1" />
          </feature>
          <feature name="3D resolution" value="resolution3d">
            <choice name="2x" value="2x" />
            <choice name="3x" value="3x" />
          </feature>
        </features>
      </core>
    </cores>
  </emulator>
</features>
"""

# What the RG DS's 1.5.5 wrote: the 3D options sit inside share performance logs, so ES never shows them.
NESTED = """<?xml version="1.0"?>
<features>
  <emulator name="drastic">
    <cores>
      <core name="drastic-sa">
        <features>
          <feature name="shader" value="shader">
            <choice name="none" value="none" />
            <choice name="ds-crisp (sharp, 1x and 2x)" value="ds-crisp" />
          </feature>
          <feature name="resume on quit">
            <choice name="on" value="1" />
            <choice name="off" value="0" />
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
              <choice name="bilinear" value="bilinear" />
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

# The following boot: the old skip stopped at the nested close and left an extra </feature>.
STRAY = NESTED.replace(
    "          </feature>\n          <feature name=\"microphone sensitivity\"",
    "          </feature>\n          </feature>\n          <feature name=\"microphone sensitivity\"",
    1,
)
# A later boot leaves one more unmatched close. Each one keeps the file from parsing.
STRAY_AGAIN = STRAY.replace(
    "          </feature>\n          <feature name=\"microphone sensitivity\"",
    "          </feature>\n          </feature>\n          <feature name=\"microphone sensitivity\"",
    1,
)


def core_features(text, core):
    root = ET.fromstring(text)
    for node in root.iter("core"):
        if node.get("name") != core:
            continue
        box = node.find("features")
        parent = box if box is not None else node
        found = {}
        for feat in list(parent):
            if feat.tag != "feature":
                continue
            found[feat.get("name")] = {
                "value": feat.get("value"),
                "kids": [(child.tag, child.get("name"), child.get("value")) for child in list(feat)],
            }
        return found
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
        self.systems = self.root / "es_systems.cfg"
        self.systems.write_text("untouched\n")

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, *args):
        env = os.environ.copy()
        env.update({
            "ESF_SYSTEM": str(self.system),
            "ESF_USER": str(self.user),
            "ESF_STATE": str(self.state),
            "ESS_USER": str(self.systems),
            "ESS_SYSTEM": str(self.root / "missing-systems.cfg"),
        })
        subprocess.run(["sh", str(SCRIPT), *args], check=True, env=env)

    def assert_siblings(self, text):
        feats = core_features(text, "drastic-sa")
        self.assertIn("3D renderer", feats)
        self.assertIn("3D texture filter", feats)
        self.assertIn("resume on quit", feats)
        self.assertIn("power profile", feats)
        self.assertIn("microphone sensitivity", feats)
        self.assertIn("wfc dns", feats)
        self.assertNotIn("share performance logs", feats)
        self.assertNotIn("3D resolution", feats)
        self.assertEqual(feats["wfc dns"]["value"], "wfc_dns")
        self.assertEqual([value for tag, _, value in feats["wfc dns"]["kids"]], ["off", "kaeru", "wiilink", "altwfc"])
        for name in ("3D renderer", "3D texture filter", "microphone sensitivity", "wfc dns"):
            self.assertFalse(any(tag == "feature" for tag, _, _ in feats[name]["kids"]), name)
        self.assertEqual(feats["3D renderer"]["value"], "renderer")
        self.assertEqual(
            [(name, value) for tag, name, value in feats["3D renderer"]["kids"]],
            [("DraStic", "drastic"), ("Gengis Engine", "superdrastic")],
        )
        self.assertNotIn('value="3x"', text)
        self.assertNotIn('name="3x"', text)
        self.assertEqual(feats["3D texture filter"]["value"], "texture_filter")
        self.assertEqual(
            [value for tag, _, value in feats["3D texture filter"]["kids"]],
            ["nearest", "bilinear", "sharp"],
        )
        self.assertEqual([value for tag, _, value in feats["resume on quit"]["kids"]], ["1", "0"])
        self.assertEqual(
            [value for tag, _, value in feats["power profile"]["kids"]],
            ["balanced", "performance", "battery"],
        )
        shader_values = [value for tag, _, value in feats["shader"]["kids"]]
        self.assertIn("none", shader_values)
        self.assertIn("ds-crisp", shader_values)
        self.assertEqual(shader_values.count("ds-crisp"), 1)
        if "nestopia" in text:
            other = core_features(text, "nestopia")
            self.assertEqual(list(other), ["shader", "nested-parent"])
            self.assertEqual(other["nested-parent"]["kids"][0][0], "feature")
            self.assertEqual(other["nested-parent"]["kids"][0][1], "nested-child")

    def test_new_options_are_siblings_and_stable(self):
        self.user.write_text(BASE)
        self.run_script()
        first = self.user.read_text()
        self.assert_siblings(first)
        self.run_script()
        self.assertEqual(self.user.read_text(), first)
        self.assertEqual(self.systems.read_text(), "untouched\n")

    def test_repairs_nested_1_5_5_output(self):
        self.user.write_text(NESTED)
        self.run_script()
        text = self.user.read_text()
        self.assert_siblings(text)
        self.assertIn("microphone sensitivity", text)
        self.run_script()
        self.assertEqual(self.user.read_text(), text)

    def test_repairs_stray_close_from_later_boots(self):
        self.assertGreater(STRAY.count("</feature>"), NESTED.count("</feature>"))
        self.assertGreater(STRAY_AGAIN.count("</feature>"), STRAY.count("</feature>"))
        for broken in (STRAY, STRAY_AGAIN):
            with self.assertRaises(ET.ParseError):
                ET.fromstring(broken)
            self.user.write_text(broken)
            self.run_script()
            self.assert_siblings(self.user.read_text())

    def test_created_copy_repairs_the_user_file_when_rocknix_is_unchanged(self):
        digest = hashlib.md5(self.system.read_bytes()).hexdigest()
        (self.state / ".esf-created").touch()
        (self.state / ".esf-system-md5").write_text(digest + "\n")
        self.user.write_text(STRAY)
        self.run_script()
        self.assert_siblings(self.user.read_text())
        self.assertFalse((self.user.parent / "es_features.cfg.rocknixds-old").exists())

    def test_strip_options_removes_ours_and_repairs_a_broken_file(self):
        self.user.write_text(STRAY_AGAIN)
        self.run_script("--strip-options")
        text = self.user.read_text()
        ET.fromstring(text)
        feats = core_features(text, "drastic-sa")
        self.assertEqual(list(feats), ["shader", "microphone sensitivity"])
        self.assertEqual([value for _, _, value in feats["shader"]["kids"]], ["none"])
        self.assertNotIn("ds-crisp", text)
        self.assertNotIn("3D renderer", text)
        self.assertNotIn("share performance logs", text)
        self.run_script("--strip-options")
        self.assertEqual(self.user.read_text(), text)
        self.assertEqual(self.systems.read_text(), "untouched\n")

    def test_strip_options_removes_what_this_line_writes(self):
        self.user.write_text(BASE)
        self.run_script()
        self.assertIn("wfc dns", self.user.read_text())
        self.run_script("--strip-options")
        text = self.user.read_text()
        feats = core_features(text, "drastic-sa")
        self.assertEqual(list(feats), ["shader", "microphone sensitivity"])
        self.assertEqual([value for _, _, value in feats["shader"]["kids"]], ["none"])
        self.assertNotIn("ds-", text)

    def test_strip_options_leaves_an_unmodified_file_alone(self):
        plain = BASE.replace(
            "          <feature name=\"3D resolution\" value=\"resolution3d\">\n"
            "            <choice name=\"2x\" value=\"2x\" />\n"
            "            <choice name=\"3x\" value=\"3x\" />\n"
            "          </feature>\n",
            "",
        )
        self.assertNotIn("3D resolution", plain)
        self.user.write_text(plain)
        self.run_script("--strip-options")
        self.assertEqual(self.user.read_text(), plain)

    def test_strip_drops_an_old_3d_resolution_option(self):
        self.user.write_text(BASE)
        self.run_script("--strip-options")
        text = self.user.read_text()
        ET.fromstring(text)
        self.assertNotIn("3D resolution", text)
        self.assertIn("microphone sensitivity", text)
        self.assertIn("nested-child", text)
        feats = core_features(text, "nestopia")
        self.assertEqual(feats["nested-parent"]["kids"][0][1], "nested-child")

    def test_a_one_line_option_of_rocknix_stays(self):
        # its close came before its open and was taken for a stray one, so the whole option was deleted
        line = ('          <feature name="smooth" value="smooth"><choice name="on" value="1" />'
                '<choice name="off" value="0" /></feature>\n')
        mic = '          <feature name="microphone sensitivity"'
        for text in (BASE, STRAY):
            self.user.write_text(text.replace(mic, line + mic, 1))
            self.run_script()
            fixed = self.user.read_text()
            self.assert_siblings(fixed)
            self.assertEqual([value for _, _, value in core_features(fixed, "drastic-sa")["smooth"]["kids"]], ["1", "0"])
            self.run_script("--strip-options")
            self.assertIn(line, self.user.read_text())

    def test_a_one_line_copy_of_ours_goes_on_its_own(self):
        # it opened a skip that ran on through the next options and the closing tags
        ours = '          <feature name="power profile"><choice name="balanced" value="balanced" /></feature>\n'
        mic = '          <feature name="microphone sensitivity"'
        self.user.write_text(BASE.replace(mic, ours + mic, 1))
        self.run_script("--strip-options")
        text = self.user.read_text()
        self.assertEqual(list(core_features(text, "drastic-sa")), ["shader", "microphone sensitivity"])
        self.assertEqual(core_features(text, "nestopia")["nested-parent"]["kids"][0][1], "nested-child")

    def test_missing_shader_option_is_left_unchanged(self):
        original = "<features><core name=\"other\"></core></features>\n"
        self.user.write_text(original)
        self.run_script()
        self.assertEqual(self.user.read_text(), original)

    def test_uninstall_calls_the_depth_aware_strip(self):
        install = (ROOT / "install.sh").read_text()
        self.assertIn("--strip-options", install)
        self.assertNotIn('share performance logs"/ { skip = 1 }', install)
