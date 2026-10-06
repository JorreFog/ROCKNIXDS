"""3x internal resolution: the same little DS-style 3D scene rendered at 1x, 2x, and 3x supersampled into 2x.

The scene is rasterised for real with point sampling at each resolution (DS-sized textures, nearest filtering), and
3x is box-filtered 3:2 into the 2x frame, as Gengis Engine's 3x does. The panels show the same crop of each."""
import numpy as np
from lib import *

TEX = 16  # texels per checker texture side (the DS's small textures)


def tex_checker(a, b, n=TEX, seed=3):
    rnd = np.random.default_rng(seed)
    t = np.zeros((n, n, 3))
    for y in range(n):
        for x in range(n):
            c = a if ((x // 4) + (y // 4)) % 2 == 0 else b
            t[y, x] = np.array(c) * (0.92 + 0.08 * rnd.random())
    return t


FLOOR = tex_checker((214, 222, 230), (61, 142, 196))
GRASS = tex_checker((70, 150, 80), (56, 128, 66), seed=5)


def tri_mask(px, py, tri):
    (x0, y0), (x1, y1), (x2, y2) = tri
    d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    l0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / d
    l1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / d
    return (l0 >= 0) & (l1 >= 0) & (1 - l0 - l1 >= 0)


def render(W, H):
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    u = (xs + 0.5) / W          # 0..1 across
    v = (ys + 0.5) / H          # 0..1 down
    img = np.zeros((H, W, 3))
    # sky
    t = np.clip(v / 0.45, 0, 1)[..., None]
    img[:] = np.array((30, 40, 70)) * (1 - t) + np.array((120, 170, 220)) * t
    horizon = 0.42
    # distant hills (polygons)
    hills = [((-0.1, horizon), (0.18, 0.28), (0.42, horizon)), ((0.3, horizon), (0.62, 0.24), (0.95, horizon)),
             ((0.75, horizon), (1.0, 0.31), (1.2, horizon))]
    for i, tr in enumerate(hills):
        m = tri_mask(u, v, tr)
        img[m] = (52, 98, 102) if i != 1 else (44, 84, 90)
    # ground plane in perspective: z from the screen row, texture coordinates from the world
    g = v > horizon
    z = 0.18 / np.maximum(v - horizon, 1e-4)
    wx = (u - 0.5) * z * 2.2
    wz = z
    tx = np.floor(np.mod(wx * 3.0, 1.0) * TEX).astype(int)
    tz = np.floor(np.mod(wz * 3.0, 1.0) * TEX).astype(int)
    road = np.abs(wx - 0.5 + np.sin(wz * 1.3) * 0.25) < 0.45
    floor = np.where(road[..., None], FLOOR[tz, tx], GRASS[tz, tx])
    fog = np.clip((z - 0.6) / 4.0, 0, 0.7)[..., None]
    floor = floor * (1 - fog) + np.array((120, 170, 220)) * fog
    img[g] = floor[g]
    # a low-poly crystal and a pine tree: flat-shaded triangles, the edges that 3x smooths
    cx, cy, s = 0.79, 0.56, 0.1
    crystal = [
        (((cx, cy - s * 1.6), (cx - s * 0.6, cy - s * 0.2), (cx, cy)), (245, 80, 95)),
        (((cx, cy - s * 1.6), (cx, cy), (cx + s * 0.6, cy - s * 0.2)), (200, 40, 60)),
        (((cx - s * 0.6, cy - s * 0.2), (cx, cy + s * 0.9), (cx, cy)), (170, 30, 50)),
        (((cx, cy), (cx, cy + s * 0.9), (cx + s * 0.6, cy - s * 0.2)), (130, 20, 40)),
    ]
    tree = []
    for k, (ty, ts) in enumerate(((0.36, 0.09), (0.44, 0.11), (0.53, 0.13))):
        tree.append((((0.62, ty - 0.08), (0.62 - ts, ty + 0.06), (0.62, ty + 0.03)), (60, 160, 80)))
        tree.append((((0.62, ty - 0.08), (0.62, ty + 0.03), (0.62 + ts, ty + 0.06)), (40, 120, 64)))
    tree.insert(0, (((0.608, 0.55), (0.632, 0.55), (0.632, 0.66)), (110, 70, 40)))
    tree.insert(0, (((0.608, 0.55), (0.632, 0.66), (0.608, 0.66)), (110, 70, 40)))
    for tr, col in tree + crystal:
        img[tri_mask(u, v, tr)] = col
    # the edge marking DS games often use: a dark outline wherever the crystal meets anything else
    return img


def to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


def downsample_3_to_2(arr):
    """3:2 box filter (each 2x pixel covers 1.5x1.5 of the 3x pixels), as the 3x path's downsample."""
    H, W, _ = arr.shape
    out = np.zeros((H * 2 // 3, W * 2 // 3, 3))
    w = [(1.0, 0.5), (0.5, 1.0)]  # weights of the 2 source pixels each of the 2 output pixels in a 3-pixel group uses
    for oy in range(out.shape[0]):
        gy, py = divmod(oy, 2)
        sy = [gy * 3 + py, gy * 3 + py + 1]
        wy = w[py]
        for ox_par in (0, 1):
            pass
        rows = arr[sy[0]] * wy[0] + arr[sy[1]] * wy[1]
        rows /= 1.5
        g = rows.reshape(-1, 3, 3)
        out[oy, 0::2] = (g[:, 0] * 1.0 + g[:, 1] * 0.5) / 1.5
        out[oy, 1::2] = (g[:, 1] * 0.5 + g[:, 2] * 1.0) / 1.5
    return out


r1 = to_img(render(256, 192))
r2 = to_img(render(512, 384))
r3 = to_img(downsample_3_to_2(render(768, 576)))

# the same crop of each: the crystal and the road behind it
crop1 = (136, 64, 136 + 96, 64 + 72)
c1 = r1.crop(crop1)
c2 = r2.crop(tuple(v * 2 for v in crop1))
c3 = r3.crop(tuple(v * 2 for v in crop1))

PW, PH = 192, 144
W, H = 640, 270
a = Art(W, H)
a.vgrad(0, 0, W, H, [(0, BG1), (1, BG2)], bands=8)
a.stars(0, 0, W, 60, 60, seed=33)

a.text(16, 12, "3x", 32, BLUE_LT, medium=True, shadow=BG0, sh=(2, 2))
a.text(62, 12, "INTERNAL RESOLUTION", 16, INK, medium=True, shadow=BG0)
a.text(62, 32, "Gengis Engine draws the 3D at 768x576, then supersamples it into the panel.", 12, INK2)
logo(a, W - 16 - 96, 14, 12)
badge(a, W - 16 - 34, 32, "NEW", BLUE)

labels = [("1x", "256x192", "The DS itself", INK3), ("2x", "512x384", "ROCKNIXDS 1.5", INK2),
          ("3x", "768x576", "New in 1.6", BLUE_LT)]
for i, (img, (big, res, sub, col)) in enumerate(zip((c1, c2, c3), labels)):
    x = 16 + i * (PW + 12)
    y = 62
    last = i == 2
    a.rrect(x - 3 + 2, y - 3 + 2, PW + 6, PH + 6, fill=BG0, r=3)
    a.rrect(x - 3, y - 3, PW + 6, PH + 6, fill=BEZEL, outline=BLUE if last else BEZEL_LO, r=3)
    a.overlay(img, x, y, PW, PH, resample=Image.NEAREST)
    a.text(x, y + PH + 8, big, 24, col, medium=True, shadow=BG0)
    a.text(x + 34, y + PH + 9, res, 12, INK, medium=True)
    a.text(x + 34, y + PH + 22, sub, 12, INK3)
    if last:
        badge(a, x + PW - 70, y + PH + 12, "SMOOTHER", BLUE)

a.text(16, H - 18, "One scene, the same crop, rendered at each resolution. 3x: 2.25 samples for every pixel on the panel.", 12, INK3)
a.render(os.path.join(OUT, "3x-resolution.png"))
