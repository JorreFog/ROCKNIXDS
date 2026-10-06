#!/usr/bin/env python3
"""screenshots.py: the pictures of Döda Kvarter, rendered by the game itself (tests/scene.c, headless).

    python3 tools/screenshots.py        build/scene must exist (tests/run.sh builds it)

Writes device/media/dodakvarter-{image,thumb,marquee}.png (EmulationStation's Ports entry) and docs/img/*.png
(the README). Needs Pillow."""
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCENE = os.path.join(ROOT, "build", "scene")


def render(name, season=1, seed=5, size="640x480", sv=False):
    out = tempfile.mktemp(suffix=".png")
    env = dict(os.environ, DK_HEADLESS_SIZE=size, DK_DATA=tempfile.mkdtemp())
    if sv:
        env["DK_LANG_SV"] = "1"
    subprocess.run([SCENE, name, out, str(season), str(seed)], check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    im = Image.open(out).convert("RGB")
    os.unlink(out)
    return im


def top(im, h=240):
    return im.crop((0, 0, im.width, h))


def x2(im):
    return im.resize((im.width * 2, im.height * 2), Image.NEAREST)


def main():
    if not os.path.exists(SCENE):
        sys.exit("build/scene first (tests/run.sh)")
    media = os.path.join(ROOT, "device", "media")
    img = os.path.join(ROOT, "docs", "img")
    os.makedirs(media, exist_ok=True)
    os.makedirs(img, exist_ok=True)
    title = render("title")
    horde = render("horde", season=1)
    x2(top(horde)).save(os.path.join(media, "dodakvarter-image.png"), optimize=True)
    x2(top(title)).save(os.path.join(media, "dodakvarter-thumb.png"), optimize=True)
    x2(top(title).crop((60, 14, 260, 96))).save(os.path.join(media, "dodakvarter-marquee.png"), optimize=True)
    # the README: both screens, as the handheld shows them
    shots = {
        "title": title, "horde-winter": horde, "horde-autumn": render("horde", season=0, seed=9),
        "midsummer": render("horde", season=2, seed=12), "wolves": render("wolves", season=0, seed=3),
        "moose": render("moose", season=1, seed=8), "box": render("box", season=2, seed=4),
        "loot": render("loot", season=0, seed=6), "gameover": render("gameover", season=1, seed=5),
        "scores": render("scores", sv=True), "plus": render("horde", season=1, seed=5, size="1024x768"),
    }
    for k, v in shots.items():
        (v if k == "plus" else x2(v)).save(os.path.join(img, k + ".png"), optimize=True)
    print("wrote", len(shots), "screenshots and the ES media")


if __name__ == "__main__":
    main()
