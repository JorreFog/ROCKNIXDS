/*
    BananaDS 2D engine: a fast software 2D renderer for melonDS-based emulators.
    Copyright 2026 ROCKNIXDS contributors. Register semantics and address formulas follow the
    melonDS 2D engine, Copyright 2016-2026 melonDS team.

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

#include <string.h>
#include "GPU2D_Banana.h"
#include "GPU_ColorOp.h"

// ---- four-lane SIMD, just what the compositor needs ------------------------------------------------
#if !defined(BANANA_NO_SIMD) && (defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#define BANANA_SIMD 1
namespace melonDS
{
typedef uint32x4_t V4;
static inline V4 VLoad(const u32* p) { return vld1q_u32(p); }
static inline void VStore(u32* p, V4 v) { vst1q_u32(p, v); }
static inline V4 VDup(u32 x) { return vdupq_n_u32(x); }
static inline V4 VZero() { return vdupq_n_u32(0); }
static inline V4 VNonZero(V4 a) { return vtstq_u32(a, a); }
static inline V4 VEq(V4 a, V4 b) { return vceqq_u32(a, b); }
static inline V4 VAnd(V4 a, V4 b) { return vandq_u32(a, b); }
static inline V4 VOr(V4 a, V4 b) { return vorrq_u32(a, b); }
static inline V4 VNot(V4 a) { return vmvnq_u32(a); }
static inline V4 VLtSmall(V4 a, V4 b) { return vcltq_u32(a, b); }        // a < b, lanes well below 2^31
static inline V4 VSel(V4 m, V4 a, V4 b) { return vbslq_u32(m, a, b); }   // m ? a : b, per bit
static inline V4 VFromBytes4(u32 x) { return vmovl_u16(vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(x))))); }
static inline V4 VLoadU16x4(const u16* p) { return vmovl_u16(vld1_u16(p)); }
template<int N> static inline V4 VShl(V4 a) { return vshlq_n_u32(a, N); }
template<int N> static inline V4 VShr(V4 a) { return vshrq_n_u32(a, N); }
static inline u32 VHorOr(V4 a) { uint32x2_t t = vorr_u32(vget_low_u32(a), vget_high_u32(a)); return vget_lane_u32(t, 0) | vget_lane_u32(t, 1); }
static inline u32 VHorAnd(V4 a) { uint32x2_t t = vand_u32(vget_low_u32(a), vget_high_u32(a)); return vget_lane_u32(t, 0) & vget_lane_u32(t, 1); }
}
#elif !defined(BANANA_NO_SIMD) && defined(__SSE2__)
#include <emmintrin.h>
#ifdef __SSE4_1__
#include <smmintrin.h>
#endif
#define BANANA_SIMD 1
namespace melonDS
{
typedef __m128i V4;
static inline V4 VLoad(const u32* p) { return _mm_loadu_si128((const __m128i*)p); }
static inline void VStore(u32* p, V4 v) { _mm_storeu_si128((__m128i*)p, v); }
static inline V4 VDup(u32 x) { return _mm_set1_epi32((int)x); }
static inline V4 VZero() { return _mm_setzero_si128(); }
static inline V4 VNonZero(V4 a) { return _mm_xor_si128(_mm_cmpeq_epi32(a, _mm_setzero_si128()), _mm_set1_epi32(-1)); }
static inline V4 VEq(V4 a, V4 b) { return _mm_cmpeq_epi32(a, b); }
static inline V4 VAnd(V4 a, V4 b) { return _mm_and_si128(a, b); }
static inline V4 VOr(V4 a, V4 b) { return _mm_or_si128(a, b); }
static inline V4 VNot(V4 a) { return _mm_xor_si128(a, _mm_set1_epi32(-1)); }
static inline V4 VLtSmall(V4 a, V4 b) { return _mm_cmplt_epi32(a, b); }   // a < b, lanes well below 2^31
static inline V4 VSel(V4 m, V4 a, V4 b)
{
#ifdef __SSE4_1__
    return _mm_blendv_epi8(b, a, m);
#else
    return _mm_or_si128(_mm_and_si128(m, a), _mm_andnot_si128(m, b));
#endif
}
static inline V4 VFromBytes4(u32 x)
{
    __m128i z = _mm_setzero_si128();
    return _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)x), z), z);
}
static inline V4 VLoadU16x4(const u16* p) { return _mm_unpacklo_epi16(_mm_loadl_epi64((const __m128i*)p), _mm_setzero_si128()); }
template<int N> static inline V4 VShl(V4 a) { return _mm_slli_epi32(a, N); }
template<int N> static inline V4 VShr(V4 a) { return _mm_srli_epi32(a, N); }
static inline u32 VHorOr(V4 a)
{
    a = _mm_or_si128(a, _mm_srli_si128(a, 8));
    a = _mm_or_si128(a, _mm_srli_si128(a, 4));
    return (u32)_mm_cvtsi128_si32(a);
}
static inline u32 VHorAnd(V4 a)
{
    a = _mm_and_si128(a, _mm_srli_si128(a, 8));
    a = _mm_and_si128(a, _mm_srli_si128(a, 4));
    return (u32)_mm_cvtsi128_si32(a);
}
}
#else
namespace melonDS
{
struct V4 { u32 v[4]; };
static inline V4 VLoad(const u32* p) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = p[i]; return r; }
static inline void VStore(u32* p, V4 v) { for (int i = 0; i < 4; i++) p[i] = v.v[i]; }
static inline V4 VDup(u32 x) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = x; return r; }
static inline V4 VZero() { return VDup(0); }
static inline V4 VNonZero(V4 a) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] ? ~0u : 0; return r; }
static inline V4 VEq(V4 a, V4 b) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = (a.v[i] == b.v[i]) ? ~0u : 0; return r; }
static inline V4 VAnd(V4 a, V4 b) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] & b.v[i]; return r; }
static inline V4 VOr(V4 a, V4 b) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] | b.v[i]; return r; }
static inline V4 VNot(V4 a) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = ~a.v[i]; return r; }
static inline V4 VLtSmall(V4 a, V4 b) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = (a.v[i] < b.v[i]) ? ~0u : 0; return r; }
static inline V4 VSel(V4 m, V4 a, V4 b) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = (m.v[i] & a.v[i]) | (~m.v[i] & b.v[i]); return r; }
static inline V4 VFromBytes4(u32 x) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = (x >> (8*i)) & 0xFF; return r; }
static inline V4 VLoadU16x4(const u16* p) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = p[i]; return r; }
template<int N> static inline V4 VShl(V4 a) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] << N; return r; }
template<int N> static inline V4 VShr(V4 a) { V4 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] >> N; return r; }
static inline u32 VHorOr(V4 a) { return a.v[0] | a.v[1] | a.v[2] | a.v[3]; }
static inline u32 VHorAnd(V4 a) { return a.v[0] & a.v[1] & a.v[2] & a.v[3]; }
}
#endif

namespace melonDS
{

// 15-bit palette colour to the 18-bit internal format; bit 15 is the low green bit
// (melonDS: applies to BG/OBJ palette colours, not to direct colour)
static inline u32 Conv15(u32 c)
{
    u32 r = (c & 0x001F) << 1;
    u32 g = ((c & 0x03E0) >> 4) | ((c & 0x8000) >> 15);
    u32 b = (c & 0x7C00) >> 9;
    return r | (g << 8) | (b << 16);
}

static inline u32 BGFlagMask(u32 bgnum)
{
    return 0x00FFFFFF | (0x01000000u << bgnum);
}

alignas(16) static const u8 AllOnes[256] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};

// colour effect decision tables (melonDS ColorComposite), indexed by a pixel's flag byte:
// the BLDCNT 2nd-target bit a pixel selects when it is the second layer
struct Target2Table
{
    u16 v[256];
    constexpr Target2Table() : v()
    {
        for (int f = 0; f < 256; f++)
        {
            if      (f & 0x80) v[f] = 0x1000;
            else if (f & 0x40) v[f] = 0x0100;
            else               v[f] = f << 8;
        }
    }
};
static constexpr Target2Table Target2Tab {};

// the BLDCNT 1st-target bit a pixel selects when it is the top layer
struct FirstBitTable
{
    u8 v[256];
    constexpr FirstBitTable() : v()
    {
        for (int f = 0; f < 256; f++)
        {
            if      (f & 0x80) v[f] = 0x10;
            else if (f & 0x40) v[f] = 0x01;
            else               v[f] = f;
        }
    }
};
static constexpr FirstBitTable FirstBitTab {};


BananaRenderer2D::BananaRenderer2D(melonDS::GPU2D& gpu2D, SoftRenderer& parent)
    : Renderer2D(gpu2D), Parent(parent),
      BGExtPalCache(new u32[4*16*256]), ObjTable(new u32[0x2000])
{
    Reset();
}

BananaRenderer2D::~BananaRenderer2D()
{
}

void BananaRenderer2D::Reset()
{
    memset(BGLineBuf, 0, sizeof(BGLineBuf));
    memset(OBJLine, 0, sizeof(OBJLine));
    memset(OBJTransAttr, 0, sizeof(OBJTransAttr));
    memset(OBJWindow, 0, sizeof(OBJWindow));
    memset(OBJColor, 0, sizeof(OBJColor));
    memset(OBJPrio, 0, sizeof(OBJPrio));
    memset(WindowMask, 0xFF, sizeof(WindowMask));
    memset(TopLine, 0, sizeof(TopLine));
    memset(SecondLine, 0, sizeof(SecondLine));
    memset(PalBG256, 0, sizeof(PalBG256));
    memset(PalBG16, 0, sizeof(PalBG16));
    memset(ObjTable.get(), 0, 0x2000 * sizeof(u32));
    Backdrop = 0x20000000;
    for (int i = 0; i < 256; i++) BackdropLine[i] = Backdrop;
    PalBGStale = true;
    PalOBJStale = true;
    BGExtPalValid = 0;
    ObjExtValid = false;
    NumSpriteTab = 0;
    SpriteTabStale = true;
    NumSprites = 0;
    ObjXMin = 256; ObjXMax = 0;
    WindowsOn = false;
    LayerDrawn = 0;
    LayerFull = 0;
    OBJPrioPresent = 0;
}


// ---- palettes ----------------------------------------------------------------------------------

void BananaRenderer2D::UpdatePalettes()
{
    u32 shift = GPU2D.Num * 2;
    u32 bgbits = (0x01 | 0x10) << shift;   // the palette half, and its colour 0 (the backdrop)
    u32 objbits = 0x02 << shift;

    if ((GPU.PaletteDirty & bgbits) || PalBGStale)
    {
        const u16* pal = (const u16*)&GPU.Palette[GPU2D.Num ? 0x400 : 0];
        for (int i = 0; i < 256; i++)
        {
            u32 c = Conv15(pal[i]) | 0xFF000000;
            PalBG256[i] = i ? c : 0;
            PalBG16[i] = (i & 0xF) ? c : 0;
        }
        u32 bd = Conv15(pal[0]) | 0x20000000;
        if (bd != Backdrop || PalBGStale)
        {
            Backdrop = bd;
            for (int i = 0; i < 256; i++) BackdropLine[i] = bd;
        }
        GPU.PaletteDirty &= ~bgbits;
        PalBGStale = false;
    }

    if ((GPU.PaletteDirty & objbits) || PalOBJStale)
    {
        const u16* pal = (const u16*)&GPU.Palette[GPU2D.Num ? 0x600 : 0x200];
        u32* table = &ObjTable[OBJ_StandardPal];
        for (int i = 0; i < 256; i++)
            table[i] = Conv15(pal[i]);
        GPU.PaletteDirty &= ~objbits;
        PalOBJStale = false;
    }
}

// the 16 OBJ extended palettes, as one 4096-entry table indexed by (palette << 8) | colour
void BananaRenderer2D::ConvertOBJExtPals()
{
    const u16* src = GPU2D.GetOBJExtPal();
    for (int i = 0; i < 4096; i++)
        ObjTable[i] = Conv15(src[i]);
    ObjExtValid = true;
}

void BananaRenderer2D::ConvertBGExtPal(u32 idx)
{
    u32* table = &BGExtPalCache[idx << 8];
    const u16* src = GPU2D.GetBGExtPal(idx >> 4, idx & 0xF);
    table[0] = 0;
    for (int i = 1; i < 256; i++)
        table[i] = Conv15(src[i]) | 0xFF000000;
    BGExtPalValid |= (1ULL << idx);
}



// ---- windows and mosaic ------------------------------------------------------------------------

// Same semantics as GPU2D::CalculateWindowMask (including the horizontal window state that is
// carried from one scanline to the next in Win0Active/Win1Active bit 1), done with range fills.
void BananaRenderer2D::ApplyWindowEdges(const u8* coords, u8& active, u8 cnt)
{
    u8 x1 = coords[0];
    u8 x2 = coords[1];
    bool v = active & 0x1;
    bool h = active & 0x2;

    auto fill = [&](int a, int b)
    {
        if (h && v && b > a)
            memset(&WindowMask[a], cnt, b - a);
    };

    // per pixel: if (i == x2) inactive; else if (i == x1) active; then test
    if (x1 < x2)
    {
        fill(0, x1);  h = true;
        fill(x1, x2); h = false;
        fill(x2, 256);
    }
    else if (x2 < x1)
    {
        fill(0, x2);  h = false;
        fill(x2, x1); h = true;
        fill(x1, 256);
    }
    else
    {
        fill(0, x2);  h = false;
        fill(x2, 256);
    }

    active = (active & 0x1) | (h ? 0x2 : 0);
}

void BananaRenderer2D::CalcWindowMask()
{
    u32 dispcnt = GPU2D.DispCnt;

    memset(WindowMask, GPU2D.WinCnt[2], 256);

    if (dispcnt & (1<<15))
    {
        u8 c = GPU2D.WinCnt[3];
        for (int i = 0; i < 256; i++)
            if (OBJWindow[i]) WindowMask[i] = c;
    }

    if (dispcnt & (1<<14))
        ApplyWindowEdges(GPU2D.Win1Coords, GPU2D.Win1Active, GPU2D.WinCnt[1]);

    if (dispcnt & (1<<13))
        ApplyWindowEdges(GPU2D.Win0Coords, GPU2D.Win0Active, GPU2D.WinCnt[0]);
}

void BananaRenderer2D::ApplyWindow(u32* line, u8 bit)
{
    for (int i = 0; i < 256; i++)
        line[i] = (WindowMask[i] & bit) ? line[i] : 0;
}

// horizontal mosaic: every pixel of a block repeats the block's first pixel
// (equivalent to melonDS sampling the BG at x - (x % (size+1)))
void BananaRenderer2D::ApplyMosaicX(u32* line, u32 size)
{
    u32 step = size + 1;
    for (u32 b = 0; b < 256; b += step)
    {
        u32 v = line[b];
        u32 end = b + step; if (end > 256) end = 256;
        for (u32 i = b + 1; i < end; i++)
            line[i] = v;
    }
}


// ---- BG layers ---------------------------------------------------------------------------------

// eight 256-colour pixels of one tile row
static inline void Expand8(u32* out, u64 pix, const u32* pal, u32 flagmask, bool hflip)
{
    if (hflip)
    {
        out[0] = pal[(pix >> 56) & 0xFF] & flagmask;
        out[1] = pal[(pix >> 48) & 0xFF] & flagmask;
        out[2] = pal[(pix >> 40) & 0xFF] & flagmask;
        out[3] = pal[(pix >> 32) & 0xFF] & flagmask;
        out[4] = pal[(pix >> 24) & 0xFF] & flagmask;
        out[5] = pal[(pix >> 16) & 0xFF] & flagmask;
        out[6] = pal[(pix >>  8) & 0xFF] & flagmask;
        out[7] = pal[(pix      ) & 0xFF] & flagmask;
    }
    else
    {
        out[0] = pal[(pix      ) & 0xFF] & flagmask;
        out[1] = pal[(pix >>  8) & 0xFF] & flagmask;
        out[2] = pal[(pix >> 16) & 0xFF] & flagmask;
        out[3] = pal[(pix >> 24) & 0xFF] & flagmask;
        out[4] = pal[(pix >> 32) & 0xFF] & flagmask;
        out[5] = pal[(pix >> 40) & 0xFF] & flagmask;
        out[6] = pal[(pix >> 48) & 0xFF] & flagmask;
        out[7] = pal[(pix >> 56) & 0xFF] & flagmask;
    }
}

// eight 16-colour pixels of one tile row; pal already points at the tile's 16-colour palette
static inline void Expand4(u32* out, u32 pix, const u32* pal, u32 flagmask, bool hflip)
{
    if (hflip)
    {
        out[0] = pal[(pix >> 28) & 0xF] & flagmask;
        out[1] = pal[(pix >> 24) & 0xF] & flagmask;
        out[2] = pal[(pix >> 20) & 0xF] & flagmask;
        out[3] = pal[(pix >> 16) & 0xF] & flagmask;
        out[4] = pal[(pix >> 12) & 0xF] & flagmask;
        out[5] = pal[(pix >>  8) & 0xF] & flagmask;
        out[6] = pal[(pix >>  4) & 0xF] & flagmask;
        out[7] = pal[(pix      ) & 0xF] & flagmask;
    }
    else
    {
        out[0] = pal[(pix      ) & 0xF] & flagmask;
        out[1] = pal[(pix >>  4) & 0xF] & flagmask;
        out[2] = pal[(pix >>  8) & 0xF] & flagmask;
        out[3] = pal[(pix >> 12) & 0xF] & flagmask;
        out[4] = pal[(pix >> 16) & 0xF] & flagmask;
        out[5] = pal[(pix >> 20) & 0xF] & flagmask;
        out[6] = pal[(pix >> 24) & 0xF] & flagmask;
        out[7] = pal[(pix >> 28) & 0xF] & flagmask;
    }
}

static inline void Zero8(u32* out)
{
    out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
    out[4] = 0; out[5] = 0; out[6] = 0; out[7] = 0;
}

static inline bool HasZeroByte(u64 pix) { return ((pix - 0x0101010101010101ULL) & ~pix & 0x8080808080808080ULL) != 0; }
static inline bool HasZeroNibble(u32 pix) { return ((pix - 0x11111111u) & ~pix & 0x88888888u) != 0; }

void BananaRenderer2D::DrawBG_3D()
{
    const u32* src = Parent.Output3D;
    u32* out = BGLine(0);
    for (int i = 0; i < 256; i++)
    {
        u32 c = src[i];
        out[i] = (c >> 24) ? (c | 0x40000000) : 0;
    }
}

template<bool bpp8>
void BananaRenderer2D::DrawBG_Text(u32 line, u32 bgnum)
{
    u16 bgcnt = GPU2D.BGCnt[bgnum];
    u32 dispcnt = GPU2D.DispCnt;

    u32 xoff = GPU2D.BGXPos[bgnum];
    u32 yoff = GPU2D.BGYPos[bgnum];
    if (bgcnt & (1<<6))
        yoff += GPU2D.BGMosaicLine;
    else
        yoff += line;
    yoff &= 0xFFFF;

    u32 widexmask = (bgcnt & (1<<14)) ? 0x100 : 0;

    bool extpal = bpp8 && (dispcnt & (1<<30));
    u32 extpalslot = ((bgnum < 2) && (bgcnt & 0x2000)) ? (2 + bgnum) : bgnum;

    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    u32 tilesetaddr, tilemapaddr;
    if (GPU2D.Num)
    {
        tilesetaddr = ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((bgcnt & 0x1F00) << 3);
    }
    else
    {
        tilesetaddr = ((dispcnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((dispcnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);
    }

    if (bgcnt & (1<<15))
    {
        tilemapaddr += ((yoff & 0x1F8) << 3);
        if (bgcnt & (1<<14))
            tilemapaddr += ((yoff & 0x100) << 3);
    }
    else
        tilemapaddr += ((yoff & 0xF8) << 3);

    u32 flagmask = BGFlagMask(bgnum);
    u32 row = yoff & 0x7;
    u32 xt = xoff & ~0x7u;
    u32* out = BGLine(bgnum) - (xoff & 0x7);
    bool full = true;
    bool drawn = false;

    for (int t = 0; t < 33; t++, xt += 8, out += 8)
    {
        u16 tile = *(u16*)&vram[(tilemapaddr + ((xt & 0xF8) >> 2) + ((xt & widexmask) << 3)) & vrammask];
        u32 r = (tile & (1<<11)) ? (7 - row) : row;

        if (bpp8)
        {
            u64 pix = *(u64*)&vram[(tilesetaddr + ((tile & 0x03FF) << 6) + (r << 3)) & vrammask];
            if (pix == 0)
            {
                Zero8(out);
                full = false;
                continue;
            }
            drawn = true;
            if (HasZeroByte(pix)) full = false;
            const u32* pal = extpal ? GetBGExtPal(extpalslot, tile >> 12) : PalBG256;
            Expand8(out, pix, pal, flagmask, tile & (1<<10));
        }
        else
        {
            u32 pix = *(u32*)&vram[(tilesetaddr + ((tile & 0x03FF) << 5) + (r << 2)) & vrammask];
            if (pix == 0)
            {
                Zero8(out);
                full = false;
                continue;
            }
            drawn = true;
            if (HasZeroNibble(pix)) full = false;
            Expand4(out, pix, PalBG16 + ((tile & 0xF000) >> 8), flagmask, tile & (1<<10));
        }
    }

    if (drawn) LayerDrawn |= (1 << bgnum);
    if (full) LayerFull |= (1 << bgnum);
}

// a rotscale BG with no rotation or scaling along the line (rotA = 1.0, rotC = 0): the line is one
// row of tiles, drawn tile by tile like a text BG. map16: 16-bit map entries with flips and extended
// palettes (mixed affine/text), otherwise 8-bit entries (affine)
template<bool map16>
void BananaRenderer2D::DrawTiledScroll(u32 bgnum, u32 tilemapaddr, u32 tilesetaddr, u32 coordmask, u32 yshift,
                                       bool wrap, bool extpal, s32 rotX, s32 rotY)
{
    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    u32 overflowmask = wrap ? 0 : ~(coordmask | 0x7FF);
    u32 flagmask = BGFlagMask(bgnum);
    u32* line = BGLine(bgnum);

    if (rotY & overflowmask)
    {
        memset(line, 0, 256*4);
        return;
    }

    u32 size = (coordmask >> 8) + 8;      // map size in pixels (128..1024)
    u32 sizemask = size - 1;
    u32 rowbase = ((rotY & coordmask) >> 11) << (yshift - 3);
    u32 tileyoff = (rotY >> 8) & 0x7;

    s32 x0 = rotX >> 8;
    u32* out = line - (x0 & 0x7);
    s32 xt = x0 & ~0x7;
    bool full = true;
    bool drawn = false;

    for (int t = 0; t < 33; t++, xt += 8, out += 8)
    {
        if (!wrap && (u32)xt >= size)
        {
            Zero8(out);
            full = false;
            continue;
        }
        u32 tx = ((u32)xt & sizemask) >> 3;
        u32 tile;
        u32 pixaddr;
        bool hflip = false;
        if (map16)
        {
            tile = *(u16*)&vram[(tilemapaddr + ((rowbase + tx) << 1)) & vrammask];
            u32 ty = (tile & (1<<11)) ? (7 - tileyoff) : tileyoff;
            hflip = tile & (1<<10);
            pixaddr = tilesetaddr + ((tile & 0x03FF) << 6) + (ty << 3);
        }
        else
        {
            tile = vram[(tilemapaddr + rowbase + tx) & vrammask];
            pixaddr = tilesetaddr + (tile << 6) + (tileyoff << 3);
        }

        u64 pix = *(u64*)&vram[pixaddr & vrammask];
        if (pix == 0)
        {
            Zero8(out);
            full = false;
            continue;
        }
        drawn = true;
        if (HasZeroByte(pix)) full = false;
        const u32* pal = (map16 && extpal) ? GetBGExtPal(bgnum, tile >> 12) : PalBG256;
        Expand8(out, pix, pal, flagmask, hflip);
    }

    if (drawn) LayerDrawn |= (1 << bgnum);
    if (full) LayerFull |= (1 << bgnum);
}

void BananaRenderer2D::DrawBG_Affine(u32 line, u32 bgnum)
{
    u16 bgcnt = GPU2D.BGCnt[bgnum];
    u32 dispcnt = GPU2D.DispCnt;

    u32 coordmask;
    u32 yshift;
    switch ((bgcnt >> 14) & 0x3)
    {
    case 0: coordmask = 0x07800; yshift = 7; break;
    case 1: coordmask = 0x0F800; yshift = 8; break;
    case 2: coordmask = 0x1F800; yshift = 9; break;
    default: coordmask = 0x3F800; yshift = 10; break;
    }

    bool wrap = bgcnt & (1<<13);
    u32 overflowmask = wrap ? 0 : ~(coordmask | 0x7FF);

    s32 rotA = GPU2D.BGRotA[bgnum-2];
    s32 rotC = GPU2D.BGRotC[bgnum-2];
    s32 rotX = GPU2D.BGXRefInternal[bgnum-2];
    s32 rotY = GPU2D.BGYRefInternal[bgnum-2];

    u32 tilesetaddr, tilemapaddr;
    if (GPU2D.Num)
    {
        tilesetaddr = ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((bgcnt & 0x1F00) << 3);
    }
    else
    {
        tilesetaddr = ((dispcnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((dispcnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);
    }

    if (rotA == 0x100 && rotC == 0)
    {
        DrawTiledScroll<false>(bgnum, tilemapaddr, tilesetaddr, coordmask, yshift, wrap, false, rotX, rotY);
        return;
    }

    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    yshift -= 3;

    u32 flagmask = BGFlagMask(bgnum);
    u32* out = BGLine(bgnum);
    u32 acc = 0;   // OR of outputs, for emptiness
    u32 all = ~0u; // AND of outputs, for fullness

    for (int i = 0; i < 256; i++)
    {
        u32 c = 0;
        if (!((rotX | rotY) & overflowmask))
        {
            u8 tile = vram[(tilemapaddr + ((((rotY & coordmask) >> 11) << yshift) + ((rotX & coordmask) >> 11))) & vrammask];
            u32 tilexoff = (rotX >> 8) & 0x7;
            u32 tileyoff = (rotY >> 8) & 0x7;
            u8 color = vram[(tilesetaddr + (tile << 6) + (tileyoff << 3) + tilexoff) & vrammask];
            c = PalBG256[color] & flagmask;
        }
        out[i] = c;
        acc |= c;
        all &= c;

        rotX += rotA;
        rotY += rotC;
    }

    if (acc) LayerDrawn |= (1 << bgnum);
    if (all) LayerFull |= (1 << bgnum);
}

// direct-colour pixels, contiguous in VRAM and in the output
static inline void ConvertRun16(u32* out, const u16* src, int n, u32 flag, u32& acc, u32& all)
{
    int j = 0;
    if (n >= 4)
    {
        V4 vacc = VZero(), vall = VDup(~0u);
        V4 vflag = VDup(flag);
        V4 m1f = VDup(0x1F), m3e0 = VDup(0x3E0), m7c00 = VDup(0x7C00), m8000 = VDup(0x8000);
        for (; j + 4 <= n; j += 4)
        {
            V4 c = VLoadU16x4(src + j);
            V4 alpha = VNonZero(VAnd(c, m8000));
            V4 col = VOr(VOr(VShl<1>(VAnd(c, m1f)), VShl<4>(VAnd(c, m3e0))), VOr(VShl<7>(VAnd(c, m7c00)), vflag));
            col = VAnd(col, alpha);
            VStore(out + j, col);
            vacc = VOr(vacc, col);
            vall = VAnd(vall, col);
        }
        acc |= VHorOr(vacc);
        all &= VHorAnd(vall);
    }
    for (; j < n; j++)
    {
        u32 color = src[j];
        u32 c = (color & 0x8000) ? (Conv15(color & 0x7FFF) | flag) : 0;
        out[j] = c;
        acc |= c;
        all &= c;
    }
}

static inline void ConvertRun8(u32* out, const u8* src, int n, const u32* pal, u32 flagmask, u32& acc, u32& all)
{
    for (int j = 0; j < n; j++)
    {
        u32 c = pal[src[j]] & flagmask;
        out[j] = c;
        acc |= c;
        all &= c;
    }
}

// direct-colour bitmap BG (extended rotscale BG with BGCNT bit 7 and bit 2)
void BananaRenderer2D::DrawBitmap16(u32* out, u32 flag, u32 base, u32 xmask, u32 ymask, u32 yshift, bool wrap,
                                    s32 rotA, s32 rotC, s32 rotX, s32 rotY, u32 bgnum)
{
    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    u32 ofxmask = wrap ? 0 : ~xmask;
    u32 ofymask = wrap ? 0 : ~ymask;
    u32 acc = 0, all = ~0u;
    u32 width = (xmask >> 8) + 1;
    u32 rowaddr = base + ((((rotY & ymask) >> 8) << yshift) << 1);

    if (rotA == 0x100 && rotC == 0 && (rowaddr & vrammask) + width * 2 <= vrammask + 1)
    {
        // scroll only: one row of the bitmap, read left to right in contiguous runs
        if (rotY & ofymask)
        {
            memset(out, 0, 256*4);
            return;
        }
        const u16* row = (const u16*)&vram[rowaddr & vrammask];
        s32 x0 = rotX >> 8;
        if (wrap)
        {
            u32 x = (u32)x0 & (width - 1);
            int i = 0;
            while (i < 256)
            {
                int n = 256 - i;
                if (n > (int)(width - x)) n = width - x;
                ConvertRun16(out + i, row + x, n, flag, acc, all);
                i += n;
                x = 0;
            }
        }
        else
        {
            s32 first = -x0; if (first < 0) first = 0; if (first > 256) first = 256;
            s32 last = (s32)width - x0; if (last < 0) last = 0; if (last > 256) last = 256;
            if (last < first) last = first;
            if (first > 0) { memset(out, 0, first * 4); all = 0; }
            if (last > first) ConvertRun16(out + first, row + (x0 + first), last - first, flag, acc, all);
            if (last < 256) { memset(out + last, 0, (256 - last) * 4); all = 0; }
        }
    }
    else
    {
        for (int i = 0; i < 256; i++)
        {
            u32 c = 0;
            if (!(rotX & ofxmask) && !(rotY & ofymask))
            {
                u16 color = *(u16*)&vram[(base + (((((rotY & ymask) >> 8) << yshift) + ((rotX & xmask) >> 8)) << 1)) & vrammask];
                c = (color & 0x8000) ? (Conv15(color & 0x7FFF) | flag) : 0;
            }
            out[i] = c;
            acc |= c;
            all &= c;
            rotX += rotA;
            rotY += rotC;
        }
    }

    if (acc) LayerDrawn |= (1 << bgnum);
    if (all) LayerFull |= (1 << bgnum);
}

// 256-colour bitmap BG (extended rotscale BG with bit 7 set and bit 2 clear, or the mode 6 large BG)
void BananaRenderer2D::DrawBitmap8(u32* out, u32 flagmask, u32 base, u32 xmask, u32 ymask, u32 yshift, bool wrap,
                                   s32 rotA, s32 rotC, s32 rotX, s32 rotY, u32 bgnum)
{
    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    u32 ofxmask = wrap ? 0 : ~xmask;
    u32 ofymask = wrap ? 0 : ~ymask;
    u32 acc = 0, all = ~0u;
    u32 width = (xmask >> 8) + 1;
    u32 rowaddr = base + (((rotY & ymask) >> 8) << yshift);

    if (rotA == 0x100 && rotC == 0 && (rowaddr & vrammask) + width <= vrammask + 1)
    {
        if (rotY & ofymask)
        {
            memset(out, 0, 256*4);
            return;
        }
        const u8* row = &vram[rowaddr & vrammask];
        s32 x0 = rotX >> 8;
        if (wrap)
        {
            u32 x = (u32)x0 & (width - 1);
            int i = 0;
            while (i < 256)
            {
                int n = 256 - i;
                if (n > (int)(width - x)) n = width - x;
                ConvertRun8(out + i, row + x, n, PalBG256, flagmask, acc, all);
                i += n;
                x = 0;
            }
        }
        else
        {
            s32 first = -x0; if (first < 0) first = 0; if (first > 256) first = 256;
            s32 last = (s32)width - x0; if (last < 0) last = 0; if (last > 256) last = 256;
            if (last < first) last = first;
            if (first > 0) { memset(out, 0, first * 4); all = 0; }
            if (last > first) ConvertRun8(out + first, row + (x0 + first), last - first, PalBG256, flagmask, acc, all);
            if (last < 256) { memset(out + last, 0, (256 - last) * 4); all = 0; }
        }
    }
    else
    {
        for (int i = 0; i < 256; i++)
        {
            u32 c = 0;
            if (!(rotX & ofxmask) && !(rotY & ofymask))
                c = PalBG256[vram[(base + (((rotY & ymask) >> 8) << yshift) + ((rotX & xmask) >> 8)) & vrammask]] & flagmask;
            out[i] = c;
            acc |= c;
            all &= c;
            rotX += rotA;
            rotY += rotC;
        }
    }

    if (acc) LayerDrawn |= (1 << bgnum);
    if (all) LayerFull |= (1 << bgnum);
}

void BananaRenderer2D::DrawBG_Extended(u32 line, u32 bgnum)
{
    u16 bgcnt = GPU2D.BGCnt[bgnum];
    u32 dispcnt = GPU2D.DispCnt;

    s32 rotA = GPU2D.BGRotA[bgnum-2];
    s32 rotC = GPU2D.BGRotC[bgnum-2];
    s32 rotX = GPU2D.BGXRefInternal[bgnum-2];
    s32 rotY = GPU2D.BGYRefInternal[bgnum-2];

    u32* out = BGLine(bgnum);

    if (bgcnt & (1<<7))
    {
        // bitmap modes
        u32 xmask, ymask;
        u32 yshift;
        switch ((bgcnt >> 14) & 0x3)
        {
        case 0: xmask = 0x07FFF; ymask = 0x07FFF; yshift = 7; break;
        case 1: xmask = 0x0FFFF; ymask = 0x0FFFF; yshift = 8; break;
        case 2: xmask = 0x1FFFF; ymask = 0x0FFFF; yshift = 9; break;
        default: xmask = 0x1FFFF; ymask = 0x1FFFF; yshift = 9; break;
        }

        bool wrap = bgcnt & (1<<13);
        u32 base = ((bgcnt & 0x1F00) << 6);

        if (bgcnt & (1<<2))
            DrawBitmap16(out, 0x01000000u << bgnum, base, xmask, ymask, yshift, wrap, rotA, rotC, rotX, rotY, bgnum);
        else
            DrawBitmap8(out, BGFlagMask(bgnum), base, xmask, ymask, yshift, wrap, rotA, rotC, rotX, rotY, bgnum);
        return;
    }

    // mixed affine/text mode: 16-bit map entries, 256-colour tiles with flips, extended palettes
    u32 coordmask;
    u32 yshift;
    switch ((bgcnt >> 14) & 0x3)
    {
    case 0: coordmask = 0x07800; yshift = 7; break;
    case 1: coordmask = 0x0F800; yshift = 8; break;
    case 2: coordmask = 0x1F800; yshift = 9; break;
    default: coordmask = 0x3F800; yshift = 10; break;
    }

    bool wrap = bgcnt & (1<<13);
    u32 overflowmask = wrap ? 0 : ~(coordmask | 0x7FF);
    bool extpal = dispcnt & (1<<30);

    u32 tilesetaddr, tilemapaddr;
    if (GPU2D.Num)
    {
        tilesetaddr = ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((bgcnt & 0x1F00) << 3);
    }
    else
    {
        tilesetaddr = ((dispcnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((dispcnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);
    }

    if (rotA == 0x100 && rotC == 0)
    {
        DrawTiledScroll<true>(bgnum, tilemapaddr, tilesetaddr, coordmask, yshift, wrap, extpal, rotX, rotY);
        return;
    }

    u8* vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    yshift -= 3;

    u32 flagmask = BGFlagMask(bgnum);
    u32 acc = 0, all = ~0u;

    for (int i = 0; i < 256; i++)
    {
        u32 c = 0;
        if (!((rotX | rotY) & overflowmask))
        {
            u16 tile = *(u16*)&vram[(tilemapaddr + (((((rotY & coordmask) >> 11) << yshift) + ((rotX & coordmask) >> 11)) << 1)) & vrammask];

            u32 tilexoff = (rotX >> 8) & 0x7;
            u32 tileyoff = (rotY >> 8) & 0x7;
            if (tile & (1<<10)) tilexoff = 7 - tilexoff;
            if (tile & (1<<11)) tileyoff = 7 - tileyoff;

            u8 color = vram[(tilesetaddr + ((tile & 0x03FF) << 6) + (tileyoff << 3) + tilexoff) & vrammask];
            const u32* pal = extpal ? GetBGExtPal(bgnum, tile >> 12) : PalBG256;
            c = pal[color] & flagmask;
        }
        out[i] = c;
        acc |= c;
        all &= c;
        rotX += rotA;
        rotY += rotC;
    }

    if (acc) LayerDrawn |= (1 << bgnum);
    if (all) LayerFull |= (1 << bgnum);
}

void BananaRenderer2D::DrawBG_Large(u32 line)
{
    u16 bgcnt = GPU2D.BGCnt[2];

    // large BG sizes: 0: 512x1024, 1: 1024x512, 2: 512x256, 3: 512x512
    u32 xmask, ymask;
    u32 yshift;
    switch ((bgcnt >> 14) & 0x3)
    {
    case 0: xmask = 0x1FFFF; ymask = 0x3FFFF; yshift = 9; break;
    case 1: xmask = 0x3FFFF; ymask = 0x1FFFF; yshift = 10; break;
    case 2: xmask = 0x1FFFF; ymask = 0x0FFFF; yshift = 9; break;
    default: xmask = 0x1FFFF; ymask = 0x1FFFF; yshift = 9; break;
    }

    bool wrap = bgcnt & (1<<13);

    DrawBitmap8(BGLine(2), BGFlagMask(2), 0, xmask, ymask, yshift, wrap,
                GPU2D.BGRotA[0], GPU2D.BGRotC[0], GPU2D.BGXRefInternal[0], GPU2D.BGYRefInternal[0], 2);
}


// ---- sprites -----------------------------------------------------------------------------------

template<bool window>
inline void BananaRenderer2D::PutOBJ(s32 x, u32 color, u32 attr)
{
    if (window)
    {
        if (color) OBJWindow[x] = 1;
        return;
    }

    // melonDS DrawSpritePixel: an opaque pixel replaces a transparent one, or an opaque one of a
    // worse (higher) BG priority; a transparent pixel over a transparent one only updates the
    // attribute bits (recorded in the side table, reconstructed by the OBJ mosaic pass)
    u32 old = OBJLine[x];
    bool write = color && (!(old & OBJ_IsOpaque) || ((attr & OBJ_BGPrioMask) < (old & OBJ_BGPrioMask)));
    OBJLine[x] = write ? (color | attr) : old;
    OBJTransAttr[x] = (u8)(attr >> 16);
}

// four 4-bit pixels (one byte each, in pixel order) from four nibbles, forward and reversed
static inline u32 SpreadNibbles(u32 v)
{
    return (v & 0x000F) | ((v & 0x00F0) << 4) | ((v & 0x0F00) << 8) | ((v & 0xF000) << 12);
}
static inline u32 SpreadNibblesRev(u32 v)
{
    return ((v & 0xF000) >> 12) | ((v & 0x0F00) >> 0) | ((v & 0x00F0) << 12) | ((v & 0x000F) << 24);
}

// four consecutive sprite pixels (colour indices in the four bytes of 'idx4') with melonDS's
// per-pixel rules, four lanes at a time
inline void BananaRenderer2D::PutOBJ4(s32 x, u32 idx4, u32 attr)
{
    V4 idx = VFromBytes4(idx4);
    V4 old = VLoad(&OBJLine[x]);
    V4 opaque = VNonZero(idx);
    V4 oldOpaque = VNonZero(VAnd(old, VDup(OBJ_IsOpaque)));
    V4 better = VLtSmall(VDup(attr & OBJ_BGPrioMask), VAnd(old, VDup(OBJ_BGPrioMask)));
    V4 write = VAnd(opaque, VOr(VNot(oldOpaque), better));
    VStore(&OBJLine[x], VSel(write, VOr(idx, VDup(attr)), old));
    *(u32*)&OBJTransAttr[x] = ((attr >> 16) & 0xFF) * 0x01010101u;
}

// the per-sprite values melonDS's DrawSprites derives from OAM on every scanline
void BananaRenderer2D::RebuildSpriteTable()
{
    static const u8 spritewidth[16] =
    {
        8, 16, 8, 8,
        16, 32, 8, 8,
        32, 32, 16, 8,
        64, 64, 32, 8
    };
    static const u8 spriteheight[16] =
    {
        8, 8, 16, 8,
        16, 8, 32, 8,
        32, 16, 32, 8,
        64, 32, 64, 8
    };

    const u16* oam = (const u16*)&GPU.OAM[GPU2D.Num ? 0x400 : 0];
    int n = 0;
    for (int sprnum = 0; sprnum < 128; sprnum++)
    {
        const u16* attrib = &oam[sprnum*4];

        u16 sprtype = (attrib[0] >> 8) & 0x3;
        if (sprtype == 2) // disabled
            continue;

        bool iswin = (((attrib[0] >> 10) & 0x3) == 2);

        u32 sizeparam = (attrib[0] >> 14) | ((attrib[1] & 0xC000) >> 12);
        u32 width = spritewidth[sizeparam];
        u32 height = spriteheight[sizeparam];
        u32 boundwidth = width;
        u32 boundheight = height;

        if (sprtype == 3) // double-size rotscale sprite
        {
            boundwidth <<= 1;
            boundheight <<= 1;
        }

        s32 xpos = (s32)(attrib[1] << 23) >> 23;
        if (xpos <= -(s32)boundwidth)
            continue;

        SpriteEntry& e = SpriteTab[n++];
        e.num = sprnum;
        e.ypos = attrib[0] & 0xFF;
        e.boundheight = boundheight;
        e.flags = (sprtype & 1) | (iswin ? 2 : 0) | ((attrib[0] & (1<<12)) ? 4 : 0);
        e.xpos = xpos;
        e.width = width;
        e.height = height;
        e.boundwidth = boundwidth;
    }
    NumSpriteTab = n;
}

void BananaRenderer2D::DrawSprites(u32 line)
{
    // the OBJ buffers don't get updated at all if the 2D engine is disabled
    if (!GPU2D.Enabled)
        return;

    if (GPU2D.Num == 0)
    {
        auto objDirty = GPU.VRAMDirty_AOBJ.DeriveState(GPU.VRAMMap_AOBJ, GPU);
        GPU.MakeVRAMFlat_AOBJCoherent(objDirty);
    }
    else
    {
        auto objDirty = GPU.VRAMDirty_BOBJ.DeriveState(GPU.VRAMMap_BOBJ, GPU);
        GPU.MakeVRAMFlat_BOBJCoherent(objDirty);
    }

    NumSprites = 0;
    ObjXMin = 256;
    ObjXMax = 0;
    memset(OBJLine, 0, sizeof(OBJLine));
    memset(OBJTransAttr, 0, sizeof(OBJTransAttr));
    memset(OBJWindow, 0, sizeof(OBJWindow));

    if (!GPU2D.OBJEnable)
        return;

    u32 oambit = 1u << GPU2D.Num;
    if ((GPU.OAMDirty & oambit) || SpriteTabStale)
    {
        RebuildSpriteTable();
        GPU.OAMDirty &= ~oambit;
        SpriteTabStale = false;
    }

    for (int i = 0; i < NumSpriteTab; i++)
    {
        const SpriteEntry& e = SpriteTab[i];

        if (((line - e.ypos) & 0xFF) >= e.boundheight)
            continue;

        s32 ypos;
        if ((e.flags & 6) == 4)
        {
            // sprite mosaic (not for OBJ-window sprites): Y position latched by the register side
            ypos = (GPU2D.OBJMosaicLine - e.ypos) & 0xFF;
            if (ypos >= e.boundheight) ypos = 0;
        }
        else
            ypos = (line - e.ypos) & 0xFF;

        s32 xpos = e.xpos;
        if (!(e.flags & 2))
        {
            s32 x0 = xpos < 0 ? 0 : xpos;
            s32 x1 = xpos + e.boundwidth; if (x1 > 256) x1 = 256;
            if (x0 < ObjXMin) ObjXMin = x0;
            if (x1 > ObjXMax) ObjXMax = x1;
        }

        if (e.flags & 1)
        {
            if (e.flags & 2) DrawSprite_Rotscale<true>(e.num, e.boundwidth, e.boundheight, e.width, e.height, xpos, ypos);
            else             DrawSprite_Rotscale<false>(e.num, e.boundwidth, e.boundheight, e.width, e.height, xpos, ypos);
        }
        else
        {
            if (e.flags & 2) DrawSprite_Normal<true>(e.num, e.width, e.height, xpos, ypos);
            else             DrawSprite_Normal<false>(e.num, e.width, e.height, xpos, ypos);
        }

        NumSprites++;
    }
}

template<bool window>
void BananaRenderer2D::DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos)
{
    u16* oam = (u16*)&GPU.OAM[GPU2D.Num ? 0x400 : 0];
    u16* attrib = &oam[num * 4];
    u16* rotparams = &oam[(((attrib[1] >> 9) & 0x1F) * 16) + 3];
    u32 dispcnt = GPU2D.DispCnt;

    u32 pixelattr = ((attrib[2] & 0x0C00) << 6) | OBJ_IsSprite | OBJ_IsOpaque;
    u32 tilenum = attrib[2] & 0x03FF;
    u32 spritemode = window ? 0 : ((attrib[0] >> 10) & 0x3);

    u32 ytilefactor;

    u8* objvram;
    u32 objvrammask;
    GPU2D.GetOBJVRAM(objvram, objvrammask);

    s32 centerX = boundwidth >> 1;
    s32 centerY = boundheight >> 1;

    if ((attrib[0] & (1<<12)) && !window)
        pixelattr |= OBJ_Mosaic;

    u32 xoff;
    if (xpos >= 0)
    {
        xoff = 0;
        if ((xpos+boundwidth) > 256)
            boundwidth = 256-xpos;
    }
    else
    {
        xoff = -xpos;
        xpos = 0;
    }

    s16 rotA = (s16)rotparams[0];
    s16 rotB = (s16)rotparams[4];
    s16 rotC = (s16)rotparams[8];
    s16 rotD = (s16)rotparams[12];

    s32 rotX = ((xoff-centerX) * rotA) + ((ypos-centerY) * rotB) + (width << 7);
    s32 rotY = ((xoff-centerX) * rotC) + ((ypos-centerY) * rotD) + (height << 7);

    width <<= 8;
    height <<= 8;

    if (spritemode == 3)
    {
        u32 alpha = attrib[2] >> 12;
        if (!alpha) return;
        alpha++;

        pixelattr |= (0xC0000000 | (alpha << 24));

        u32 pixelsaddr;
        if (dispcnt & 0x40)
        {
            if (dispcnt & 0x20)
            {
                // 'reserved': draws nothing
                return;
            }
            else
            {
                pixelsaddr = tilenum << (7 + ((dispcnt >> 22) & 0x1));
                ytilefactor = ((width >> 8) * 2);
            }
        }
        else
        {
            if (dispcnt & 0x20)
            {
                pixelsaddr = ((tilenum & 0x01F) << 4) + ((tilenum & 0x3E0) << 7);
                ytilefactor = (256 * 2);
            }
            else
            {
                pixelsaddr = ((tilenum & 0x00F) << 4) + ((tilenum & 0x3F0) << 7);
                ytilefactor = (128 * 2);
            }
        }

        for (; xoff < boundwidth;)
        {
            if ((u32)rotX < width && (u32)rotY < height)
            {
                u16 color = *(u16*)&objvram[(pixelsaddr + ((rotY >> 8) * ytilefactor) + ((rotX >> 8) << 1)) & objvrammask];
                PutOBJ<window>(xpos, (color & 0x8000) ? color : 0, pixelattr);
            }

            rotX += rotA;
            rotY += rotC;
            xoff++;
            xpos++;
        }
    }
    else
    {
        u32 pixelsaddr = tilenum;
        if (dispcnt & (1<<4))
        {
            pixelsaddr <<= ((dispcnt >> 20) & 0x3);
            ytilefactor = (width >> 11) << ((attrib[0] & 0x2000) ? 1:0);
        }
        else
        {
            ytilefactor = 0x20;
        }

        if (spritemode == 1) pixelattr |= 0x80000000;
        else                 pixelattr |= 0x10000000;

        ytilefactor <<= 5;
        pixelsaddr <<= 5;

        if (attrib[0] & (1<<13))
        {
            // 256-color
            if (!window)
            {
                if (!(dispcnt & (1<<31)))
                    pixelattr |= OBJ_StandardPal;
                else
                    pixelattr |= ((attrib[2] & 0xF000) >> 4);
            }

            for (; xoff < boundwidth;)
            {
                if ((u32)rotX < width && (u32)rotY < height)
                {
                    u8 color = objvram[(pixelsaddr + ((rotY>>11)*ytilefactor) + ((rotY&0x700)>>5) + ((rotX>>11)*64) + ((rotX&0x700)>>8)) & objvrammask];
                    PutOBJ<window>(xpos, color, pixelattr);
                }

                rotX += rotA;
                rotY += rotC;
                xoff++;
                xpos++;
            }
        }
        else
        {
            // 16-color
            if (!window)
            {
                pixelattr |= OBJ_StandardPal;
                pixelattr |= ((attrib[2] & 0xF000) >> 8);
            }

            for (; xoff < boundwidth;)
            {
                if ((u32)rotX < width && (u32)rotY < height)
                {
                    u8 color = objvram[(pixelsaddr + ((rotY>>11)*ytilefactor) + ((rotY&0x700)>>6) + ((rotX>>11)*32) + ((rotX&0x700)>>9)) & objvrammask];
                    if (rotX & 0x100)
                        color >>= 4;
                    else
                        color &= 0x0F;
                    PutOBJ<window>(xpos, color, pixelattr);
                }

                rotX += rotA;
                rotY += rotC;
                xoff++;
                xpos++;
            }
        }
    }
}

template<bool window>
void BananaRenderer2D::DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos)
{
    u16* oam = (u16*)&GPU.OAM[GPU2D.Num ? 0x400 : 0];
    u16* attrib = &oam[num * 4];
    u32 dispcnt = GPU2D.DispCnt;

    u32 pixelattr = ((attrib[2] & 0x0C00) << 6) | OBJ_IsSprite | OBJ_IsOpaque;
    u32 tilenum = attrib[2] & 0x03FF;
    u32 spritemode = window ? 0 : ((attrib[0] >> 10) & 0x3);

    if ((attrib[0] & (1<<12)) && !window)
        pixelattr |= OBJ_Mosaic;

    u8* objvram;
    u32 objvrammask;
    GPU2D.GetOBJVRAM(objvram, objvrammask);

    // yflip
    if (attrib[1] & (1<<13))
        ypos = height-1 - ypos;

    u32 xoff;
    u32 xend = width;
    if (xpos >= 0)
    {
        xoff = 0;
        if ((xpos+xend) > 256)
            xend = 256-xpos;
    }
    else
    {
        xoff = -xpos;
        xpos = 0;
    }

    bool xflip = attrib[1] & (1<<12);

    if (spritemode == 3)
    {
        // bitmap sprite
        u32 alpha = attrib[2] >> 12;
        if (!alpha) return;
        alpha++;

        pixelattr |= (0xC0000000 | (alpha << 24));

        u32 pixelsaddr = tilenum;
        if (dispcnt & 0x40)
        {
            if (dispcnt & 0x20)
            {
                // 'reserved': draws nothing
                return;
            }
            else
            {
                pixelsaddr <<= (7 + ((dispcnt >> 22) & 0x1));
                pixelsaddr += (ypos * width * 2);
            }
        }
        else
        {
            if (dispcnt & 0x20)
            {
                pixelsaddr = ((tilenum & 0x01F) << 4) + ((tilenum & 0x3E0) << 7);
                pixelsaddr += (ypos * 256 * 2);
            }
            else
            {
                pixelsaddr = ((tilenum & 0x00F) << 4) + ((tilenum & 0x3F0) << 7);
                pixelsaddr += (ypos * 128 * 2);
            }
        }

        s32 pixelstride;
        if (xflip)
        {
            pixelsaddr += ((width-1) << 1);
            pixelsaddr -= (xoff << 1);
            pixelstride = -2;
        }
        else
        {
            pixelsaddr += (xoff << 1);
            pixelstride = 2;
        }

        for (; xoff < xend; xoff++, xpos++)
        {
            u16 color = *(u16*)&objvram[pixelsaddr & objvrammask];
            pixelsaddr += pixelstride;
            PutOBJ<window>(xpos, (color & 0x8000) ? color : 0, pixelattr);
        }
        return;
    }

    // tiled sprite: byte address of the tile row this scanline uses, tile column 0
    u32 tilebase = tilenum;
    if (dispcnt & (1<<4))
    {
        tilebase <<= ((dispcnt >> 20) & 0x3);
        tilebase += ((ypos >> 3) * (width >> 3)) << ((attrib[0] & 0x2000) ? 1:0);
    }
    else
    {
        tilebase += ((ypos >> 3) * 0x20);
    }
    tilebase <<= 5;

    if (spritemode == 1) pixelattr |= 0x80000000;
    else                 pixelattr |= 0x10000000;

    u32 ntiles = width >> 3;

    if (attrib[0] & (1<<13))
    {
        // 256-color: 64 bytes per tile, 8 bytes per row
        tilebase += ((ypos & 0x7) << 3);

        if (!window)
        {
            if (!(dispcnt & (1<<31)))
                pixelattr |= OBJ_StandardPal;
            else
                pixelattr |= ((attrib[2] & 0xF000) >> 4);
        }

        u32 x = xoff;
        while (x < xend)
        {
            u32 j = x >> 3;
            u32 k0 = x & 0x7;
            u32 k1 = xend - (j << 3); if (k1 > 8) k1 = 8;
            u32 sj = xflip ? (ntiles - 1 - j) : j;
            u64 pix = *(u64*)&objvram[(tilebase + (sj << 6)) & objvrammask];

            if (pix == 0 && !window)
            {
                u8 a = (u8)(pixelattr >> 16);
                for (u32 k = k0; k < k1; k++)
                    OBJTransAttr[xpos + (k - k0)] = a;
            }
            else if (!window && k0 == 0 && k1 == 8)
            {
                u32 lo = (u32)pix, hi = (u32)(pix >> 32);
                if (xflip) { u32 t = __builtin_bswap32(hi); hi = __builtin_bswap32(lo); lo = t; }
                PutOBJ4(xpos, lo, pixelattr);
                PutOBJ4(xpos + 4, hi, pixelattr);
            }
            else if (xflip)
            {
                for (u32 k = k0; k < k1; k++)
                    PutOBJ<window>(xpos + (k - k0), (pix >> ((7 - k) << 3)) & 0xFF, pixelattr);
            }
            else
            {
                for (u32 k = k0; k < k1; k++)
                    PutOBJ<window>(xpos + (k - k0), (pix >> (k << 3)) & 0xFF, pixelattr);
            }

            xpos += k1 - k0;
            x = (j << 3) + k1;
        }
    }
    else
    {
        // 16-color: 32 bytes per tile, 4 bytes per row
        tilebase += ((ypos & 0x7) << 2);

        if (!window)
        {
            pixelattr |= OBJ_StandardPal;
            pixelattr |= ((attrib[2] & 0xF000) >> 8);
        }

        u32 x = xoff;
        while (x < xend)
        {
            u32 j = x >> 3;
            u32 k0 = x & 0x7;
            u32 k1 = xend - (j << 3); if (k1 > 8) k1 = 8;
            u32 sj = xflip ? (ntiles - 1 - j) : j;
            u32 pix = *(u32*)&objvram[(tilebase + (sj << 5)) & objvrammask];

            if (pix == 0 && !window)
            {
                u8 a = (u8)(pixelattr >> 16);
                for (u32 k = k0; k < k1; k++)
                    OBJTransAttr[xpos + (k - k0)] = a;
            }
            else if (!window && k0 == 0 && k1 == 8)
            {
                u32 lo, hi;
                if (xflip) { lo = SpreadNibblesRev(pix >> 16); hi = SpreadNibblesRev(pix & 0xFFFF); }
                else       { lo = SpreadNibbles(pix & 0xFFFF); hi = SpreadNibbles(pix >> 16); }
                PutOBJ4(xpos, lo, pixelattr);
                PutOBJ4(xpos + 4, hi, pixelattr);
            }
            else if (xflip)
            {
                for (u32 k = k0; k < k1; k++)
                    PutOBJ<window>(xpos + (k - k0), (pix >> ((7 - k) << 2)) & 0xF, pixelattr);
            }
            else
            {
                for (u32 k = k0; k < k1; k++)
                    PutOBJ<window>(xpos + (k - k0), (pix >> (k << 2)) & 0xF, pixelattr);
            }

            xpos += k1 - k0;
            x = (j << 3) + k1;
        }
    }
}

// melonDS ApplySpriteMosaicX, with the transparent pixels' attribute words rebuilt from the side table
void BananaRenderer2D::ApplySpriteMosaicX()
{
    u8 mosw = GPU2D.OBJMosaicSize[0];
    if (mosw == 0) return;

    u8 mosx = 0;
    u32 latchcolor = 0;
    for (int i = 0; i < 256; i++)
    {
        u32 curcolor = OBJLine[i];
        if (!(curcolor & OBJ_IsOpaque))
            curcolor = ((u32)OBJTransAttr[i] << 16) & (OBJ_IsSprite | OBJ_Mosaic | OBJ_BGPrioMask);

        bool latch = false;

        if (mosx == 0)
            latch = true;
        else if (!(curcolor & OBJ_Mosaic))
            latch = true;
        else if (!(latchcolor & OBJ_Mosaic))
            latch = true;
        else if ((curcolor & OBJ_BGPrioMask) < (latchcolor & OBJ_BGPrioMask))
            latch = true;

        if (latch)
            latchcolor = curcolor;

        OBJLine[i] = latchcolor;

        if (mosx == mosw)
            mosx = 0;
        else
            mosx++;
    }

    // the mosaic can move opaque pixels anywhere along the line
    ObjXMin = 0;
    ObjXMax = 256;
}

// resolve the OBJ line into colours (melonDS InterleaveSprites, all priorities at once). The colour
// of a palette pixel comes from one table: standard palette entries at OBJ_StandardPal, the 16
// extended palettes below it, both exactly where the OBJ pixel's low 13 bits point.
template<bool windows>
void BananaRenderer2D::ResolveSpritesImpl()
{
    u8 present = 0;
    const u32* table = ObjTable.get();

    for (int i = ObjXMin; i < ObjXMax; i++)
    {
        u32 p = OBJLine[i];
        if (!(p & OBJ_IsOpaque) || (windows && !(WindowMask[i] & 0x10)))
        {
            OBJColor[i] = 0;
            continue;
        }

        u32 c = (p & OBJ_DirectColor) ? Conv15(p & 0x7FFF) : table[p & 0x1FFF];
        OBJColor[i] = c | (p & 0xFF000000);
        u32 prio = (p >> 16) & 0x3;
        OBJPrio[i] = prio;
        present |= (1 << prio);
    }

    OBJPrioPresent = present;
}

void BananaRenderer2D::ResolveSprites()
{
    int x0 = ObjXMin, x1 = ObjXMax;
    if (x1 <= x0)
    {
        memset(OBJColor, 0, sizeof(OBJColor));
        OBJPrioPresent = 0;
        return;
    }
    if (x0 > 0) memset(OBJColor, 0, x0 * 4);
    if (x1 < 256) memset(OBJColor + x1, 0, (256 - x1) * 4);

    if (!ObjExtValid && (GPU2D.DispCnt & (1u << 31)))
        ConvertOBJExtPals();

    if (WindowsOn) ResolveSpritesImpl<true>();
    else           ResolveSpritesImpl<false>();
}


// ---- compositing -------------------------------------------------------------------------------

struct CompStep
{
    const u32* line;
    const u32* prio;   // null for a BG; the OBJ priority line for a sprite step
    u32 prioval;
};

// painter's algorithm over N layer lines, four pixels at a time with the top (and second) pixel kept
// in registers; startLine is the layer everything is painted onto (the backdrop, or a layer that
// covers the whole line)
template<int N, bool needSecond>
static void CompositeN(const CompStep* steps, const u32* startLine, u32* outTop, u32* outSecond)
{
    for (int i = 0; i < 256; i += 4)
    {
        V4 top = VLoad(startLine + i);
        V4 sec = VZero();
        for (int k = 0; k < N; k++)
        {
            V4 c = VLoad(steps[k].line + i);
            V4 m = VNonZero(c);
            if (steps[k].prio)
                m = VAnd(m, VEq(VLoad(steps[k].prio + i), VDup(steps[k].prioval)));
            if (needSecond)
                sec = VSel(m, top, sec);
            top = VSel(m, c, top);
        }
        VStore(outTop + i, top);
        if (needSecond)
            VStore(outSecond + i, sec);
    }
}

void BananaRenderer2D::Composite(u32* outTop, u32* outSecond, bool needSecond)
{
    // painter's order, back to front: for each priority 3..0, BG3, BG2, BG1, BG0 of that priority,
    // then the sprites of that priority (the same order as melonDS)
    CompStep steps[8];
    u8 full[8];
    int n = 0;
    for (int p = 3; p >= 0; p--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if ((LayerDrawn & (1<<bg)) && ((GPU2D.BGCnt[bg] & 0x3) == p))
            {
                steps[n] = { BGLine(bg), nullptr, 0 };
                full[n] = (LayerFull >> bg) & 1;
                n++;
            }
        }
        if (OBJPrioPresent & (1<<p))
        {
            steps[n] = { OBJColor, OBJPrio, (u32)p };
            full[n] = 0;
            n++;
        }
    }

    // layers under the top-most fully opaque layer (the two top-most when the second layer is
    // needed for blending) can't show through: paint onto that layer and skip what is below
    int start = 0;
    int wanted = needSecond ? 2 : 1;
    int found = 0;
    const u32* startLine = BackdropLine;
    for (int k = n - 1; k >= 0; k--)
    {
        if (full[k])
        {
            found++;
            if (found == wanted)
            {
                start = k + 1;
                startLine = steps[k].line;
                break;
            }
        }
    }

    const CompStep* s = steps + start;
    switch (n - start)
    {
#define BANANA_COMP(N) case N: if (needSecond) CompositeN<N, true>(s, startLine, outTop, outSecond); else CompositeN<N, false>(s, startLine, outTop, outSecond); break;
    BANANA_COMP(0) BANANA_COMP(1) BANANA_COMP(2) BANANA_COMP(3)
    BANANA_COMP(4) BANANA_COMP(5) BANANA_COMP(6) BANANA_COMP(7) BANANA_COMP(8)   // at most 4 BGs + 4 OBJ steps
    default: break;
#undef BANANA_COMP
    }
}

// brightness up/down with no 2nd targets in BLDCNT: the only effect that can apply is on 1st-target
// pixels inside the effect window
template<bool up>
void BananaRenderer2D::BrightnessPass(u32* dst)
{
    u32 blendCnt = GPU2D.BlendCnt;
    u32 evy = GPU2D.EVY;
    const u8* wm = WindowsOn ? WindowMask : AllOnes;

    for (int i = 0; i < 256; i++)
    {
        u32 val = TopLine[i];
        u32 f1 = val >> 24;
        u32 fb = (f1 & 0x40) ? 0x01 : f1;
        fb = (f1 & 0x80) ? 0x10 : fb;
        bool hit = (blendCnt & fb) && (wm[i] & 0x20);
        u32 e = up ? ColorBrightnessUp(val, evy, 0x8) : ColorBrightnessDown(val, evy, 0x7);
        dst[i] = hit ? e : val;
    }
}

// melonDS ColorComposite with the 2nd-target bits set: sprite and 3D blending, alpha, brightness
template<u32 effect>
void BananaRenderer2D::EffectsPass(u32* dst)
{
    u32 blendCnt = GPU2D.BlendCnt;
    const u8* wm = WindowsOn ? WindowMask : AllOnes;
    u32 EVA = GPU2D.EVA, EVB = GPU2D.EVB, EVY = GPU2D.EVY;

    for (int i = 0; i < 256; i++)
    {
        u32 val1 = TopLine[i];
        u32 val2 = SecondLine[i];
        u32 flag1 = val1 >> 24;
        bool second = blendCnt & Target2Tab.v[val2 >> 24];

        u32 out;
        if (flag1 & 0x80)
        {
            // sprite blending (semi-transparent or bitmap sprite over a 2nd target)
            if (second)
            {
                u32 eva, evb;
                if (flag1 & 0x40)
                {
                    eva = flag1 & 0x1F;
                    evb = 16 - eva;
                }
                else
                {
                    eva = EVA;
                    evb = EVB;
                }
                out = ColorBlend4(val1, val2, eva, evb);
            }
            else if (effect >= 2 && (blendCnt & 0x10) && (wm[i] & 0x20))
                out = (effect == 2) ? ColorBrightnessUp(val1, EVY, 0x8) : ColorBrightnessDown(val1, EVY, 0x7);
            else
                out = val1;
        }
        else if (flag1 & 0x40)
        {
            // 3D layer over a 2nd target
            if (second)
                out = ColorBlend5(val1, val2);
            else if (effect >= 2 && (blendCnt & 0x01) && (wm[i] & 0x20))
                out = (effect == 2) ? ColorBrightnessUp(val1, EVY, 0x8) : ColorBrightnessDown(val1, EVY, 0x7);
            else
                out = val1;
        }
        else
        {
            out = val1;
            if (effect != 0 && (blendCnt & flag1) && (wm[i] & 0x20))
            {
                if (effect == 1)
                {
                    if (second) out = ColorBlend4(val1, val2, EVA, EVB);
                }
                else if (effect == 2)
                    out = ColorBrightnessUp(val1, EVY, 0x8);
                else
                    out = ColorBrightnessDown(val1, EVY, 0x7);
            }
        }
        dst[i] = out;
    }
}


// ---- scanline ----------------------------------------------------------------------------------

void BananaRenderer2D::DrawScanline(u32 line)
{
    u32* dst = Parent.Output2D[GPU2D.Num];

    if (!GPU2D.Enabled)
    {
        // if this 2D unit is disabled in POWCNT, the output is a fixed color
        // (black for unit A, white for unit B)
        u32 fillcolor = (GPU2D.Num == 0) ? 0xFF000000 : 0xFF3F3F3F;
        for (int i = 0; i < 256; i++)
            dst[i] = fillcolor;
        return;
    }

    if (GPU2D.ForcedBlank)
    {
        for (int i = 0; i < 256; i++)
            dst[i] = 0xFF3F3F3F;
        return;
    }

    bool bgExtChanged, objExtChanged;
    if (GPU2D.Num == 0)
    {
        auto bgDirty = GPU.VRAMDirty_ABG.DeriveState(GPU.VRAMMap_ABG, GPU);
        GPU.MakeVRAMFlat_ABGCoherent(bgDirty);
        auto bgExtPalDirty = GPU.VRAMDirty_ABGExtPal.DeriveState(GPU.VRAMMap_ABGExtPal, GPU);
        bgExtChanged = GPU.MakeVRAMFlat_ABGExtPalCoherent(bgExtPalDirty);
        auto objExtPalDirty = GPU.VRAMDirty_AOBJExtPal.DeriveState(&GPU.VRAMMap_AOBJExtPal, GPU);
        objExtChanged = GPU.MakeVRAMFlat_AOBJExtPalCoherent(objExtPalDirty);
    }
    else
    {
        auto bgDirty = GPU.VRAMDirty_BBG.DeriveState(GPU.VRAMMap_BBG, GPU);
        GPU.MakeVRAMFlat_BBGCoherent(bgDirty);
        auto bgExtPalDirty = GPU.VRAMDirty_BBGExtPal.DeriveState(GPU.VRAMMap_BBGExtPal, GPU);
        bgExtChanged = GPU.MakeVRAMFlat_BBGExtPalCoherent(bgExtPalDirty);
        auto objExtPalDirty = GPU.VRAMDirty_BOBJExtPal.DeriveState(&GPU.VRAMMap_BOBJExtPal, GPU);
        objExtChanged = GPU.MakeVRAMFlat_BOBJExtPalCoherent(objExtPalDirty);
    }
    if (bgExtChanged) BGExtPalValid = 0;
    if (objExtChanged) ObjExtValid = false;

    UpdatePalettes();

    u32 dispcnt = GPU2D.DispCnt;
    u8 enable = GPU2D.LayerEnable;

    WindowsOn = (dispcnt & 0xE000) != 0;
    if (WindowsOn)
        CalcWindowMask();

    LayerDrawn = 0;
    LayerFull = 0;

    // BG types per mode: 0 none, 1 text, 2 affine, 3 extended, 4 large, 5 3D
    enum { T_None, T_Text, T_Affine, T_Ext, T_Large, T_3D };
    u8 type[4] = { T_None, T_None, T_None, T_None };
    bool bg0_3d = (!GPU2D.Num) && (dispcnt & 0x8);
    switch (dispcnt & 0x7)
    {
    case 0: type[0] = T_Text; type[1] = T_Text; type[2] = T_Text;   type[3] = T_Text;   break;
    case 1: type[0] = T_Text; type[1] = T_Text; type[2] = T_Text;   type[3] = T_Affine; break;
    case 2: type[0] = T_Text; type[1] = T_Text; type[2] = T_Affine; type[3] = T_Affine; break;
    case 3: type[0] = T_Text; type[1] = T_Text; type[2] = T_Text;   type[3] = T_Ext;    break;
    case 4: type[0] = T_Text; type[1] = T_Text; type[2] = T_Affine; type[3] = T_Ext;    break;
    case 5: type[0] = T_Text; type[1] = T_Text; type[2] = T_Ext;    type[3] = T_Ext;    break;
    case 6: type[0] = bg0_3d ? T_3D : T_None; type[2] = T_Large; break;
    case 7: type[0] = T_Text; type[1] = T_Text; break;
    }
    if (bg0_3d && type[0] == T_Text) type[0] = T_3D;

    u32 bgmosaic = GPU2D.BGMosaicSize[0];

    for (u32 bg = 0; bg < 4; bg++)
    {
        if (!(enable & (1<<bg)) || type[bg] == T_None)
            continue;

        u16 bgcnt = GPU2D.BGCnt[bg];
        u32* L = BGLine(bg);

        switch (type[bg])
        {
        case T_3D:
            DrawBG_3D();
            LayerDrawn |= (1<<bg);
            break;
        case T_Text:
            if (bgcnt & (1<<7)) DrawBG_Text<true>(line, bg);
            else                DrawBG_Text<false>(line, bg);
            break;
        case T_Affine:
            DrawBG_Affine(line, bg);
            break;
        case T_Ext:
            DrawBG_Extended(line, bg);
            break;
        case T_Large:
            DrawBG_Large(line);
            break;
        }

        if (!(LayerDrawn & (1<<bg)))
            continue;

        if (type[bg] != T_3D && (bgcnt & (1<<6)) && bgmosaic > 0)
            ApplyMosaicX(L, bgmosaic);

        if (WindowsOn)
            ApplyWindow(L, 1 << bg);
    }
    if (WindowsOn)
        LayerFull = 0;

    if ((enable & (1<<4)) && NumSprites)
    {
        ApplySpriteMosaicX();
        ResolveSprites();
    }
    else
        OBJPrioPresent = 0;

    // which colour effects can apply on this line? blending (sprite, 3D or the alpha effect) needs
    // a 2nd target in BLDCNT; brightness needs 1st targets
    u32 blendCnt = GPU2D.BlendCnt;
    u32 effect = (blendCnt >> 6) & 0x3;
    bool secondTargets = (blendCnt & 0x3F00) != 0;
    bool possible = secondTargets || ((effect >= 2) && (blendCnt & 0x3F));

    if (!possible)
    {
        Composite(dst, nullptr, false);
        return;
    }

    if (!secondTargets)
    {
        Composite(TopLine, nullptr, false);
        if (effect == 2) BrightnessPass<true>(dst);
        else             BrightnessPass<false>(dst);
        return;
    }

    Composite(TopLine, SecondLine, true);
    switch (effect)
    {
    case 0: EffectsPass<0>(dst); break;
    case 1: EffectsPass<1>(dst); break;
    case 2: EffectsPass<2>(dst); break;
    default: EffectsPass<3>(dst); break;
    }
}

}
