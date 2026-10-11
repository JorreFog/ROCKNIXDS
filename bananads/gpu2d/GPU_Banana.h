/*
    BananaDS 2D engine: a fast software 2D renderer for melonDS-based emulators.
    Copyright 2026 ROCKNIXDS contributors. Based on the melonDS software renderer,
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

#ifndef GPU_BANANA_H
#define GPU_BANANA_H

#include "GPU_Soft.h"
#include "GPU2D_Banana.h"

namespace melonDS
{

// BananaRenderer: melonDS's software renderer with the Banana 2D engine for both 2D units, and the
// per-scanline output stage (display mode, master brightness, 6-to-8-bit expansion) fused into one
// pass per screen instead of three. The 3D rasterizer and display capture are melonDS's own.
//
// Use it exactly like SoftRenderer: nds->GPU.SetRenderer(std::make_unique<BananaRenderer>(*nds)).
class BananaRenderer : public SoftRenderer
{
public:
    explicit BananaRenderer(melonDS::NDS& nds);
    ~BananaRenderer() override;

    void DrawScanline(u32 line) override;

private:
    void OutputLineA(u32 line, u32* dst);
    void OutputLineB(u32* dst);
    static void WriteLine(u32* dst, const u32* src, u16 brightness);
    static void FillLine(u32* dst, u32 val);
};

}

#endif // GPU_BANANA_H
