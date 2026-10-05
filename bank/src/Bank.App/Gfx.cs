using System.Runtime.InteropServices;
using StbImageSharp;
using StbTrueTypeSharp;

namespace Rocknixds.Bank.App;

public readonly record struct Color(byte R, byte G, byte B, byte A = 255)
{
    public static Color Hex(uint rgb, byte a = 255) => new((byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb, a);
    public Color WithAlpha(byte a) => this with { A = a };

    public Color Mix(Color o, float t) => new(
        (byte)(R + (o.R - R) * t), (byte)(G + (o.G - G) * t), (byte)(B + (o.B - B) * t), (byte)(A + (o.A - A) * t));
}

/// <summary>ROCKNIXDS Pixel's colours (the rnds engine's dark and light palettes), plus the legality colours.</summary>
public sealed record Palette(
    Color Bg, Color Band, Color Body, Color Low, Color Ink, Color Muted, Color Line, Color Edge, Color Red,
    Color Chip, Color Rule, Color Good, Color Warn, Color Bad, Color Shade, Color Accent, Color KeyBg, Color KeyInk,
    bool IsDark)
{
    public static readonly Palette Dark = new(
        Bg: Color.Hex(0x121820), Band: Color.Hex(0x2e3a48), Body: Color.Hex(0x121820), Low: Color.Hex(0x070a0e),
        Ink: Color.Hex(0xe7eef6), Muted: Color.Hex(0x93a0b0), Line: Color.Hex(0x7d8b9c), Edge: Color.Hex(0xd5dee8),
        Red: Color.Hex(0xff3b4a), Chip: Color.Hex(0x0e141c), Rule: Color.Hex(0x3a4654),
        Good: Color.Hex(0x45d483), Warn: Color.Hex(0xf5b942), Bad: Color.Hex(0xff5a66),
        Shade: new Color(0, 0, 0, 150), Accent: Color.Hex(0x5aa9ff), KeyBg: Color.Hex(0x243040), KeyInk: Color.Hex(0xe7eef6), IsDark: true);

    public static readonly Palette Light = new(
        Bg: Color.Hex(0xe6ebf0), Band: Color.Hex(0xffffff), Body: Color.Hex(0xedf1f6), Low: Color.Hex(0xc5cfda),
        Ink: Color.Hex(0x1d2733), Muted: Color.Hex(0x66758a), Line: Color.Hex(0x8d9bab), Edge: Color.Hex(0x2b3644),
        Red: Color.Hex(0xe23b4a), Chip: Color.Hex(0xeef2f7), Rule: Color.Hex(0xc3ccd6),
        Good: Color.Hex(0x1f9d57), Warn: Color.Hex(0xb7791f), Bad: Color.Hex(0xd63a46),
        Shade: new Color(20, 35, 58, 110), Accent: Color.Hex(0x2f7bd9), KeyBg: Color.Hex(0xffffff), KeyInk: Color.Hex(0x1d2733), IsDark: false);
}

/// <summary>A TrueType font rendered on demand into one texture per pixel size.</summary>
public sealed unsafe class Font : IDisposable
{
    private readonly IntPtr _renderer;
    private readonly List<(StbTrueType.stbtt_fontinfo Info, byte[] Data)> _faces = [];
    private readonly Dictionary<int, Atlas> _atlases = [];

    private sealed class Atlas
    {
        public IntPtr Texture;
        public int Size;
        public int X, Y, RowH;
        public float Ascent;
        public readonly Dictionary<int, Glyph> Glyphs = [];
    }

    private readonly record struct Glyph(int X, int Y, int W, int H, int OffX, int OffY, float Advance);

    private const int AtlasSize = 1024;

    public Font(IntPtr renderer, string path, IEnumerable<string> fallbacks)
    {
        _renderer = renderer;
        AddFace(path);
        foreach (var f in fallbacks)
        {
            if (File.Exists(f))
            {
                try { AddFace(f); } catch (Exception) { /* a font we can't read */ }
            }
        }
    }

    private void AddFace(string path)
    {
        var data = File.ReadAllBytes(path);
        var info = StbTrueType.CreateFont(data, 0) ?? throw new InvalidDataException($"can't read the font {path}");
        _faces.Add((info, data));
    }

    private Atlas Get(int px)
    {
        if (_atlases.TryGetValue(px, out var a))
            return a;
        a = new Atlas { Size = px };
        a.Texture = Sdl.CreateTexture(_renderer, Sdl.PIXELFORMAT_ABGR8888, Sdl.TEXTUREACCESS_STATIC, AtlasSize, AtlasSize);
        Sdl.SetTextureBlendMode(a.Texture, Sdl.BLENDMODE_BLEND);
        Sdl.SetTextureScaleMode(a.Texture, Sdl.ScaleModeNearest);
        var clear = new byte[AtlasSize * AtlasSize * 4];
        fixed (byte* p = clear)
            Sdl.UpdateTexture(a.Texture, IntPtr.Zero, p, AtlasSize * 4);
        var f = _faces[0].Info;
        int ascent, descent, gap;
        StbTrueType.stbtt_GetFontVMetrics(f, &ascent, &descent, &gap);
        float scale = StbTrueType.stbtt_ScaleForPixelHeight(f, px);
        a.Ascent = ascent * scale;
        _atlases[px] = a;
        return a;
    }

    private Glyph GlyphFor(Atlas a, int cp)
    {
        if (a.Glyphs.TryGetValue(cp, out var g))
            return g;
        var face = _faces.FirstOrDefault(f => StbTrueType.stbtt_FindGlyphIndex(f.Info, cp) != 0).Info;
        if (face is null)
            return a.Glyphs[cp] = cp == '?' ? default : GlyphFor(a, '?');
        float scale = StbTrueType.stbtt_ScaleForPixelHeight(face, a.Size);
        int adv, lsb, x0, y0, x1, y1;
        StbTrueType.stbtt_GetCodepointHMetrics(face, cp, &adv, &lsb);
        StbTrueType.stbtt_GetCodepointBitmapBox(face, cp, scale, scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0)
            return a.Glyphs[cp] = new Glyph(0, 0, 0, 0, 0, 0, adv * scale);
        if (a.X + w + 1 >= AtlasSize)
        {
            a.X = 0;
            a.Y += a.RowH + 1;
            a.RowH = 0;
        }
        if (a.Y + h >= AtlasSize)
        {
            // full: start over (rare: thousands of different glyphs at one size)
            a.Glyphs.Clear();
            a.X = a.Y = a.RowH = 0;
        }
        var mono = new byte[w * h];
        fixed (byte* m = mono)
            StbTrueType.stbtt_MakeCodepointBitmap(face, m, w, h, w, scale, scale, cp);
        var rgba = new byte[w * h * 4];
        for (int i = 0; i < mono.Length; i++)
        {
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = mono[i];
        }
        fixed (byte* p = rgba)
            Sdl.UpdateTexture(a.Texture, new Sdl.Rect(a.X, a.Y, w, h), p, w * 4);
        g = new Glyph(a.X, a.Y, w, h, x0, y0, adv * scale);
        a.X += w + 1;
        a.RowH = Math.Max(a.RowH, h);
        return a.Glyphs[cp] = g;
    }

    /// <summary>Width in pixels of <paramref name="text"/> at <paramref name="px"/>.</summary>
    public float Measure(string text, int px)
    {
        var a = Get(px);
        float w = 0;
        foreach (var cp in Codepoints(text))
            w += GlyphFor(a, cp).Advance;
        return w;
    }

    /// <summary>Draws with the top of the line at <paramref name="y"/> (device pixels).</summary>
    public void Draw(string text, float x, float y, int px, Color c)
    {
        var a = Get(px);
        Sdl.SetTextureColorMod(a.Texture, c.R, c.G, c.B);
        Sdl.SetTextureAlphaMod(a.Texture, c.A);
        float pen = x;
        float baseline = y + a.Ascent;
        foreach (var cp in Codepoints(text))
        {
            var g = GlyphFor(a, cp);
            if (g.W > 0)
            {
                var src = new Sdl.Rect(g.X, g.Y, g.W, g.H);
                var dst = new Sdl.Rect((int)MathF.Round(pen) + g.OffX, (int)MathF.Round(baseline) + g.OffY, g.W, g.H);
                Sdl.RenderCopy(_renderer, a.Texture, src, dst);
            }
            pen += g.Advance;
        }
    }

    private static IEnumerable<int> Codepoints(string s)
    {
        for (int i = 0; i < s.Length; i++)
        {
            if (char.IsHighSurrogate(s[i]) && i + 1 < s.Length)
            {
                yield return char.ConvertToUtf32(s[i], s[i + 1]);
                i++;
            }
            else
            {
                yield return s[i];
            }
        }
    }

    public void Dispose()
    {
        foreach (var a in _atlases.Values)
            Sdl.DestroyTexture(a.Texture);
        _atlases.Clear();
    }
}

/// <summary>A texture and the part of it with something drawn (sprites are cropped to their pixels).</summary>
public sealed record Picture(IntPtr Texture, int W, int H, Sdl.Rect Content);

/// <summary>PNG pictures (the Pokémon sprites, ball icons) loaded once into textures.</summary>
public sealed unsafe class Pictures(IntPtr renderer, string folder) : IDisposable
{
    private readonly Dictionary<string, Picture?> _cache = [];

    public string Folder { get; } = folder;

    public Picture? Get(string name)
    {
        if (_cache.TryGetValue(name, out var p))
            return p;
        p = null;
        var path = Path.Combine(Folder, name + ".png");
        if (File.Exists(path))
        {
            try
            {
                var img = ImageResult.FromMemory(File.ReadAllBytes(path), ColorComponents.RedGreenBlueAlpha);
                p = FromRgba(renderer, img.Data, img.Width, img.Height, true);
            }
            catch (Exception)
            {
                p = null;
            }
        }
        _cache[name] = p;
        return p;
    }

    public static Picture FromRgba(IntPtr renderer, byte[] rgba, int w, int h, bool crop)
    {
        var tex = Sdl.CreateTexture(renderer, Sdl.PIXELFORMAT_ABGR8888, Sdl.TEXTUREACCESS_STATIC, w, h);
        Sdl.SetTextureBlendMode(tex, Sdl.BLENDMODE_BLEND);
        fixed (byte* d = rgba)
            Sdl.UpdateTexture(tex, IntPtr.Zero, d, w * 4);
        var content = new Sdl.Rect(0, 0, w, h);
        if (crop)
        {
            int minX = w, minY = h, maxX = -1, maxY = -1;
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    if (rgba[(y * w + x) * 4 + 3] < 8)
                        continue;
                    minX = Math.Min(minX, x);
                    maxX = Math.Max(maxX, x);
                    minY = Math.Min(minY, y);
                    maxY = Math.Max(maxY, y);
                }
            }
            if (maxX >= 0)
                content = new Sdl.Rect(minX, minY, maxX - minX + 1, maxY - minY + 1);
        }
        return new Picture(tex, w, h, content);
    }

    public void Dispose()
    {
        foreach (var p in _cache.Values)
        {
            if (p is not null)
                Sdl.DestroyTexture(p.Texture);
        }
        _cache.Clear();
    }
}

/// <summary>Small pixel-art symbols the font doesn't have, drawn as squares of the screen's scale.</summary>
public static class Icons
{
    public static readonly string[] Male =
    [
        "...####",
        ".....##",
        "....#.#",
        ".###..#",
        "#...#..",
        "#...#..",
        "#...#..",
        ".###...",
    ];

    public static readonly string[] Female =
    [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
        "..#..",
        "#####",
        "..#..",
    ];

    public static readonly string[] Star =
    [
        "...#...",
        "...#...",
        "..###..",
        "#######",
        ".#####.",
        "..###..",
        ".##.##.",
        "##...##",
    ];

    public static readonly string[] Check =
    [
        "......#",
        ".....##",
        "#...##.",
        "##.##..",
        ".###...",
        "..#....",
    ];

    public static readonly string[] Cross =
    [
        "#....#",
        "##..##",
        ".####.",
        "..##..",
        ".####.",
        "##..##",
        "#....#",
    ];

    public static readonly string[] Left =
    [
        "...#",
        "..##",
        ".###",
        "####",
        ".###",
        "..##",
        "...#",
    ];

    public static readonly string[] Right =
    [
        "#...",
        "##..",
        "###.",
        "####",
        "###.",
        "##..",
        "#...",
    ];

    public static readonly string[] Up =
    [
        "...#...",
        "..###..",
        ".#####.",
        "#######",
    ];

    public static readonly string[] Down =
    [
        "#######",
        ".#####.",
        "..###..",
        "...#...",
    ];

    public static readonly string[] Lock =
    [
        ".###.",
        "#...#",
        "#...#",
        "#####",
        "##.##",
        "##.##",
        "#####",
    ];

    public static readonly string[] Warn =
    [
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".....",
        "..#..",
    ];
}
