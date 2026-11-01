"""After a game, start_es_rgds.sh puts the panels back side by side and restores the touch
calibration it read when ES started. Vertical arcade games stack the panels and recalibrate
the touchscreen; until the next boot the menu then missed the bottom panel (RG DS Plus, test
build 6). These run the script's own parsers.
"""
import json
import re
import subprocess
import unittest

from loadmod import ROOT

SCRIPT = (ROOT / "dii-ess-aye" / "overlay" / "scripts" / "start_es_rgds.sh").read_text()


def python_block(marker):
    m = re.search(re.escape(marker) + r"\| python3 -c '\n(.*?)'", SCRIPT, re.S)
    if not m:
        raise AssertionError("parser not found: " + marker)
    return m.group(1)


def run_python(code, payload):
    r = subprocess.run(["python3", "-c", code], input=payload, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


TOUCH = python_block("get_inputs 2>/dev/null ")
HIDDEN = python_block("get_tree ")
FULLSCREEN = re.search(r"grep -q '([^']*fullscreen_mode[^']*)'", SCRIPT).group(1)


class PanelRestoreTest(unittest.TestCase):
    def test_touch_calibration_is_the_first_touchscreen(self):
        inputs = [
            {"identifier": "0:0:gpio_keys", "type": "keyboard",
             "libinput": {"calibration_matrix": [9, 9, 9, 9, 9, 9, 9, 9, 9]}},
            {"identifier": "1:1:Goodix", "type": "touch", "libinput": {}},
            {"identifier": "1:2:Goodix_Touch", "type": "touch",
             "libinput": {"calibration_matrix": [1, 0, 0, 0, 1, 0.5, 0, 0, 1]}},
        ]
        code, out, err = run_python(TOUCH, json.dumps(inputs))
        self.assertEqual(code, 0, err)
        self.assertEqual(out.strip(), "1:2:Goodix_Touch 1 0 0 0 1 0.5 0 0 1")
        code, out, err = run_python(TOUCH, "[]")
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "")

    def test_the_calibration_line_quotes_the_identifier(self):
        cal = "1:2:Goodix_Touch 1 0 0 0 1 0.5 0 0 1"
        r = subprocess.run(
            ["bash", "-c", 'TOUCH_CAL="$1"; printf \'input "%s" calibration_matrix %s\\n\' "${TOUCH_CAL%% *}" "${TOUCH_CAL#* }"',
             "bash", cal],
            capture_output=True, text=True)
        self.assertEqual(r.stdout, 'input "1:2:Goodix_Touch" calibration_matrix 1 0 0 0 1 0.5 0 0 1\n')
        # the script applies it only when a calibration was read
        self.assertIn('[ -n "$TOUCH_CAL" ]', SCRIPT)

    def test_a_hidden_es_window_is_found_on_either_list(self):
        def hidden(tree):
            code, _, err = run_python(HIDDEN, json.dumps(tree))
            self.assertIn(code, (0, 1), err)
            return code == 0
        self.assertTrue(hidden({"nodes": [{"app_id": "emulationstation", "visible": False}]}))
        self.assertTrue(hidden({"nodes": [], "floating_nodes": [
            {"app_id": "emulationstation", "visible": False}]}))
        self.assertFalse(hidden({"nodes": [{"app_id": "emulationstation", "visible": True}]}))
        self.assertFalse(hidden({"nodes": [{"app_id": "retroarch", "visible": False}]}))
        self.assertFalse(hidden({"nodes": []}))

    def test_fullscreen_es_matches_sways_key_order_and_a_workspace_does_not(self):
        """sway writes fullscreen_mode, then later app_id, on the window. A workspace's own
        fullscreen_mode is 1 and its children sit earlier in the object, so they must not count."""
        def matches(tree):
            flat = json.dumps(tree, separators=(",", ":"))
            r = subprocess.run(["grep", "-q", FULLSCREEN], input=flat, capture_output=True, text=True)
            return r.returncode == 0
        window = {"id": 1, "nodes": [], "fullscreen_mode": 1, "app_id": "emulationstation", "visible": True}
        self.assertTrue(matches(window))
        self.assertFalse(matches({**window, "fullscreen_mode": 0}))
        workspace = {
            "type": "workspace",
            "nodes": [{"fullscreen_mode": 0, "app_id": "emulationstation", "visible": True}],
            "fullscreen_mode": 1,
        }
        self.assertFalse(matches(workspace))
        other = {"nodes": [
            {"fullscreen_mode": 1, "app_id": "retroarch"},
            {"fullscreen_mode": 0, "app_id": "emulationstation"},
        ]}
        self.assertFalse(matches(other))

    def test_a_new_es_window_puts_the_panels_back_side_by_side(self):
        self.assertIn('output DSI-2 pos 0 0, output DSI-1 pos 640 0, $OUTPUTS', SCRIPT)
        self.assertIn("DUAL_THEMES=", SCRIPT)
        self.assertIn('OUTPUTS=\'output DSI-1 power off\'', SCRIPT)
