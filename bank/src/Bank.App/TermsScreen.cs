using System.Text.RegularExpressions;

namespace Rocknixds.Bank.App;

/// <summary>
/// The terms of use (LEGAL.md). At the first start, and whenever their version changes, they must be read to the end
/// and accepted before anything else; not accepting quits. From the menu they can be read again.
/// </summary>
public sealed partial class TermsScreen(App app, Action? accepted) : Screen(app)
{
    private const float Top = 48, Bottom = 470;
    private readonly List<(string Text, int Size, bool Bold, float Indent, float Gap)> _lines = [];
    private float _laidOutFor = -1;
    private int _scroll;
    private int _visible = 1;
    private bool _readToEnd;
    private readonly List<(RectF R, Action A)> _hit = [];

    private bool MustAccept => accepted is not null;

    private int MaxScroll => Math.Max(0, _lines.Count - _visible);

    private void Scroll(int d)
    {
        _scroll = Math.Clamp(_scroll + d, 0, MaxScroll);
        if (_scroll >= MaxScroll)
            _readToEnd = true;
    }

    private void Agree()
    {
        if (!_readToEnd)
        {
            Scroll(_visible - 1);
            App.ShowToast("Read the terms to the end first (Down, R, or Next page).");
            return;
        }
        LegalTerms.Accept(App.Cfg);
        App.TrySaveConfig();
        App.Log($"terms of use version {LegalTerms.Version} accepted");
        accepted!();
    }

    private void Decline() => App.Confirm("Not accepted",
        "ROCKNIXDS Bank & Trade can only be used by people who accept its terms. It will close now; you can read the terms " +
        "again and accept them the next time you start it.", "Close the app", () => App.QuitRequested = true, "Back to the terms");

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.Quit)
        {
            App.QuitRequested = true;
            return;
        }
        if (e.Kind == InputKind.TouchUp)
        {
            foreach (var (r, a) in _hit)
            {
                if (r.Contains(e.X, e.Y))
                {
                    a();
                    return;
                }
            }
            return;
        }
        if (e.Is(Btn.Down)) Scroll(1);
        else if (e.Is(Btn.Up)) Scroll(-1);
        else if (e.Is(Btn.R)) Scroll(_visible - 1);
        else if (e.Is(Btn.L)) Scroll(-(_visible - 1));
        else if (MustAccept && (e.Pressed(Btn.A) || e.Pressed(Btn.Start))) Agree();
        else if (MustAccept && e.Pressed(Btn.B)) Decline();
        else if (!MustAccept && (e.Pressed(Btn.B) || e.Pressed(Btn.A))) App.Pop();
    }

    /// <summary>LEGAL.md as lines on the top screen: headings, paragraphs, bullets (Markdown's soft line breaks joined).</summary>
    private void LayOut(Canvas c)
    {
        _lines.Clear();
        const float width = 600;
        var paragraph = new List<string>();
        bool bullet = false;
        void Flush()
        {
            if (paragraph.Count == 0)
                return;
            var text = Plain(string.Join(" ", paragraph));
            float indent = bullet ? 18 : 0;
            var wrapped = c.Wrap(text, 15, width - indent);
            for (int i = 0; i < wrapped.Count; i++)
                _lines.Add(((bullet && i == 0 ? "• " : bullet ? "  " : "") + wrapped[i], 15, false, bullet && i == 0 ? 4 : indent, 0));
            if (!bullet)
                _lines.Add(("", 15, false, 0, 6));
            paragraph.Clear();
            bullet = false;
        }
        foreach (var raw in LegalTerms.Text.Replace("\r", "").Split('\n'))
        {
            var line = raw.TrimEnd();
            if (line.StartsWith("# ", StringComparison.Ordinal))
            {
                Flush();
                foreach (var w in c.Wrap(Plain(line[2..]), 21, width, bold: true))
                    _lines.Add((w, 21, true, 0, 0));
                _lines.Add(("", 15, false, 0, 4));
            }
            else if (line.StartsWith("## ", StringComparison.Ordinal))
            {
                Flush();
                _lines.Add((Plain(line[3..]), 18, true, 0, 6));
            }
            else if (line.StartsWith("- ", StringComparison.Ordinal))
            {
                Flush();
                bullet = true;
                paragraph.Add(line[2..]);
            }
            else if (line.Length == 0)
            {
                Flush();
            }
            else
            {
                paragraph.Add(line.Trim());
            }
        }
        Flush();
        _laidOutFor = c.S;
    }

    private static string Plain(string md) => Markup().Replace(md, "$1$2");

    [GeneratedRegex(@"\*\*([^*]+)\*\*|`([^`]+)`")]
    private static partial Regex Markup();

    public override void DrawTop(Canvas c)
    {
        if (_laidOutFor != c.S)
            LayOut(c);
        var p = c.P;
        Views.Header(c, App, "Terms of use", $"Version {LegalTerms.Version}");
        float y = Top + 6;
        int shown = 0;
        for (int i = _scroll; i < _lines.Count; i++)
        {
            var (text, size, bold, indent, gap) = _lines[i];
            float h = size * 1.3f + gap;
            if (y + h > Bottom)
                break;
            c.Text(text, 20 + indent, y + gap, size, bold ? p.Ink : p.Ink.Mix(p.Muted, 0.15f), bold: bold);
            y += h;
            shown++;
        }
        _visible = Math.Max(1, shown);
        if (_scroll >= MaxScroll)
            _readToEnd = true;
        // where in the text we are
        float barH = Bottom - Top, thumb = Math.Max(20, barH * _visible / Math.Max(1, _lines.Count));
        float ty = Top + (barH - thumb) * (MaxScroll == 0 ? 1 : (float)_scroll / MaxScroll);
        c.Fill(630, Top, 4, barH, p.Chip);
        c.Fill(630, ty, 4, thumb, p.Line);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        c.Text(MustAccept ? "Before you start" : "Terms of use", 320, 14, 22, p.Ink, Align.Center, bold: true);
        c.Paragraph("ROCKNIXDS does not support piracy. Use only game data you made yourself from cartridges you own, and " +
                    "never pirated copies. Pokémon, Nintendo and the other names belong to their owners; this fan project " +
                    "isn't affiliated with them. The full terms are on the top screen.", 32, 50, 576, 16, p.Ink);
        _hit.Add((c.Button("Previous page", 32, 196, 280, 48, false, size: 16), () => Scroll(-(_visible - 1))));
        _hit.Add((c.Button(_readToEnd ? "At the end" : "Next page", 328, 196, 280, 48, false, size: 16), () => Scroll(_visible - 1)));
        if (MustAccept)
        {
            var agree = c.Button("I agree: I'll use only my own games", 32, 266, 576, 60, _readToEnd, _readToEnd ? p.Good : p.Rule, 18);
            if (!_readToEnd)
                c.Text("Read to the end to accept", 320, 332, 14, p.Muted, Align.Center);
            _hit.Add((agree, Agree));
            _hit.Add((c.Button("I don't agree", 32, 356, 576, 48, false, size: 16), Decline));
            float x = 8;
            x += c.Hint("Up/Down", "Read", x, 446);
            x += c.Hint("A", "I agree", x, 446);
            c.Hint("B", "I don't agree", x, 446);
        }
        else
        {
            _hit.Add((c.Button("Close", 220, 300, 200, 56, true), App.Pop));
            c.Hint("B", "Close", 8, 446);
        }
    }
}
