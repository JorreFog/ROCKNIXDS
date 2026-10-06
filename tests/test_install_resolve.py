"""Which tree install.sh installs. Releases come in pairs (vX.Y for the RG DS, vX.Y-plus for the RG DS Plus), each
handheld has its beta branch, and whichever installer a handheld starts from (the README's command fetches main's,
a 1.5 beta's updater fetches main's or beta's) it must end up running its own release's or branch head's
installer, with the exact ref to record. Runs install.sh up to that hand-over, with the device files and the
network stubbed; the handed-to installer only reports what it was given."""
import json
import os
import re
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

SHA_BETA = "b" * 40
SHA_PLUS = "c" * 40
RELEASES = [("v1.6-beta.1", True), ("v1.5-plus", False), ("v1.5", False), ("v1.5-plus-beta.5", True),
            ("v1.4", False), ("v1.4-plus-alpha.2", True)]


class InstallResolveTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "net.log"
        (self.root / "os-release").write_text('NAME="ROCKNIX"\nOS_VERSION="20260901"\n')
        self.model = self.root / "model"
        self.modes = self.root / "modes"
        self.modes.write_text("640x480\n")
        self.releases(RELEASES)
        # the installer under test, pointed at the stub device files
        src = (ROOT / "install.sh").read_text()
        src = src.replace("/etc/os-release", str(self.root / "os-release"))
        src = src.replace("/proc/device-tree/model", str(self.model))
        src = src.replace("/sys/class/drm/card*-DSI-*/modes", str(self.modes))
        # CI doesn't run as root, and nothing up to the hand-over needs it
        root_check = '[ "$(id -u)" = 0 ] ||'
        self.assertIn(root_check, src)
        src = src.replace(root_check, "true ||", 1)
        # its own (empty) backup folder: an uninstall in a test must never find a real install's
        src = src.replace("/storage/rgds-rocknix-backup", str(self.root / "backup"))
        # never past the hand-over in a test: the install itself would run on this machine
        marker = "# ---- fetch this repo"
        self.assertIn(marker, src)
        src = src.replace(marker, 'echo "FELL THROUGH"; exit 99\n' + marker, 1)
        self.script = self.root / "install.sh"
        self.script.write_text(src)
        child = ('#!/bin/sh\necho "CHILD branch=$RGDS_BRANCH ref=$RGDS_REF args=$*"\n')
        self.stub("curl", """#!/bin/sh
echo "$*" >> "$STUB_LOG"
[ -e "$STUB_OFFLINE" ] && exit 7
out=
while [ $# -gt 0 ]; do
  case "$1" in -o) out=$2; shift ;; *) url=$1 ;; esac
  shift
done
case "$url" in
  *releases\\?per_page*) cat "$STUB_RELEASES" ;;
  *commits/plus-beta) echo '  "sha": "%s",' ;;
  *commits/beta) echo '  "sha": "%s",' ;;
  *commits/*) exit 22 ;;
  https://api.github.com/) exit 0 ;;
  *raw.githubusercontent.com/*/install.sh) printf '%%s' '%s' > "$out" ;;
  *) exit 22 ;;
esac
""" % (SHA_PLUS, SHA_BETA, child.replace("'", "'\\''")))
        self.stub("systemctl", "#!/bin/sh\nexit 3\n")

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, text):
        p = self.bin / name
        p.write_text(text)
        p.chmod(p.stat().st_mode | stat.S_IEXEC)

    def releases(self, rel):
        (self.root / "releases.json").write_text(json.dumps(
            [{"tag_name": t, "prerelease": pre, "draft": False} for t, pre in rel]))

    def device(self, plus):
        self.model.write_bytes(("Anbernic RG DS Plus" if plus else "Anbernic RG DS").encode() + b"\0")

    def run_installer(self, branch=None, *args, env_extra=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith("RGDS_")}
        env["PATH"] = str(self.bin) + os.pathsep + env["PATH"]
        env["STUB_LOG"] = str(self.log)
        env["STUB_RELEASES"] = str(self.root / "releases.json")
        env["STUB_OFFLINE"] = str(self.root / "offline")
        if branch:
            env["RGDS_BRANCH"] = branch
        env.update(env_extra or {})
        return subprocess.run(["sh", str(self.script), *args], capture_output=True, text=True, env=env)

    def handed(self, r):
        m = re.search(r"CHILD branch=(\S*) ref=(\S*) args=(.*)", r.stdout)
        self.assertIsNotNone(m, r.stdout + r.stderr)
        return m.group(1), m.group(2), m.group(3)

    def fetched(self):
        if not self.log.exists():
            return []
        return [l for l in self.log.read_text().splitlines() if "raw.githubusercontent.com" in l]

    def test_stable_is_each_handhelds_newest_release(self):
        self.device(False)
        r = self.run_installer()                                   # the README's command
        self.assertEqual(self.handed(r), ("main", "v1.5", ""))
        self.assertIn("/v1.5/install.sh", self.fetched()[-1])
        self.device(True)
        r = self.run_installer("main", "--no-canvas")
        self.assertEqual(self.handed(r), ("main", "v1.5-plus", "--no-canvas"))
        self.assertIn("/v1.5-plus/install.sh", self.fetched()[-1])

    def test_a_bank_release_is_never_taken_for_a_rocknixds_one(self):
        # ROCKNIXDS Bank & Trade releases (bank-v*) live in the same repository, newer than the last ROCKNIXDS release
        self.releases([("bank-v0.2.0", False), ("bank-v0.1.0", True), ("v1.5-plus", False), ("v1.5", False)])
        self.device(False)
        self.assertEqual(self.handed(self.run_installer())[1], "v1.5")
        self.device(True)
        self.assertEqual(self.handed(self.run_installer())[1], "v1.5-plus")

    def test_a_wide_panel_is_a_plus_too(self):
        self.model.write_bytes(b"Anbernic RG DS\0")
        self.modes.write_text("1024x768\n")
        self.assertEqual(self.handed(self.run_installer())[1], "v1.5-plus")

    def test_beta_is_each_handhelds_beta_branch_at_its_current_commit(self):
        self.device(False)
        self.assertEqual(self.handed(self.run_installer("beta")), ("beta", SHA_BETA, ""))
        self.device(True)
        self.assertEqual(self.handed(self.run_installer("beta")), ("plus-beta", SHA_PLUS, ""))
        self.assertEqual(self.handed(self.run_installer("plus-beta")), ("plus-beta", SHA_PLUS, ""))
        self.device(False)
        self.assertEqual(self.handed(self.run_installer("plus-beta")), ("beta", SHA_BETA, ""))

    def test_a_tag_is_that_release_and_only_on_its_handheld(self):
        self.device(True)
        self.assertEqual(self.handed(self.run_installer("v1.5-plus")), ("v1.5-plus", "v1.5-plus", ""))
        r = self.run_installer("v1.5")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("RG DS's release", r.stdout)
        self.device(False)
        r = self.run_installer("v1.5-plus")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("RG DS Plus's release", r.stdout)

    def test_no_plus_release_yet_says_to_use_the_beta(self):
        self.releases([("v1.4", False), ("v1.5-plus-beta.5", True)])
        self.device(True)
        r = self.run_installer()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("no ROCKNIXDS release for the RG DS Plus yet", r.stdout)
        self.assertEqual(self.fetched(), [])

    def test_offline_installs_nothing(self):
        (self.root / "offline").touch()
        self.device(False)
        for branch in ("main", "beta"):
            r = self.run_installer(branch)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("couldn't reach GitHub", r.stdout)
            self.assertNotIn("CHILD", r.stdout)

    def test_a_given_ref_and_uninstall_are_not_resolved_again(self):
        self.device(True)
        r = self.run_installer("main", "--uninstall")
        self.assertNotIn("CHILD", r.stdout)
        self.assertIn("nothing to undo", r.stdout)               # no backup in this sandbox: it stops there
        self.assertEqual(self.fetched(), [])
        r = self.run_installer("main", env_extra={"RGDS_REF": "v1.5-plus"})    # the handed-to installer
        self.assertIn("FELL THROUGH", r.stdout)                  # goes on to install that ref itself
        self.assertNotIn("CHILD", r.stdout)
        self.assertEqual(self.fetched(), [])


if __name__ == "__main__":
    unittest.main()
