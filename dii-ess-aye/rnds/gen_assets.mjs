// Renders the mockup's status-bar pictures (wifi, battery outline, ROCKNIXDS logo) with Chromium at 1x (RG DS) and
// 1.6x (RG DS Plus), so the engine draws exactly what the browser draws.
//   node gen_assets.mjs <mockup dir> <out dir>
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
import fs from 'fs';
const [mockup, out] = process.argv.slice(2);
const wifi = '<svg viewBox="0 0 16 14" width="18" height="16"><path d="M1 5.2a8 8 0 0 1 14 0" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="square"/><path d="M3.6 8a4.6 4.6 0 0 1 8.8 0" fill="none" stroke="#fff" stroke-width="2"/><rect x="6.4" y="10.2" width="3.2" height="3.2" fill="#fff"/></svg>';
// the mockup's battery without its level bar: the engine draws the bar from the real charge
const batt = '<svg viewBox="0 0 26 14" width="26" height="14"><rect x="1" y="1" width="20" height="12" fill="none" stroke="#fff" stroke-width="2"/><rect x="22" y="4" width="3" height="6" fill="#fff"/></svg>';
const logo = `<img src="data:image/svg+xml;base64,${fs.readFileSync(mockup + '/logo.svg').toString('base64')}" style="height:20px;width:auto;display:block">`;
const items = { wifi: [wifi, 18, 16], battery: [batt, 26, 14], logo: [logo, 134.23, 20] };
const b = await chromium.launch();
for (const [scale, sfx] of [[1, '@1x'], [1.6, '@1.6x']]) {
  const p = await b.newPage({ viewport: { width: 400, height: 100 }, deviceScaleFactor: scale });
  for (const [name, [html, w, h]] of Object.entries(items)) {
    await p.setContent(`<html><body style="margin:0;background:transparent"><div id="a" style="position:absolute;left:0;top:0;width:${w}px;height:${h}px">${html}</div></body></html>`);
    await p.waitForTimeout(50);
    const W = Math.ceil(w * scale), H = Math.ceil(h * scale);
    await p.screenshot({ path: `${out}/${name}${sfx}.png`, omitBackground: true, clip: { x: 0, y: 0, width: W / scale, height: H / scale } });
  }
  await p.close();
}
await b.close();
