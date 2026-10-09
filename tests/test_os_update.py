"""The base ROCKNIX from the menu (INTERFACES.md A): rocknixds-update os-* around ROCKNIX's own rocknix-update, and
the autostart hook that puts back what a ROCKNIX update undoes on the first boot on it."""
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

SCRIPT = ROOT / "dsflip" / "device" / "rocknixds-update"
HOOK = ROOT / "dsflip" / "device" / "autostart-rocknixds-os"


class OsUpdateTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cfg = self.root / "system.cfg"
        self.cfg.write_text("")
        self.state = self.root / "state"
        self.state.mkdir()
        (self.state / "rocknix-verified").write_text("# a comment\n20261001\n")
        self.osrel = self.root / "os-release"
        self.os("20261001")
        self.upd = self.root / "update"
        self.upd.mkdir()
        self.autostart = self.root / "autostart"
        self.autostart.mkdir()
        hook = self.autostart / "rocknixds-os"
        hook.write_text("#!/bin/sh\n")
        hook.chmod(0o755)
        self.log = self.root / "stub.log"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self._stub("rocknix-update", """#!/bin/sh
# as ROCKNIX's: the branch from updates.branch; the server answers auto with nothing, and has the monthly releases
# on stable and the nightlies on nightly only
B=$(grep '^updates\\.branch=' "$ROCKNIXDS_CFG" 2>/dev/null | tail -n1 | cut -d= -f2)
printf '%s\\n' "rocknix-update [$B] $*" >> "$ROCKNIXDS_STUB_LOG"
case "$1" in
  releases)
    [ "$STUB_OFFLINE" = 1 ] && { echo "Network connection unavailable."; exit 1; }
    echo "Checking available releases..."
    case "$B" in
      stable) printf '20261001\\n20260901\\n' ;;
      nightly) printf '["ROCKNIX-RK3566.aarch64-20260915.tar", "20261101", "size 123456789"]\\n' ;;
      *) echo "No releases found." ;;
    esac ;;
  2026*)
    case "$B:$1" in stable:20261001|stable:20260901|nightly:20261101|nightly:20260915) ;;
      *) echo "Specified version '$1' not found."; echo "No update available or invalid update URL."; exit 1 ;; esac
    [ "$STUB_FAIL" = 1 ] && { echo "Downloading update file..."; echo "#####  50%"; echo "Failed to download update file."; exit 1; }
    : > "$ROCKNIXDS_UPDATE_DIR/ROCKNIX-RK3566.aarch64-$1.tar"; echo "Checksum verified successfully. Reboot to apply the update." ;;
  *) exit 1 ;;
esac
""")

    def tearDown(self):
        self.tmp.cleanup()

    def _stub(self, name, text):
        p = self.bin / name
        p.write_text(text)
        p.chmod(p.stat().st_mode | stat.S_IEXEC)

    def os(self, v):
        self.osrel.write_text('NAME="ROCKNIX"\nOS_VERSION="%s"\nVERSION_ID="x"\n' % v)

    def env(self, **extra):
        env = os.environ.copy()
        env.update({
            "PATH": str(self.bin) + os.pathsep + env.get("PATH", ""),
            "ROCKNIXDS_CFG": str(self.cfg), "ROCKNIXDS_STATE": str(self.state),
            "ROCKNIXDS_MODEL_FILE": str(self.root / "model"), "ROCKNIXDS_MODES_GLOB": str(self.root / "modes"),
            "ROCKNIXDS_OS_RELEASE": str(self.osrel), "ROCKNIXDS_UPDATE_DIR": str(self.upd),
            "ROCKNIXDS_AUTOSTART": str(self.autostart), "ROCKNIXDS_STUB_LOG": str(self.log),
            "ROCKNIXDS_NOTIFY": "0",
        })
        env.update(extra)
        return env

    def run_update(self, *args, **extra):
        return subprocess.run(["sh", str(SCRIPT), *args], capture_output=True, text=True, env=self.env(**extra))

    def last(self, r):
        return [l for l in r.stdout.splitlines() if l.strip()][-1]

    def test_current_verified_and_list(self):
        r = self.run_update("os-current")
        self.assertEqual((r.returncode, self.last(r)), (0, "20261001"))
        r = self.run_update("os-verified")
        self.assertEqual((r.returncode, self.last(r)), (0, "20261001"))
        r = self.run_update("os-list")
        self.assertEqual((r.returncode, self.last(r)), (0, "20261101 20261001 20260915 20260901"))
        r = self.run_update("os-list", STUB_OFFLINE="1")
        self.assertEqual(r.returncode, 1)
        self.assertIn("CHECK THE NETWORK", self.last(r))

    def test_check_follows_the_setting(self):
        r = self.run_update("os-check")
        self.assertEqual((r.returncode, self.last(r)), (0, "ROCKNIX IS ON 20261001 (VERIFIED)"))
        self.assertFalse(self.log.exists())                          # the verified one needs no network
        self.cfg.write_text("rocknixds.os=latest\n")
        r = self.run_update("os-check")
        self.assertEqual(self.last(r), "UPDATE 20261101 ROCKNIX 20261101, the latest nightly (not verified for ROCKNIXDS)")
        self.cfg.write_text("rocknixds.os=20260915\n")
        r = self.run_update("os-check")
        self.assertEqual(self.last(r), "UPDATE 20260915 ROCKNIX 20260915 (not verified for ROCKNIXDS), older than the installed 20261001")
        self.os("20261101")
        self.cfg.write_text("rocknixds.os=verified\n")
        r = self.run_update("os-check")
        self.assertEqual(self.last(r), "UPDATE 20261001 ROCKNIX 20261001 (verified for ROCKNIXDS), older than the installed 20261101")
        self.cfg.write_text("rocknixds.os=latest\n")
        r = self.run_update("os-check")
        self.assertEqual(self.last(r), "ROCKNIX IS ON 20261101 (THE LATEST NIGHTLY)")
        r = self.run_update("os-check", STUB_OFFLINE="1")
        self.assertEqual(r.returncode, 1)
        self.assertIn("COULDN'T REACH", self.last(r))

    def test_install_stages_with_rocknix_update(self):
        (self.upd / "ROCKNIX-RK3566.aarch64-20260101.tar").write_text("an earlier choice")
        r = self.run_update("os-install", "latest")
        self.assertEqual((r.returncode, self.last(r)), (0, "READY 20261101"))
        self.assertIn("rocknix-update [nightly] 20261101", self.log.read_text())
        self.assertEqual(sorted(p.name for p in self.upd.iterdir()), ["ROCKNIX-RK3566.aarch64-20261101.tar"])
        self.assertEqual((self.state / "os-pending").read_text().split(), ["20261101", "20261001"])

        r = self.run_update("os-install", "20261001")
        self.assertEqual(r.returncode, 1)
        self.assertEqual(self.last(r), "ROCKNIX IS ALREADY ON 20261001")

        r = self.run_update("os-install", "20261101", STUB_FAIL="1")
        self.assertEqual(r.returncode, 1)
        self.assertEqual(self.last(r), "THE ROCKNIX UPDATE DIDN'T DOWNLOAD: FAILED TO DOWNLOAD UPDATE FILE.")
        self.assertFalse((self.state / "os-pending").exists())

        r = self.run_update("os-install", "bogus")
        self.assertEqual(r.returncode, 1)
        self.assertIn("UNKNOWN ROCKNIX VERSION", self.last(r))

    def test_branch_per_call_and_put_back(self):
        # ROCKNIX's default (auto) lists nothing on the update server: each call names its branch, and the player's
        # updates.branch is what it was afterwards (or absent, if it was)
        self.cfg.write_text("updates.branch=auto\nrocknixds.os=20260901\n")
        r = self.run_update("os-install")
        self.assertEqual((r.returncode, self.last(r)), (0, "READY 20260901"))
        self.assertIn("rocknix-update [stable] 20260901", self.log.read_text())
        self.assertEqual(self.cfg.read_text(), "updates.branch=auto\nrocknixds.os=20260901\n")
        self.cfg.write_text("rocknixds.os=20260915\n")
        r = self.run_update("os-install")
        self.assertEqual((r.returncode, self.last(r)), (0, "READY 20260915"))
        self.assertIn("rocknix-update [nightly] 20260915", self.log.read_text())
        self.assertEqual(self.cfg.read_text(), "rocknixds.os=20260915\n")

    def test_install_needs_the_reapply_hook(self):
        (self.autostart / "rocknixds-os").unlink()
        self.cfg.write_text("rocknixds.os=20261101\n")
        r = self.run_update("os-install")
        self.assertEqual(r.returncode, 1)
        self.assertIn("UPDATE ROCKNIXDS FIRST", self.last(r))
        self.assertFalse(self.log.exists())

    def run_hook(self):
        r = subprocess.run(["sh", str(HOOK)], capture_output=True, text=True, env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_hook_reapplies_once_per_new_rocknix(self):
        apply = self.state / "apply-60hz-dtb.sh"
        apply.write_text('echo run >> "$ROCKNIXDS_STUB_LOG"; echo "60 Hz panel timing applied; reboot to use it"\n')
        self.run_hook()                                            # no record yet: recorded, nothing done
        self.assertEqual((self.state / "os-version").read_text().strip(), "20261001")
        self.assertFalse(self.log.exists())
        self.run_hook()                                            # same ROCKNIX: nothing
        self.assertFalse(self.log.exists())
        self.os("20261101")
        (self.state / "os-pending").write_text("20261101 20261001\n")
        self.run_hook()                                            # new ROCKNIX, no 60 Hz chosen
        self.assertFalse(self.log.exists())
        self.assertFalse((self.state / "os-pending").exists())
        self.assertIn("20261001 -> 20261101", (self.state / "os-reapply.log").read_text())
        (self.state / ".with-60hz").touch()
        self.os("20261201")
        self.run_hook()
        self.run_hook()
        self.assertEqual(self.log.read_text().count("run"), 1)
        self.assertEqual((self.state / "os-version").read_text().strip(), "20261201")


class RepoFilesTest(unittest.TestCase):
    def test_the_verified_rocknix_and_the_themes_offered(self):
        import re
        lines = [l for l in (ROOT / "ROCKNIX").read_text().splitlines() if l and not l.startswith("#")]
        self.assertRegex(lines[0], r"^20[0-9]{6}$")
        out = subprocess.run(["sh", "-c", re.search(r"^themes_allow\(\).*$", (ROOT / "install.sh").read_text(), re.M)
                              .group(0) + "\nthemes_allow"], capture_output=True, text=True, check=True).stdout.split()
        self.assertIn("es-theme-art-book-next", out)          # ROCKNIX's own default theme (#48)
        self.assertIn("rocknixds-pixel-light", out)


if __name__ == "__main__":
    unittest.main()
