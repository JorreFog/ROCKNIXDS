"""Discord server icons from the standard logo (logo/rocknixds-logo-stack.svg), as SVG and 512x512 PNG.

Discord shows a server icon as a circle, as small as 32 px, so everything sits inside the inscribed circle:
  discord-icon-mark   the two screens and DS (the stacked logo's lower half): readable at any size
  discord-icon-full   the whole stacked logo, ROCKNIX above"""
import os, re, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "1.6 artwork", "discord")
src = open(os.path.join(REPO, "logo/rocknixds-logo-stack.svg")).read()
inner = re.search(r"<svg[^>]*>(.*)</svg>", src, re.S).group(1)
BG = "#131921"
# the content box of each icon, in the stack logo's own coordinates (viewBox 0 0 354.99 219.97), and its width
# inside the 512 square (the circle's diameter is 512; the corners of the box stay inside it)
ICONS = {
    "discord-icon-mark": ((12.0, 93.0, 331.0, 115.0), 380),
    "discord-icon-full": ((12.0, 8.0, 331.0, 200.0), 340),
}
os.makedirs(OUT, exist_ok=True)
for name, ((x, y, w, h), size) in ICONS.items():
    sw, sh = size, size * h / w
    ox, oy = (512 - sw) / 2, (512 - sh) / 2
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">'
           f'<rect width="512" height="512" fill="{BG}"/>'
           f'<svg x="{ox:.2f}" y="{oy:.2f}" width="{sw:.2f}" height="{sh:.2f}" viewBox="{x} {y} {w} {h}">{inner}</svg></svg>')
    p = os.path.join(OUT, name + ".svg")
    open(p, "w").write(svg)
    subprocess.run(["node", os.path.join(HERE, "svg2png.mjs"), p, os.path.join(OUT, name + ".png"), "512"], check=True)
    print(name)
