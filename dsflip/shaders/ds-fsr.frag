// ds-fsr -- AMD FidelityFX Super Resolution 1.0 (EASU) for DraStic on the RG DS, fast enough for both panels.
//
// The DS screen is 256x192 (512x384 with hires 3D) and each panel 640x480: a 2.5x (1.25x at 2x) scale that no
// integer mapping can fill. EASU reconstructs edges along their own direction instead of blurring across them,
// then clamps to the 2x2 texels around the sample (no ringing) and adds a light sharpen. Smooth rather than
// pixel-sharp: for pixel-exact sharpness use ds-crisp.
//
// Same algorithm as the fsr.frag port (the RetroArch/melonDS EASU), made ~2x cheaper on this Mali-G52 without
// changing the result (measured with dsflip/shtest, pipelined GPU time per 640x480 panel at 800 MHz):
//   fsr.frag  9.7 ms  (19.3 ms per frame for both panels: every other frame dropped)
//   ds-fsr    4.8 ms  (worst case, a noise frame; ~9.6 ms per frame)
//   ds-crisp  1.9 ms
// How: the four edge analyses and the twelve tap weights are computed as vec4 over four taps/corners at a time,
// which lets the compiler use packed fp16 (two half-precision operations per lane), and the tap weight
// w = (1.5625 (0.4 d2 - 1)^2 - 0.5625) (lob d2 - 1)^2 is factored to (0.25 d2 - 1)(d2 - 1)(lob d2 - 1)^2 with the
// rotation folded into per-pixel coefficients. Output matches the reference EASU to within 2/255 (65 dB) on
// real frames. Content-independent cost: no branches, so a busy scene costs what a flat one does.
//
// Positions are highp on purpose: mediump is fp16 here, which snaps a texel coordinate past x=256 to quarter
// texels (see ds-crisp.frag). fsr.frag computed them in mediump; that alone changed 13% of its output values.
//
// FSR 1.0: Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
// copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
// LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

precision mediump float;
varying highp vec2 v_texcoord;
uniform sampler2D u_texture;
uniform highp vec2 u_texture_size;
const float kSharpness = 0.35;
void main()
{
    // the 2x2 quad f g / j k around the sample, t = position inside it
    highp vec2 invSize = 1.0 / u_texture_size;
    highp vec2 pp = v_texcoord * u_texture_size - 0.5;
    highp vec2 fp = floor(pp);
    vec2 t = pp - fp;
    highp vec2 base = (fp + 0.5) * invSize;
#define TX(ox, oy) SWIZ(texture2D(u_texture, base + vec2(ox, oy) * invSize)).rgb
    //        b c
    //      e f g h
    //      i j k l
    //        n o
    vec3 sb = TX(0.0, -1.0);
    vec3 sc = TX(1.0, -1.0);
    vec3 se = TX(-1.0, 0.0);
    vec3 sf = TX(0.0, 0.0);
    vec3 sg = TX(1.0, 0.0);
    vec3 sh = TX(2.0, 0.0);
    vec3 si = TX(-1.0, 1.0);
    vec3 sj = TX(0.0, 1.0);
    vec3 sk = TX(1.0, 1.0);
    vec3 sl = TX(2.0, 1.0);
    vec3 sn = TX(0.0, 2.0);
    vec3 so = TX(1.0, 2.0);
    float lb = sb.g;
    float lc = sc.g;
    float le = se.g;
    float lf = sf.g;
    float lg = sg.g;
    float lh = sh.g;
    float li = si.g;
    float lj = sj.g;
    float lk = sk.g;
    float ll = sl.g;
    float ln = sn.g;
    float lo = so.g;
    // edge direction and length: EASU's per-corner analysis (luma = green) for f, g, j, k at once, bilinearly
    // weighted by the sample position. For corner X: lA = above, lB = left, lC = X, lD = right, lE = below.
    vec4 lA = vec4(lb, lc, lf, lg), lB = vec4(le, lf, li, lj), lC = vec4(lf, lg, lj, lk);
    vec4 lD = vec4(lg, lh, lk, ll), lE = vec4(lj, lk, ln, lo);
    vec4 dX = lD - lB, dY = lE - lA;
    vec4 lenX = clamp(abs(dX) / max(max(abs(lD - lC), abs(lC - lB)), vec4(1.0 / 32768.0)), 0.0, 1.0);
    vec4 lenY = clamp(abs(dY) / max(max(abs(lE - lC), abs(lC - lA)), vec4(1.0 / 32768.0)), 0.0, 1.0);
    vec4 w4 = vec4((1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y);
    vec2 dir = vec2(dot(dX, w4), dot(dY, w4));
    float len = dot(lenX * lenX + lenY * lenY, w4) * 0.5;
    // kernel shape: rotated to the edge, stretched along it, lobe narrowed where the edge is strong
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zeroDir = dirR < (1.0 / 32768.0);
    dirR = inversesqrt(max(dirR, 1.0 / 32768.0));
    dir *= dirR;

    float lenSq = len * len;
    float lenScaled = 2.0 - lenSq;
    float stretch = (dir.x * dir.x + dir.y * dir.y) == 0.0 ? 1.0
                  : (dir.x * dir.x) / max(abs(dir.x), abs(dir.y));
    stretch = max(stretch, 1.0);
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * lenScaled * 0.5, 1.0 - 0.5 * lenSq);
    if (zeroDir) {
        dir = vec2(1.0, 0.0);
        len2 = vec2(1.0, 1.0);
    }

    float lob = 0.5 - 0.29 * lenSq;
    float clp = 1.0 / max(lob, 0.0001);
    vec2 pos = t;

    // the 12 taps, 4 at a time. Weights w = (0.25 d2 - 1)(d2 - 1)(lob d2 - 1)^2 with d2 = |v|^2 clipped.
    // Kept exactly as measured: equivalent rewrites (a helper function, one long sum, folded temporaries)
    // compiled 0.4 ms per panel slower on this Mali.
    vec2 cx = dir * len2.x;                     /* vx = (o - pos) . dir * len2.x */
    vec2 cy = vec2(-dir.y, dir.x) * len2.y;     /* vy = (o - pos) . perp(dir) * len2.y */
    float kx = dot(pos, cx), ky = dot(pos, cy);
    vec3 accumulated = vec3(0.0);
    vec4 wsum = vec4(0.0);
    {
        vec4 vx = vec4(0.0, 1.0, -1.0, 0.0) * cx.x + vec4(-1.0, -1.0, 0.0, 0.0) * cx.y - kx;
        vec4 vy = vec4(0.0, 1.0, -1.0, 0.0) * cy.x + vec4(-1.0, -1.0, 0.0, 0.0) * cy.y - ky;
        vec4 d2 = min(vx * vx + vy * vy, vec4(clp));
        vec4 wA = lob * d2 - 1.0;
        vec4 w = (0.25 * d2 - 1.0) * (d2 - 1.0) * (wA * wA);
        accumulated += sb * w.x + sc * w.y + se * w.z + sf * w.w;
        wsum += w;
    }
    {
        vec4 vx = vec4(1.0, 2.0, -1.0, 0.0) * cx.x + vec4(0.0, 0.0, 1.0, 1.0) * cx.y - kx;
        vec4 vy = vec4(1.0, 2.0, -1.0, 0.0) * cy.x + vec4(0.0, 0.0, 1.0, 1.0) * cy.y - ky;
        vec4 d2 = min(vx * vx + vy * vy, vec4(clp));
        vec4 wA = lob * d2 - 1.0;
        vec4 w = (0.25 * d2 - 1.0) * (d2 - 1.0) * (wA * wA);
        accumulated += sg * w.x + sh * w.y + si * w.z + sj * w.w;
        wsum += w;
    }
    {
        vec4 vx = vec4(1.0, 2.0, 0.0, 1.0) * cx.x + vec4(1.0, 1.0, 2.0, 2.0) * cx.y - kx;
        vec4 vy = vec4(1.0, 2.0, 0.0, 1.0) * cy.x + vec4(1.0, 1.0, 2.0, 2.0) * cy.y - ky;
        vec4 d2 = min(vx * vx + vy * vy, vec4(clp));
        vec4 wA = lob * d2 - 1.0;
        vec4 w = (0.25 * d2 - 1.0) * (d2 - 1.0) * (wA * wA);
        accumulated += sk * w.x + sl * w.y + sn * w.z + so * w.w;
        wsum += w;
    }
    float weightSum = dot(wsum, vec4(1.0));
    // no ringing: clamp to the 2x2 quad; then EASU's light sharpen, clamped the same way
    vec3 result = accumulated / max(weightSum, 0.0001);
    vec3 lo4 = min(min(sf, sg), min(sj, sk));
    vec3 hi4 = max(max(sf, sg), max(sj, sk));
    result = clamp(result, lo4, hi4);
    vec3 blurred = (sf + sg + sj + sk) * 0.25;
    result = clamp(result + (result - blurred) * kSharpness, lo4, hi4);
    gl_FragColor = vec4(result, 1.0);
}
