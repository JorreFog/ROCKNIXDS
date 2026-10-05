# rnds: the engine behind ROCKNIXDS Pixel

ROCKNIXDS Pixel (`../themes/rocknixds-pixel-dark`) is the ROCKNIXDS menu mockup drawn by a native engine in the patched
EmulationStation (`../es-rgds-rnds.patch`, sources in `es-app/src/rnds/`):

| File | What it is |
|---|---|
| `RndsRaster.*` | CPU rasterizer: CSS boxes (border, inset/outer shadows, hard-stop gradients, the dither tile), Pixelify Sans text laid out like Chrome (whole-pixel advances and kerning, no hinting), image fitting |
| `RndsUI.*` | The three screens (home, game list, ready) in the mockup's CSS px, its animations (`cubic-bezier`, `steps()`), input and touch, the texture cache and a worker queue for box art, screenshots and big boxes. Knows nothing about ES |
| `RndsEs.*` | ES glue: the GL backend, the data source (systems, games, play stats, favourites, RetroAchievements progress), the worker thread, power-saver wake-ups |
| `views/gamelist/RndsGameListView.*` | A game list for the engine (ES's folders, filters, sorting and launching) |

The theme's pictures and fonts are made by the scripts here:

```sh
python3 gen_fonts.py 'PixelifySans[wght].ttf' ../themes/rocknixds-pixel-dark/rnds/fonts   # static 400/500 + kern table
node gen_assets.mjs <mockup dir> ../themes/rocknixds-pixel-dark/rnds                       # wifi, battery, logo (Chromium)
python3 gen_icons.py ../themes/rocknixds-pixel-dark/rnds/icons                             # the drawn system icons
node gen_splash.mjs ../themes                                                              # both themes' boot splash
```

The light theme (`../themes/rocknixds-pixel-light`) has only its `theme.xml` (its palette: `<text name="palette">`)
and its splash; it uses the dark theme's `rnds/` folder. `logo.svg` is the mockup's logo, for the splash.

`gen_fonts.py` needs fontTools; `gen_assets.mjs` needs Playwright's Chromium. The variable font is
`ofl/pixelifysans/PixelifySans[wght].ttf` in [google/fonts](https://github.com/google/fonts); the mockup is
`mockup/` on the `cursor/mockup-pack-ac7b` branch.

## Testing against the mockup

`test/harness.cpp` runs the engine on the host with the mockup's own systems and games and a software compositor,
and writes PNGs of both screens. Render the same states of the mockup in Chromium (`index.html?view=home&system=7`,
animations finished) and compare:

```sh
python3 test/mockdata.py <mockup dir>/index.html > test/mockdata.inc
g++ -O2 -std=c++17 -I<es>/es-app/src -I/usr/include/freetype2 -I<stb> test/harness.cpp \
    <es>/es-app/src/rnds/RndsRaster.cpp <es>/es-app/src/rnds/RndsUI.cpp -lfreetype -lpthread -o harness
./harness ../themes/rocknixds-pixel-dark/rnds <mockup dir> 1   out/ "home 7; wait 2400; shot home-psx"
./harness ../themes/rocknixds-pixel-dark/rnds <mockup dir> 1.6 out/ "lib 0 0; wait 2400; shot lib-nds"
compare -metric MAE chrome-home-psx-top.png out/home-psx-top.png null:
```

Results when it was written (mean absolute difference over the screen): home 0.25% (top) and 0.33% (bottom), game
list 0.74% / 0.66% at 1x; 1.7-3% at 1.6x, where Chrome's dither tile and glyph edges round differently. The game
list's top screen and the ready screen differ on purpose: the box cover replaces the screenshot, and the cartridge
goes into the console. (The harness takes a 6th argument, `light`, for the light palette.)

A library with no games (a fresh card): build the harness with `-DMOCKDATA='"empty.inc"'` (a DS and a Favorites
collection with zero games; the mock dir needs `icons/nds.png` and `icons/favorites.png`) and, say, `-O1 -g
-fsanitize=address,undefined`; `./harness-empty <assets> <mock dir> 1 out/ "home 0; wait 2400; shot home; press a;
wait 2400; shot lib; press a; press x; press y; press up; press down; press b; wait 600; shot back"` must exit 0
with no sanitizer report, every press after the first A printing `view 1` until B prints `view 0`, and no `launch`
in the source log. With games present the engine must not change a pixel: render the same script with
`mockdata.inc` against the previous engine and `cmp` the PNGs.

The engine's files are "new file" sections of `dii-ess-aye/es-rgds-rnds.patch`. After editing them in the tree
`tools/build-es.sh` patched, `tools/regen-rnds-patch.sh <es src> dii-ess-aye/es-rgds-rnds.patch` rewrites exactly
those sections; the patch's hunks in ES's own files stay as they are.

`test/fixture.py` builds a `/storage` with the mockup's systems and games (art, play counts, last played, favourites)
for running the real ES on a desktop (`--resolution 1920 480 --windowed`, or 3072 768 for the RG DS Plus layout).
