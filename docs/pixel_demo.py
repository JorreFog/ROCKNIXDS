#!/usr/bin/env python3
"""The README's ROCKNIXDS Pixel images, from the rnds host harness (dii-ess-aye/rnds/test): an animated tour (home
shelf, the DS library with its bobbing box art, a launch with the cartridge going into the slot) and framed stills.
Pixel light for GitHub's light page, Pixel dark for the dark one; framed as the clamshell ds_frame.py draws.

usage: pixel_demo.py <harness> <assets: themes/rocknixds-pixel-dark/rnds> <mockup dir> <work dir>
"""
import os
import subprocess
import sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ds_frame import frame  # noqa: E402

SCALE = 0.5
BG = {"light": (255, 255, 255), "dark": (13, 17, 23)}      # GitHub's page backgrounds
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")


def tour():
    cmds, durs = ["home 0"], []

    def shots(ms, step=80):
        for _ in range(0, ms, step):
            cmds.extend([f"wait {step}", f"shot f{len(durs):03d}"])
            durs.append(step)
    shots(1500)
    for b in ("right", "right", "left", "left"):
        cmds.append("press " + b)
        shots(640)
    shots(300)
    cmds.append("press a")
    shots(1900)
    for _ in range(2):
        cmds.append("press right")
        shots(1100)
    cmds.append("press a")
    shots(1500, 60)
    durs[-1] = 1600                                       # hold the inserted cartridge before looping
    return "; ".join(cmds), durs


STILLS = "home 0; wait 3000; shot home; lib 0 2; wait 3000; shot games; press a; wait 700; shot insert"


def pair(work, name):
    im = Image.new("RGB", (1280, 480))
    im.paste(Image.open(f"{work}{name}-top.png"), (0, 0))
    im.paste(Image.open(f"{work}{name}-bot.png"), (640, 0))
    p = f"{work}{name}.png"
    im.save(p)
    f = frame(p)
    return f.resize((round(f.width * SCALE), round(f.height * SCALE)), Image.LANCZOS)


def flat(im, bg):
    out = Image.new("RGB", im.size, bg)
    out.paste(im, (0, 0), im)
    return out


def main():
    harness, assets, mock, work = sys.argv[1:5]
    script, durs = tour()
    for v in BG:
        d = os.path.join(work, v) + "/"
        os.makedirs(d, exist_ok=True)
        subprocess.run([harness, assets, mock, "1", d, script, v], check=True, stderr=subprocess.DEVNULL)
        imgs = [flat(pair(d, f"f{i:03d}"), BG[v]) for i in range(len(durs))]
        p = os.path.join(OUT, f"demo-{v}.webp")
        imgs[0].save(p, save_all=True, append_images=imgs[1:], duration=durs, loop=0, quality=80, method=6)
        print(p, os.path.getsize(p) // 1024, "KB,", len(imgs), "frames")
        subprocess.run([harness, assets, mock, "1", d, STILLS, v], check=True, stderr=subprocess.DEVNULL)
        for s in ("home", "games", "insert"):
            p = os.path.join(OUT, f"pixel-{v}-{s}.png")
            pair(d, s).save(p, optimize=True)
            print(p, os.path.getsize(p) // 1024, "KB")


if __name__ == "__main__":
    main()
