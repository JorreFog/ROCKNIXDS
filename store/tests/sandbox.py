"""A handheld in a folder, for the Store's tests and pictures: /storage under a temp dir (RNDS_STORE_ROOT), ROCKNIXDS's
ES systems file and ROCKNIX Pixel's systems.cfg as install.sh puts them, a catalog of the real apps plus a new one
("hello", which ROCKNIXDS doesn't know), their releases as GitHub lists them and their packages, all file:// URLs.

    python3 store/tests/sandbox.py <dir>     makes one and prints the environment to use it (for the Store app)
"""
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLI = REPO / "store" / "device" / "rocknixds-store"
THEME = REPO / "dii-ess-aye" / "themes" / "rocknixds-pixel-dark" / "rnds"

HELLO_ICON = THEME / "icons" / "fantasy.png"


def tar_package(path, root, files, mode_exec=()):
    """files: {relative path: bytes}"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with tarfile.open(path, "w:gz") as t:
        dirs = set()
        for rel in files:
            parts = rel.split("/")[:-1]
            for i in range(len(parts)):
                dirs.add("/".join(parts[:i + 1]))
        for d in sorted(dirs | {""}):
            ti = tarfile.TarInfo((root + "/" + d).rstrip("/"))
            ti.type, ti.mode = tarfile.DIRTYPE, 0o755
            t.addfile(ti)
        for rel, body in files.items():
            ti = tarfile.TarInfo(root + "/" + rel)
            ti.size = len(body)
            ti.mode = 0o755 if rel in mode_exec or rel.endswith(".sh") else 0o644
            t.addfile(ti, io.BytesIO(body))
    sha = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    Path(path + ".sha256").write_text("%s  %s\n" % (sha, os.path.basename(path)))
    return sha


class Sandbox:
    def __init__(self, base, tiles=True, device="rgds", rocknixds="1.6.1"):
        self.base = Path(base)
        self.root = self.base / "root"
        self.storage = self.root / "storage"
        self.es = self.storage / ".config" / "emulationstation"
        self.theme = self.es / "themes" / "rocknixds-pixel-dark" / "rnds"
        self.data = self.storage / ".config" / "rocknixds" / "store" / "data"
        self.rel = self.base / "releases"
        self.dl = self.base / "dl"
        self.releases = {}
        (self.storage / "roms" / "ports").mkdir(parents=True, exist_ok=True)
        self.theme.mkdir(parents=True, exist_ok=True)
        (self.theme / "icons").mkdir(exist_ok=True)
        shutil.copy(THEME / "systems.cfg", self.theme / "systems.cfg")
        for i in ("dodakvarter.png", "bank.png", "store.png"):
            shutil.copy(THEME / "icons" / i, self.theme / "icons" / i)
        # the light theme uses the dark one's rnds folder
        light = self.es / "themes" / "rocknixds-pixel-light"
        light.mkdir(parents=True, exist_ok=True)
        os.symlink("../rocknixds-pixel-dark/rnds", light / "rnds")
        if tiles:
            shutil.copy(REPO / "dii-ess-aye" / "device" / "es_systems_rocknixds.cfg", self.es / "es_systems_rocknixds.cfg")
        (self.storage / ".config" / "rocknixds-version").write_text(rocknixds + "\n")
        self.device = device
        self.catalog_path = self.base / "store" / "catalog.json"
        self.write_catalog()

    # ---- the catalog: the real one, its pictures as file:// URLs, plus "hello" ----
    def write_catalog(self, extra=None):
        c = json.loads((REPO / "store" / "catalog.json").read_text())
        for a in c["apps"]:
            for k in ("icon",):
                if a.get(k):
                    a[k] = (REPO / "store" / a[k]).resolve().as_uri()
            a["screenshots"] = [(REPO / "store" / s).resolve().as_uri() for s in a.get("screenshots", [])]
        c["apps"].append({
            "id": "hello", "name": "Hello Handheld", "tile": "Hello", "kind": "app", "developer": "Someone Else",
            "genre": "Demo", "players": "1", "accent": "#b07ef0",
            "summary": "A new app ROCKNIXDS has never heard of.",
            "description": "It comes with its own tile, icon and colour: no ROCKNIXDS release needed.",
            "icon": HELLO_ICON.as_uri(),
            "release": {"github": "Someone/hello", "tag_prefix": "v", "asset": "hello-{version}-aarch64.tar.gz"},
        })
        for a in extra or []:
            c["apps"].append(a)
        self.catalog_path.parent.mkdir(parents=True, exist_ok=True)
        self.catalog_path.write_text(json.dumps(c, ensure_ascii=False, indent=1))
        return c

    # ---- releases and packages ----
    def publish(self, repo, tag, asset, body_files, root, size=None, exec_=()):
        p = self.dl / tag / asset
        tar_package(str(p), root, body_files, exec_)
        self.releases.setdefault(repo, []).insert(0, {
            "tag_name": tag, "draft": False, "prerelease": True,
            "assets": [{"name": asset, "size": size or p.stat().st_size}]})
        f = self.rel / (repo + ".json")
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(json.dumps(self.releases[repo]))
        return p

    def publish_dodakvarter(self, v):
        media = {"media/dodakvarter-%s.png" % k: (REPO / "dodakvarter" / "device" / "media" / ("dodakvarter-%s.png" % k)).read_bytes()
                 for k in ("image", "thumb", "marquee")}
        files = {"dodakvarter": b"#!/bin/sh\necho game " + v.encode() + b"\n", "VERSION": (v + "\n").encode(),
                 "launch.sh": b"#!/bin/sh\n", "session.sh": b"#!/bin/sh\n", "restore.sh": b"#!/bin/sh\n",
                 "selftest.sh": b"#!/bin/sh\n", "gamelist.py": b"", "update.sh": b"#!/bin/sh\n",
                 "Doda Kvarter.sh": (REPO / "dodakvarter" / "device" / "Doda Kvarter.sh").read_bytes()}
        files.update(media)
        return self.publish("JorreFog/ROCKNIXDS", "dodakvarter-v" + v, "dodakvarter-%s-aarch64.tar.gz" % v, files,
                            "dodakvarter", exec_=("dodakvarter",))

    def publish_bank(self, v):
        files = {"rocknixds-bank": b"\x7fELF fake", "rocknixds-bank.sh": b"#!/bin/sh\n", "VERSION": (v + "\n").encode(),
                 "ROCKNIXDS Bank.sh": (REPO / "bank" / "device" / "ROCKNIXDS Bank.sh").read_bytes(),
                 "media/bank-image.png": (REPO / "bank" / "device" / "media" / "bank-image.png").read_bytes(),
                 "media/bank-thumb.png": (REPO / "bank" / "device" / "media" / "bank-thumb.png").read_bytes(),
                 "media/bank-marquee.png": (REPO / "bank" / "device" / "media" / "bank-marquee.png").read_bytes()}
        return self.publish("JorreFog/ROCKNIXDS", "bank-v" + v, "rocknixds-bank-%s-aarch64.tar.gz" % v, files,
                            "rocknixds-bank", size=24156930, exec_=("rocknixds-bank",))

    def publish_hello(self, v, app_json=None, extra=None):
        man = {"id": "hello", "check": "hello", "keep": ["saves"], "entry": "Hello.sh", "icon": "icon.png",
               "media": {"image": "media/hello-image.png"}, "post_install": "setup.sh"}
        if app_json:
            man.update(app_json)
        files = {"app.json": json.dumps(man).encode(), "hello": b"#!/bin/sh\necho hello\n",
                 "Hello.sh": b"#!/bin/sh\nexec /storage/.config/rocknixds/hello/hello\n",
                 "setup.sh": b"#!/bin/sh\necho set up > setup-ran\n",
                 "icon.png": HELLO_ICON.read_bytes(),
                 "media/hello-image.png": (REPO / "bank" / "device" / "media" / "bank-thumb.png").read_bytes()}
        files.update(extra or {})
        return self.publish("Someone/hello", "v" + v, "hello-%s-aarch64.tar.gz" % v, files, "hello", exec_=("hello",))

    def legacy_dodakvarter(self, v="0.2.0"):
        """Döda Kvarter as ROCKNIXDS's install.sh puts it: the game, its data, its tile"""
        d = self.storage / ".config" / "rocknixds" / "dodakvarter"
        (d / "data").mkdir(parents=True, exist_ok=True)
        (d / "data" / "scores.txt").write_text("JRF 30\n")
        (d / "dodakvarter").write_text("old game\n")
        (d / "VERSION").write_text(v + "\n")
        t = self.storage / ".config" / "rocknixds" / "apps" / "dodakvarter"
        (t / "images").mkdir(parents=True, exist_ok=True)
        (t / "Doda Kvarter.sh").write_text("#!/bin/bash\n")
        return d

    def env(self):
        e = dict(os.environ)
        e.update({
            "RNDS_STORE_ROOT": str(self.root),
            "RNDS_STORE_CATALOG": str(self.catalog_path),
            "RNDS_STORE_API": "file://" + str(self.rel) + "/{repo}.json",
            "RNDS_STORE_DOWNLOAD": "file://" + str(self.dl) + "/{tag}/{asset}",
            "RNDS_STORE_DEVICE": self.device,
            "RNDS_STORE_CLI": str(CLI),
            "RNDS_STORE_DATA": str(self.data),
        })
        return e


if __name__ == "__main__":
    base = Path(sys.argv[1])
    shutil.rmtree(base, ignore_errors=True)
    s = Sandbox(base)
    s.legacy_dodakvarter("0.2.0")
    s.publish_dodakvarter("0.2.0")
    s.publish_dodakvarter("0.3.0")
    s.publish_bank("0.1.0")
    s.publish_hello("1.0.0")
    for k, v in s.env().items():
        if k.startswith("RNDS_STORE_"):
            print("export %s='%s'" % (k, v))
