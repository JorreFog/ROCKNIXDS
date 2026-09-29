import numpy as np
from PIL import Image
def load(p):
    d = open(p, "rb").read(); nl = d.index(b"\n"); w, h, pitch, bpp = map(int, d[:nl].split()[:4]); px = d[nl + 1:]
    a = np.frombuffer(px, np.uint8)[: pitch * h].reshape(h, pitch)[:, : w * 4].reshape(h, w, 4)
    return a[:, :, 2::-1].copy()          # XRGB little-endian -> RGB
def ppm(p): return np.asarray(Image.open(p).convert("RGB"))
