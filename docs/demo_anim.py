#!/usr/bin/env python3
"""The 1.4 README's animation (since 1.5 docs/pixel_demo.py writes the README's demo-*.webp): EmulationStation on both screens, then a DS game resuming where it was quit, framed as
the clamshell ds_frame.py draws. Writes docs/img/demo-light.webp and demo-dark.webp (animated WebP: full colour and
a soft shadow, which a GIF's 256 colours and 1-bit transparency can't do).

usage: demo_anim.py <es-dir> <game-dir> [first-game-frame last-game-frame]
  <es-dir>:   NNN.ppm grim captures (1280x480, top panel left) + "times" (one ns timestamp per frame)
  <game-dir>: comp/NNN.png 1280x480 composites of libdsflip's scanout dumps (+ overlay) + "times"
"""
import os
import sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ds_frame import frame  # noqa: E402

SCALE = 0.5
BG = {"light": (255, 255, 255), "dark": (13, 17, 23)}      # GitHub's page backgrounds


def clip(files, times, lo=0, hi=None):
    t = [int(x) for x in open(times).read().split()]
    files, t = files[lo:hi], t[lo:hi]
    durs = [max(40, (b - a) // 1_000_000) for a, b in zip(t, t[1:])] + [120]
    return list(zip(files, durs))


def main():
    es, game = sys.argv[1], sys.argv[2]
    lo, hi = (int(sys.argv[3]), int(sys.argv[4]) + 1) if len(sys.argv) > 4 else (0, None)
    esf = sorted(os.path.join(es, f) for f in os.listdir(es) if f.endswith(".ppm"))
    gf = sorted(os.path.join(game, "comp", f) for f in os.listdir(os.path.join(game, "comp")) if f.endswith(".png"))
    seq = clip(esf, os.path.join(es, "times")) + clip(gf, os.path.join(game, "times"), lo, hi)
    framed = []
    for path, dur in seq:
        im = frame(path)
        framed.append((im.resize((round(im.width * SCALE), round(im.height * SCALE)), Image.LANCZOS), dur))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
    for name, bg in BG.items():
        imgs = []
        for im, _ in framed:
            flat = Image.new("RGB", im.size, bg)
            flat.paste(im, (0, 0), im)
            imgs.append(flat)
        p = os.path.join(out, f"demo-{name}.webp")
        imgs[0].save(p, save_all=True, append_images=imgs[1:], duration=[d for _, d in framed], loop=0,
                     quality=80, method=6)
        print(p, os.path.getsize(p) // 1024, "KB,", len(imgs), "frames,", sum(d for _, d in framed) / 1000, "s")


if __name__ == "__main__":
    main()
