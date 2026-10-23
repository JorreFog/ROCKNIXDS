/*
    BananaDS 2D engine: a fast software 2D renderer for melonDS-based emulators.
    Copyright 2026 ROCKNIXDS contributors. Based on the melonDS 2D engine interface,
    Copyright 2016-2026 melonDS team.

    This file is part of BananaDS.

    BananaDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    BananaDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with BananaDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef GPU2D_BANANA_H
#define GPU2D_BANANA_H

#include <memory>

#include "GPU2D.h"
#include "GPU_Soft.h"

namespace melonDS
{

// BananaRenderer2D: a drop-in replacement for melonDS's SoftRenderer2D.
//
// It produces the same Output2D line (18-bit colour + layer flags) as SoftRenderer2D, pixel for
// pixel, but is built around whole layer lines instead of per-pixel painter's writes:
//
//   * each enabled BG is rendered into its own 256-pixel line of converted colours (0 = transparent),
//     tile by tile for text BGs (8 pixels per map fetch, palette tables converted once per change),
//     with scroll-only fast paths for bitmap, affine and mixed affine/text BGs;
//   * the window mask is built from the window edges as a few memsets, and applied to a layer line
//     only when windows are on;
//   * mosaic is a post-pass on the layer line;
//   * sprites come from a table rebuilt only when OAM changes, and are drawn into an OBJ line tile-row
//     by tile-row, with the per-pixel attribute bookkeeping of transparent sprite pixels (only
//     observable through OBJ mosaic) kept in a 1-byte side table;
//   * the layers are composited back to front in one SIMD pass that keeps the top (and, when BLDCNT
//     can blend, the second) pixel in registers, skipping everything below the layer(s) that cover
//     the whole line, and writing straight to the output when no colour effect can apply.
class BananaRenderer2D : public Renderer2D
{
public:
    BananaRenderer2D(melonDS::GPU2D& gpu2D, SoftRenderer& parent);
    ~BananaRenderer2D() override;
    bool Init() override { return true; }
    void Reset() override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override {}
    void VBlankEnd() override {}

private:
    SoftRenderer& Parent;

    // OBJ line pixel format, the same as melonDS's SoftRenderer2D
    enum : u32
    {
        OBJ_StandardPal = (1<<12),
        OBJ_DirectColor = (1<<15),
        OBJ_BGPrioMask = (0x3<<16),
        OBJ_IsOpaque = (1<<18),
        OBJ_OpaPrioMask = (OBJ_BGPrioMask | OBJ_IsOpaque),
        OBJ_IsSprite = (1<<19),
        OBJ_Mosaic = (1<<20),
    };

    static constexpr int LinePad = 8;

    // one rendered line per BG: colour in the 18-bit format, flag byte = 0x01<<bg, 0 = transparent.
    // padded on both sides so tile renderers can write whole tiles.
    alignas(16) u32 BGLineBuf[4][LinePad + 256 + LinePad];
    u32* BGLine(int bg) { return &BGLineBuf[bg][LinePad]; }

    alignas(16) u32 OBJLine[256];       // melonDS OBJ pixel format, built by DrawSprites
    alignas(16) u8  OBJTransAttr[256];  // attribute bits (>>16) of the last sprite that touched a pixel
    alignas(16) u8  OBJWindow[256];
    alignas(16) u32 OBJColor[256];      // resolved sprite colours for the current line, 0 = none
    alignas(16) u32 OBJPrio[256];
    alignas(16) u8  WindowMask[256];
    alignas(16) u32 TopLine[256];
    alignas(16) u32 SecondLine[256];
    alignas(16) u32 BackdropLine[256];

    // standard palettes converted to the 18-bit format (flag byte 0xFF = opaque marker, 0 = transparent)
    alignas(16) u32 PalBG256[256];  // 256-colour: entry 0 transparent
    alignas(16) u32 PalBG16[256];   // 16-colour: every 16th entry transparent
    u32 Backdrop;
    bool PalBGStale, PalOBJStale;

    // BG extended palettes, converted lazily per (slot, palette)
    std::unique_ptr<u32[]> BGExtPalCache;   // [4 slots][16 palettes][256]
    u64 BGExtPalValid;
    // every OBJ palette colour, indexed by an OBJ pixel's low 13 bits: the 16 extended palettes at
    // 0x0000..0x0FFF, the standard palette at OBJ_StandardPal
    std::unique_ptr<u32[]> ObjTable;        // [0x2000]
    bool ObjExtValid;

    // the sprites that can appear at all, from OAM, rebuilt when OAM changes
    struct SpriteEntry
    {
        u8 num;
        u8 ypos;
        u8 boundheight;
        u8 flags;       // 1: rotscale, 2: window, 4: mosaic
        s16 xpos;
        u8 width, height;
        u16 boundwidth;
    };
    SpriteEntry SpriteTab[128];
    int NumSpriteTab;
    bool SpriteTabStale;

    u32 NumSprites;
    int ObjXMin, ObjXMax;   // screen extent touched by non-window sprites on this line
    bool WindowsOn;
    u8 LayerDrawn;      // bit per BG: a layer line exists for this scanline
    u8 LayerFull;       // bit per BG: every pixel of the layer line is opaque
    u8 OBJPrioPresent;  // bit per priority: opaque sprite pixels with that priority exist

    void UpdatePalettes();
    inline const u32* GetBGExtPal(u32 slot, u32 pal)
    {
        u32 idx = (slot << 4) | pal;
        if (!(BGExtPalValid & (1ULL << idx))) ConvertBGExtPal(idx);
        return &BGExtPalCache[idx << 8];
    }
    void ConvertBGExtPal(u32 idx);
    void ConvertOBJExtPals();

    void CalcWindowMask();
    void ApplyWindowEdges(const u8* coords, u8& active, u8 cnt);
    void ApplyWindow(u32* line, u8 bit);
    static void ApplyMosaicX(u32* line, u32 size);

    void DrawBG_3D();
    template<bool bpp8> void DrawBG_Text(u32 line, u32 bgnum);
    void DrawBG_Affine(u32 line, u32 bgnum);
    void DrawBG_Extended(u32 line, u32 bgnum);
    void DrawBG_Large(u32 line);
    template<bool map16> void DrawTiledScroll(u32 bgnum, u32 tilemapaddr, u32 tilesetaddr, u32 coordmask, u32 yshift,
                                              bool wrap, bool extpal, s32 rotX, s32 rotY);
    void DrawBitmap16(u32* out, u32 flag, u32 base, u32 xmask, u32 ymask, u32 yshift, bool wrap,
                      s32 rotA, s32 rotC, s32 rotX, s32 rotY, u32 bgnum);
    void DrawBitmap8(u32* out, u32 flagmask, u32 base, u32 xmask, u32 ymask, u32 yshift, bool wrap,
                     s32 rotA, s32 rotC, s32 rotX, s32 rotY, u32 bgnum);

    void RebuildSpriteTable();
    void ApplySpriteMosaicX();
    void ResolveSprites();
    template<bool windows> void ResolveSpritesImpl();
    template<bool window> inline void PutOBJ(s32 x, u32 color, u32 attr);
    inline void PutOBJ4(s32 x, u32 idx4, u32 attr);
    template<bool window> void DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos);
    template<bool window> void DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos);

    void Composite(u32* outTop, u32* outSecond, bool needSecond);
    template<bool up> void BrightnessPass(u32* dst);
    template<u32 effect> void EffectsPass(u32* dst);
};

}

#endif // GPU2D_BANANA_H
