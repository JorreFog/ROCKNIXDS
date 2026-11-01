"""The Store app reads `rocknixds-store list` in catalog.c. A tab, a newline or a backslash in a
description has to stay inside its field, a needs-* state is its own tab, and a picture that is
not a small RNDSIMG1 is skipped. The package manager's tests check the lines it prints; this
runs the C parser on them.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

CATALOG_C = ROOT / "store" / "src" / "catalog.c"
TEST_C = ROOT / "tests" / "catalog_parse_test.c"


def compile_parser(dest):
    cmd = ["cc", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-I", str(ROOT / "store" / "src"), "-I", str(ROOT / "dodakvarter" / "src"),
           "-o", str(dest), str(TEST_C), str(CATALOG_C)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise AssertionError(r.stderr)
    return dest


def raw_image(w, h, pixels):
    body = b"".join(bytes(p) for p in pixels)
    return b"RNDSIMG1" + w.to_bytes(2, "little") + h.to_bytes(2, "little") + body


def load(lines, data):
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        fixture = d / "list.txt"
        fixture.write_text("".join(line + "\n" for line in lines))
        cli = d / "cli"
        cli.write_text("#!/bin/sh\ncat '%s'\n" % fixture)
        cli.chmod(0o755)
        exe = d / "catalog_parse_test"
        compile_parser(exe)
        env = dict(os.environ)
        env["RNDS_STORE_CLI"] = str(cli)
        env["RNDS_STORE_DATA"] = str(data)
        r = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise AssertionError(r.stdout + r.stderr)
        return r.stdout


def apps(text):
    blocks = []
    cur = None
    for line in text.splitlines():
        if line.startswith("app "):
            cur = {}
            blocks.append(cur)
        elif cur is not None and " " in line:
            k, v = line.split(" ", 1)
            cur[k] = v
        elif cur is not None:
            cur[line] = ""
    return blocks


class CatalogParseTest(unittest.TestCase):
    def test_fields_escapes_states_and_pictures(self):
        with tempfile.TemporaryDirectory() as data:
            data = Path(data)
            pix = data / "media" / "pix"
            pix.mkdir(parents=True)
            (pix / "icon.raw").write_bytes(raw_image(1, 1, [(0x11, 0x22, 0x33, 0xFF)]))
            (pix / "shot.raw").write_bytes(raw_image(1, 1, [(0x01, 0x02, 0x03, 0x80)]))
            huge = data / "media" / "huge"
            huge.mkdir()
            (huge / "icon.raw").write_bytes(raw_image(1025, 1, []))
            short = data / "media" / "short"
            short.mkdir()
            (short / "icon.raw").write_bytes(b"RNDSIMG1" + (1).to_bytes(2, "little") + (1).to_bytes(2, "little"))
            bad = data / "media" / "badmagic"
            bad.mkdir()
            (bad / "icon.raw").write_bytes(b"NOTANIMG" + b"\0" * 4)
            long_summary = "S" * 300

            def row(*fields):
                return "\t".join(fields)

            lines = [
                row("CATALOG", "3", "1700000000"),
                row("NOPE", "ignored"),
                row("APP", "pix", "game", "Pixel", "Dev", "1.0", "1.1", "update", "12",
                    long_summary, "#abc", "RPG", "1-2"),
                row("DESC", "pix", "hello\\nworld\\\\x"),
                row("NOTES", "pix", "what\\\\n is new"),
                row("DESC", "missing", "does not attach"),
                row("APP", "huge", "app", "Huge", "Dev", "", "2.0", "install", "0", "sum", "green", "Puzzle", "1"),
                row("APP", "short", "app", "Short", "Dev", "1.0", "", "installed", "0", "sum", "#", "Tool", "2"),
                row("APP", "badmagic", "app", "Bad", "Dev", "1.0", "1.2", "needs-1.6.4", "0", "sum", "#zz", "Tool", "1"),
                row("APP", "weird", "app", "Weird", "Dev", "", "", "mystery", "0", "sum", "", "Tool", "1"),
                row("APP", "idonly"),
            ]
            text = load(lines, data)
            head = text.splitlines()
            self.assertEqual(head[0], "n 6")
            self.assertEqual(head[1], "refreshed 1700000000")
            self.assertEqual(head[2], "updates 1")
            by_id = {a["id"]: a for a in apps(text)}
            pix_a = by_id["pix"]
            self.assertEqual(pix_a["state"], "update")
            self.assertEqual(pix_a["desc"], r"hello\nworld\\x")  # newline, and a real backslash
            self.assertEqual(pix_a["notes"], r"what\\n is new")   # the two characters \ and n
            self.assertEqual(pix_a["summary_len"], "199")
            self.assertEqual(pix_a["players"], "1-2")            # the long summary did not eat the next fields
            self.assertEqual(pix_a["accent"], "abc")
            self.assertEqual(pix_a["icon"], "1 1 ff332211")
            self.assertEqual(pix_a["shot"], "1 1 80030201")
            self.assertEqual(pix_a["tab_games"], "1")
            self.assertEqual(pix_a["tab_apps"], "0")
            self.assertEqual(pix_a["tab_installed"], "1")
            self.assertEqual(pix_a["tab_updates"], "1")
            self.assertEqual(by_id["huge"]["state"], "install")
            self.assertEqual(by_id["huge"]["accent"], "2eae62")  # not a #colour: the default green
            self.assertEqual(by_id["huge"]["icon"], "0 0 0")
            self.assertEqual(by_id["huge"]["tab_apps"], "1")
            self.assertEqual(by_id["huge"]["tab_installed"], "0")
            self.assertEqual(by_id["short"]["state"], "installed")
            self.assertEqual(by_id["short"]["icon"], "0 0 0")    # header promised a pixel the file did not have
            self.assertEqual(by_id["short"]["accent"], "0")      # "#" with no digits is 0, not the default
            self.assertEqual(by_id["badmagic"]["state"], "needs")
            self.assertEqual(by_id["badmagic"]["state_s"], "needs-1.6.4")
            self.assertEqual(by_id["badmagic"]["tab_updates"], "0")
            self.assertEqual(by_id["badmagic"]["icon"], "0 0 0")
            self.assertEqual(by_id["weird"]["state"], "unavailable")
            self.assertEqual(by_id["idonly"]["state"], "unavailable")
            self.assertEqual(by_id["idonly"]["id"], "idonly")

    def test_the_list_stops_at_64_apps(self):
        lines = ["\t".join(["APP", "x%02d" % i, "app", "N", "D", "", "1", "install", "0", "s", "#1", "G", "1"])
                 for i in range(70)]
        text = load(lines, Path("/tmp"))
        self.assertTrue(text.startswith("n 64\n"))
        ids = [a["id"] for a in apps(text)]
        self.assertEqual(ids[0], "x00")
        self.assertEqual(ids[-1], "x63")
        self.assertNotIn("x64", ids)

    def test_a_missing_list_is_an_empty_catalog(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / "catalog_parse_test"
            compile_parser(exe)
            env = dict(os.environ)
            env["RNDS_STORE_CLI"] = str(Path(d) / "no-such-cli")
            r = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(r.stdout.startswith("n 0\n"))

    def test_tsv_from_the_package_manager_round_trips(self):
        import importlib.machinery
        import importlib.util
        cli = ROOT / "store" / "device" / "rocknixds-store"
        loader = importlib.machinery.SourceFileLoader("rocknixds_store_parse", str(cli))
        mod = importlib.util.module_from_spec(importlib.util.spec_from_loader("rocknixds_store_parse", loader))
        loader.exec_module(mod)
        original = "a\nb\\c\td"
        text = load(["\t".join(["APP", "round", "app", "R", "D", "", "1", "install", "0", "s", "#1", "G", "1"]),
                     "\t".join(["DESC", "round", mod.tsv(original)]),
                     "\t".join(["NOTES", "round", mod.tsv(original)])], Path("/tmp"))
        app = apps(text)[0]
        # a tab is a column, so the list turns it into a space before the app sees it
        self.assertEqual(app["desc"], r"a\nb\\c d")
        self.assertEqual(app["notes"], r"a\nb\\c d")
