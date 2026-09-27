// ds-integer -- pixel-perfect DS screens on the RG DS: no scaling blur, no uneven pixels, with a bezel around them.
//
// The panels are 640x480 and the DS screen 256x192: 2.5x, which no integer scale fills. This shows each screen at
// exactly 2x, centred: every DS pixel is a 2x2 block at 1x (hires off), and every rendered pixel maps 1:1 at 2x
// (hires on, where DraStic's buffer is already 512x384). The 64 px left/right and 48 px top/bottom are a bezel
// drawn here, in the theme's dark grey: a recessed screen well with a thin lit edge. Taps on the bezel do nothing;
// libdsflip maps touch into the screen rectangle declared on the next line.
// dsflip-viewport: 64 48 512 384
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
    highp vec2 q = p - vec2(64.0, 48.0);                     // position inside the 512x384 screen window
    if (q.x >= 0.0 && q.y >= 0.0 && q.x < 512.0 && q.y < 384.0) {
        highp vec2 texel = floor(q * u_texture_size / vec2(512.0, 384.0));   // 2x2 blocks at 1x, 1:1 at 2x
        gl_FragColor = vec4(SWIZ(texture2D(u_texture, (texel + 0.5) / u_texture_size)).rgb, 1.0);
        return;
    }
    // bezel: signed distance from the window's edge (> 0 outside), rounded outer corners of the well
    vec2 d = abs(p - vec2(320.0, 240.0)) - vec2(256.0, 192.0);
    float out_d = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    vec3 face = mix(vec3(0.118, 0.125, 0.137), vec3(0.086, 0.090, 0.098), v_texcoord.y);   // soft vertical gradient
    float well = 1.0 - smoothstep(4.0, 5.5, out_d);          // the recessed rim right around the screen
    float lit = smoothstep(4.5, 5.5, out_d) * (1.0 - smoothstep(5.5, 7.0, out_d));   // its thin lit outer edge
    vec3 c = mix(face, vec3(0.035, 0.037, 0.040), well);
    c += lit * 0.07;
    gl_FragColor = vec4(c, 1.0);
}
