#!/usr/bin/env python3
"""cartart.py <LaunchBox Metadata.xml> <games.json> <outdir>

Real DS cartridge images from the LaunchBox Games Database (https://gamesdb.launchbox-app.com/Metadata.zip,
community-contributed "Cart - Front" images). For each game in games.json (ES's /systems/nds/games output) it finds
the LaunchBox entry by name on the Nintendo DS platform, picks the best cart front (clean cut-outs with
transparency first, then region: North America, Europe, Australia, ...), trims it to the card and fits it to the
theme's card shape. Writes <outdir>/<ES game id>.png (for ES's "cartridge" media) and prints what it chose.
"""
import json, os, re, sys, unicodedata, urllib.request, xml.etree.ElementTree as ET
from io import BytesIO
from PIL import Image

CARD_ASPECT = 0.9174            # the theme's card area (w/h), the proportions of real DS card scans
OUT_H = 480
REGIONS = ["North America", "United States", "World", "Europe", "United Kingdom", "Australia", "Canada",
           "Germany", "France", "Spain", "Italy", "Japan", "Korea"]
UA = "rgds-theme/1.0 (ROCKNIX; RG DS) cartart"


def norm(name):
    n = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().lower()
    n = re.sub(r"\(.*?\)|\[.*?\]", "", n)
    return re.sub(r"\s+", " ", re.sub(r"[^a-z0-9 ]", " ", n)).strip()


def load_db(path, wanted):
    games, imgs = {}, {}
    for _, el in ET.iterparse(path, events=("end",)):
        if el.tag == "Game":
            if el.findtext("Platform") == "Nintendo DS":
                n = norm(el.findtext("Name") or "")
                if n in wanted:
                    games.setdefault(n, el.findtext("DatabaseID"))
            el.clear()
        elif el.tag == "GameImage":
            if el.findtext("Type") == "Cart - Front":
                imgs.setdefault(el.findtext("DatabaseID"), []).append((el.findtext("Region") or "", el.findtext("FileName")))
            el.clear()
    return games, imgs


def pick(cands):
    def key(c):
        region, fn = c
        rank = REGIONS.index(region) if region in REGIONS else len(REGIONS)
        return (0 if fn.lower().endswith(".png") else 1, rank)   # cut-outs are PNG with alpha
    return sorted(cands, key=key)


def fit(img):
    """Trim to the card (alpha), then scale to the card area's exact shape."""
    img = img.convert("RGBA")
    a = img.getchannel("A")
    bbox = a.point(lambda v: 255 if v > 16 else 0).getbbox()
    if bbox:
        img = img.crop(bbox)
    return img.resize((round(OUT_H * CARD_ASPECT), OUT_H), Image.LANCZOS)


def main():
    db, games_json, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    games = json.load(open(games_json))
    wanted = {norm(g["name"]): g for g in games}
    ids, imgs = load_db(db, set(wanted))
    for n, g in wanted.items():
        dbid = ids.get(n)
        cands = pick(imgs.get(dbid, [])) if dbid else []
        for region, fn in cands:
            try:
                req = urllib.request.Request("https://images.launchbox-app.com/" + fn, headers={"User-Agent": UA})
                img = Image.open(BytesIO(urllib.request.urlopen(req, timeout=30).read()))
            except OSError:
                continue
            if img.mode != "RGBA" and any(c[1].lower().endswith(".png") for c in cands):
                continue                  # a photo on a background; a cut-out exists
            fitted = fit(img)
            fitted.info.pop("icc_profile", None)   # SDL_image rejects some iCCP profiles and ES then drops the cart
            fitted.save(os.path.join(out, g["id"] + ".png"))
            print(f"{g['name']}: {region} {fn}")
            break
        else:
            print(f"{g['name']}: no cart image (LaunchBox id {dbid})")


if __name__ == "__main__":
    main()
