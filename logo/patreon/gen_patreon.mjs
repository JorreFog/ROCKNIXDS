// Patreon art for ROCKNIXDS, rendered by Chromium from the logos next door (../rocknixds-logo*.svg, made by make_logo.py)
// in the Pixel theme's look: a dark dot-grid panel with a soft red glow behind the mark.
//   node gen_patreon.mjs      (writes, next to this script:)
//     rocknixds-patreon-avatar.png   1024x1024, the stacked logo inside the circle Patreon crops the avatar to
//     rocknixds-patreon-icon.png     1024x1024, the two-screen icon alone, for spots too small for the wordmark
//     rocknixds-patreon-cover.png    1600x400, the one-line logo and a tagline (Patreon's cover size)
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
import fs from 'fs';
import path from 'path';
const here = path.dirname(new URL(import.meta.url).pathname);
const b64 = f => fs.readFileSync(f).toString('base64');
const svg = f => `data:image/svg+xml;base64,${b64(path.join(here, '..', f))}`;
const font = `data:font/ttf;base64,${b64(path.join(here, '../../dii-ess-aye/themes/rocknixds-pixel-dark/rnds/fonts/PixelifySans-Medium.ttf'))}`;
const RED = '#f5303f', GREY = '#8f8f8f';

// The DS icon from make_logo.py's icon(), alone: red screen over grey screen.
const icon = (() => {
  const cap = 100, w = 0.69 * cap, h = 0.54 * cap, gap = 0.07 * cap, s = 0.085 * cap, r = 0.11 * cap;
  const rect = (y, c) => `<rect x="${s / 2}" y="${y + s / 2}" width="${w - s}" height="${h - s}" rx="${r - s / 2}" fill="none" stroke="${c}" stroke-width="${s}"/>`;
  const H = 2 * h + gap;
  return `data:image/svg+xml;base64,${Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${H}" viewBox="0 0 ${w} ${H}">${rect(0, RED)}${rect(h + gap, GREY)}</svg>`).toString('base64')}`;
})();

const dots = (cell, dot) => `url("data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' width='${cell}' height='${cell}' shape-rendering='crispEdges'><rect width='${dot}' height='${dot}' fill='rgba(255,255,255,.07)'/><rect x='${cell / 2}' y='${cell / 2}' width='${dot}' height='${dot}' fill='rgba(255,255,255,.035)'/></svg>`)}")`;

const page = (w, h, cell, body) => `<html><head><style>
@font-face { font-family: PxM; src: url(${font}); }
* { margin: 0; padding: 0; box-sizing: border-box; }
body { width: ${w}px; height: ${h}px; overflow: hidden; position: relative;
  background: radial-gradient(ellipse at 50% 50%, rgba(245,48,63,.20) 0%, rgba(245,48,63,.06) 38%, rgba(0,0,0,0) 65%),
              radial-gradient(ellipse at 50% 120%, #1b2330 0%, rgba(0,0,0,0) 70%), #0e1218; }
body::before { content: ''; position: absolute; inset: 0; background-image: ${dots(cell, cell / 4)}; background-size: ${cell}px ${cell}px; }
.c { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; }
img { display: block; filter: drop-shadow(0 ${cell}px ${cell * 2}px rgba(0,0,0,.55)); }
.tag { font-family: PxM; color: #c9d3de; letter-spacing: .04em; }
.tag b { color: ${RED}; font-weight: inherit; }
</style></head><body><div class="c">${body}</div></body></html>`;

const shots = [
  // Avatar: Patreon crops to a circle, so the 355x220 stack sits well inside it (diagonal < 80% of the width).
  ['rocknixds-patreon-avatar.png', 1024, 1024, page(1024, 1024, 16, `<img src="${svg('rocknixds-logo-stack.svg')}" style="width:700px">`)],
  ['rocknixds-patreon-icon.png', 1024, 1024, page(1024, 1024, 16, `<img src="${icon}" style="height:560px">`)],
  ['rocknixds-patreon-cover.png', 1600, 400, page(1600, 400, 12,
    `<img src="${svg('rocknixds-logo.svg')}" style="width:860px">
     <div class="tag" style="font-size:34px;margin-top:30px">Full-speed DS <b>&middot;</b> two screens <b>&middot;</b> ROCKNIX on the RG DS</div>`)],
];

const b = await chromium.launch();
for (const [name, w, h, html] of shots) {
  const p = await b.newPage({ viewport: { width: w, height: h } });
  await p.setContent(html);
  await p.evaluate(() => document.fonts.ready);
  await p.screenshot({ path: path.join(here, name) });
  await p.close();
  console.log(name, `${w}x${h}`);
}
await b.close();
