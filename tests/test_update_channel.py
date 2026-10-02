"""Which tree an update installs. The menu stores Beta as "beta" on both handhelds, and a
Plus that followed that literally installed the RG DS beta. A Plus (by name, or by a panel
wider than 640) on the beta channel installs plus-beta; stable stays on the releases."""
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT


SCRIPT = ROOT / "dsflip" / "device" / "rocknixds-update"
SHA_PLUS = "abcdef1234567890abcdef1234567890abcdef12"
SHA_BETA = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"


class UpdateChannelTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cfg = self.root / "system.cfg"
        self.state = self.root / "state"
        self.state.mkdir()
        self.model = self.root / "model"
        self.modes = self.root / "modes"
        self.modes.write_text("640x480\n")
        self.log = self.root / "stub.log"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self._stub("curl", """#!/bin/sh
printf '%s\\n' "$*" >> "$ROCKNIXDS_STUB_LOG"
case " $* " in
  *releases/latest*) printf '%s\\n' '  "tag_name": "v1.5.0"' ;;
  *commits/plus-beta*) printf '%s\\n' '  "sha": "__PLUS__"' ;;
  *commits/beta*) printf '%s\\n' '  "sha": "__BETA__"' ;;
  *localhost:1234/notify*)
      [ "$ROCKNIXDS_NOTIFY_FAIL" = 1 ] && exit 1
      exit 0 ;;
  *) exit 1 ;;
esac
""".replace("__PLUS__", SHA_PLUS).replace("__BETA__", SHA_BETA))
        self._stub("systemctl", """#!/bin/sh
printf '%s\\n' "systemctl $*" >> "$ROCKNIXDS_STUB_LOG"
[ "$ROCKNIXDS_UPDATE_ACTIVE" = 1 ] && exit 0
exit 3
""")
        self._stub("systemd-run", """#!/bin/sh
printf '%s\\n' "systemd-run $*" >> "$ROCKNIXDS_STUB_LOG"
exit 0
""")

    def tearDown(self):
        self.tmp.cleanup()

    def _stub(self, name, text):
        path = self.bin / name
        path.write_text(text)
        path.chmod(path.stat().st_mode | stat.S_IEXEC)

    def _cfg(self, text):
        self.cfg.write_text(text)

    def _model(self, text):
        self.model.write_bytes(text.encode() + b"\x00\x00")

    def invoke(self, *args, active=False, notify_fail=False):
        env = os.environ.copy()
        env["PATH"] = str(self.bin) + os.pathsep + env.get("PATH", "")
        env["ROCKNIXDS_CFG"] = str(self.cfg)
        env["ROCKNIXDS_STATE"] = str(self.state)
        env["ROCKNIXDS_MODEL_FILE"] = str(self.model)
        env["ROCKNIXDS_MODES_GLOB"] = str(self.modes)
        env["ROCKNIXDS_STUB_LOG"] = str(self.log)
        env["ROCKNIXDS_UPDATE_ACTIVE"] = "1" if active else "0"
        env["ROCKNIXDS_NOTIFY_FAIL"] = "1" if notify_fail else "0"
        return subprocess.run([str(SCRIPT), *args], capture_output=True, text=True, env=env)

    def test_rgds_beta_stays_on_the_beta_branch(self):
        self._model("Anbernic RG DS")
        self._cfg("rocknixds.channel=stable\nrocknixds.channel=beta\n")
        r = self.invoke("check")
        self.assertEqual(r.returncode, 0)
        self.assertIn("UPDATE ROCKNIXDS beta %s" % SHA_BETA[:7], r.stdout)
        self.assertNotIn("plus-beta", r.stdout)
        self.assertEqual(self.cfg.read_text(), "rocknixds.channel=stable\nrocknixds.channel=beta\n")

    def test_plus_beta_installs_plus_beta_from_the_name_or_a_wide_panel(self):
        self._model("Anbernic RG DS Plus")
        self._cfg("rocknixds.channel=beta\n")
        r = self.invoke("check")
        self.assertIn("UPDATE ROCKNIXDS plus-beta %s" % SHA_PLUS[:7], r.stdout)
        self.assertNotIn(" beta ", r.stdout)

        self._model("Anbernic RG DS")
        self.modes.write_text("1024x768p60\n")
        r = self.invoke("check")
        self.assertIn("plus-beta", r.stdout)

        self.modes.write_text("641x480\n")
        r = self.invoke("check")
        self.assertIn("plus-beta", r.stdout)

        self.modes.write_text("640x480\n")
        r = self.invoke("check")
        self.assertIn("ROCKNIXDS beta ", r.stdout)
        self.assertNotIn("plus-beta", r.stdout)

    def test_an_old_plus_channel_value_is_rewritten_to_beta(self):
        self._model("Anbernic RG DS Plus")
        self._cfg("rocknixds.channel=plus\n")
        r = self.invoke("check")
        self.assertEqual(r.returncode, 0)
        self.assertIn("plus-beta", r.stdout)
        self.assertEqual(self.cfg.read_text().strip(), "rocknixds.channel=beta")

        self._model("Anbernic RG DS")
        self.modes.write_text("640x480\n")
        self._cfg("rocknixds.channel=plus\n")
        r = self.invoke("check")
        self.assertIn("plus-beta", r.stdout)
        self.assertEqual(self.cfg.read_text().strip(), "rocknixds.channel=plus")

    def test_stable_is_the_latest_release_even_on_a_plus(self):
        self._model("Anbernic RG DS Plus")
        self._cfg("rocknixds.channel=stable\n")
        r = self.invoke("check")
        self.assertEqual(r.stdout.strip(), "UPDATE ROCKNIXDS 1.5.0")
        (self.state / "installed-id").write_text("v1.5.0")
        r = self.invoke("check")
        self.assertEqual(r.stdout.strip(), "ROCKNIXDS IS UP TO DATE (ROCKNIXDS 1.5.0)")
        self.assertEqual(r.returncode, 0)

    def test_notify_is_once_per_update_and_silent_when_autocheck_is_off(self):
        self._model("Anbernic RG DS")
        self._cfg("rocknixds.channel=beta\nrocknixds.autocheck=0\n")
        r = self.invoke("notify")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stdout, "")
        self.assertFalse((self.state / "notified-id").exists())

        self._cfg("rocknixds.channel=beta\n")
        r = self.invoke("notify")
        self.assertIn("UPDATE ROCKNIXDS beta", r.stdout)
        self.assertEqual((self.state / "notified-id").read_text().strip(), SHA_BETA)
        r = self.invoke("notify")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stdout, "")

        (self.state / "notified-id").unlink()
        r = self.invoke("notify", notify_fail=True)
        self.assertIn("UPDATE", r.stdout)
        self.assertFalse((self.state / "notified-id").exists())

    def test_install_uses_the_channel_branch_and_refuses_a_second_one(self):
        self._model("Anbernic RG DS Plus")
        self._cfg("rocknixds.channel=beta\n")
        r = self.invoke("install", active=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("AN UPDATE IS ALREADY RUNNING", r.stdout)
        self.assertNotIn("systemd-run", self.log.read_text())

        self.log.write_text("")
        r = self.invoke("install")
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stdout.strip(), "STARTED")
        log = self.log.read_text()
        self.assertIn("RGDS_BRANCH=plus-beta", log)
        self.assertIn("/plus-beta/install.sh", log)

        self._model("Anbernic RG DS")
        self.modes.write_text("640x480\n")
        self.log.write_text("")
        r = self.invoke("install")
        log = self.log.read_text()
        self.assertIn("RGDS_BRANCH=beta", log)
        self.assertIn("/beta/install.sh", log)
        self.assertNotIn("plus-beta", log)

    def test_github_being_unreachable_is_reported(self):
        self._model("Anbernic RG DS")
        self._cfg("rocknixds.channel=beta\n")
        (self.bin / "curl").write_text("#!/bin/sh\nexit 1\n")
        (self.bin / "curl").chmod((self.bin / "curl").stat().st_mode | stat.S_IEXEC)
        r = self.invoke("check")
        self.assertEqual(r.returncode, 1)
        self.assertIn("COULDN'T REACH GITHUB", r.stdout)

    def test_usage(self):
        self._model("Anbernic RG DS")
        self._cfg("")
        r = self.invoke()
        self.assertEqual(r.returncode, 2)
        self.assertIn("usage:", r.stderr)


if __name__ == "__main__":
    unittest.main()
