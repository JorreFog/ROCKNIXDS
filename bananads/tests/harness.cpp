// harness.cpp: drives melonDS's stock 2D renderer and the Banana 2D engine over identical GPU state,
// line by line in the order GPU::StartHBlank uses, and compares every output pixel; times both.
//
//   banana2d-test                 scenes + fuzz (200 seeds) + benchmark
//   banana2d-test --scenes        the hand-made scenes only
//   banana2d-test --fuzz N        N random scenes (--seed S starts at S)
//   banana2d-test --bench N       N frames per scene per renderer
//   banana2d-test --dump DIR      write PPMs of the first mismatching frame
//
// Copyright 2026 ROCKNIXDS contributors. GPL-3.0-or-later, like melonDS.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU3D.h"
#include "GPU_Soft.h"
#include "GPU_Banana.h"

using namespace melonDS;

namespace melonDS::Platform { void SetLogThreshold(int level); }

static double NowMs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

struct Rng
{
    u64 s;
    explicit Rng(u64 seed)
    {
        s = seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
        if (!s) s = 1;
        next(); next();
    }
    u32 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (u32)(s >> 16); }
    u32 range(u32 n) { return n ? next() % n : 0; }
    int rangei(int lo, int hi) { return lo + (int)range((u32)(hi - lo + 1)); }
    bool chance(int pct) { return range(100) < (u32)pct; }
};

// ---- a 3D renderer that returns deterministic synthetic lines ----------------------------------

class Fake3D : public Renderer3D
{
public:
    Fake3D(melonDS::GPU3D& g, int mode, u32 seed) : Renderer3D(g), Mode(mode), Seed(seed) {}
    void Reset() override {}
    void RenderFrame() override {}
    u32* GetLine(int line) override
    {
        if (Mode == 0)
        {
            memset(Line, 0, sizeof(Line));
            return Line;
        }
        Rng r((u64)Seed * 977 + line);
        for (int i = 0; i < 256; i++)
        {
            u32 rgb, alpha;
            if (Mode == 1)
            {
                rgb = r.next() & 0x3F3F3F;
                alpha = r.range(32);
            }
            else
            {
                int dx = i - 128, dy = line - 96;
                int d2 = dx*dx + dy*dy;
                if (d2 < 60*60) alpha = 31;
                else if (d2 < 80*80) alpha = 1 + r.range(31);
                else alpha = 0;
                rgb = ((i >> 2) & 0x3F) | (((line >> 2) & 0x3F) << 8) | ((((i + line) >> 3) & 0x3F) << 16);
            }
            Line[i] = alpha ? (rgb | (alpha << 24)) : 0;
        }
        return Line;
    }
    alignas(16) u32 Line[256];
    int Mode;
    u32 Seed;
};

struct StockR : public SoftRenderer
{
    using SoftRenderer::SoftRenderer;
    void Set3D(std::unique_ptr<Renderer3D> r) { Rend3D = std::move(r); }
};

struct BananaR : public BananaRenderer
{
    using BananaRenderer::BananaRenderer;
    void Set3D(std::unique_ptr<Renderer3D> r) { Rend3D = std::move(r); }
};

// ---- GPU state helpers -------------------------------------------------------------------------

static const u32 LCDCBase[9] = { 0x06800000, 0x06820000, 0x06840000, 0x06860000, 0x06880000,
                                 0x06890000, 0x06894000, 0x06898000, 0x068A0000 };
static const u32 BankSize[9] = { 0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x4000, 0x4000, 0x8000, 0x4000 };

static void MapBank(GPU& g, int bank, u8 cnt)
{
    switch (bank)
    {
    case 0: case 1: g.MapVRAM_AB(bank, cnt); break;
    case 2: case 3: g.MapVRAM_CD(bank, cnt); break;
    case 4: g.MapVRAM_E(bank, cnt); break;
    case 5: case 6: g.MapVRAM_FG(bank, cnt); break;
    case 7: g.MapVRAM_H(bank, cnt); break;
    case 8: g.MapVRAM_I(bank, cnt); break;
    }
}

// write to a bank while it is mapped to LCDC (as games load VRAM)
static void W16(GPU& g, int bank, u32 off, u16 v) { g.WriteVRAM_LCDC<u16>(LCDCBase[bank] + (off & (BankSize[bank] - 1)), v); }
static void W32(GPU& g, int bank, u32 off, u32 v) { g.WriteVRAM_LCDC<u32>(LCDCBase[bank] + (off & (BankSize[bank] - 1)), v); }
static void W8(GPU& g, int bank, u32 off, u8 v) { g.WriteVRAM_LCDC<u8>(LCDCBase[bank] + (off & (BankSize[bank] - 1)), v); }

static void ResetGPUState(NDS& nds)
{
    GPU& g = nds.GPU;
    // everything to LCDC, then clear
    for (int b = 0; b < 9; b++) MapBank(g, b, 0x80);
    memset(g.VRAM_A, 0, sizeof(g.VRAM_A)); memset(g.VRAM_B, 0, sizeof(g.VRAM_B));
    memset(g.VRAM_C, 0, sizeof(g.VRAM_C)); memset(g.VRAM_D, 0, sizeof(g.VRAM_D));
    memset(g.VRAM_E, 0, sizeof(g.VRAM_E)); memset(g.VRAM_F, 0, sizeof(g.VRAM_F));
    memset(g.VRAM_G, 0, sizeof(g.VRAM_G)); memset(g.VRAM_H, 0, sizeof(g.VRAM_H));
    memset(g.VRAM_I, 0, sizeof(g.VRAM_I));
    for (int b = 0; b < 9; b++) memset(g.VRAMDirty[b].Data, 0xFF, sizeof(g.VRAMDirty[b].Data));
    memset(g.Palette, 0, sizeof(g.Palette));
    memset(g.OAM, 0, sizeof(g.OAM));
    g.PaletteDirty = 0xFF;
    g.OAMDirty = 0x3;
    g.GPU2D_A.Reset(); g.GPU2D_B.Reset();
    g.GPU2D_A.Enabled = true; g.GPU2D_B.Enabled = true;
    g.MasterBrightnessA = 0; g.MasterBrightnessB = 0;
    g.ScreensEnabled = true; g.ScreenSwap = false;
    g.CaptureCnt = 0; g.CaptureEnable = false;
    memset(g.DispFIFOBuffer, 0, sizeof(g.DispFIFOBuffer));
    g.VCount = 0;
}

// the usual layout: A,B -> ABG; E -> AOBJ; C -> BBG; D -> BOBJ; F -> ABG ext pal slots 0-1;
// G -> AOBJ ext pal (or ABG ext pal slots 2-3); H -> BBG ext pal; I -> BOBJ ext pal
enum { BK_A, BK_B, BK_C, BK_D, BK_E, BK_F, BK_G, BK_H, BK_I };
static void StandardMapping(GPU& g, bool gSlots23 = false)
{
    MapBank(g, BK_A, 0x81);
    MapBank(g, BK_B, 0x89);
    MapBank(g, BK_C, 0x84);
    MapBank(g, BK_D, 0x84);
    MapBank(g, BK_E, 0x82);
    MapBank(g, BK_F, 0x84);
    MapBank(g, BK_G, gSlots23 ? 0x8C : 0x85);
    MapBank(g, BK_H, 0x82);
    MapBank(g, BK_I, 0x83);
}

// ---- content generators (all through LCDC writes, before the banks are mapped) -------------------

static void GenTiles16(GPU& g, int bank, u32 addr, int ntiles, Rng& rng, int zeroPct)
{
    for (int t = 0; t < ntiles; t++)
        for (int row = 0; row < 8; row++)
        {
            u32 v = 0;
            for (int k = 0; k < 8; k++)
            {
                u32 nib = rng.chance(zeroPct) ? 0 : (1 + rng.range(15));
                v |= nib << (k*4);
            }
            W32(g, bank, addr + t*32 + row*4, v);
        }
}

static void GenTiles256(GPU& g, int bank, u32 addr, int ntiles, Rng& rng, int zeroPct)
{
    for (int t = 0; t < ntiles; t++)
        for (int i = 0; i < 64; i++)
            W8(g, bank, addr + t*64 + i, rng.chance(zeroPct) ? 0 : (u8)(1 + rng.range(255)));
}

// text map entries: 'blocks' 2KB blocks of 32x32 entries
static void GenTextMap(GPU& g, int bank, u32 addr, int blocks, int ntiles, Rng& rng, bool palbits, bool flips, int emptyPct)
{
    for (int i = 0; i < blocks * 1024; i++)
    {
        u16 e = (u16)rng.range(ntiles);
        if (rng.chance(emptyPct)) e = 0;
        if (flips) e |= (rng.chance(30) ? 0x400 : 0) | (rng.chance(30) ? 0x800 : 0);
        if (palbits) e |= rng.range(16) << 12;
        W16(g, bank, addr + i*2, e);
    }
}

static void GenAffineMap(GPU& g, int bank, u32 addr, int entries, int ntiles, Rng& rng)
{
    for (int i = 0; i < entries; i++)
        W8(g, bank, addr + i, (u8)rng.range(ntiles));
}

static void GenBitmap16(GPU& g, int bank, u32 addr, int w, int h, Rng& rng, int alphaPct, bool shaded)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            u16 c;
            if (shaded) c = (x & 31) | (((y >> 1) & 31) << 5) | ((((x + y) >> 2) & 31) << 10);
            else c = rng.next() & 0x7FFF;
            if (rng.chance(alphaPct)) c |= 0x8000;
            W16(g, bank, addr + (y*w + x)*2, c);
        }
}

static void GenBitmap8(GPU& g, int bank, u32 addr, int w, int h, Rng& rng, int zeroPct)
{
    for (int i = 0; i < w*h; i++)
        W8(g, bank, addr + i, rng.chance(zeroPct) ? 0 : (u8)(1 + rng.range(255)));
}

static void GenPalette(GPU& g, u32 base, int count, Rng& rng)
{
    for (int i = 0; i < count; i++)
        g.WritePalette<u16>(base + i*2, (u16)rng.next());
}

// a whole bank of palette data (ext palettes), random colours
static void GenExtPal(GPU& g, int bank, u32 bytes, Rng& rng)
{
    for (u32 i = 0; i < bytes; i += 2)
        W16(g, bank, i, (u16)rng.next());
}

static void SetOAM(GPU& g, int unit, int idx, u16 a0, u16 a1, u16 a2)
{
    u32 base = unit * 0x400 + idx * 8;
    g.WriteOAM<u16>(base + 0, a0);
    g.WriteOAM<u16>(base + 2, a1);
    g.WriteOAM<u16>(base + 4, a2);
}

static void SetRot(GPU& g, int unit, int group, s16 a, s16 b, s16 c, s16 d)
{
    u32 base = unit * 0x400 + group * 32 + 6;
    g.WriteOAM<u16>(base + 0, (u16)a);
    g.WriteOAM<u16>(base + 8, (u16)b);
    g.WriteOAM<u16>(base + 16, (u16)c);
    g.WriteOAM<u16>(base + 24, (u16)d);
}

// attr0: y | mode<<10 | mosaic<<12 | 256col<<13 | shape<<14 (+ rotscale<<8, double<<9)
// attr1: x9 | (hflip<<12 | vflip<<13 | or rotgroup<<9) | size<<14
// attr2: tile | prio<<10 | pal<<12
static u16 A0(int y, int rotscale, int dbl_or_disable, int mode, int mosaic, int c256, int shape)
{
    return (u16)((y & 0xFF) | (rotscale << 8) | (dbl_or_disable << 9) | (mode << 10) | (mosaic << 12) | (c256 << 13) | (shape << 14));
}
static u16 A1(int x, int flags, int size) { return (u16)((x & 0x1FF) | (flags << 12) | (size << 14)); }
static u16 A1Rot(int x, int group, int size) { return (u16)((x & 0x1FF) | (group << 9) | (size << 14)); }
static u16 A2(int tile, int prio, int pal) { return (u16)((tile & 0x3FF) | (prio << 10) | (pal << 12)); }

// ---- scenes ------------------------------------------------------------------------------------

struct Scene
{
    std::string name;
    std::function<void(NDS&)> setup;
    std::function<void(NDS&, int frame, int line)> perLine;   // CPU writes during line 'line'
    int mode3D = 0;
    u32 seed3D = 0;
    int frames = 3;
    bool checkVRAM = false;
};

// 2D register helpers (direct, like a CPU write after masking)
static GPU2D& U(NDS& nds, int unit) { return unit ? nds.GPU.GPU2D_B : nds.GPU.GPU2D_A; }

static void SetBG(GPU2D& u, int bg, u16 cnt, u16 x, u16 y)
{
    u.BGCnt[bg] = cnt;
    u.BGXPos[bg] = x;
    u.BGYPos[bg] = y;
}

static void SetRotBG(GPU2D& u, int idx, s16 a, s16 b, s16 c, s16 d, s32 xref, s32 yref)
{
    u.BGRotA[idx] = a; u.BGRotB[idx] = b; u.BGRotC[idx] = c; u.BGRotD[idx] = d;
    u.BGXRef[idx] = xref; u.BGYRef[idx] = yref;
    u.BGXRefReload[idx] = xref; u.BGYRefReload[idx] = yref;
}

static void SetWindow(GPU2D& u, int win, int x1, int x2, int y1, int y2)
{
    u8* c = win ? u.Win1Coords : u.Win0Coords;
    c[0] = x1; c[1] = x2; c[2] = y1; c[3] = y2;
}

// 1D-mapped 16-colour sprite tiles live at AOBJ 0; 256-colour at 0x8000; bitmaps at 0xC000
static void GenSpriteGraphics(GPU& g, int objBank, Rng& rng)
{
    GenTiles16(g, objBank, 0x0000, 1024, rng, 35);        // 32KB of 16-colour tiles
    GenTiles256(g, objBank, 0x8000, 256, rng, 35);        // 16KB of 256-colour tiles
    GenBitmap16(g, objBank, 0xC000, 128, 64, rng, 70, false); // 16KB bitmap area
}

// a sprite field: n sprites, 16/256-colour tiles, some flips, some modes
static void SpriteField(GPU& g, int unit, int n, Rng& rng, int semiPct, int bitmapPct, int rotPct, int winPct, int mosaicPct)
{
    for (int i = 0; i < 128; i++)
        SetOAM(g, unit, i, A0(0, 0, 1, 0, 0, 0, 0), 0, 0);   // disabled
    for (int grp = 0; grp < 32; grp++)
    {
        double ang = (grp * 11) * 3.14159 / 64;
        double sc = 0.75 + (grp % 5) * 0.2;
        s16 a = (s16)lrint(cos(ang) / sc * 256), b = (s16)lrint(sin(ang) / sc * 256);
        SetRot(g, unit, grp, a, (s16)-b, b, a);
    }
    for (int i = 0; i < n; i++)
    {
        int shape = rng.range(3);
        int size = rng.range(4);
        int x = rng.rangei(-24, 250);
        int y = rng.rangei(-16, 200);
        int mode = rng.chance(semiPct) ? 1 : 0;
        if (rng.chance(winPct)) mode = 2;
        if (rng.chance(bitmapPct)) mode = 3;
        int c256 = rng.chance(40);
        int rot = rng.chance(rotPct);
        int dbl = rot && rng.chance(30);
        int mosaic = rng.chance(mosaicPct);
        int prio = rng.range(4);
        int tile;
        if (mode == 3) tile = 0x300 + rng.range(0x80);          // bitmaps at 0xC000 (1D: tile<<7)
        else if (c256) tile = 0x200 + (rng.range(0x60) & ~1);   // 256-colour tiles at 0x8000 (tile<<5)
        else tile = rng.range(0x3C0);
        u16 a2 = A2(tile, prio, mode == 3 ? (1 + rng.range(15)) : rng.range(16));
        u16 a1 = rot ? A1Rot(x, rng.range(32), size) : A1(x, rng.range(4), size);
        SetOAM(g, unit, i, A0(y, rot, dbl, mode, mosaic, c256, shape), a1, a2);
    }
}

static void CommonPalettes(GPU& g, Rng& rng)
{
    GenPalette(g, 0x000, 256, rng);   // A BG
    GenPalette(g, 0x200, 256, rng);   // A OBJ
    GenPalette(g, 0x400, 256, rng);   // B BG
    GenPalette(g, 0x600, 256, rng);   // B OBJ
}

static std::vector<Scene> MakeScenes()
{
    std::vector<Scene> v;

    // 0: nothing but the backdrop (the per-frame floor)
    v.push_back({"backdrop", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(1);
        CommonPalettes(g, rng);
        StandardMapping(g);
        U(nds, 0).DispCnt = 0x00010000;
        U(nds, 1).DispCnt = 0x00010000;
    }});

    // 1: three text BGs and sprites on A, two text BGs and sprites on B; the common RPG layout
    v.push_back({"text-3bg", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(2);
        CommonPalettes(g, rng);
        // A: tiles at ABG 0x00000 (16-colour) and 0x10000 (256-colour); maps at 0x08000..
        GenTiles16(g, BK_A, 0x00000, 512, rng, 30);
        GenTiles256(g, BK_A, 0x10000, 256, rng, 25);
        GenTextMap(g, BK_A, 0x08000, 1, 512, rng, true, true, 10);    // BG0 map, 256x256
        GenTextMap(g, BK_A, 0x08800, 2, 256, rng, true, true, 30);    // BG1 map, 512x256
        GenTextMap(g, BK_A, 0x09800, 2, 256, rng, true, true, 0);     // BG2 map, 256x512
        GenExtPal(g, BK_F, 0x4000, rng);
        GenSpriteGraphics(g, BK_E, rng);
        // B: tiles at BBG 0x0000, maps at 0xC000
        GenTiles16(g, BK_C, 0x00000, 512, rng, 30);
        GenTiles256(g, BK_C, 0x08000, 256, rng, 20);
        GenTextMap(g, BK_C, 0x0C000, 1, 512, rng, true, true, 5);
        GenTextMap(g, BK_C, 0x0C800, 1, 256, rng, true, true, 40);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x40011F10 | (0 << 24) | (0 << 27);  // mode 0, BG0-3 + OBJ, 1D OBJ mapping, BG ext pal
        SetBG(a, 0, 0x0000 | (0x10 << 8) | (0 << 2) | 0, 13, 7);                 // 16-colour, map 0x08000 (block 16), prio 0
        SetBG(a, 1, 0x0001 | (0x11 << 8) | (4 << 2) | (1<<7) | (1<<14), 100, 20);  // 256-colour std pal, 512x256, char base 0x10000
        SetBG(a, 2, 0x0002 | (0x13 << 8) | (4 << 2) | (1<<7) | (1<<15) | (1<<13), 300, 40);  // 256-colour ext pal slot 2
        a.BGCnt[3] = 3;
        SpriteField(g, 0, 48, rng, 0, 0, 0, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011310;   // mode 0, BG0, BG1, OBJ, 1D mapping
        SetBG(b, 0, 0x0000 | (0x18 << 8) | (0 << 2), 5, 3);
        SetBG(b, 1, 0x0001 | (0x19 << 8) | (2 << 2) | (1<<7), 60, 90);
        SpriteField(g, 1, 24, rng, 0, 0, 0, 0, 0);
    }});

    // 2: the same with windows, blending, semi-transparent sprites and an OBJ window; B is a 256-colour bitmap
    v.push_back({"rpg-blend", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(3);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 512, rng, 30);
        GenTiles256(g, BK_A, 0x10000, 256, rng, 25);
        GenTextMap(g, BK_A, 0x08000, 1, 512, rng, true, true, 10);
        GenTextMap(g, BK_A, 0x08800, 2, 256, rng, true, true, 30);
        GenTextMap(g, BK_A, 0x09800, 2, 256, rng, true, true, 0);
        GenExtPal(g, BK_F, 0x4000, rng);
        GenSpriteGraphics(g, BK_E, rng);
        GenBitmap8(g, BK_C, 0x00000, 256, 256, rng, 5);
        GenTiles16(g, BK_C, 0x10000, 256, rng, 30);
        GenTextMap(g, BK_C, 0x1C000, 1, 256, rng, true, true, 50);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x4001BF10;   // + win0, win1, OBJ window
        SetBG(a, 0, 0x0000 | (0x10 << 8), 13, 7);
        SetBG(a, 1, 0x0001 | (0x11 << 8) | (4 << 2) | (1<<7) | (1<<14), 100, 20);
        SetBG(a, 2, 0x0002 | (0x13 << 8) | (4 << 2) | (1<<7) | (1<<15) | (1<<13), 300, 40);
        SetBG(a, 3, 0x0003 | (0x10 << 8), 0, 0);
        SetWindow(a, 0, 24, 200, 16, 150);
        SetWindow(a, 1, 180, 60, 100, 30);    // wrapping window
        a.WinCnt[0] = 0x3F; a.WinCnt[1] = 0x1B; a.WinCnt[2] = 0x2D; a.WinCnt[3] = 0x37;
        a.BlendCnt = 0x0442;      // BG1 (1st) alpha over BG2 (2nd), OBJ 2nd
        a.BlendCnt |= 0x1000 | 0x0010;
        a.EVA = 9; a.EVB = 7; a.BlendAlpha = 9 | (7 << 8);
        a.EVY = 6;
        SpriteField(g, 0, 56, rng, 30, 15, 0, 6, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011505;   // mode 5, BG0, BG2, OBJ
        SetBG(b, 0, 0x0000 | (0x1C << 8) | (4 << 2), 2, 1);
        SetBG(b, 2, 0x0001 | (0 << 8) | (1<<7) | (1<<14) | (1<<13), 0, 0);   // 256-colour bitmap 256x256, wrap
        SetRotBG(b, 0, 0x100, 0, 0, 0x100, 20 << 8, 10 << 8);
        b.BlendCnt = 0x3F44 | 0x0004;    // brightness... no: alpha BG2 over everything
        b.BlendCnt = 0x00C4 | 0x3F00;    // brightness down on BG2
        b.EVY = 5;
        SpriteField(g, 1, 32, rng, 20, 20, 0, 0, 0);
    }});

    // 3: 128 sprites on each unit, large ones, many modes
    v.push_back({"sprite-storm", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(4);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 256, rng, 30);
        GenTextMap(g, BK_A, 0x08000, 1, 256, rng, true, true, 10);
        GenTextMap(g, BK_A, 0x08800, 1, 256, rng, true, true, 60);
        GenSpriteGraphics(g, BK_E, rng);
        GenExtPal(g, BK_G, 0x2000, rng);
        GenTiles16(g, BK_C, 0x00000, 256, rng, 30);
        GenTextMap(g, BK_C, 0x0C000, 1, 256, rng, true, true, 10);
        GenSpriteGraphics(g, BK_D, rng);
        GenExtPal(g, BK_I, 0x2000, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x80311310;   // OBJ ext pal, 1D mapping, boundary 128
        SetBG(a, 0, 0x0001 | (0x10 << 8), 0, 0);
        SetBG(a, 1, 0x0002 | (0x11 << 8), 50, 50);
        // all big
        for (int i = 0; i < 128; i++)
        {
            int size = 2 + rng.range(2);
            int x = rng.rangei(-60, 250), y = rng.rangei(-50, 200);
            int c256 = rng.chance(50);
            int mode = rng.chance(10) ? 3 : (rng.chance(20) ? 1 : 0);
            int rot = rng.chance(15);
            int tile = mode == 3 ? 0x300 + rng.range(0x40) : (c256 ? 0x200 + rng.range(0x40) : rng.range(0x300));
            SetOAM(g, 0, i, A0(y, rot, rot && rng.chance(50), mode, 0, c256, rng.range(3)),
                   rot ? A1Rot(x, rng.range(32), size) : A1(x, rng.range(4), size),
                   A2(tile, rng.range(4), mode == 3 ? 1 + rng.range(15) : rng.range(16)));
        }
        for (int grp = 0; grp < 32; grp++)
        {
            s16 s = (s16)(0x80 + rng.range(0x180));
            SetRot(g, 0, grp, s, (s16)rng.rangei(-0x40, 0x40), (s16)rng.rangei(-0x40, 0x40), s);
            SetRot(g, 1, grp, s, (s16)rng.rangei(-0x40, 0x40), (s16)rng.rangei(-0x40, 0x40), s);
        }

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x80311110;
        SetBG(b, 0, 0x0001 | (0x18 << 8), 0, 0);
        SpriteField(g, 1, 128, rng, 15, 10, 15, 3, 0);
    }});

    // 4: rotated and scaled affine BGs; extended text-mixed and direct-colour bitmaps rotated on B
    v.push_back({"affine", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(5);
        CommonPalettes(g, rng);
        GenTiles256(g, BK_A, 0x00000, 256, rng, 20);
        GenAffineMap(g, BK_A, 0x10000, 32*32, 256, rng);    // BG2: 256x256 (32x32 entries)
        GenAffineMap(g, BK_A, 0x10400, 64*64, 256, rng);    // BG3: 512x512
        GenSpriteGraphics(g, BK_E, rng);
        GenTiles256(g, BK_C, 0x00000, 256, rng, 20);
        GenTextMap(g, BK_C, 0x08000, 2, 256, rng, true, true, 10);   // ext text-mixed map (16-bit entries, 64x64 → 4 blocks? 512x512 needs 8KB)
        GenTextMap(g, BK_C, 0x09000, 2, 256, rng, true, true, 10);
        GenBitmap16(g, BK_C, 0x10000, 128, 128, rng, 85, true);
        GenExtPal(g, BK_H, 0x8000, rng);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00011C02 | (1<<4);   // mode 2, BG2, BG3, OBJ
        SetBG(a, 2, 0x0000 | (0x20 << 8) | (0 << 14) | (1 << 13), 0, 0);     // 256x256 wrap, map 0x10000
        SetBG(a, 3, 0x0001 | (0x21 << 8) | (3 << 14) | (0 << 13), 0, 0);     // 512x512 no wrap (map block 0x21 → 0x10800? see below)
        a.BGCnt[3] = 0x0001 | (0x21 << 8) | (3 << 14);
        SetRotBG(a, 0, 0x00E0, 0x0040, (s16)-0x0040, 0x00E0, -(40 << 8), 10 << 8);
        SetRotBG(a, 1, 0x0180, (s16)-0x0020, 0x0020, 0x0180, 30 << 8, -(20 << 8));
        SpriteField(g, 0, 24, rng, 0, 0, 40, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x40011C05 | (1<<4);   // mode 5, BG2 (ext text-mixed), BG3 (direct bitmap), OBJ, ext pal
        SetBG(b, 2, 0x0000 | (0x10 << 8) | (1 << 14) | (1 << 13), 0, 0);    // 256x256, wrap, map 0x08000 (block 16)
        SetBG(b, 3, 0x0001 | (0x04 << 8) | (1 << 7) | (1 << 2) | (0 << 14), 0, 0);   // direct bitmap 128x128 at 0x10000 (block 4 << 6 = 0x100 … )
        b.BGCnt[3] = 0x0001 | (0x04 << 8) | (1 << 7) | (1 << 2);
        SetRotBG(b, 0, 0x0120, 0x0060, (s16)-0x0060, 0x0120, 10 << 8, 5 << 8);
        SetRotBG(b, 1, 0x0090, 0x0030, (s16)-0x0030, 0x0090, -(10 << 8), 0);
        SpriteField(g, 1, 16, rng, 0, 0, 60, 0, 0);
    }});

    // 5: bitmap BGs scrolling (the fast path), brightness effects, master brightness; a large BG on B
    v.push_back({"bitmap-fade", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(6);
        CommonPalettes(g, rng);
        GenBitmap16(g, BK_A, 0x00000, 256, 256, rng, 90, true);   // 128KB direct colour, 256x256
        GenBitmap8(g, BK_B, 0x00000, 256, 256, rng, 10);          // in ABG at 0x20000
        GenTiles16(g, BK_B, 0x10000, 256, rng, 40);
        GenTextMap(g, BK_B, 0x18000, 1, 256, rng, true, true, 50);
        GenSpriteGraphics(g, BK_E, rng);
        GenBitmap8(g, BK_C, 0x00000, 512, 256, rng, 0);           // large BG 512x256 = 128KB
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00011D05 | (1<<4);   // mode 5, BG0, BG2, BG3, OBJ
        SetBG(a, 0, 0x0000 | (0x18 << 8) | (4 << 2), 3, 9);     // text BG0 at ABG map 0x2C000, tiles 0x30000
        a.BGCnt[0] = 0x0000 | (0x30 << 8) | (4 << 2);            // map block 0x30 -> 0x18000? (block<<11): 0x30<<11 = 0x18000 ✓ wait: (bgcnt&0x1F00)<<3 -> 0x30 doesn't fit 5 bits
        a.BGCnt[0] = 0x0000 | (0x1F << 8) | (4 << 2);            // map at 0x1F<<11 = 0xF800 (bank A, unused area of the bitmap? it overlaps) -- see below
        SetBG(a, 2, 0x0001 | (0x08 << 8) | (1<<7) | (1<<2) | (1<<14) | (1<<13), 0, 0);  // direct 256x256 at 0x08<<14 = 0x20000? no: (bgcnt&0x1F00)<<6 = 0x08<<14 = 0x20000
        a.BGCnt[2] = 0x0001 | (0x00 << 8) | (1<<7) | (1<<2) | (1<<14) | (1<<13);          // direct colour at 0x00000
        SetBG(a, 3, 0x0002 | (0x08 << 8) | (1<<7) | (1<<14) | (1<<13), 0, 0);             // 256-colour bitmap at 0x20000
        SetRotBG(a, 0, 0x100, 0, 0, 0x100, 37 << 8, 100 << 8);
        SetRotBG(a, 1, 0x100, 0, 0, 0x100, -(13 << 8), 50 << 8);
        a.BlendCnt = 0x00C4;     // brightness down, BG2 1st target
        a.EVY = 7;
        g.MasterBrightnessA = 0x4000 | 5;   // up 5
        SpriteField(g, 0, 20, rng, 20, 30, 0, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00010406;  // mode 6, BG2 (large)
        b.BGCnt[2] = 0x0000 | (2 << 14) | (1 << 13);   // 512x256, wrap
        SetRotBG(b, 0, 0x100, 0, 0, 0x100, 60 << 8, 12 << 8);
        g.MasterBrightnessB = 0x8000 | 9;   // down 9
    }});

    // 6: 3D on BG0 with a HUD on top, 3D blended against BG1, semi-transparent sprites
    v.push_back({"3d-hud", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(7);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 512, rng, 45);
        GenTiles256(g, BK_A, 0x10000, 256, rng, 20);
        GenTextMap(g, BK_A, 0x08000, 1, 512, rng, true, true, 60);
        GenTextMap(g, BK_A, 0x08800, 1, 256, rng, true, true, 0);
        GenSpriteGraphics(g, BK_E, rng);
        GenTiles16(g, BK_C, 0x00000, 512, rng, 30);
        GenTextMap(g, BK_C, 0x0C000, 1, 512, rng, true, true, 5);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00011718;   // mode 0, BG0 (3D), BG1, BG2, OBJ, 1D
        a.BGCnt[0] = 0x0001;                                        // 3D at prio 1
        SetBG(a, 1, 0x0000 | (0x10 << 8), 0, 0);                    // HUD 16-colour on top
        SetBG(a, 2, 0x0002 | (0x11 << 8) | (4 << 2) | (1<<7), 24, 40); // 256-colour behind the 3D
        a.BlendCnt = 0x0441 | 0x2000;   // 3D (BG0) 1st, BG2 + backdrop 2nd, alpha
        a.EVA = 8; a.EVB = 8;
        SpriteField(g, 0, 40, rng, 40, 10, 0, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011110;
        SetBG(b, 0, 0x0000 | (0x18 << 8), 8, 8);
        SpriteField(g, 1, 30, rng, 0, 0, 0, 0, 0);
    }, nullptr, 2, 11});

    // 7: mosaic on text, affine and sprites, with windows
    v.push_back({"mosaic", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(8);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 512, rng, 30);
        GenTiles256(g, BK_A, 0x10000, 256, rng, 20);
        GenTextMap(g, BK_A, 0x08000, 1, 512, rng, true, true, 10);
        GenAffineMap(g, BK_A, 0x08800, 32*32, 256, rng);
        GenSpriteGraphics(g, BK_E, rng);
        GenTiles16(g, BK_C, 0x00000, 512, rng, 30);
        GenTextMap(g, BK_C, 0x0C000, 1, 512, rng, true, true, 10);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00013512 | (1<<4);   // mode 2, BG0, BG2, OBJ, win0
        SetBG(a, 0, 0x0000 | (0x10 << 8) | (1<<6), 5, 3);                       // mosaic text
        SetBG(a, 2, 0x0001 | (0x11 << 8) | (4 << 2) | (1<<6) | (1<<13), 0, 0);   // mosaic affine (map 0x08800, tiles 0x10000)
        SetRotBG(a, 0, 0x00C0, 0x0020, (s16)-0x0030, 0x00D0, 0, 0);
        a.BGMosaicSize[0] = 3; a.BGMosaicSize[1] = 2;
        a.OBJMosaicSize[0] = 4; a.OBJMosaicSize[1] = 5;
        SetWindow(a, 0, 40, 220, 20, 170);
        a.WinCnt[0] = 0x3F; a.WinCnt[2] = 0x15;
        SpriteField(g, 0, 40, rng, 10, 10, 10, 0, 60);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011110;
        SetBG(b, 0, 0x0000 | (0x18 << 8) | (1<<6), 0, 0);
        b.BGMosaicSize[0] = 7; b.BGMosaicSize[1] = 1;
        b.OBJMosaicSize[0] = 2; b.OBJMosaicSize[1] = 0;
        SpriteField(g, 1, 40, rng, 0, 0, 0, 0, 80);
    }});

    // 8: display modes: VRAM display and FIFO on A, screen off on B, display capture into bank C
    v.push_back({"display-modes", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(9);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 256, rng, 30);
        GenTextMap(g, BK_A, 0x08000, 1, 256, rng, true, true, 10);
        GenSpriteGraphics(g, BK_E, rng);
        GenBitmap16(g, BK_B, 0, 256, 192, rng, 0, true);   // a picture in bank B, shown via VRAM display
        for (int i = 0; i < 256; i++) g.DispFIFOBuffer[i] = (u16)rng.next();
        MapBank(g, BK_A, 0x81);
        MapBank(g, BK_B, 0x80);   // LCDC
        MapBank(g, BK_C, 0x80);   // LCDC, capture target
        MapBank(g, BK_E, 0x82);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00021110 | (1 << 18);   // VRAM display of bank B; BG0+OBJ still rendered (captured)
        SetBG(a, 0, 0x0000 | (0x10 << 8), 1, 2);
        SpriteField(g, 0, 20, rng, 0, 0, 0, 0, 0);
        g.MasterBrightnessA = 0x8000 | 3;
        g.CaptureCnt = (1u << 31) | (2 << 29) | (0 << 24) | (3 << 20) | (2 << 16) | (0 << 18) | 8 | (6 << 8);  // A+B blend, 256x192, dst bank C
        g.CaptureEnable = true;

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00000000;   // screen off
    }, [](NDS& nds, int frame, int line) {
        // FIFO display from line 96 down
        if (line == 96) U(nds, 0).DispCnt = (U(nds, 0).DispCnt & ~0x30000) | 0x30000;
        if (line == 0) U(nds, 0).DispCnt = (U(nds, 0).DispCnt & ~0x30000) | 0x20000;
    }, 0, 0, 3, true});

    // 9: HBlank effects: per-line scroll, BLDY ramps, moving windows, per-line rotation
    v.push_back({"hblank-fx", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(10);
        CommonPalettes(g, rng);
        GenTiles16(g, BK_A, 0x00000, 512, rng, 30);
        GenTiles256(g, BK_A, 0x10000, 256, rng, 20);
        GenTextMap(g, BK_A, 0x08000, 1, 512, rng, true, true, 10);
        GenTextMap(g, BK_A, 0x08800, 1, 256, rng, true, true, 0);
        GenAffineMap(g, BK_A, 0x09000, 32*32, 256, rng);
        GenSpriteGraphics(g, BK_E, rng);
        GenTiles16(g, BK_C, 0x00000, 512, rng, 30);
        GenTextMap(g, BK_C, 0x0C000, 1, 512, rng, true, true, 10);
        GenBitmap16(g, BK_C, 0x10000, 128, 128, rng, 90, true);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0x00013712 | (1<<4);   // mode 2, BG0, BG1, BG2, OBJ, win0
        SetBG(a, 0, 0x0000 | (0x10 << 8), 0, 0);
        SetBG(a, 1, 0x0001 | (0x11 << 8) | (4 << 2) | (1<<7), 0, 0);
        SetBG(a, 2, 0x0002 | (0x12 << 8) | (4 << 2) | (1<<13), 0, 0);
        SetRotBG(a, 0, 0x100, 0, 0, 0x100, 0, 0);
        SetWindow(a, 0, 0, 255, 0, 192);
        a.WinCnt[0] = 0x3F; a.WinCnt[2] = 0x1C;
        a.BlendCnt = 0x00C1 | 0x0003;   // brightness down on BG0, BG1
        SpriteField(g, 0, 32, rng, 20, 0, 0, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011905 | (1<<4);   // mode 5, BG0, BG3 (bitmap), OBJ
        SetBG(b, 0, 0x0000 | (0x18 << 8), 0, 0);
        SetBG(b, 3, 0x0001 | (0x04 << 8) | (1<<7) | (1<<2) | (1<<13), 0, 0);
        SetRotBG(b, 1, 0x100, 0, 0, 0x100, 0, 0);
        SpriteField(g, 1, 16, rng, 0, 0, 0, 0, 0);
    }, [](NDS& nds, int frame, int line) {
        GPU2D& a = U(nds, 0);
        a.BGXPos[0] = (u16)(line + frame * 2);
        a.BGXPos[1] = (u16)(40 + (int)(20 * sin((line + frame * 4) * 0.08)));
        a.EVY = (u8)((line / 12) & 0xF);
        int w = 20 + (int)(100 * (0.5 + 0.5 * sin(line * 0.05 + frame)));
        a.Win0Coords[0] = (u8)(128 - w); a.Win0Coords[1] = (u8)(128 + w);
        a.BGRotA[0] = (s16)(0x100 + (int)(0x60 * sin(line * 0.03)));
        a.BGRotC[0] = (s16)(0x20 * cos(line * 0.03));
        if ((line & 7) == 0) a.BGXRef[0] += 0x80;
        GPU2D& b = U(nds, 1);
        b.BGXRef[1] = (s32)((int)(30 * sin((line + frame * 3) * 0.07)) << 8);
        if (line == 100) nds.GPU.MasterBrightnessB = 0x8000 | 8;
        if (line == 0) nds.GPU.MasterBrightnessB = 0;
    }, 0, 0, 3});

    // 10: a dense sprite-heavy mode-5 bottom screen (touch UI style), ext text-mixed BG with ext palette
    v.push_back({"touch-ui", [](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(12);
        CommonPalettes(g, rng);
        GenBitmap16(g, BK_A, 0x00000, 256, 256, rng, 100, true);
        GenTiles256(g, BK_B, 0x00000, 256, rng, 25);
        GenTextMap(g, BK_B, 0x08000, 2, 256, rng, true, true, 20);
        GenExtPal(g, BK_F, 0x4000, rng);
        GenSpriteGraphics(g, BK_E, rng);
        GenExtPal(g, BK_G, 0x2000, rng);
        GenBitmap8(g, BK_C, 0, 256, 256, rng, 0);
        GenSpriteGraphics(g, BK_D, rng);
        StandardMapping(g);

        GPU2D& a = U(nds, 0);
        a.DispCnt = 0xC0011C15 | (1 << 4) | (1 << 6) | (1 << 5);   // mode 5, BG2, BG3, OBJ; BG+OBJ ext pal; 1D bitmap OBJ 256-wide
        a.BGCnt[2] = 0x0001 | (0x10 << 8) | (1 << 14) | (1 << 13);   // ext text-mixed 256x256 at 0x08000... (bgcnt&0x1F00)<<3 = 0x8000; tiles char base 0x20000 via (bgcnt&0x3C)<<12 = 0x20000 => (8<<2)
        a.BGCnt[2] |= (8 << 2);
        a.BGCnt[3] = 0x0000 | (0x00 << 8) | (1<<7) | (1<<2) | (1<<14) | (1<<13);   // direct colour 256x256 at 0
        SetRotBG(a, 0, 0x100, 0, 0, 0x100, 0, 0);
        SetRotBG(a, 1, 0x100, 0, 0, 0x100, 0, 0);
        SpriteField(g, 0, 96, rng, 10, 40, 5, 0, 0);

        GPU2D& b = U(nds, 1);
        b.DispCnt = 0x00011405 | (1 << 4);
        b.BGCnt[2] = 0x0000 | (1<<7) | (1<<14) | (1<<13);
        SetRotBG(b, 0, 0x100, 0, 0, 0x100, 0, 0);
        SpriteField(g, 1, 64, rng, 0, 30, 0, 0, 0);
    }});

    return v;
}

// ---- random scenes -----------------------------------------------------------------------------

static void FuzzWrites(NDS& nds, u64 seed, int frame, int line, int count)
{
    Rng r(seed * 7919 + (u64)frame * 263 + line);
    GPU& g = nds.GPU;
    for (int i = 0; i < count; i++)
    {
        int unit = r.range(2);
        GPU2D& u = unit ? g.GPU2D_B : g.GPU2D_A;
        u32 base = unit ? 0x04001000 : 0x04000000;
        switch (r.range(10))
        {
        case 0: case 1: case 2: case 3:
            u.Write16(base + (r.range(0x30) << 1), (u16)r.next());
            break;
        case 4:
            u.Write8(base + r.range(0x56), (u8)r.next());
            break;
        case 5:
            u.Write32(base + (r.range(0x16) << 2), r.next());
            break;
        case 6:
            g.WritePalette<u16>(r.range(0x400) << 1, (u16)r.next());
            break;
        case 7:
            g.WriteOAM<u16>(r.range(0x400) << 1, (u16)r.next());
            break;
        case 8:
            if (r.chance(50)) g.WriteVRAM_ABG<u16>(r.range(0x40000) << 1, (u16)r.next());
            else g.WriteVRAM_BBG<u16>(r.range(0x10000) << 1, (u16)r.next());
            break;
        case 9:
            if (r.chance(50)) g.WriteVRAM_AOBJ<u16>(r.range(0x20000) << 1, (u16)r.next());
            else g.WriteVRAM_BOBJ<u16>(r.range(0x10000) << 1, (u16)r.next());
            break;
        }
    }
}

static Scene MakeFuzzScene(u64 seed)
{
    Scene s;
    s.name = "fuzz-" + std::to_string(seed);
    Rng pick(seed ^ 0xABCDEF);
    s.mode3D = pick.chance(50) ? pick.rangei(1, 2) : 0;
    s.seed3D = (u32)seed;
    s.frames = 2;
    s.checkVRAM = true;
    bool hblank = pick.chance(50);
    int writesPerLine = pick.rangei(1, 4);
    s.setup = [seed](NDS& nds) {
        GPU& g = nds.GPU;
        Rng rng(seed);
        CommonPalettes(g, rng);
        // random content everywhere, with transparent holes
        for (int b = 0; b < 9; b++)
        {
            int zero = rng.rangei(0, 70);
            for (u32 off = 0; off < BankSize[b]; off += 8)
            {
                u32 lo = rng.chance(zero) ? 0 : rng.next();
                u32 hi = rng.chance(zero) ? 0 : rng.next();
                if (rng.chance(10)) { lo &= 0x0F0F0F0F; hi &= 0xF0F0F0F0; }
                W32(g, b, off, lo);
                W32(g, b, off + 4, hi);
            }
        }
        for (int i = 0; i < 256; i++) g.DispFIFOBuffer[i] = (u16)rng.next();

        // mapping: one of a few layouts
        switch (rng.range(4))
        {
        case 0: StandardMapping(g, false); break;
        case 1: StandardMapping(g, true); break;
        case 2:
            MapBank(g, BK_A, 0x81); MapBank(g, BK_B, 0x82); MapBank(g, BK_C, 0x84); MapBank(g, BK_D, 0x84);
            MapBank(g, BK_E, 0x84); MapBank(g, BK_F, 0x85); MapBank(g, BK_G, 0x89); MapBank(g, BK_H, 0x82); MapBank(g, BK_I, 0x83);
            break;
        default:
            MapBank(g, BK_A, 0x80); MapBank(g, BK_B, 0x89); MapBank(g, BK_C, 0x80); MapBank(g, BK_D, 0x84);
            MapBank(g, BK_E, 0x81); MapBank(g, BK_F, 0x82); MapBank(g, BK_G, 0x8A); MapBank(g, BK_H, 0x81); MapBank(g, BK_I, 0x82);
            break;
        }

        for (int unit = 0; unit < 2; unit++)
        {
            GPU2D& u = unit ? g.GPU2D_B : g.GPU2D_A;
            u32 dispcnt = rng.next();
            dispcnt &= ~0x30000;
            dispcnt |= (rng.chance(85) ? 1 : rng.range(4)) << 16;   // mostly regular display
            if (rng.chance(60)) dispcnt &= ~(1<<7);                 // forced blank rarely
            if (rng.chance(70)) dispcnt |= 0x1F00;                  // all layers on, usually
            u.Write32(unit ? 0x04001000 : 0x04000000, dispcnt);   // applies unit B's mask
            for (int bg = 0; bg < 4; bg++)
            {
                u.BGCnt[bg] = (u16)rng.next();
                u.BGXPos[bg] = (u16)(rng.chance(50) ? rng.range(512) : rng.next());
                u.BGYPos[bg] = (u16)(rng.chance(50) ? rng.range(512) : rng.next());
            }
            for (int i = 0; i < 2; i++)
            {
                s16 a = rng.chance(50) ? 0x100 : (s16)rng.rangei(-0x300, 0x300);
                s16 d = rng.chance(50) ? 0x100 : (s16)rng.rangei(-0x300, 0x300);
                s16 b = rng.chance(60) ? 0 : (s16)rng.rangei(-0x100, 0x100);
                s16 c = rng.chance(60) ? 0 : (s16)rng.rangei(-0x100, 0x100);
                s32 x = rng.chance(50) ? (rng.rangei(-300, 300) << 8) : (s32)(rng.next() << 4) >> 4;
                s32 y = rng.chance(50) ? (rng.rangei(-300, 300) << 8) : (s32)(rng.next() << 4) >> 4;
                SetRotBG(u, i, a, b, c, d, x, y);
            }
            SetWindow(u, 0, rng.range(256), rng.range(256), rng.range(256), rng.range(256));
            SetWindow(u, 1, rng.range(256), rng.range(256), rng.range(256), rng.range(256));
            for (int i = 0; i < 4; i++) u.WinCnt[i] = (u8)rng.next() & 0x3F;
            u.Win0Active = rng.range(4); u.Win1Active = rng.range(4);
            u.BGMosaicSize[0] = rng.chance(70) ? 0 : rng.range(16);
            u.BGMosaicSize[1] = rng.chance(70) ? 0 : rng.range(16);
            u.OBJMosaicSize[0] = rng.chance(70) ? 0 : rng.range(16);
            u.OBJMosaicSize[1] = rng.chance(70) ? 0 : rng.range(16);
            u.BlendCnt = (u16)(rng.next() & 0x3FFF);
            u.Write16((unit ? 0x04001000 : 0x04000000) + 0x52, (u16)rng.next());   // BLDALPHA with clamping
            u.Write16((unit ? 0x04001000 : 0x04000000) + 0x54, (u16)rng.next());   // BLDY
            // OAM
            for (int i = 0; i < 128; i++)
            {
                u16 a0 = (u16)rng.next(), a1 = (u16)rng.next(), a2 = (u16)rng.next();
                if (rng.chance(30)) a0 = (a0 & ~0x300) | 0x200;   // disabled
                if (rng.chance(60)) a1 = (a1 & ~0x1FF) | (rng.rangei(-40, 250) & 0x1FF);
                if (rng.chance(60)) a0 = (a0 & ~0xFF) | rng.rangei(-30, 200);
                SetOAM(g, unit, i, a0, a1, a2);
            }
        }
        g.MasterBrightnessA = rng.chance(60) ? 0 : (u16)rng.next();
        g.MasterBrightnessB = rng.chance(60) ? 0 : (u16)rng.next();
        g.ScreenSwap = rng.chance(20);
        g.ScreensEnabled = rng.chance(95);
        if (rng.chance(30))
        {
            // capture into a bank that is in LCDC
            int dst = -1;
            for (int b = 0; b < 4; b++) if (g.VRAMMap_LCDC & (1 << b)) { dst = b; break; }
            if (dst >= 0)
            {
                g.CaptureCnt = (rng.next() & 0xEF3F1F1F) | (1u << 31) | (dst << 16);
                g.CaptureEnable = true;
            }
        }
    };
    if (hblank)
        s.perLine = [seed, writesPerLine](NDS& nds, int frame, int line) {
            if (((line * 7 + frame) % 5) == 0) FuzzWrites(nds, seed, frame, line, writesPerLine);
        };
    return s;
}

// ---- the frame driver (the renderer-facing part of GPU::StartScanline / StartHBlank) ------------

static void RunFrame(NDS& nds, Renderer& rend, const Scene& sc, int frame)
{
    GPU& g = nds.GPU;
    for (u32 line = 0; line < 263; line++)
    {
        g.VCount = line;
        g.GPU2D_A.UpdateWindows(line);
        g.GPU2D_B.UpdateWindows(line);
        if (line == 192) rend.VBlank();

        if (sc.perLine) sc.perLine(nds, frame, line);

        bool resetregs = (line == 262);
        g.GPU2D_A.UpdateRegistersPreDraw(resetregs);
        g.GPU2D_B.UpdateRegistersPreDraw(resetregs);

        if (line < 192)
        {
            rend.DrawScanline(line);
            if (line < 191)
                rend.DrawSprites(line+1);
        }
        else if (line == 215)
            rend.Start3DRendering();
        else if (line == 262)
            rend.DrawSprites(0);

        g.GPU2D_A.UpdateRegistersPostDraw(resetregs);
        g.GPU2D_B.UpdateRegistersPostDraw(resetregs);
    }
    rend.SwapBuffers();
}

struct FrameRecord
{
    std::vector<u32> top, bottom;
    std::vector<u8> vram;
};

static void Record(NDS& nds, Renderer& rend, FrameRecord& rec, bool vram)
{
    void *top, *bottom;
    rend.GetFramebuffers(&top, &bottom);
    rec.top.assign((u32*)top, (u32*)top + 256*192);
    rec.bottom.assign((u32*)bottom, (u32*)bottom + 256*192);
    if (vram)
    {
        rec.vram.clear();
        GPU& g = nds.GPU;
        for (int b = 0; b < 4; b++) rec.vram.insert(rec.vram.end(), g.VRAM[b], g.VRAM[b] + 0x20000);
    }
}

static void WritePPM(const char* path, const u32* px)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 192\n255\n");
    for (int i = 0; i < 256*192; i++)
    {
        u32 c = px[i];
        u8 rgb[3] = { (u8)(c >> 16), (u8)(c >> 8), (u8)c };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static std::string DumpDir;

static bool CompareFrames(const Scene& sc, int frame, const FrameRecord& a, const FrameRecord& b, NDS& nds)
{
    for (int screen = 0; screen < 2; screen++)
    {
        const std::vector<u32>& pa = screen ? a.bottom : a.top;
        const std::vector<u32>& pb = screen ? b.bottom : b.top;
        for (int i = 0; i < 256*192; i++)
        {
            if (pa[i] != pb[i])
            {
                int x = i & 255, y = i >> 8;
                int count = 0;
                for (int j = 0; j < 256*192; j++) if (pa[j] != pb[j]) count++;
                printf("MISMATCH %s frame %d screen %d at (%d,%d): stock %08X banana %08X (%d pixels differ)\n",
                       sc.name.c_str(), frame, screen, x, y, pa[i], pb[i], count);
                GPU2D& u = nds.GPU.ScreenSwap ? U(nds, screen) : U(nds, screen ^ 1);
                printf("  unit %d DispCnt %08X BGCnt %04X %04X %04X %04X BldCnt %04X EVA %d EVB %d EVY %d mosaic %d/%d %d/%d win %08X\n",
                       u.Num, u.DispCnt, u.BGCnt[0], u.BGCnt[1], u.BGCnt[2], u.BGCnt[3], u.BlendCnt, u.EVA, u.EVB, u.EVY,
                       u.BGMosaicSize[0], u.BGMosaicSize[1], u.OBJMosaicSize[0], u.OBJMosaicSize[1],
                       u.WinCnt[0] | (u.WinCnt[1] << 8) | (u.WinCnt[2] << 16) | (u.WinCnt[3] << 24));
                if (!DumpDir.empty())
                {
                    std::string p1 = DumpDir + "/" + sc.name + "-stock.ppm";
                    std::string p2 = DumpDir + "/" + sc.name + "-banana.ppm";
                    WritePPM(p1.c_str(), pa.data());
                    WritePPM(p2.c_str(), pb.data());
                    printf("  wrote %s and %s\n", p1.c_str(), p2.c_str());
                }
                return false;
            }
        }
    }
    if (sc.checkVRAM && a.vram != b.vram)
    {
        for (size_t i = 0; i < a.vram.size(); i++)
            if (a.vram[i] != b.vram[i])
            {
                printf("MISMATCH %s frame %d: captured VRAM differs at bank %c offset %05zX (stock %02X banana %02X)\n",
                       sc.name.c_str(), frame, 'A' + (int)(i >> 17), i & 0x1FFFF, a.vram[i], b.vram[i]);
                return false;
            }
    }
    return true;
}

template<typename R>
static std::unique_ptr<R> MakeRenderer(NDS& nds, const Scene& sc)
{
    auto r = std::make_unique<R>(nds);
    r->Set3D(std::make_unique<Fake3D>(nds.GPU.GPU3D, sc.mode3D, sc.seed3D));
    r->Reset();
    return r;
}

static bool CheckScene(NDS& nds, const Scene& sc)
{
    std::vector<FrameRecord> stockFrames(sc.frames);
    {
        ResetGPUState(nds);
        sc.setup(nds);
        auto r = MakeRenderer<StockR>(nds, sc);
        for (int f = 0; f < sc.frames; f++)
        {
            RunFrame(nds, *r, sc, f);
            Record(nds, *r, stockFrames[f], sc.checkVRAM);
        }
    }
    bool ok = true;
    {
        ResetGPUState(nds);
        sc.setup(nds);
        auto r = MakeRenderer<BananaR>(nds, sc);
        for (int f = 0; f < sc.frames && ok; f++)
        {
            RunFrame(nds, *r, sc, f);
            FrameRecord rec;
            Record(nds, *r, rec, sc.checkVRAM);
            ok = CompareFrames(sc, f, stockFrames[f], rec, nds);
        }
    }
    return ok;
}

template<typename R>
static double BenchScene(NDS& nds, const Scene& sc, int frames)
{
    ResetGPUState(nds);
    sc.setup(nds);
    auto r = MakeRenderer<R>(nds, sc);
    for (int f = 0; f < 2; f++) RunFrame(nds, *r, sc, f);   // warm up
    double best = 1e30;
    for (int round = 0; round < 3; round++)
    {
        double t0 = NowMs();
        for (int f = 0; f < frames; f++) RunFrame(nds, *r, sc, f);
        double t = (NowMs() - t0) / frames;
        if (t < best) best = t;
    }
    return best;
}

// ---- a real ROM through the whole emulator -------------------------------------------------------

// a renderer that measures the time spent in its scanline and sprite calls (the 2D work; the 3D
// renderer is melonDS's own in both cases)
template<typename Base>
struct TimedRenderer : public Base
{
    using Base::Base;
    double Time2D = 0;
    void DrawScanline(u32 line) override
    {
        double t0 = NowMs();
        Base::DrawScanline(line);
        Time2D += NowMs() - t0;
    }
    void DrawSprites(u32 line) override
    {
        double t0 = NowMs();
        Base::DrawSprites(line);
        Time2D += NowMs() - t0;
    }
};

static std::vector<u8> ReadFile(const char* path)
{
    std::vector<u8> v;
    FILE* f = fopen(path, "rb");
    if (!f) return v;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    v.resize(len);
    if (fread(v.data(), 1, len, f) != (size_t)len) v.clear();
    fclose(f);
    return v;
}

struct RomRun
{
    std::vector<FrameRecord> frames;
    double time2D = 0;      // ms in the 2D renderer, all frames
    double timeTotal = 0;   // ms in RunFrame, all frames
};

template<typename R>
static bool RunRom(const std::vector<u8>& rom, int nframes, RomRun& out, const char* dumpPrefix)
{
    auto nds = std::make_unique<NDS>();
    auto rend = std::make_unique<TimedRenderer<R>>(*nds);
    TimedRenderer<R>* rp = rend.get();
    nds->GPU.SetRenderer(std::move(rend));
    nds->Reset();
    auto cart = NDSCart::ParseROM(rom.data(), (u32)rom.size(), nullptr, std::nullopt);
    if (!cart)
    {
        printf("could not parse the ROM\n");
        return false;
    }
    nds->SetNDSCart(std::move(cart));
    nds->SetupDirectBoot("banana2d.nds");
    nds->Start();

    out.frames.resize(nframes);
    double t0 = NowMs();
    for (int f = 0; f < nframes; f++)
    {
        nds->RunFrame();
        void *top, *bottom;
        nds->GPU.GetFramebuffers(&top, &bottom);
        out.frames[f].top.assign((u32*)top, (u32*)top + 256*192);
        out.frames[f].bottom.assign((u32*)bottom, (u32*)bottom + 256*192);
        if (dumpPrefix && (f % 60) == 30)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s-f%03d-top.ppm", dumpPrefix, f);
            WritePPM(path, out.frames[f].top.data());
            snprintf(path, sizeof(path), "%s-f%03d-bottom.ppm", dumpPrefix, f);
            WritePPM(path, out.frames[f].bottom.data());
        }
    }
    out.timeTotal = NowMs() - t0;
    out.time2D = rp->Time2D;
    return true;
}

static int RomTest(const char* path, int nframes)
{
    std::vector<u8> rom = ReadFile(path);
    if (rom.empty())
    {
        printf("could not read %s\n", path);
        return 2;
    }
    printf("== ROM %s: %d frames through melonDS with each renderer\n", path, nframes);
    RomRun stock, banana;
    std::string ds = DumpDir.empty() ? "" : DumpDir + "/rom-stock";
    std::string db = DumpDir.empty() ? "" : DumpDir + "/rom-banana";
    if (!RunRom<SoftRenderer>(rom, nframes, stock, ds.empty() ? nullptr : ds.c_str())) return 2;
    if (!RunRom<BananaRenderer>(rom, nframes, banana, db.empty() ? nullptr : db.c_str())) return 2;

    int bad = 0;
    for (int f = 0; f < nframes; f++)
    {
        for (int screen = 0; screen < 2 && bad < 5; screen++)
        {
            const std::vector<u32>& a = screen ? stock.frames[f].bottom : stock.frames[f].top;
            const std::vector<u32>& b = screen ? banana.frames[f].bottom : banana.frames[f].top;
            if (a != b)
            {
                int i = 0;
                while (a[i] == b[i]) i++;
                int count = 0;
                for (int j = 0; j < 256*192; j++) if (a[j] != b[j]) count++;
                printf("MISMATCH frame %d screen %d at (%d,%d): stock %08X banana %08X (%d pixels)\n",
                       f, screen, i & 255, i >> 8, a[i], b[i], count);
                bad++;
            }
        }
    }
    // how much of the frames was actually drawn (not the blank boot frames)
    int nonblank = 0;
    for (int f = 0; f < nframes; f++)
    {
        const std::vector<u32>& a = stock.frames[f].top;
        for (int j = 0; j < 256*192; j += 97) if ((a[j] & 0xFFFFFF) != 0 && (a[j] & 0xFFFFFF) != 0xFFFFFF) { nonblank++; break; }
    }
    printf("frames compared: %d, with picture: %d, mismatching: %d\n", nframes, nonblank, bad);
    printf("2D time per frame: stock %.3f ms, banana %.3f ms, speedup %.2fx (whole emulation: %.2f / %.2f ms per frame)\n",
           stock.time2D / nframes, banana.time2D / nframes, stock.time2D / banana.time2D,
           stock.timeTotal / nframes, banana.timeTotal / nframes);
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    bool doScenes = false, doFuzz = false, doBench = false;
    bool bananaOnly = false, stockOnly = false;
    int fuzzCount = 200, benchFrames = 60;
    u64 seed = 1;
    std::string only;
    std::string romPath;
    int romFrames = 680;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--scenes") doScenes = true;
        else if (a == "--fuzz") { doFuzz = true; if (i+1 < argc) fuzzCount = atoi(argv[++i]); }
        else if (a == "--bench") { doBench = true; if (i+1 < argc) benchFrames = atoi(argv[++i]); }
        else if (a == "--seed" && i+1 < argc) seed = strtoull(argv[++i], nullptr, 0);
        else if (a == "--dump" && i+1 < argc) DumpDir = argv[++i];
        else if (a == "--only" && i+1 < argc) only = argv[++i];
        else if (a == "--verbose") Platform::SetLogThreshold(0);
        else if (a == "--rom" && i+1 < argc) romPath = argv[++i];
        else if (a == "--rom-frames" && i+1 < argc) romFrames = atoi(argv[++i]);
        else if (a == "--banana-only") bananaOnly = true;
        else if (a == "--stock-only") stockOnly = true;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Platform::SetLogThreshold(2);
    if (!romPath.empty())
        return RomTest(romPath.c_str(), romFrames);
    if (!doScenes && !doFuzz && !doBench) doScenes = doFuzz = doBench = true;

    auto nds = std::make_unique<NDS>();
    auto scenes = MakeScenes();

    int failures = 0;
    if (doScenes)
    {
        printf("== scenes: Banana vs melonDS stock, pixel for pixel\n");
        for (auto& sc : scenes)
        {
            if (!only.empty() && sc.name != only) continue;
            bool ok = CheckScene(*nds, sc);
            printf("%-16s %s\n", sc.name.c_str(), ok ? "identical" : "DIFFERENT");
            if (!ok) failures++;
        }
    }

    if (doFuzz)
    {
        printf("== fuzz: %d random scenes from seed %llu\n", fuzzCount, (unsigned long long)seed);
        int bad = 0;
        for (int k = 0; k < fuzzCount; k++)
        {
            Scene sc = MakeFuzzScene(seed + k);
            if (!CheckScene(*nds, sc))
            {
                bad++;
                if (bad >= 5) { printf("(stopping after 5 failures)\n"); break; }
            }
        }
        printf("fuzz: %d/%d identical\n", fuzzCount - bad, fuzzCount);
        failures += bad;
    }

    if (doBench)
    {
        printf("== benchmark: ms per frame (both 2D units, %d frames, best of 3)\n", benchFrames);
        printf("%-16s %10s %10s %8s\n", "scene", "stock", "banana", "speedup");
        double sumStock = 0, sumBanana = 0;
        for (auto& sc : scenes)
        {
            if (!only.empty() && sc.name != only) continue;
            double ts = bananaOnly ? 0 : BenchScene<StockR>(*nds, sc, benchFrames);
            double tb = stockOnly ? 0 : BenchScene<BananaR>(*nds, sc, benchFrames);
            sumStock += ts; sumBanana += tb;
            printf("%-16s %9.3f  %9.3f  %7.2fx\n", sc.name.c_str(), ts, tb, tb > 0 ? ts / tb : 0.0);
        }
        printf("%-16s %9.3f  %9.3f  %7.2fx\n", "total", sumStock, sumBanana, sumBanana > 0 ? sumStock / sumBanana : 0.0);
    }

    if (failures)
    {
        printf("FAILED: %d mismatching scenes\n", failures);
        return 1;
    }
    printf("all identical\n");
    return 0;
}
