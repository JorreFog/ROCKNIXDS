// ds-integer -- pixel-perfect DS screens: no scaling blur, no uneven pixels, with a bezel around them if needed.
//
// Each DS screen (256x192) is shown at the largest whole-number scale that fits the panel, centred.
// RG DS (640x480): 2.5x fits no integer, so it is exactly 2x with a bezel: every DS pixel is a 2x2 block at 1x
//   (hires off), and every rendered pixel maps 1:1 at 2x (hires on, where DraStic's buffer is already 512x384).
//   The 64 px left/right and 48 px top/bottom are a bezel drawn here, in the theme's dark grey: a recessed screen
//   well with a thin lit edge.
// RG DS Plus (1024x768): exactly 4x, so the screen fills the panel with no bezel: 4x4 blocks at 1x, 2x2 at 2x.
// Taps on the bezel do nothing; libdsflip computes the same rectangle and maps touch into it (next line).
// dsflip-viewport: integer
//
// One texture fetch at the texel centre (exact under GL_LINEAR) for screen pixels; the bezel is arithmetic only.
// highp positions: mediump is fp16 on this Mali, too coarse for pixel positions past 256 (see ds-crisp.frag).

precision mediump float;
varying highp vec2 v_texcoord;
uniform sampler2D u_texture;
uniform highp vec2 u_texture_size;
uniform highp vec2 u_output_size;

void main()
{
    highp vec2 p = v_texcoord * u_output_size;               // panel pixel (x right, y down), centre at +0.5
    highp float k = max(1.0, floor(min(u_output_size.x / 256.0, u_output_size.y / 192.0)));   // 2 on the RG DS, 4 on the Plus
    highp vec2 win = vec2(256.0, 192.0) * k;                 // the screen window, in panel pixels
    highp vec2 org = floor((u_output_size - win) * 0.5);     // its top-left corner (64,48 on the RG DS; 0,0 on the Plus)
    highp vec2 q = p - org;                                  // position inside the screen window
    if (q.x >= 0.0 && q.y >= 0.0 && q.x < win.x && q.y < win.y) {
        highp vec2 texel = floor(q * u_texture_size / win);  // kxk blocks at 1x, (k/2)x(k/2) at 2x
        gl_FragColor = vec4(SWIZ(texture2D(u_texture, (texel + 0.5) / u_texture_size)).rgb, 1.0);
        return;
    }
    // bezel: signed distance from the window's edge (> 0 outside), rounded outer corners of the well
    vec2 d = abs(p - (org + win * 0.5)) - win * 0.5;
    float out_d = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    vec3 face = mix(vec3(0.118, 0.125, 0.137), vec3(0.086, 0.090, 0.098), v_texcoord.y);   // soft vertical gradient
    float well = 1.0 - smoothstep(4.0, 5.5, out_d);          // the recessed rim right around the screen
    float lit = smoothstep(4.5, 5.5, out_d) * (1.0 - smoothstep(5.5, 7.0, out_d));   // its thin lit outer edge
    vec3 c = mix(face, vec3(0.035, 0.037, 0.040), well);
    c += lit * 0.07;
    gl_FragColor = vec4(c, 1.0);
}
