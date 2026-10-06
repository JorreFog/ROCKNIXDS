using PKHeX.Core;

namespace Rocknixds.Bank.App;

public enum Align { Left, Center, Right }

/// <summary>
/// Drawing on one screen in the layout's units: every screen is 640x480 units, scaled to the panel (1x on the RG DS,
/// 1.6x on the RG DS Plus). Text is rendered at the panel's own pixel size, so it stays sharp.
/// </summary>
public sealed class Canvas(IntPtr renderer, Font regular, Font medium, Pictures sprites)
{
    public const int W = 640, H = 480;

    public IntPtr Renderer { get; } = renderer;
    public Palette P { get; set; } = Palette.Dark;
    public float S { get; private set; } = 1;
    public int OX { get; private set; }
    public int OY { get; private set; }
    public Pictures Sprites { get; } = sprites;
    public uint Ticks { get; set; }

    public void Begin(int ox, int oy, float scale)
    {
        OX = ox;
        OY = oy;
        S = scale;
        Sdl.RenderSetClipRect(Renderer, new Sdl.Rect(ox, oy, (int)MathF.Ceiling(W * scale), (int)MathF.Ceiling(H * scale)));
        Fill(0, 0, W, H, P.Bg);
    }

    public void End() => Sdl.RenderSetClipRect(Renderer, IntPtr.Zero);

    private Sdl.Rect Dev(float x, float y, float w, float h)
    {
        int x0 = OX + (int)MathF.Round(x * S), y0 = OY + (int)MathF.Round(y * S);
        int x1 = OX + (int)MathF.Round((x + w) * S), y1 = OY + (int)MathF.Round((y + h) * S);
        return new Sdl.Rect(x0, y0, Math.Max(0, x1 - x0), Math.Max(0, y1 - y0));
    }

    public void Fill(float x, float y, float w, float h, Color c)
    {
        if (c.A == 0 || w <= 0 || h <= 0)
            return;
        Sdl.SetRenderDrawBlendMode(Renderer, c.A == 255 ? Sdl.BLENDMODE_NONE : Sdl.BLENDMODE_BLEND);
        Sdl.SetRenderDrawColor(Renderer, c.R, c.G, c.B, c.A);
        Sdl.RenderFillRect(Renderer, Dev(x, y, w, h));
    }

    /// <summary>A pixel-art box: a border with its corner pixels left out, and a fill.</summary>
    public void Box(float x, float y, float w, float h, Color fill, Color border, float t = 2)
    {
        Fill(x + t, y + t, w - 2 * t, h - 2 * t, fill);
        Fill(x + t, y, w - 2 * t, t, border);
        Fill(x + t, y + h - t, w - 2 * t, t, border);
        Fill(x, y + t, t, h - 2 * t, border);
        Fill(x + w - t, y + t, t, h - 2 * t, border);
    }

    /// <summary>The theme's panel: a lighter top band over the body, a dark lip at the bottom.</summary>
    public void Panel(float x, float y, float w, float h, bool focused = false, float band = 0)
    {
        Box(x, y, w, h, P.Body, focused ? P.Edge : P.Rule);
        if (band > 0)
            Fill(x + 2, y + 2, w - 4, band, P.Band);
        Fill(x + 2, y + h - 4, w - 4, 2, P.Low);
    }

    public void Frame(float x, float y, float w, float h, Color c, float t = 2)
    {
        Fill(x, y, w, t, c);
        Fill(x, y + h - t, w, t, c);
        Fill(x, y + t, t, h - 2 * t, c);
        Fill(x + w - t, y + t, t, h - 2 * t, c);
    }

    public void Shade(Color c) => Fill(0, 0, W, H, c);

    private Font F(bool bold) => bold ? medium : regular;
    private int Px(int size) => Math.Max(6, (int)MathF.Round(size * S));

    public float Measure(string text, int size, bool bold = false) => F(bold).Measure(text, Px(size)) / S;

    /// <summary>Text with its top at <paramref name="y"/>. Too wide for <paramref name="maxW"/>: cut with an ellipsis.</summary>
    public float Text(string text, float x, float y, int size, Color c, Align align = Align.Left, bool bold = false, float maxW = 0)
    {
        if (string.IsNullOrEmpty(text))
            return 0;
        if (maxW > 0 && Measure(text, size, bold) > maxW)
            text = Ellipsize(text, size, bold, maxW);
        float w = Measure(text, size, bold);
        float left = align switch { Align.Center => x - w / 2, Align.Right => x - w, _ => x };
        F(bold).Draw(text, OX + left * S, OY + y * S, Px(size), c);
        return w;
    }

    public string Ellipsize(string text, int size, bool bold, float maxW)
    {
        if (Measure(text, size, bold) <= maxW)
            return text;
        int lo = 0, hi = text.Length;
        while (lo < hi)
        {
            int mid = (lo + hi + 1) / 2;
            if (Measure(text[..mid] + "…", size, bold) <= maxW)
                lo = mid;
            else
                hi = mid - 1;
        }
        return text[..lo].TrimEnd() + "…";
    }

    /// <summary>Breaks <paramref name="text"/> into lines no wider than <paramref name="maxW"/>.</summary>
    public List<string> Wrap(string text, int size, float maxW, bool bold = false)
    {
        var lines = new List<string>();
        foreach (var para in text.Replace("\r", "").Split('\n'))
        {
            var line = "";
            foreach (var word in para.Split(' '))
            {
                var trial = line.Length == 0 ? word : line + " " + word;
                if (Measure(trial, size, bold) <= maxW || line.Length == 0)
                {
                    line = trial;
                    if (Measure(line, size, bold) > maxW)
                    {
                        lines.Add(Ellipsize(line, size, bold, maxW));
                        line = "";
                    }
                }
                else
                {
                    lines.Add(line);
                    line = word;
                }
            }
            lines.Add(line);
        }
        return lines;
    }

    /// <summary>Wrapped text; returns the height used.</summary>
    public float Paragraph(string text, float x, float y, float maxW, int size, Color c, int maxLines = 99, float lineH = 0, bool bold = false)
    {
        if (lineH <= 0)
            lineH = size * 1.35f;
        var lines = Wrap(text, size, maxW, bold);
        if (lines.Count > maxLines)
        {
            lines = lines.Take(maxLines).ToList();
            lines[^1] = Ellipsize(lines[^1] + "…", size, bold, maxW);
        }
        for (int i = 0; i < lines.Count; i++)
            Text(lines[i], x, y + i * lineH, size, c, bold: bold);
        return lines.Count * lineH;
    }

    public void Icon(string[] pattern, float x, float y, float dot, Color c)
    {
        Sdl.SetRenderDrawBlendMode(Renderer, c.A == 255 ? Sdl.BLENDMODE_NONE : Sdl.BLENDMODE_BLEND);
        Sdl.SetRenderDrawColor(Renderer, c.R, c.G, c.B, c.A);
        for (int row = 0; row < pattern.Length; row++)
        {
            for (int col = 0; col < pattern[row].Length; col++)
            {
                if (pattern[row][col] == '#')
                    Sdl.RenderFillRect(Renderer, Dev(x + col * dot, y + row * dot, dot, dot));
            }
        }
    }

    public static float IconWidth(string[] pattern, float dot) => pattern.Max(r => r.Length) * dot;

    /// <summary>Draws a picture's content centred in the box, at most at <paramref name="maxScale"/>.</summary>
    public void Picture(Picture p, float x, float y, float w, float h, float maxScale = 1, byte alpha = 255, bool smooth = false)
    {
        var c = p.Content;
        float scale = Math.Min(maxScale, Math.Min(w / c.W, h / c.H));
        float dw = c.W * scale, dh = c.H * scale;
        // whole device pixels per sprite pixel where it can be: crisp pixel art
        float dev = scale * S;
        bool integral = dev >= 1 && MathF.Abs(dev - MathF.Round(dev)) < 0.01f;
        Sdl.SetTextureScaleMode(p.Texture, integral || !smooth ? Sdl.ScaleModeNearest : Sdl.ScaleModeLinear);
        Sdl.SetTextureAlphaMod(p.Texture, alpha);
        Sdl.SetTextureColorMod(p.Texture, 255, 255, 255);
        var dst = Dev(x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
        Sdl.RenderCopy(Renderer, p.Texture, c, dst);
    }

    /// <summary>The Pokémon's sprite (PKHeX's box sprites), or its egg.</summary>
    public bool Mon(PKM pk, float x, float y, float w, float h, float maxScale = 1, byte alpha = 255) =>
        Mon(pk.Species, pk.Form, pk.Gender, pk.IsShiny, pk.IsEgg, x, y, w, h, maxScale, alpha);

    public bool Mon(ushort species, byte form, byte gender, bool shiny, bool egg, float x, float y, float w, float h, float maxScale = 1, byte alpha = 255)
    {
        var pic = egg ? Sprites.Get("b_egg") : SpriteFor(species, form, gender, shiny);
        if (pic is null)
        {
            Text(egg ? "Egg" : Names.Species(species), x + w / 2, y + h / 2 - 8, 14, P.Muted, Align.Center, maxW: w);
            return false;
        }
        Picture(pic, x, y, w, h, maxScale, alpha, smooth: true);
        return true;
    }

    private static readonly HashSet<ushort> Gendered = [449, 450, 521, 592, 593, 668];

    private Picture? SpriteFor(ushort species, byte form, byte gender, bool shiny)
    {
        var name = $"b_{species}";
        if (form != 0 && species is not (414 or 664 or 665 or 744 or 778 or 854 or 855 or 892 or 982 or 1012 or 1013))
            name += $"-{form}";
        if (gender == 1 && Gendered.Contains(species))
            name += "f";
        return (shiny ? Sprites.Get(name + "s") : null)
               ?? Sprites.Get(name)
               ?? (shiny ? Sprites.Get($"b_{species}s") : null)
               ?? Sprites.Get($"b_{species}")
               ?? Sprites.Get("b_unknown");
    }

    /// <summary>A button with its gamepad label: "[A] Move".</summary>
    public float Hint(string button, string label, float x, float y, Color? color = null, int size = 15)
    {
        var c = color ?? P.Muted;
        float bw = Math.Max(22, Measure(button, size - 3, true) + 10);
        Box(x, y, bw, size + 7, P.Chip, c, 1);
        Text(button, x + bw / 2, y + 3, size - 3, c, Align.Center, bold: true);
        float lw = Text(label, x + bw + 6, y + 2, size, P.Ink);
        return bw + 6 + lw + 14;
    }

    /// <summary>A clickable button; returns its rectangle for touch.</summary>
    public RectF Button(string label, float x, float y, float w, float h, bool focused, Color? accent = null, int size = 18)
    {
        var a = accent ?? P.Edge;
        Box(x, y, w, h, focused ? P.Band : P.Body, focused ? a : P.Rule, 2);
        if (focused)
            Fill(x + 2, y + h - 4, w - 4, 2, a);
        Text(label, x + w / 2, y + (h - size) / 2 - 1, size, focused ? P.Ink : P.Muted, Align.Center, bold: focused, maxW: w - 12);
        return new RectF(x, y, w, h);
    }

    /// <summary>A small coloured tag: "PK4", "Legal".</summary>
    public float Tag(string text, float x, float y, Color c, int size = 13, bool filled = false)
    {
        float w = Measure(text, size, true) + 10;
        Box(x, y, w, size + 7, filled ? c : P.Chip, c, 1);
        Text(text, x + 5, y + 3, size, filled ? (P.IsDark ? Color.Hex(0x0b1016) : Color.Hex(0xffffff)) : c, bold: true);
        return w;
    }

    public void GenderIcon(byte gender, float x, float y, float dot = 2)
    {
        if (gender == 0)
            Icon(Icons.Male, x, y, dot, Color.Hex(0x4aa3ff));
        else if (gender == 1)
            Icon(Icons.Female, x, y, dot, Color.Hex(0xff6b9a));
    }
}

public readonly record struct RectF(float X, float Y, float W, float H)
{
    public bool Contains(float px, float py) => px >= X && py >= Y && px < X + W && py < Y + H;
}
