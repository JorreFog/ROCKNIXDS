"""What uninstall puts back. A file that was not there before the first install stays gone, an absent marker
is not copied onto the device as a file, rocknixds.* settings go while everything else stays, and a CPU
governor that cannot be written does not abort the undo."""
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT


INSTALL = ROOT / "install.sh"


class BackupOnceTest(unittest.TestCase):
    def test_a_missing_file_stays_missing_and_a_real_one_is_kept_once(self):
        text = INSTALL.read_text()
        start = text.index("backup_once() {")
        end = text.index("\nset_cfg() {")
        fn = text[start:end]
        self.assertIn("rocknixds-absent", fn)
        root = Path(tempfile.mkdtemp())
        backup = root / "backup"
        live_missing = root / "live" / "hook"
        live_cfg = root / "live" / "cfg"
        script = root / "backup.sh"
        script.write_text(
            "set -e\nBACKUP=%s\n%s\n"
            "rm -f \"$LIVE_MISSING\"\n"
            "backup_once \"$LIVE_MISSING\"\n"
            "mkdir -p \"$(dirname \"$LIVE_MISSING\")\"\n"
            "printf 'installed-later\\n' > \"$LIVE_MISSING\"\n"
            "backup_once \"$LIVE_MISSING\"\n"
            "mkdir -p \"$(dirname \"$LIVE_CFG\")\"\n"
            "printf 'original\\n' > \"$LIVE_CFG\"\n"
            "backup_once \"$LIVE_CFG\"\n"
            "printf 'changed-later\\n' > \"$LIVE_CFG\"\n"
            "backup_once \"$LIVE_CFG\"\n"
            % (sh_quote(backup), fn))
        r = subprocess.run(["sh", str(script)], capture_output=True, text=True, env={
            **os.environ,
            "LIVE_MISSING": str(live_missing),
            "LIVE_CFG": str(live_cfg),
        })
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        marker = Path(str(backup) + str(live_missing) + ".rocknixds-absent")
        copied = Path(str(backup) + str(live_missing))
        self.assertTrue(marker.is_file())
        self.assertEqual(marker.stat().st_size, 0)
        self.assertFalse(copied.exists())
        self.assertEqual(live_missing.read_text(), "installed-later\n")
        self.assertEqual(Path(str(backup) + str(live_cfg)).read_text(), "original\n")
        self.assertEqual(live_cfg.read_text(), "changed-later\n")


class UninstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.storage = self.root / "storage"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "sys.log"
        stub = self.bin / "systemctl"
        stub.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$STUB_LOG\"\n"
                        "[ \"$1\" = is-active ] && exit 3\nexit 0\n")
        stub.chmod(stub.stat().st_mode | stat.S_IEXEC)
        (self.root / "os-release").write_text('NAME="ROCKNIX"\n')
        (self.root / "model").write_bytes(b"Anbernic RG DS\0")
        (self.root / "modes").write_text("640x480\n")
        # a directory: the redirect fails, which used to abort uninstall under set -e
        (self.root / "governor").mkdir()
        src = INSTALL.read_text()
        root_check = '[ "$(id -u)" = 0 ] ||'
        self.assertIn(root_check, src)
        src = src.replace(root_check, "true ||", 1)
        src = src.replace("/etc/os-release", str(self.root / "os-release"))
        src = src.replace("/proc/device-tree/model", str(self.root / "model"))
        src = src.replace("/sys/class/drm/card*-DSI-*/modes", str(self.root / "modes"))
        gov = "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor"
        self.assertIn(gov, src)
        src = src.replace(gov, str(self.root / "governor"))
        src = src.replace("/storage/", str(self.storage) + "/")
        needle = 'p=${f#.}\n            mkdir -p "$(dirname "$p")"; cp -a "$BACKUP$p" "$p"'
        self.assertIn(needle, src)
        src = src.replace(
            needle,
            'rel=${f#.}\n            mkdir -p "$(dirname "$RGDS_SANDBOX$rel")"; '
            'cp -a "$BACKUP$rel" "$RGDS_SANDBOX$rel"',
            1)
        src = src.replace("sleep 2", ":", 1)
        self.script = self.root / "install.sh"
        self.script.write_text(src)
        self._dirs()

    def tearDown(self):
        self.tmp.cleanup()

    def _dirs(self):
        scripts = self.storage / ".config" / "emulationstation" / "scripts"
        for name in ("theme-changed", "game-end", "game-start", "start"):
            (scripts / name).mkdir(parents=True, exist_ok=True)
        (self.storage / ".config" / "system.d" / "timers.target.wants").mkdir(parents=True)
        cfg = self.storage / ".config" / "system" / "configs"
        cfg.mkdir(parents=True)
        es = self.storage / ".config" / "emulationstation"
        es.mkdir(parents=True, exist_ok=True)

    def _backup(self):
        return self.storage / "rgds-rocknix-backup"

    def _seed_common(self):
        b = self._backup() / "storage"
        (b / ".config" / "system" / "configs").mkdir(parents=True)
        (b / ".config" / "emulationstation").mkdir(parents=True)
        (b / ".config" / "autostart").mkdir(parents=True)
        (b / ".config" / "sway").mkdir(parents=True)
        unit = self.storage / ".config" / "system.d"
        (unit / "rocknixds-update-check.service").write_text("unit\n")
        (unit / "rocknixds-update-check.timer").write_text("timer\n")
        (unit / "timers.target.wants" / "rocknixds-update-check.timer").write_text("link\n")
        (self.storage / ".config" / "rocknixds").mkdir()
        (self.storage / ".config" / "rocknixds" / "token").write_text("sekret\n")
        # the marker must not become a file on the device; the sway config is a real original
        (b / ".config" / "autostart" / "dii-ess-aye.rocknixds-absent").write_bytes(b"")
        (b / ".config" / "sway" / "config").write_text("original sway\n")

    def _run(self, *args):
        env = {k: v for k, v in os.environ.items() if not k.startswith("RGDS_")}
        env["PATH"] = str(self.bin) + os.pathsep + env.get("PATH", "")
        env["STUB_LOG"] = str(self.log)
        env["RGDS_SANDBOX"] = str(self.root)
        return subprocess.run(["sh", str(self.script), *args], capture_output=True, text=True, env=env)

    def _finished(self, r):
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Done.", r.stdout)
        self.assertFalse(self._backup().exists())
        self.assertTrue(any(p.name.startswith("rgds-rocknix-backup.undone-")
                            for p in self.storage.iterdir()))
        self.assertIn("daemon-reload", self.log.read_text())
        self.assertFalse((self.storage / ".config" / "rocknixds").exists())
        self.assertFalse((self.storage / ".config" / "system.d" / "rocknixds-update-check.timer").exists())

    def test_settings_made_since_install_survive_and_rocknixds_ones_go(self):
        self._seed_common()
        b = self._backup() / "storage"
        (b / ".config" / "system" / "configs" / "system.cfg").write_text("nds.hires_3d=1\n")
        (b / ".config" / "emulationstation" / "es_settings.cfg").write_text(
            "<config>\n"
            "\t<string name=\"ThemeSet\" value=\"es-theme-carbon\" />\n"
            "</config>\n")
        syscfg = self.storage / ".config" / "system" / "configs" / "system.cfg"
        syscfg.write_text(
            "nds.hires_3d=0\n"
            "rocknixds.channel=beta\n"
            "rocknixds.autocheck=0\n"
            "global.retroachievements.username=alice\n")
        es = self.storage / ".config" / "emulationstation" / "es_settings.cfg"
        es.write_text(
            "<config>\n"
            "\t<string name=\"ThemeSet\" value=\"rocknixds-pixel-dark\" />\n"
            "\t<string name=\"FullScreenMenu\" value=\"yes\" />\n"
            "\t<string name=\"AudioDevice\" value=\"speakers\" />\n"
            "\t<bool name=\"DrawFramerate\" value=\"true\" />\n"
            "</config>\n")
        sway = self.storage / ".config" / "sway" / "config"
        sway.parent.mkdir(parents=True)
        sway.write_text("user sway\n")
        r = self._run("--uninstall")
        self._finished(r)
        self.assertEqual(syscfg.read_text(), "nds.hires_3d=1\nglobal.retroachievements.username=alice\n")
        text = es.read_text()
        self.assertIn('name="ThemeSet" value="es-theme-carbon"', text)
        self.assertNotIn("FullScreenMenu", text)
        self.assertIn("AudioDevice", text)
        self.assertIn("DrawFramerate", text)
        # plain uninstall restores sway from the backup; it does not invent the absent hook
        self.assertEqual(sway.read_text(), "original sway\n")
        self.assertFalse((self.storage / ".config" / "autostart" / "dii-ess-aye").exists())
        self.assertFalse((self.storage / ".config" / "autostart" / "dii-ess-aye.rocknixds-absent").exists())

    def test_restore_files_puts_originals_back_but_not_absent_markers(self):
        self._seed_common()
        b = self._backup() / "storage"
        (b / ".config" / "system" / "configs" / "system.cfg").write_text(
            "nds.hires_3d=1\nplayer.keep=yes\nrocknixds.channel=should-not-survive\n")
        (b / ".config" / "emulationstation" / "es_settings.cfg").write_text(
            "<config>\n\t<string name=\"ThemeSet\" value=\"es-theme-carbon\" />\n</config>\n")
        (b / ".config" / "kept.cfg").write_text("from-backup\n")
        syscfg = self.storage / ".config" / "system" / "configs" / "system.cfg"
        syscfg.write_text("nds.hires_3d=0\nplayer.keep=no\nrocknixds.autocheck=1\nadded.since=1\n")
        es = self.storage / ".config" / "emulationstation" / "es_settings.cfg"
        es.write_text(
            "<config>\n"
            "\t<string name=\"AudioDevice\" value=\"speakers\" />\n"
            "</config>\n")
        kept = self.storage / ".config" / "kept.cfg"
        kept.write_text("user-edit\n")
        r = self._run("--uninstall", "--restore-files")
        self._finished(r)
        self.assertEqual(syscfg.read_text(), "nds.hires_3d=1\nplayer.keep=yes\n")
        self.assertNotIn("rocknixds.", syscfg.read_text())
        self.assertNotIn("added.since", syscfg.read_text())
        self.assertIn("es-theme-carbon", es.read_text())
        self.assertNotIn("AudioDevice", es.read_text())
        self.assertEqual(kept.read_text(), "from-backup\n")
        self.assertEqual((self.storage / ".config" / "sway" / "config").read_text(), "original sway\n")
        self.assertFalse((self.storage / ".config" / "autostart" / "dii-ess-aye").exists())
        self.assertFalse((self.storage / ".config" / "autostart" / "dii-ess-aye.rocknixds-absent").exists())


def sh_quote(path):
    return "'" + str(path).replace("'", "'\\''") + "'"


if __name__ == "__main__":
    unittest.main()
