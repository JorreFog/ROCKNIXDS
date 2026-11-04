"""Guards around an install: a package cannot aim the Store outside its folder, a running app is left alone, a second
job waits, and what is written into the menu cannot grow a second line or a second command. Version order is numeric,
so 1.2.10 is newer than 1.2.9 and 1.10 is newer than 1.9."""
import fcntl
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

import importlib.machinery
import importlib.util

from loadmod import ROOT

sys.path.insert(0, str(ROOT / "store" / "tests"))
from sandbox import CLI, Sandbox, tar_package, zip_package  # noqa: E402

_loader = importlib.machinery.SourceFileLoader("rocknixds_store_guards", str(CLI))
store = importlib.util.module_from_spec(importlib.util.spec_from_loader("rocknixds_store_guards", _loader))
_loader.exec_module(store)


class StorePureTest(unittest.TestCase):
    def test_versions_compare_by_number_not_as_text(self):
        self.assertTrue(store.newer("1.2.10", "1.2.9"))
        self.assertFalse(store.newer("1.2.9", "1.2.10"))
        self.assertTrue(store.newer("1.10", "1.9"))
        self.assertFalse(store.newer("1.9", "1.10"))
        # a trailing .0 is newer than the shorter number: 1.2.0 is not the same release as 1.2
        self.assertTrue(store.newer("1.2.0", "1.2"))
        self.assertFalse(store.newer("1.2", "1.2.0"))
        self.assertFalse(store.newer("1.2", "1.2"))
        self.assertFalse(store.newer("", "1.0"))
        # leading zeros are the same number, so 1.02 does not sort after 1.2
        self.assertFalse(store.newer("1.02", "1.2"))
        self.assertFalse(store.newer("1.2", "1.02"))

    def test_notes_lose_markup_and_stay_short(self):
        md = ("<!-- hide\nthis -->\n# Title\n\n- **Bold** and `code`\n\n"
              "![pic](http://x/a.png)\n\nSee [the notes](http://x)\n")
        text = store.plain_notes(md)
        self.assertNotIn("hide", text)
        self.assertNotIn("http", text)
        self.assertNotIn("**", text)
        self.assertNotIn("`", text)
        self.assertNotIn("pic", text)
        self.assertIn("Title", text)
        self.assertIn("- Bold and code", text)
        self.assertIn("the notes", text)
        self.assertEqual(store.plain_notes(None), "")
        short = store.plain_notes("word " * 100, limit=30)
        self.assertTrue(short.endswith("..."))
        self.assertLessEqual(len(short), 30)

    def test_a_list_field_cannot_break_the_line(self):
        self.assertEqual(store.tsv("a\tb\r\nc\\"), "a b\\nc\\\\")
        self.assertNotIn("\t", store.tsv("a\tb"))
        self.assertNotIn("\n", store.tsv("a\nb"))


class StoreGuardsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sb = Sandbox(self.tmp.name)
        self.sb.publish_hello("1.0.0")

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, ok=True, env=None):
        r = subprocess.run([sys.executable, str(CLI)] + list(args), env=env or self.sb.env(),
                           capture_output=True, text=True)
        last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
        if ok:
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r, last

    @property
    def cfg(self):
        return self.sb.storage / ".config"

    def hello_dest(self):
        return self.cfg / "rocknixds" / "hello"

    def test_versions_offered_follow_the_numbers(self):
        # 1.2 is listed last, so it is first in the releases file; 1.2.9 is listed before 1.2.10
        self.sb.publish_hello("1.2.9")
        self.sb.publish_hello("1.2.10")
        self.sb.publish_hello("1.2")
        self.run_cli("refresh")
        _, last = self.run_cli("install", "hello")
        self.assertEqual(last, "DONE 1.2.10")
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.2.10")

    def test_rocknixds_requirement_uses_the_same_order(self):
        """1.10 required is newer than 1.9 installed. An app already there stays installed, and the install
        still refuses to replace it."""
        self.sb.write_catalog(extra=[
            {"id": "future", "name": "Future", "requires": {"rocknixds": "1.10"},
             "release": {"url": "file:///no-such-package", "version": "1.1", "sha256": "00"}},
            {"id": "older", "name": "Older", "requires": {"rocknixds": "1.9"},
             "release": {"url": "file:///no-such-package", "version": "1.0", "sha256": "00"}},
            {"id": "same", "name": "Same", "requires": {"rocknixds": "1.6.1"},
             "release": {"url": "file:///no-such-package", "version": "1.0", "sha256": "00"}},
        ])
        ver = self.sb.storage / ".config" / "rocknixds-version"
        ver.write_text("1.9\n")
        self.run_cli("refresh")
        r, _ = self.run_cli("list")
        states = {l.split("\t")[1]: l.split("\t")[7] for l in r.stdout.splitlines() if l.startswith("APP\t")}
        self.assertEqual(states["future"], "needs-1.10")
        self.assertEqual(states["older"], "install")
        _, last = self.run_cli("install", "future", ok=False)
        self.assertEqual(last, "FAIL Needs ROCKNIXDS 1.10")

        ver.write_text("1.6.1 extra\n")
        self.run_cli("refresh")
        r, _ = self.run_cli("list")
        states = {l.split("\t")[1]: l.split("\t")[7] for l in r.stdout.splitlines() if l.startswith("APP\t")}
        self.assertEqual(states["same"], "install")          # equal is new enough
        d = self.cfg / "rocknixds" / "future"
        d.mkdir(parents=True)
        (d / "VERSION").write_text("1.0\n")
        r, _ = self.run_cli("list")
        states = {l.split("\t")[1]: l.split("\t")[7] for l in r.stdout.splitlines() if l.startswith("APP\t")}
        self.assertEqual(states["future"], "update")         # already installed: not "needs-1.10"
        _, last = self.run_cli("install", "future", ok=False)
        self.assertEqual(last, "FAIL Needs ROCKNIXDS 1.10")
        self.assertEqual((d / "VERSION").read_text().strip(), "1.0")

    def test_a_package_cannot_name_another_app_or_leave_storage(self):
        self.sb.publish_hello("1.1.0", app_json={"id": "bank"})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertEqual(last, "FAIL Wrong app inside")
        self.assertFalse(self.hello_dest().exists())

        self.sb.publish_hello("1.1.1", app_json={"dest": "/tmp/rocknixds-store-escape"})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("must be under /storage", last)
        self.assertFalse(Path("/tmp/rocknixds-store-escape").exists())

        self.sb.publish_hello("1.1.2", app_json={"folder": "/etc/rocknixds-hello"})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("must be under /storage", last)

        self.sb.publish_hello("1.1.3", app_json={"system": "Hello"})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("bad system name", last)

        self.sb.publish_hello("1.1.4", app_json={"system": "my-app"})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("bad system name", last)
        self.assertFalse(self.hello_dest().exists())

    def test_keep_and_executable_cannot_climb_out(self):
        self.run_cli("install", "hello")
        marker = self.hello_dest() / "hello"
        before = marker.read_text()
        self.sb.publish_hello("1.2.0", app_json={"keep": ["../bank"]})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("bad path", last)
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.0.0")
        self.assertEqual(marker.read_text(), before)
        self.assertFalse((self.cfg / "rocknixds" / "bank").exists())

        self.sb.publish_hello("1.2.1", app_json={"executable": ["../../bin/hello"]})
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertIn("bad path", last)
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.0.0")

    def test_a_link_or_a_backslash_is_not_unpacked(self):
        p = self.sb.publish_hello("3.0.0")
        with tarfile.open(p, "w:gz") as t:
            ti = tarfile.TarInfo("hello/escape")
            ti.type = tarfile.SYMTYPE
            ti.linkname = "/etc/passwd"
            t.addfile(ti)
        Path(str(p) + ".sha256").write_text(hashlib.sha256(p.read_bytes()).hexdigest() + "  x\n")
        _, last = self.run_cli("install", "hello", "--version", "3.0.0", ok=False)
        self.assertEqual(last, "FAIL Package damaged")
        self.assertFalse(self.hello_dest().exists())

        p = self.sb.publish_hello("3.0.1")
        with tarfile.open(p, "w:gz") as t:
            ti = tarfile.TarInfo("hello/escape")
            ti.type = tarfile.SYMTYPE
            ti.linkname = "nested/../../outside"
            t.addfile(ti)
        Path(str(p) + ".sha256").write_text(hashlib.sha256(p.read_bytes()).hexdigest() + "  x\n")
        _, last = self.run_cli("install", "hello", "--version", "3.0.1", ok=False)
        self.assertEqual(last, "FAIL Package damaged")

        p = self.sb.publish_hello("3.1.0")
        zip_package(str(p), {"hello\\..\\evil.sh": b"#!/bin/sh\n"})
        Path(str(p) + ".sha256").write_text(hashlib.sha256(p.read_bytes()).hexdigest() + "  x\n")
        _, last = self.run_cli("install", "hello", "--version", "3.1.0", ok=False)
        self.assertEqual(last, "FAIL Package damaged")
        self.assertFalse((self.sb.base / "evil.sh").exists())
        self.assertFalse(self.hello_dest().exists())

    def _systemctl(self, active):
        bindir = Path(self.tmp.name) / "bin"
        bindir.mkdir(exist_ok=True)
        log = Path(self.tmp.name) / "systemctl.log"
        script = bindir / "systemctl"
        script.write_text(
            "#!/bin/sh\nprintf '%%s\\n' \"$*\" >> '%s'\n"
            "case \" $* \" in\n"
            "  *' is-active -q hello.service '*) exit %d ;;\n"
            "esac\nexit 1\n" % (log, 0 if active else 1))
        script.chmod(0o755)
        env = self.sb.env()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["RNDS_STORE_CHECK_IDLE"] = "1"
        return env, log

    def test_a_running_app_is_not_replaced_or_removed(self):
        self.run_cli("install", "hello")
        self.sb.publish_hello("1.1.0", app_json={"unit": "hello"})
        env, log = self._systemctl(active=True)
        _, last = self.run_cli("install", "hello", ok=False, env=env)
        self.assertEqual(last, "FAIL It's running: quit it first")
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.0.0")
        self.assertIn("is-active -q hello.service", log.read_text())

        man = json.loads((self.hello_dest() / "app.json").read_text())
        man["unit"] = "hello"
        (self.hello_dest() / "app.json").write_text(json.dumps(man))
        _, last = self.run_cli("remove", "hello", ok=False, env=env)
        self.assertEqual(last, "FAIL It's running: quit it first")
        self.assertTrue((self.hello_dest() / "hello").exists())

    def test_a_stopped_unit_can_be_updated(self):
        self.run_cli("install", "hello")
        self.sb.publish_hello("1.1.0", app_json={"unit": "hello"})
        env, _ = self._systemctl(active=False)
        _, last = self.run_cli("install", "hello", env=env)
        self.assertEqual(last, "DONE 1.1.0")
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.1.0")

    def test_a_second_job_is_refused_while_one_holds_the_lock(self):
        os.makedirs(self.sb.data, exist_ok=True)
        lock = open(self.sb.data / ".lock", "w")
        self.addCleanup(lock.close)
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        _, last = self.run_cli("install", "hello", ok=False)
        self.assertEqual(last, "FAIL The Store is busy with another job")
        self.assertFalse(self.hello_dest().exists())
        fcntl.flock(lock, fcntl.LOCK_UN)
        _, last = self.run_cli("install", "hello")
        self.assertEqual(last, "DONE 1.0.0")

    def test_one_broken_update_does_not_skip_the_others(self):
        self.sb.publish_dodakvarter("0.2.0")
        self.sb.publish_dodakvarter("0.3.0")
        d = self.sb.legacy_dodakvarter("0.2.0")
        self.run_cli("install", "hello")
        pkg = self.sb.publish_hello("1.1.0")
        pkg.write_bytes(pkg.read_bytes()[:-8] + b"01234567")   # the .sha256 beside it is still the good archive's
        self.run_cli("refresh")
        r, last = self.run_cli("update", ok=False)
        self.assertEqual(r.returncode, 1)
        self.assertIn("hello", last)
        self.assertIn("damaged", last)
        self.assertEqual((d / "VERSION").read_text().strip(), "0.3.0")
        self.assertEqual((d / "data" / "scores.txt").read_text(), "JRF 30\n")
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.0.0")

    def test_a_setup_script_that_fails_leaves_the_install(self):
        self.sb.publish_hello("1.0.0", extra={"setup.sh": b"#!/bin/sh\nexit 7\n"})
        r, last = self.run_cli("install", "hello")
        self.assertEqual(last, "DONE 1.0.0")
        self.assertIn("setup script failed (7)", r.stdout)
        self.assertTrue((self.hello_dest() / "hello").exists())
        self.assertFalse((self.hello_dest() / "setup-ran").exists())

    def test_menu_text_cannot_grow_a_line_or_a_command(self):
        c = json.loads(self.sb.catalog_path.read_text())
        for a in c["apps"]:
            if a["id"] == "hello":
                a["name"] = "Hello\nexit 9"
                a["tile"] = "A & B <x>"
                a["developer"] = "Evil\nsecond #ff0000 [injected]"
                a["description"] = "line one\nline\ttwo"
        self.sb.catalog_path.write_text(json.dumps(c))
        self.sb.publish_hello("1.4.0", app_json={"launch": "it's.sh"},
                              extra={"it's.sh": b"#!/bin/sh\nprintf '%s\\n' \"$@\"\nexit 4\n"})
        self.run_cli("refresh")
        self.run_cli("install", "hello")

        es = (self.sb.es / "es_systems_rocknixds-store.cfg").read_text()
        self.assertIn("<fullname>A &amp; B &lt;x&gt;</fullname>", es)
        self.assertNotIn("<x>", es)

        theme = (self.sb.theme / "systems.cfg").read_text()
        block = theme.split(store.THEME_MARK_BEGIN, 1)[1].split(store.THEME_MARK_END, 1)[0]
        app_lines = [ln for ln in block.splitlines() if ln.strip() and not ln.startswith("#")]
        self.assertEqual(len(app_lines), 1)
        self.assertTrue(app_lines[0].startswith("hello"))
        self.assertNotIn("\nsecond", block)
        self.assertNotIn("#ff0000", app_lines[0])
        self.assertNotIn("[injected]", app_lines[0])
        self.assertIn("Evilsecond", app_lines[0])

        r, _ = self.run_cli("list")
        desc = [l for l in r.stdout.splitlines() if l.startswith("DESC\thello\t")]
        self.assertEqual(len(desc), 1)
        self.assertIn("line one\\nline two", desc[0])

        entry = (self.cfg / "rocknixds" / "apps" / "hello" / "Hello.sh").read_text()
        self.assertIn("APP=" + store.shq("/storage/.config/rocknixds/hello/it's.sh"), entry)
        self.assertNotIn("\nexit 9", entry)
        app = self.sb.base / "app.sh"
        app.write_text('#!/bin/sh\necho "$@" > "%s"\nexit 4\n' % (self.sb.base / "args"))
        app.chmod(0o755)
        ran = entry.replace(store.shq("/storage/.config/rocknixds/hello/it's.sh"), store.shq(str(app)))
        env = {k: v for k, v in os.environ.items() if k != "SWAYSOCK"}
        env["XDG_RUNTIME_DIR"] = str(self.sb.base)
        r = subprocess.run(["sh", "-c", ran, "Hello.sh", "a", "b c"], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 4, r.stderr)
        self.assertEqual((self.sb.base / "args").read_text(), "a b c\n")

    def test_a_gamelist_that_does_not_parse_is_left_alone(self):
        self.run_cli("install", "hello")
        gl = self.cfg / "rocknixds" / "apps" / "hello" / "gamelist.xml"
        gl.write_text("this is not xml <")
        self.sb.publish_hello("1.5.0")
        r, last = self.run_cli("install", "hello")
        self.assertEqual(last, "DONE 1.5.0")
        self.assertIn("doesn't parse: left alone", r.stdout)
        self.assertEqual(gl.read_text(), "this is not xml <")
        self.assertEqual((self.hello_dest() / "VERSION").read_text().strip(), "1.5.0")

    def test_a_hidden_app_is_updated_and_not_shown(self):
        p = Path(self.tmp.name) / "pkgs" / "quiet-1.1.0.tar.gz"
        sha = tar_package(str(p), "quiet", {
            "quiet": b"#!/bin/sh\necho quiet\n", "Quiet.sh": b"#!/bin/sh\n", "VERSION": b"1.1.0\n"})
        self.sb.write_catalog(extra=[{
            "id": "quiet", "name": "Quiet Tool", "hidden": True, "kind": "app", "developer": "Someone",
            "package": {"check": "quiet", "keep": ["saves"], "entry": "Quiet.sh"},
            "release": {"url": p.as_uri(), "version": "1.1.0", "sha256": sha}}])
        d = self.cfg / "rocknixds" / "quiet"
        (d / "saves").mkdir(parents=True)
        (d / "quiet").write_text("old\n")
        (d / "VERSION").write_text("1.0.0\n")
        (d / "saves" / "book").write_text("kept\n")
        self.run_cli("refresh")
        r, _ = self.run_cli("list")
        self.assertFalse(any(l.startswith("APP\tquiet\t") for l in r.stdout.splitlines()))
        _, last = self.run_cli("notify")
        self.assertEqual(last, "UPTODATE")
        _, last = self.run_cli("update")
        self.assertEqual(last, "DONE 1.1.0")
        self.assertEqual((d / "VERSION").read_text().strip(), "1.1.0")
        self.assertEqual((d / "saves" / "book").read_text(), "kept\n")

    def test_the_autocheck_switch_is_its_last_line(self):
        self.sb.publish_dodakvarter("0.2.0")
        self.sb.publish_dodakvarter("0.3.0")
        self.sb.legacy_dodakvarter("0.2.0")
        cfg = self.cfg / "system" / "configs" / "system.cfg"
        cfg.parent.mkdir(parents=True)
        cfg.write_text("rocknixds.autocheck=0\nrocknixds.autocheckextra=1\nrocknixds.autocheck=1\n")
        import http.server
        import threading
        got = []

        class H(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                got.append(self.rfile.read(int(self.headers["Content-Length"])).decode())
                self.send_response(200)
                self.end_headers()

            def log_message(self, *a):
                pass

        srv = http.server.HTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(srv.shutdown)
        self.addCleanup(srv.server_close)
        env = self.sb.env()
        env["RNDS_STORE_NOTIFY"] = "http://127.0.0.1:%d/notify" % srv.server_port
        r = subprocess.run([sys.executable, str(CLI), "notify"], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("NOTIFIED", r.stdout)
        self.assertEqual(len(got), 1)
        cfg.write_text("# rocknixds.autocheck=1\nrocknixds.autocheck=1\nrocknixds.autocheck=0\n")
        (self.sb.data / "notified.json").unlink()
        r = subprocess.run([sys.executable, str(CLI), "notify"], env=env, capture_output=True, text=True)
        self.assertIn("UPDATES", r.stdout)
        self.assertNotIn("NOTIFIED", r.stdout)
        self.assertEqual(len(got), 1)


if __name__ == "__main__":
    unittest.main()
