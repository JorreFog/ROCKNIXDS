import sys, numpy as np, potrace
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
K = 4
im = im.resize((im.width*K, im.height*K), Image.LANCZOS)
a = np.asarray(im).astype(float)
alpha = a[..., 3] / 255
red = a[..., 0] - a[..., 2] > 40        # ROCK is red, NIX is grey
def trace(mask):
    bm = potrace.Bitmap(~mask)
    plist = bm.trace(turdsize=8, alphamax=1.0, opticurve=True, opttolerance=0.2)
    d = []
    for c in plist:
        s = c.start_point; d.append(f"M{s.x/K:.2f},{s.y/K:.2f}")
        for seg in c.segments:
            if seg.is_corner:
                d.append(f"L{seg.c.x/K:.2f},{seg.c.y/K:.2f}L{seg.end_point.x/K:.2f},{seg.end_point.y/K:.2f}")
            else:
                d.append(f"C{seg.c1.x/K:.2f},{seg.c1.y/K:.2f} {seg.c2.x/K:.2f},{seg.c2.y/K:.2f} {seg.end_point.x/K:.2f},{seg.end_point.y/K:.2f}")
        d.append("Z")
    return "".join(d)
on = alpha > 0.5
# bounding box of ink, so the SVG has no padding
ys, xs = np.where(on)
x0, y0, x1, y1 = xs.min()/K, ys.min()/K, (xs.max()+1)/K, (ys.max()+1)/K
print(f"{x0} {y0} {x1} {y1}")
open(sys.argv[2], "w").write(f'<path d="{trace(on & red)}" fill="RED"/>\n<path d="{trace(on & ~red)}" fill="GREY"/>\n')
