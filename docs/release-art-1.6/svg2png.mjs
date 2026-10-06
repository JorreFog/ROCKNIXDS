// svg2png.mjs in.svg out.png height: an SVG drawn by Chromium at the given pixel height, transparent background.
// (ImageMagick here can't read SVG; Playwright's Chromium is what dii-ess-aye/rnds uses for its assets too.)
import { createRequire } from "module";
import { execSync } from "child_process";
import { readFileSync } from "fs";
const require = createRequire(execSync("npm root -g").toString().trim() + "/");
const { chromium } = require("playwright");
const [src, out, h] = process.argv.slice(2);
const svg = readFileSync(src, "utf8");
const m = svg.match(/viewBox="([\d.\s-]+)"/);
const [, , vw, vh] = m[1].trim().split(/\s+/).map(Number);
const H = Number(h), W = Math.ceil(H * vw / vh);
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: W, height: H } });
const body = svg.replace(/<svg([^>]*?)width="[^"]*"([^>]*?)height="[^"]*"/, `<svg$1width="${W}"$2height="${H}"`);
await page.setContent(`<html><body style="margin:0;background:transparent">${body}</body></html>`);
await page.screenshot({ path: out, omitBackground: true, clip: { x: 0, y: 0, width: W, height: H } });
await browser.close();
