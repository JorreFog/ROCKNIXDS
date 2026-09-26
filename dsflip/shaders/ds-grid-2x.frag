// ds-grid-2x -- integer 2x scaling with a genuinely even LCD grid, for DraStic at 1x
// on the Anbernic RG DS.
//
// WHY THIS EXISTS
// The panel gives each DS screen 640x480 for a 256x192 source: exactly 2.5x. At that
// ratio a per-DS-pixel grid can never look even, because 5 output pixels is the
// repeating unit -- one pixel boundary lands inside an output pixel and the next
// straddles two, so the grid renders as alternating heavier and lighter lines. That is
// arithmetic, not a tuning problem (see ds-grid.frag, which minimises it by staying
// subtle).
//
// This file takes the other way out: scale by exactly 2 instead of 2.5 and letterbox
// the result. At an integer scale every DS pixel is an identical 2x2 block and every
// grid line falls on an output pixel boundary, so all lines are identical by
// construction. The grid can then be as strong as you like and still look right.
//
// The cost is screen area: the image is 512x384 inside 640x480, so a 64px border down
// each side and 48px top and bottom, about 20% smaller in each axis. That is the
// classic integer-scaling trade and it is the only way to get an authentic, even LCD
// grid out of a 2.5x panel.
//
// Scaling is plain nearest, which is exact here -- at an integer ratio there is nothing
// to reconstruct, so there is no blending, no wobble and no shimmer.
//
// highp is required: mediump on this Mali-G52 is fp16 and quantises a texel coordinate
// near x=256 to 0.25 texels, which would break the cell phase.
// No #version and no backslash continuations: the shim compiles this as ESSL 1.00.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D u_texture;
uniform vec2 u_texture_size;
uniform vec2 u_output_size;

// Cells are DS pixels (256x192), not texture pixels: at 2x (hires 3D) the texture is 512x384, so each 2x2
// output cell shows 2x2 texels, one texel per output pixel (pixel-perfect), inside the same DS-pixel grid.
const vec2 DS_SIZE = vec2(256.0, 192.0);

// --- taste knobs -------------------------------------------------------------
// Grid line width in OUTPUT pixels. At 2x a cell is 2px, so 1.0 darkens one of the two
// columns and one of the two rows -- the classic handheld LCD grid. Fractional values
// work and are anti-aliased exactly.
const float GRID_WIDTH_PX = 1.0;
// Line darkness in linear light. Because every line here is identical, this can go much
// darker than ds-grid without looking uneven.
const float GRID_LEVEL    = 0.55;
// Fraction of the removed light to put back. Full compensation reads as glare.
const float GRID_COMPENSATE = 0.5;
// -----------------------------------------------------------------------------

void main()
{
    // Largest integer scale that fits, and the centred image rectangle it implies.
    float s      = max(1.0, floor(min(u_output_size.x / DS_SIZE.x,
                                      u_output_size.y / DS_SIZE.y)));
    vec2  imgSize = DS_SIZE * s;
    vec2  origin  = floor((u_output_size - imgSize) * 0.5);

    vec2 p     = floor(v_texcoord * u_output_size);   // this output pixel
    vec2 local = p - origin;                          // position inside the image

    // Letterbox: anything outside the image rectangle is panel black.
    if (any(lessThan(local, vec2(0.0))) || any(greaterThanEqual(local, imgSize))) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 texel = floor(local * u_texture_size / imgSize);   // 1x: one texel per cell; 2x: one per output pixel
    vec4 c = SWIZ(texture2D(u_texture, (texel + 0.5) / u_texture_size));

    // Grid: the line occupies the last GRID_WIDTH_PX of each cell. Because s is an
    // integer, every cell sees exactly the same geometry, so every line matches.
    vec2 f   = local - floor(local / s) * s;          // 0 .. s-1 within the DS-pixel cell
    vec2 ov  = max(vec2(0.0), min(f + 1.0, vec2(s)) - max(f, vec2(s - GRID_WIDTH_PX)));
    float coverage = ov.x + ov.y - ov.x * ov.y;       // union of row and column bands
    float g = mix(1.0, GRID_LEVEL, coverage);

    float meanAxis = min(GRID_WIDTH_PX / s, 1.0);
    float meanCov  = meanAxis + meanAxis - meanAxis * meanAxis;
    float bright   = mix(1.0, 1.0 / mix(1.0, GRID_LEVEL, meanCov), GRID_COMPENSATE);

    vec3 lin = c.rgb * c.rgb * (g * bright);
    gl_FragColor = vec4(sqrt(min(lin, vec3(1.0))), c.a);
}
