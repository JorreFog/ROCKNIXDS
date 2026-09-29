#!/usr/bin/env python3
"""Quality metrics for every DraStic shader on matched real frames (see the session notes for how they were made)."""
import json, os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rawio import load, ppm

HERE = os.path.dirname(os.path.abspath(__file__))
SC, OUT = os.path.join(HERE, "..", "sc"), os.path.join(HERE, "out")
SHADERS = ["null", "sharp-bilinear", "sharp-shimmerless", "quilez", "ds-crisp", "ds-fsr", "ds-integer",
           "lcd3x", "lcd1x-nds-color", "scanlines", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-grid-2x"]
STYLE = {"lcd3x", "lcd1x-nds-color", "scanlines", "ds-crisp-color", "ds-grid", "ds-grid-color", "ds-grid-2x"}


_AM = {}
def area_matrix(n_out, n_in):
    if (n_out, n_in) not in _AM: _AM[(n_out, n_in)] = _area_matrix(n_out, n_in)
    return _AM[(n_out, n_in)]


def _area_matrix(n_out, n_in):
    """rows: output pixels, weights = overlap of [i, i+1) * (n_in/n_out) with source pixels"""
    s = n_in / n_out
    W = np.zeros((n_out, n_in))
    for i in range(n_out):
        a, b = i * s, (i + 1) * s
        for j in range(int(np.floor(a)), min(n_in, int(np.ceil(b)))):
            W[i, j] = max(0.0, min(b, j + 1) - max(a, j))
    return W / W.sum(1, keepdims=True)


def resample(img, w, h):
    f = img.astype(float)
    Wy, Wx = area_matrix(h, f.shape[0]), area_matrix(w, f.shape[1])
    return np.stack([Wy @ f[..., c] @ Wx.T for c in range(f.shape[2])], -1)


def luma(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def blur(a, k):
    r = len(k) // 2
    p = np.pad(a, ((r, r), (0, 0)), mode="edge"); a = sum(k[i] * p[i:i + a.shape[0]] for i in range(len(k)))
    p = np.pad(a, ((0, 0), (r, r)), mode="edge"); return sum(k[i] * p[:, i:i + a.shape[1]] for i in range(len(k)))


G = np.exp(-0.5 * (np.arange(11) - 5) ** 2 / 1.5 ** 2); G /= G.sum()


def ssim(x, y):
    x, y = luma(x), luma(y)
    mx, my = blur(x, G), blur(y, G)
    vx, vy, cxy = blur(x * x, G) - mx * mx, blur(y * y, G) - my * my, blur(x * y, G) - mx * my
    C1, C2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    return float((((2 * mx * my + C1) * (2 * cxy + C2)) / ((mx * mx + my * my + C1) * (vx + vy + C2))).mean())


def psnr(x, y):
    m = ((x.astype(float) - y) ** 2).mean()
    return 99.0 if m == 0 else float(10 * np.log10(255 ** 2 / m))


def grad(a):
    y = luma(a)
    gx = np.abs(np.diff(y, axis=1)).mean(); gy = np.abs(np.diff(y, axis=0)).mean()
    return gx + gy


def overshoot(out, src):
    """mean amount (0-255) by which output pixels leave the range of the 2x2 source texels around them"""
    h, w = out.shape[:2]; sh, sw = src.shape[:2]
    y = np.clip(((np.arange(h) + 0.5) * sh / h - 0.5), 0, sh - 1); x = np.clip(((np.arange(w) + 0.5) * sw / w - 0.5), 0, sw - 1)
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int); y1, x1 = np.minimum(y0 + 1, sh - 1), np.minimum(x0 + 1, sw - 1)
    s = src.astype(float)
    q = [s[y0][:, x0], s[y0][:, x1], s[y1][:, x0], s[y1][:, x1]]
    lo, hi = np.minimum.reduce(q), np.maximum.reduce(q)
    o = out.astype(float)
    return float((np.maximum(0, o - hi) + np.maximum(0, lo - o)).mean())


def result(o, src, refs):
    r = {"sharp": grad(o) / grad(refs["faithful"]), "overshoot": overshoot(o, src),
         "roundtrip": psnr(resample(o, src.shape[1], src.shape[0]), src)}
    for k, ref in refs.items():
        r["psnr_" + k] = psnr(o, ref); r["ssim_" + k] = ssim(o, ref)
    return r


def main():
    res = {}
    for g in ("hg", "b2", "pt"):
        for scr in ("0", "1"):
            hi = load(f"{SC}/{g}-a-2x-{scr}.raw")
            for rz in ("1x", "2x"):
                src = load(f"{SC}/{g}-a-{rz}-{scr}.raw")
                refs = {"faithful": resample(src, 640, 480)}
                if rz == "1x" and scr == "0": refs["detail"] = resample(hi, 640, 480)
                for s in SHADERS:
                    o = ppm(f"{OUT}/{g}-a-{rz}-{scr}.{s}.ppm")
                    if s == "ds-integer":        # 2x integer in a 512x384 viewport
                        o = o[48:48 + 384, 64:64 + 512]
                        rr = {"faithful": resample(src, 512, 384)}
                        if "detail" in refs: rr["detail"] = resample(hi, 512, 384)
                        r = result(o, src, rr)
                    else:
                        r = result(o, src, refs)
                    res[f"{g}/{'top' if scr == '0' else 'bottom'}/{rz}/{s}"] = r
    json.dump(res, open(os.path.join(HERE, "metrics.json"), "w"), indent=1)

    def table(title, rz, scr, keys):
        print(f"\n## {title}")
        print("| shader | " + " | ".join(keys) + " |"); print("|---" * (len(keys) + 1) + "|")
        for s in SHADERS:
            rows = [res[f"{g}/{scr}/{rz}/{s}"] for g in ("hg", "b2", "pt")]
            vals = [np.mean([r[k] for r in rows]) for k in keys]
            fmt = ["%.3f" % v if k.startswith("ssim") or k == "sharp" else "%.2f" % v for k, v in zip(keys, vals)]
            print(f"| {s}{' (style)' if s in STYLE else ''} | " + " | ".join(fmt) + " |")
    table("Top screen (3D), 1x input -> 640x480: vs the 2x render (real detail) and vs faithful squares", "1x", "top",
          ["psnr_detail", "ssim_detail", "psnr_faithful", "ssim_faithful", "sharp", "overshoot", "roundtrip"])
    table("Top screen (3D), 2x input (hires) -> 640x480", "2x", "top", ["psnr_faithful", "ssim_faithful", "sharp", "overshoot", "roundtrip"])
    table("Bottom screen (2D pixel art), 1x -> 640x480", "1x", "bottom", ["psnr_faithful", "ssim_faithful", "sharp", "overshoot", "roundtrip"])
    table("Bottom screen (2D pixel art), 2x input -> 640x480", "2x", "bottom", ["psnr_faithful", "ssim_faithful", "sharp", "overshoot", "roundtrip"])


if __name__ == "__main__":
    main()
