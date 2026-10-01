#!/usr/bin/env python3
"""rocknixds-media.py --device <ip> [options]   (run on a PC; needs python3, Pillow, numpy, ssh access as root)

One command for every DS game's art and text on a ROCKNIXDS device, with no scraper account:

  media type   what the theme shows                       source
  thumbnail    box art                                    libretro-thumbnails Named_Boxarts
  image        screenshot (side by side)                  libretro-thumbnails Named_Snaps
  titleshot    title screen (side by side)                libretro-thumbnails Named_Titles
  boxart       3D game case on the game list top screen   box3d.py from the cover
  boxback      label art for the drawn cartridge          labelart.py from the cover
  cartridge    the real DS card on the carousel           LaunchBox Games Database cart scans (nds-carts.json index)
  wheel        RetroAchievements progress strip           ra-fetch.py ON the device + ra_panel.py
  metadata     description, genre, developer, publisher,  nds-meta.json.gz (LaunchBox overviews), only where ES has none
               release date

Games are matched by name (the ROM's No-Intro style file name for libretro, a normalised title for LaunchBox),
with a fuzzy fallback and a preference for USA/World/Europe releases. Existing media is kept unless --force.

Options:
  --device IP         the RG DS (ssh root@IP; default password rocknix). RGDS_SSH overrides the ssh command.
  --game SUBSTRING    only games whose name or file matches
  --force             re-render and re-push media the game already has (text fields are still only filled where empty)
  --no-ra             skip RetroAchievements (needs the account set up in ES on the device)
  --no-push           download and render only (keep the files under --out)
  --out DIR           work directory (default: ./media-out)
  --dry-run           show what would be fetched and pushed, touch nothing
"""
import argparse, difflib, gzip, html, io, json, os, re, shlex, shutil, subprocess, sys, unicodedata, urllib.parse, urllib.request
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LR = "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/"
LB_IMG = "https://images.launchbox-app.com/"
UA = "ROCKNIXDS media tool (https://github.com/JorreFog/ROCKNIXDS)"
REGION_RANK = ["USA", "World", "Europe", "Australia", "Canada", "Japan"]
LB_REGIONS = ["North America", "United States", "World", "Europe", "United Kingdom", "Australia", "Canada",
              "Germany", "France", "Spain", "Italy", "Japan", "Korea"]
CARD_ASPECT, CARD_H = 0.9174, 480
FONT_ON_DEVICE = "/storage/.config/emulationstation/themes/dii-ess-aye/assets/fonts/dsi_font.otf"


def log(*a):
    print(*a, flush=True)


# ---------- device ----------
class Device:
    def __init__(self, ip, dry):
        self.ssh = shlex.split(os.environ["RGDS_SSH"]) if os.environ.get("RGDS_SSH") else ["ssh", "-o", "ConnectTimeout=8", f"root@{ip}"]
        self.dry = dry

    def run(self, cmd, data=None, binary=False):
        r = subprocess.run(self.ssh + [cmd], input=data, capture_output=True, check=False)
        if r.returncode != 0 and not binary:
            raise RuntimeError(f"ssh failed: {cmd[:80]}: {r.stderr.decode(errors='replace')[-300:]}")
        return r.stdout if binary else r.stdout.decode(errors="replace")

    def games(self):
        return json.loads(self.run("curl -s localhost:1234/systems/nds/games"))

    def fetch(self, path):
        return self.run(f"cat {shlex.quote(path)}", binary=True)

    def push_media(self, gid, mtype, data):
        if self.dry:
            return "dry"
        self.run(f"cat > /tmp/rgds-media.bin && curl -s -o /dev/null -w '%{{http_code}}' -X POST -H 'Content-Type: image/png' "
                 f"--data-binary @/tmp/rgds-media.bin localhost:1234/systems/nds/games/{gid}/media/{mtype}", data=data)
        return "ok"

    def push_meta(self, gid, meta):
        if self.dry:
            return "dry"
        return self.run(f"curl -s -o /dev/null -w '%{{http_code}}' -X POST -H 'Content-Type: application/json' "
                        f"--data-binary {shlex.quote(json.dumps(meta))} localhost:1234/systems/nds/games/{gid}")


# ---------- naming ----------
def norm(name):
    n = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().lower()
    n = re.sub(r"\(.*?\)|\[.*?\]", "", n)
    n = n.replace("pokemon", "pokemon").replace("&", " and ")
    n = re.sub(r"\b(version|the)\b", " ", n)
    return re.sub(r"\s+", " ", re.sub(r"[^a-z0-9 ]", " ", n)).strip()


REGION_WORDS = {"USA": "USA", "U": "USA", "Europe": "Europe", "E": "Europe", "World": "World", "Japan": "Japan", "J": "Japan",
                "Australia": "Australia", "Canada": "Canada"}
# kiosk demos, betas and the like share the retail game's name once the brackets are stripped, and sort first
# ("(USA) (Demo)" < "(USA, Australia)"): Mario Kart DS got the kiosk demo's box and screenshots
NON_RETAIL = re.compile(r"\((?:[^)]*\b(?:Demo|Kiosk|Beta|Proto|Prototype|Sample|Preview|Debug|Pirate|Unl|Aftermarket|"
                        r"Program|Competition|Taikenban|Trial)\b[^)]*)\)", re.I)


def own_region(name):
    """the region a ROM's file name says it is, if any: "(Europe)", "(E)", "(USA, Europe)" ..."""
    for tag in re.findall(r"\(([^)]*)\)", name):
        for part in re.split(r"[,\s]+", tag):
            if part in REGION_WORDS:
                return REGION_WORDS[part]
    return None


def region_rank(name, prefer=None):
    """0 for the ROM's own region, then USA, World, Europe, ..."""
    if prefer and (f"({prefer}" in name or f", {prefer}" in name):
        return 0
    for i, r in enumerate(REGION_RANK):
        if f"({r}" in name or f", {r}" in name:
            return i + 1
    return len(REGION_RANK) + 1


def best_match(wanted, candidates, cutoff=0.82):
    """candidates: list of display names. Exact normalised match first, then the closest by ratio. Among equals:
    retail releases before demos and betas, then the ROM's own region, then USA, World, Europe, ..."""
    w, prefer = norm(wanted), own_region(wanted)
    rank = lambda c: (1 if NON_RETAIL.search(c) else 0, region_rank(c, prefer))
    exact = [c for c in candidates if norm(c) == w]
    if exact:
        return sorted(exact, key=rank)[0], 1.0
    keyed = {norm(c): c for c in sorted(candidates, key=rank, reverse=True)}   # later (better) wins
    close = difflib.get_close_matches(w, list(keyed), n=5, cutoff=cutoff)
    if not close:
        return None, 0
    pick = sorted(close, key=lambda k: (-difflib.SequenceMatcher(None, w, k).ratio(), rank(keyed[k])))[0]
    return keyed[pick], difflib.SequenceMatcher(None, w, pick).ratio()


# ---------- RetroAchievements IDs ----------
# ES knows a game's RetroAchievements ID only once it has hashed the ROM (its "find all games with achievements"
# job); a newly copied game has none, so ra-fetch.py skipped it. The RA hash is computed on the device the way
# rcheevos does it for DS (the first 0x160 header bytes, the ARM9 and ARM7 code and 0xA00 bytes of icon/title, a
# SuperCard header skipped), RA says which game it belongs to, and ES is given both.
RA_HASH = r"""
import hashlib, json, sys
out = {}
for p in json.load(sys.stdin):
    try:
        f = open(p, "rb"); h = f.read(512); off = 0
        if h[0:4] == b"\x2e\x00\x00\xea" and h[0xb0:0xb4] == b"\x44\x46\x96\x00":
            off = 512; f.seek(off); h = f.read(512)
        u = lambda o: int.from_bytes(h[o:o + 4], "little")
        if u(0x2c) + u(0x3c) > 16 << 20: raise ValueError("ARM9 + ARM7 code over 16 MB")
        m = hashlib.md5(h[:0x160])
        for o, n in ((u(0x20), u(0x2c)), (u(0x30), u(0x3c)), (u(0x68), 0xa00)):
            f.seek(off + o); m.update(f.read(n))
        out[p] = m.hexdigest()
    except Exception as e:
        out[p] = "error: %s" % e
print(json.dumps(out))
"""


def fill_cheevos_ids(dev, games):
    todo = [g for g in games if not int(g.get("cheevosId") or 0)]
    if not todo:
        return
    paths = {g["id"]: g["path"] if g["path"].startswith("/") else "/storage/roms/nds/" + g["path"].lstrip("./") for g in todo}
    hashes = json.loads(dev.run("python3 -c " + shlex.quote(RA_HASH), data=json.dumps(list(paths.values())).encode()))
    for g in todo:
        h = hashes.get(paths[g["id"]], "")
        if not re.fullmatch(r"[0-9a-f]{32}", h):
            log(f"  {g['name']}: no RetroAchievements hash ({h or 'not read'})"); continue
        try:
            rid = int(json.loads(http(f"https://retroachievements.org/dorequest.php?r=gameid&m={h}")).get("GameID") or 0)
        except (OSError, ValueError) as e:
            log(f"  {g['name']}: RetroAchievements lookup failed ({e})"); continue
        if rid:
            dev.push_meta(g["id"], {"cheevosHash": h.upper(), "cheevosId": str(rid)})   # upper case, as ES stores it
            g["cheevosId"] = rid
            log(f"  {g['name']}: RetroAchievements game {rid} (hash {h})")
        else:
            log(f"  {g['name']}: RetroAchievements doesn't know this ROM (hash {h})")


# ---------- sources ----------
def http(url, timeout=40):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


class Libretro:
    def __init__(self, cache):
        self.cache = cache
        self.lists = {}

    def names(self, kind):
        if kind not in self.lists:
            p = os.path.join(self.cache, f"lr-{kind}.html")
            if not os.path.exists(p):
                with open(p, "wb") as f:
                    f.write(http(LR + kind + "/"))
            s = open(p, encoding="utf-8", errors="replace").read()
            self.lists[kind] = [html.unescape(urllib.parse.unquote(m))[:-4] for m in re.findall(r'href="([^"]+\.png)"', s)]
        return self.lists[kind]

    def image(self, kind, name):
        p = os.path.join(self.cache, kind, name + ".png")
        if not os.path.exists(p):
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(http(LR + kind + "/" + urllib.parse.quote(name) + ".png"))
        return Image.open(p).convert("RGBA")


def side_by_side(img):
    """DS snaps are top over bottom (256x384): the RG DS shows them side by side (512x192)."""
    w, h = img.size
    if h > w:
        top, bot = img.crop((0, 0, w, h // 2)), img.crop((0, h // 2, w, h))
        out = Image.new("RGBA", (w * 2, h // 2))
        out.paste(top, (0, 0)); out.paste(bot, (w, 0))
        return out
    return img


def png_bytes(img):
    # Drop iCCP. ES loads these with SDL_image, which fails the whole file when libpng
    # rejects the profile ("invalid data"), then retries the load every frame.
    img.info.pop("icc_profile", None)
    b = io.BytesIO(); img.save(b, "PNG", optimize=True); return b.getvalue()


def cart_image(index, name, cache):
    """A real cart scan from the LaunchBox index, fitted to the theme's card shape (cut-outs first)."""
    match, score = best_match(name, list(index))
    if not match:
        return None, "no LaunchBox entry"
    own = {"USA": "North America", "Europe": "Europe", "Japan": "Japan", "Australia": "Australia"}.get(own_region(name) or "")
    cands = sorted(index[match], key=lambda c: (0 if c[1].lower().endswith(".png") else 1, 0 if own and c[0] == own else 1,
                                                LB_REGIONS.index(c[0]) if c[0] in LB_REGIONS else len(LB_REGIONS)))
    has_cutout = any(c[1].lower().endswith(".png") for c in cands)
    for region, fn in cands:
        p = os.path.join(cache, "lb", os.path.basename(fn))
        try:
            if not os.path.exists(p):
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.write(http(LB_IMG + fn))
            img = Image.open(p)
        except OSError:
            continue
        if img.mode != "RGBA" and has_cutout:
            continue                        # a photo on a background while a cut-out exists
        img = img.convert("RGBA")
        bbox = img.getchannel("A").point(lambda v: 255 if v > 16 else 0).getbbox()
        if bbox:
            img = img.crop(bbox)
        return img.resize((round(CARD_H * CARD_ASPECT), CARD_H), Image.LANCZOS), f"{match} [{region or 'no region'}]"
    return None, f"{match}: no downloadable cart image"


def run_tool(script, *args):
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True, capture_output=True)


# ---------- main ----------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", required=True); ap.add_argument("--game", default="")
    ap.add_argument("--force", action="store_true"); ap.add_argument("--no-ra", action="store_true")
    ap.add_argument("--no-push", action="store_true"); ap.add_argument("--out", default="media-out")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    dev = Device(a.device, a.dry_run or a.no_push)
    out = os.path.abspath(a.out); cache = os.path.join(out, "cache"); os.makedirs(cache, exist_ok=True)
    lr = Libretro(cache)
    carts = json.load(open(os.path.join(HERE, "nds-carts.json")))["games"]
    with gzip.open(os.path.join(HERE, "nds-meta.json.gz"), "rt", encoding="utf-8") as f:
        meta_db = json.load(f)["games"]

    games = [g for g in dev.games() if a.game.lower() in (g["name"] + g["path"]).lower()]
    log(f"{len(games)} game(s) on the device" + (f" matching '{a.game}'" if a.game else ""))
    font = os.path.join(cache, "dsi_font.otf")
    if not os.path.exists(font):
        with open(font, "wb") as f:
            f.write(dev.fetch(FONT_ON_DEVICE))

    # RetroAchievements: the numbers come from the device (its account, its token)
    ra, ra_info = {}, {}
    if not a.no_ra:
        try:
            fill_cheevos_ids(dev, games)
            src = open(os.path.join(HERE, "ra-fetch.py"), "rb").read()
            # its output is the per-game summary (or why it failed): show it, a silent failure left old strips in place
            fetched = dev.run("cat > /tmp/ra-fetch.py && rm -rf /tmp/ra && python3 /tmp/ra-fetch.py /tmp/ra 2>&1; "
                              "echo \"exit $?\"", data=src)
            log("RetroAchievements (fetched on the device):\n  " + fetched.strip().replace("\n", "\n  "))
            tarball = dev.run("cd /tmp/ra && tar cf - .", binary=True)
            radir = os.path.join(out, "ra"); shutil.rmtree(radir, ignore_errors=True); os.makedirs(radir)
            subprocess.run(["tar", "xf", "-", "-C", radir], input=tarball, check=True)
            if not os.path.exists(os.path.join(radir, "ra.json")):
                raise RuntimeError("ra-fetch.py wrote no ra.json (see above)")
            shutil.rmtree(os.path.join(out, "wheel"), ignore_errors=True)
            run_tool("ra_panel.py", os.path.join(radir, "ra.json"), radir, font, os.path.join(out, "wheel"))
            ra_info = json.load(open(os.path.join(radir, "ra.json")))
            ra = {gid: os.path.join(out, "wheel", gid + ".png") for gid in ra_info}
        except Exception as e:                                      # no account, offline, ...: the rest still runs
            log(f"RetroAchievements skipped: {e}")

    for g in games:
        gid, name, stem = g["id"], g["name"], os.path.splitext(os.path.basename(g["path"]))[0]
        have = lambda t: (t in g) and not a.force
        log(f"\n== {name}  ({stem})")
        gdir = os.path.join(out, gid); os.makedirs(gdir, exist_ok=True)
        results = {}

        def push(mtype, img):
            data = png_bytes(img) if isinstance(img, Image.Image) else img
            with open(os.path.join(gdir, mtype + ".png"), "wb") as f:
                f.write(data)
            results[mtype] = dev.push_media(gid, mtype, data)

        # libretro: cover, snap, title (by the ROM's name, No-Intro style)
        cover = None
        for kind, mtype in (("Named_Boxarts", "thumbnail"), ("Named_Snaps", "image"), ("Named_Titles", "titleshot")):
            match, score = best_match(stem, lr.names(kind)) or best_match(name, lr.names(kind))
            if not match:
                results[mtype] = "no match"; continue
            try:
                img = lr.image(kind, match)
            except OSError as e:
                results[mtype] = f"download failed ({e})"; continue
            if kind == "Named_Boxarts":
                cover = img
            else:
                img = side_by_side(img)
            log(f"  {mtype:10s} {match}" + ("" if score == 1 else f"  (fuzzy {score:.2f})"))
            if not have(mtype):
                push(mtype, img)
            else:
                results[mtype] = "kept"
        # derived from the cover: the 3D case and the label art
        if cover is not None:
            cp = os.path.join(gdir, "cover.png"); cover.save(cp)
            for mtype, script, extra in (("boxart", "box3d.py", [font]), ("boxback", "labelart.py", [])):
                if have(mtype):
                    results[mtype] = "kept"; continue
                op = os.path.join(gdir, mtype + ".png")
                run_tool(script, cp, op, *extra)
                results[mtype] = dev.push_media(gid, mtype, open(op, "rb").read())
        # the real cartridge
        if have("cartridge"):
            results["cartridge"] = "kept"
        else:
            img, why = cart_image(carts, name, cache)
            log(f"  cartridge  {why}")
            if img is not None:
                push("cartridge", img)
            else:
                results["cartridge"] = "none"
        # RetroAchievements strip: always pushed, it shows progress, which changes between runs
        if gid in ra and os.path.exists(ra[gid]):
            results["wheel"] = dev.push_media(gid, "wheel", open(ra[gid], "rb").read())
        elif gid in ra and ra_info.get(gid, {}).get("error"):
            results["wheel"] = "not updated (RetroAchievements error, see the top)"
        # text, only for fields ES has empty
        match, _ = best_match(name, list(meta_db))
        if match:
            m = meta_db[match]; fill = {}
            for k in ("desc", "genre", "developer", "publisher"):
                if m.get(k) and not g.get(k):
                    fill[k] = m[k]
            if m.get("released") and not g.get("releasedate"):
                d = re.sub(r"[^0-9]", "", m["released"])[:8]
                if len(d) == 8:
                    fill["releasedate"] = d + "T000000"
            if fill:
                results["metadata"] = f"{dev.push_meta(gid, fill)} ({', '.join(fill)})"
            else:
                results["metadata"] = "kept"
        log("  " + "  ".join(f"{k}={v}" for k, v in results.items()))
    log("\ndone" + (" (dry run, nothing pushed)" if a.dry_run else ("" if not a.no_push else " (nothing pushed)")) +
        f"; files under {out}. Restart ES or open the game list again to see the new art.")


if __name__ == "__main__":
    main()
