# BananaDS 2D engine

**A faster software 2D renderer for BananaDS and any other emulator built on melonDS.** It draws the
same picture as melonDS's own 2D engine, pixel for pixel, in about a third of the time: 2.9x faster
over the test scenes and 2.7x on a real ROM running through the whole emulator, measured on an x86
host (the numbers to beat are the ones on the RG DS's Cortex-A55; the static aarch64 build of the test
program is here to measure them, see [On the handheld](#on-the-handheld)).

It is a drop-in `Renderer2D` for melonDS's software renderer: two source files, two friend declarations
in melonDS, one line in the frontend. `integrate.sh` does all of it.

```
bananads/
  gpu2d/GPU2D_Banana.{h,cpp}   the 2D engine (melonDS::BananaRenderer2D, a Renderer2D)
  gpu2d/GPU_Banana.{h,cpp}     melonDS::BananaRenderer: SoftRenderer with the Banana engine for both
                               2D units and a fused output stage (display mode, master brightness,
                               6->8 bit expansion in one pass)
  melonds-core.patch           the only change in melonDS's own files: two friend declarations
  integrate.sh <tree>          drops the engine into a melonDS / BananaDS source tree
  tests/harness.cpp            drives both renderers over identical GPU state and compares every pixel;
                               benchmarks; random scenes; a real ROM through the emulator
  tests/platform_stub.cpp      melonDS's Platform:: interface for a headless build
  rom/                         a 2D test ROM (bare-metal ARM9, built with clang + lld, no devkitARM)
  build.sh, CMakeLists.txt     host and aarch64 (RG DS) builds against the pinned melonDS
  fetch-melonds.sh             the pinned melonDS commit (MELONDS_COMMIT), patched
```

## Results

Milliseconds per frame for both 2D units, 192 scanlines each, on an Intel Xeon at 2.8 GHz (gcc 13, -O3,
no LTO, both renderers built the same way; the Banana library adds `-msse4.1` on x86, the stock renderer
has no SIMD to gain from it). Best of three runs of 60 frames.

| scene | what it is | melonDS 2D | Banana 2D | speedup |
|---|---|---|---|---|
| backdrop | nothing enabled: the per-frame floor | 0.487 | 0.149 | 3.3x |
| text-3bg | 3 text BGs (16/256-colour, extended palette) + 48 sprites on A; 2 BGs + 24 on B | 4.146 | 1.018 | 4.1x |
| rpg-blend | the same with two windows, alpha blending, semi-transparent sprites, an OBJ window; B: 256-colour bitmap, brightness | 4.525 | 1.721 | 2.6x |
| sprite-storm | 128 sprites on each unit: 16/256-colour, bitmap, rotscale, OBJ extended palette | 3.833 | 1.227 | 3.1x |
| affine | two rotating affine BGs; mixed affine/text and direct-colour bitmap rotated on B | 2.988 | 1.373 | 2.2x |
| bitmap-fade | bitmaps scrolling, BLDCNT brightness, master brightness; a large (mode 6) BG on B | 2.266 | 0.685 | 3.3x |
| 3d-hud | BG0 = 3D with alpha, HUD BGs, 3D blended against BG, semi-transparent sprites | 2.972 | 1.036 | 2.9x |
| mosaic | BG and OBJ mosaic, a window | 2.969 | 1.292 | 2.3x |
| display-modes | VRAM display, FIFO display, screen off, display capture with blending | 1.341 | 0.409 | 3.3x |
| hblank-fx | per-line scroll, BLDY, window and rotation writes, as a raster-effect game does | 3.315 | 1.162 | 2.9x |
| touch-ui | direct-colour bitmap, mixed affine/text BG with extended palette, 96 sprites (bitmaps too) | 3.324 | 0.942 | 3.5x |
| **total** | | **32.17** | **11.02** | **2.9x** |

A real ROM, `rom/banana2d.nds`, through melonDS itself (the interpreter, both renderers in turn; only the
time in the 2D renderer's scanline and sprite calls is counted): 720 frames, all 720 identical,
**3.32 ms -> 1.20 ms per frame, 2.8x**. The same ROM through the static aarch64 build under qemu: 720
identical frames.

Where the time went in melonDS's engine, and what changed: every BG pixel was a function call with a
window test, a tile fetch every 8 pixels, a palette lookup and a two-word write (the top and the second
pixel for blending); sprites were composited by scanning the whole line once per priority; the colour
effect pass ran on every pixel with a chain of branches; the parent renderer made three more passes
over each output line. The Banana engine renders each layer as a whole line (tile rows of 8 pixels,
palette tables converted once), applies windows and mosaic as line passes only when they are on, keeps
sprites in a table that is rebuilt only when OAM changes and draws tile rows four pixels at a time,
composites all layers in one SIMD pass that keeps the top (and second) pixel in registers and skips
everything under a layer that covers the whole line, runs the colour effects only on lines where
BLDCNT can have an effect (writing straight to the output otherwise), and does the output stage in one
pass. The details are at the top of `gpu2d/GPU2D_Banana.h`.

## Exactness

The engine's output is compared with melonDS's `SoftRenderer2D`, pixel for pixel, on the final 32-bit
framebuffers (after master brightness and colour expansion) and on captured VRAM:

- the eleven hand-made scenes above, several frames each;
- random scenes: random registers, VRAM, palettes, OAM and VRAM mapping, and for half of them random
  register, palette, OAM and VRAM writes during the frame (as a game's HBlank handler does), checked
  after each frame; 200 seeds in `build.sh test`, 300 in CI, both 2D units, no mismatch found;
- the test ROM through the whole emulator, 720 frames (548 with a picture: the rest is the boot and
  the white screens after the last phase);
- all of it again as the aarch64 build (NEON code paths) under qemu-user.

The register semantics are melonDS's: the two-scanline layer enable delay, the window edge state that
carries from one line to the next, the sprite priority rules, the OBJ mosaic latch (including the
attribute bits transparent sprite pixels leave behind), the 3D layer's alpha, the display capture.
Nothing is approximated; the differential test is the proof.

## Using it in BananaDS

```sh
sh bananads/integrate.sh ~/src/BananaDS
```

copies the four engine files into `src/`, adds the two friend declarations to `src/GPU_Soft.h` (the
`melonds-core.patch`), lists the two `.cpp` files in `src/CMakeLists.txt`, and makes the Qt frontend
construct `BananaRenderer` where it constructed `SoftRenderer`. Every step checks what it finds and says
what it did; on a tree that differs from upstream melonDS it tells you the one-line change to make by
hand. The engine depends on melonDS's headers only (`GPU.h`, `GPU2D.h`, `GPU_Soft.h`, `GPU_ColorOp.h`)
and on `SoftRenderer3D` for the 3D layer, which it leaves alone, as it does the OpenGL renderers.

Another frontend:

```cpp
#include "GPU_Banana.h"
nds->SetRenderer(std::make_unique<melonDS::BananaRenderer>(*nds));
```

`BananaRenderer` is a `SoftRenderer`: the same framebuffers, savestates, threaded 3D and display capture.
Developed and tested against melonDS commit `906e9eb` (`MELONDS_COMMIT`); the interface it implements
(`Renderer2D`, `SoftRenderer::Output2D`/`Output3D`, the VRAM flattening) has been stable across recent
melonDS.

The engine consumes `GPU::PaletteDirty` and `GPU::OAMDirty` (as melonDS's OpenGL 2D renderer does) to
rebuild its palette tables and sprite table; melonDS's savestate loading, resets and renderer switches
set those flags.

## Building and testing

```sh
sh bananads/build.sh test          # fetches the pinned melonDS, builds, runs scenes + 200 random scenes + benchmark
python3 bananads/rom/build.py      # the test ROM (clang, ld.lld, llvm-objcopy)
bananads/build/host/banana2d-test --rom bananads/rom/out/banana2d.nds
sh bananads/build.sh aarch64       # static build for the RG DS (gcc-aarch64-linux-gnu)
qemu-aarch64-static bananads/build/aarch64/banana2d-test --scenes --fuzz 40
```

`banana2d-test` options: `--scenes`, `--fuzz N` (`--seed S`), `--bench N`, `--only <scene>`,
`--banana-only` / `--stock-only` (benchmark one renderer, for profiling), `--rom <nds>`
(`--rom-frames N`), `--dump DIR` (PPM images of the first mismatching frame, and of ROM frames).
`MELONDS=path` points `build.sh` at another melonDS tree (with `melonds-core.patch` applied).
CI (`.github/workflows/bananads.yml`) runs all of it on every change and keeps the aarch64 binary and
the ROM as an artifact.

## On the handheld

The static `banana2d-test` for aarch64 runs on the RG DS as it is (copy it and `rom/out/banana2d.nds`
over ssh):

```sh
./banana2d-test --bench 60                    # the scene table above, on the A55
./banana2d-test --rom banana2d.nds            # the ROM through the interpreter, 2D time per frame
./banana2d-test --scenes --fuzz 100           # the pixel comparison, on the device's own CPU
```

The built-in benchmark builds the core without the JIT (the test program never runs the CPUs; the
ROM mode uses the interpreter), so the "whole emulation" time it prints for the ROM is not BananaDS's
frame time; the 2D time per frame is what the engine saves.

## Notes

- `rom/`: eight phases of 60 frames (text layers and sprites, blending and windows, a 128-sprite storm,
  affine rotation, bitmaps with fades, mosaic, raster effects from a VCOUNT poll, and a 3D layer with
  translucent quads under 2D layers), all graphics from a PRNG so there is nothing to ship but the code.
  The boot (filling VRAM) takes about 140 frames on the interpreter, and the raster phase two frames per
  iteration, so the 3D phase runs from about frame 630 to 690 (`--rom-frames 720` covers all of it).
- The harness builds melonDS headless (no Qt, no OpenGL, no GDB stub, no JIT, no LTO) with the same
  optimisation flags for both renderers.
- License: `gpu2d/`, `tests/` and `melonds-core.patch` are GPL-3.0-or-later like melonDS, which they
  link against; `rom/` and the scripts are MIT like the rest of ROCKNIXDS.
