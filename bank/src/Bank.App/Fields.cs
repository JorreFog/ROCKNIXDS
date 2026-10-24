using PKHeX.Core;

namespace Rocknixds.Bank.App;

/// <summary>
/// One row of a list of values to change: <paramref name="Step"/> (left/right, the row's arrows) nudges it, and
/// <paramref name="Activate"/> (A, a tap) opens what changes it otherwise (a list, a keyboard) or toggles it. A row with
/// neither is shown and can't be changed; a header (<see cref="Head"/>) starts a group.
/// </summary>
public sealed record Field(string Label, Func<string>? Value, Action? Activate = null, Action<int>? Step = null)
{
    public bool IsHeader { get; private init; }
    public bool Changeable => Activate is not null || Step is not null;

    public static Field Head(string label, Action? activate = null, Func<string>? value = null) => new(label, value, activate) { IsHeader = true };
}

/// <summary>
/// A scrolling list of <see cref="Field"/>s on the bottom screen, with the d-pad and touch: up/down choose, left/right
/// step by one, L/R by ten, A changes, Y the actions, START saves, B cancels. A finger drags the list, taps a row,
/// taps the arrows at a row's end. The editors are built on it.
/// </summary>
public abstract class FieldScreen(App app) : Screen(app)
{
    protected const float RowH = 42, ListY = 4;
    protected const int Visible = 9;

    protected int Sel, Scroll;
    private readonly List<(RectF R, Action A)> _touch = [];
    private float _touchY = -1;
    private bool _swiped;
    private int _touchRow = -1;

    protected abstract IReadOnlyList<Field> Fields { get; }

    /// <summary>What A on a changeable row without an action of its own does... and START, and the Save button.</summary>
    protected abstract void Save();

    protected abstract void Cancel();

    protected virtual string SaveLabel => "Save";
    protected virtual string CancelLabel => "Cancel";

    /// <summary>The Y menu (and the Actions button).</summary>
    protected virtual IReadOnlyList<DialogChoice> Actions() => [];

    /// <summary>Input the screen handles itself, before the list does: true when it did.</summary>
    protected virtual bool Extra(InputEvent e) => false;

    protected Field? Current => Sel >= 0 && Sel < Fields.Count ? Fields[Sel] : null;

    public override void Enter() => Fix(1);

    protected void ShowActions()
    {
        var a = Actions();
        if (a.Count == 0)
            return;
        App.Menu("Actions", [.. a, new DialogChoice("Close", () => { })], a.Count);
    }

    /// <summary>Keeps the selection on a row that isn't a header, and in view.</summary>
    protected void Fix(int dir)
    {
        var f = Fields;
        if (f.Count == 0)
        {
            Sel = Scroll = 0;
            return;
        }
        Sel = Math.Clamp(Sel, 0, f.Count - 1);
        for (int k = 0; k < f.Count && f[Sel].IsHeader && f[Sel].Activate is null; k++)
        {
            int next = Sel + dir;
            if (next < 0 || next >= f.Count)
            {
                dir = -dir;
                next = Sel + dir;
                if (next < 0 || next >= f.Count)
                    break;
            }
            Sel = next;
        }
        if (Sel < Scroll)
            Scroll = Sel;
        if (Sel >= Scroll + Visible)
            Scroll = Sel - Visible + 1;
        // a header just above the selection comes into view with it
        if (Sel == Scroll && Sel > 0 && f[Sel - 1].IsHeader)
            Scroll = Sel - 1;
        Scroll = Math.Clamp(Scroll, 0, Math.Max(0, f.Count - Visible));
    }

    private void Page(int d)
    {
        Sel += d * (Visible - 1);
        Scroll += d * (Visible - 1);
        Fix(d);
    }

    protected void Do(Field f, int step)
    {
        if (step != 0 && f.Step is not null)
            f.Step(step);
        else if (step == 0 && f.Activate is not null)
            f.Activate();
        else if (step == 0 && f.Step is not null)
            f.Step(1);
    }

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.Quit)
        {
            App.QuitRequested = true;
            return;
        }
        if (Extra(e))
            return;
        var fields = Fields;
        switch (e.Kind)
        {
            case InputKind.TouchDown:
                foreach (var (r, a) in _touch)
                {
                    if (r.Contains(e.X, e.Y))
                    {
                        a();
                        return;
                    }
                }
                _touchY = e.Y;
                _swiped = false;
                _touchRow = e.Y >= ListY && e.Y < ListY + Visible * RowH ? Scroll + (int)((e.Y - ListY) / RowH) : -1;
                return;
            case InputKind.TouchMove when _touchY >= 0:
                float dy = e.Y - _touchY;
                if (MathF.Abs(dy) > RowH * 0.6f)
                {
                    Scroll = Math.Clamp(Scroll - Math.Sign(dy), 0, Math.Max(0, fields.Count - Visible));
                    _touchY = e.Y;
                    _swiped = true;
                }
                return;
            case InputKind.TouchUp:
                if (!_swiped && _touchRow >= 0 && _touchRow < fields.Count && Scroll + (int)((e.Y - ListY) / RowH) == _touchRow)
                {
                    var f = fields[_touchRow];
                    if (f.IsHeader && f.Activate is null)
                        return;
                    Sel = _touchRow;
                    // the arrows at the row's end step it; the rest of the row changes it
                    if (f.Step is not null && e.X >= MinusX && e.X < MinusX + ArrowW)
                        Do(f, -1);
                    else if (f.Step is not null && e.X >= PlusX)
                        Do(f, 1);
                    else
                        Do(f, 0);
                }
                _touchY = -1;
                return;
        }
        if (e.Is(Btn.Up)) { Sel--; Fix(-1); }
        else if (e.Is(Btn.Down)) { Sel++; Fix(1); }
        else if (e.Is(Btn.Left) && Current is { } l) Do(l, -1);
        else if (e.Is(Btn.Right) && Current is { } r) Do(r, 1);
        else if (e.Is(Btn.L) && Current is { Step: not null } l10) Do(l10, -10);
        else if (e.Is(Btn.R) && Current is { Step: not null } r10) Do(r10, 10);
        else if (e.Is(Btn.L)) Page(-1);
        else if (e.Is(Btn.R)) Page(1);
        else if (e.Pressed(Btn.A) && Current is { } a) Do(a, 0);
        else if (e.Pressed(Btn.Y)) ShowActions();
        else if (e.Pressed(Btn.Start)) Save();
        else if (e.Pressed(Btn.B)) Cancel();
    }

    private const float MinusX = 540, PlusX = 588, ArrowW = 44;

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _touch.Clear();
        var fields = Fields;
        Fix(1);
        for (int i = 0; i < Visible && Scroll + i < fields.Count; i++)
        {
            int idx = Scroll + i;
            var f = fields[idx];
            float y = ListY + i * RowH;
            bool sel = idx == Sel;
            if (f.IsHeader)
            {
                c.Fill(8, y + 4, 616, RowH - 6, p.Band);
                float hw = c.Text(f.Label, 20, y + 12, 16, sel ? p.Ink : p.Muted, bold: true, maxW: 590);
                if (f.Value?.Invoke() is { Length: > 0 } hv)
                    c.Text(hv, 612, y + 12, 16, p.Muted, Align.Right, maxW: 590 - hw - 16);
                if (sel)
                    c.Frame(8, y + 4, 616, RowH - 6, p.Edge, 2);
                continue;
            }
            c.Box(8, y + 2, 616, RowH - 4, sel ? p.Band : p.Body, sel ? p.Edge : p.Rule, 2);
            var value = f.Value?.Invoke() ?? "";
            float right = f.Step is not null ? MinusX - 8 : 612;
            float lw = c.Text(f.Label, 20, y + 11, 17, f.Changeable ? p.Ink : p.Muted, bold: sel, maxW: 300);
            c.Text(value, right, y + 11, 17, sel ? p.Accent : f.Changeable ? p.Ink : p.Muted, Align.Right, maxW: right - 20 - lw - 16);
            if (f.Step is not null)
            {
                c.Box(MinusX, y + 5, ArrowW, RowH - 10, p.Chip, p.Rule, 1);
                c.Icon(Icons.Left, MinusX + 17, y + 14, 2, p.Muted);
                c.Box(PlusX, y + 5, ArrowW - 8, RowH - 10, p.Chip, p.Rule, 1);
                c.Icon(Icons.Right, PlusX + 13, y + 14, 2, p.Muted);
            }
        }
        if (fields.Count > Visible)
        {
            float h = Visible * RowH, th = Math.Max(16, h * Visible / fields.Count);
            float ty = ListY + (h - th) * Scroll / Math.Max(1, fields.Count - Visible);
            c.Fill(628, ListY, 6, h, p.Chip);
            c.Fill(628, ty, 6, th, p.Line);
        }

        // the buttons, for touch
        float by = 384, bw = 118, gap = 8.5f;
        var buttons = new List<(string Label, Action A, string[]? Icon)>
        {
            ("", () => Page(-1), Icons.Up),
            ("", () => Page(1), Icons.Down),
        };
        if (Actions().Count > 0)
            buttons.Add(("Actions", ShowActions, null));
        buttons.Add((CancelLabel, Cancel, null));
        buttons.Add((SaveLabel, Save, null));
        float bx = 8 + (5 - buttons.Count) * (bw + gap);
        for (int i = 0; i < buttons.Count; i++)
        {
            float x = i < 2 ? 8 + i * (bw + gap) : bx + i * (bw + gap);
            var (label, action, icon) = buttons[i];
            var r = c.Button(label, x, by, bw, 44, i == buttons.Count - 1, i == buttons.Count - 1 ? p.Good : null, size: 16);
            if (icon is not null)
                c.Icon(icon, x + bw / 2 - 7, by + 17, 2, p.Muted);
            _touch.Add((r, action));
        }
        DrawHints(c, 440);
    }

    protected virtual void DrawHints(Canvas c, float y)
    {
        float x = 8;
        x += c.Hint("A", "Change", x, y);
        x += c.Hint("←→", "±1", x, y);
        x += c.Hint("L/R", "±10", x, y);
        if (Actions().Count > 0)
            x += c.Hint("Y", "Actions", x, y);
        x += c.Hint("START", SaveLabel, x, y);
        c.Hint("B", CancelLabel, x, y);
    }
}

/// <summary>A long list to choose one from (species, moves, items...): a search (Y) narrows it.</summary>
public sealed class PickerScreen : FieldScreen
{
    private readonly string _title;
    private readonly IReadOnlyList<(int Value, string Text)> _all;
    private readonly Action<int> _picked;
    private readonly Action<Canvas, int>? _preview;
    private string _filter = "";
    private List<Field> _fields = [];
    private List<int> _values = [];

    public PickerScreen(App app, string title, IReadOnlyList<(int Value, string Text)> items, int current, Action<int> picked,
        Action<Canvas, int>? preview = null) : base(app)
    {
        _title = title;
        _all = items;
        _picked = picked;
        _preview = preview;
        Filter();
        Sel = Math.Max(0, _values.IndexOf(current));
        Scroll = Math.Max(0, Sel - Visible / 2);
    }

    private void Filter()
    {
        var f = _filter.Trim();
        var list = f.Length == 0 ? _all : _all.Where(i => i.Text.Contains(f, StringComparison.OrdinalIgnoreCase) || i.Value.ToString() == f).ToList();
        _values = list.Select(i => i.Value).ToList();
        _fields = list.Select(i => new Field(i.Text, null, () => Pick(i.Value))).ToList();
        Sel = Scroll = 0;
    }

    private void Pick(int value)
    {
        App.Pop();
        _picked(value);
    }

    protected override IReadOnlyList<Field> Fields => _fields;
    protected override string SaveLabel => "Search";
    protected override string CancelLabel => "Back";

    protected override void Save() => App.Push(new KeyboardScreen(App, $"Search: {_title}", KeyboardScreen.Kind.Text, _filter, 20, t =>
    {
        _filter = t;
        Filter();
    }));

    protected override void Cancel()
    {
        if (_filter.Length > 0)
        {
            _filter = "";
            Filter();
            return;
        }
        App.Pop();
    }

    protected override IReadOnlyList<DialogChoice> Actions() => [];

    protected override bool Extra(InputEvent e)
    {
        if (e.Pressed(Btn.Y) || e.Pressed(Btn.X))
        {
            Save();
            return true;
        }
        if (e.Pressed(Btn.Select) && _filter.Length > 0)
        {
            _filter = _filter[..^1];
            Filter();
            return true;
        }
        // left/right page through a list of names
        if (e.Is(Btn.Left) || e.Is(Btn.Right))
        {
            Sel += e.Btn == Btn.Left ? -(Visible - 1) : Visible - 1;
            Fix(e.Btn == Btn.Left ? -1 : 1);
            return true;
        }
        if (e.Pressed(Btn.Start))
            return true;
        return false;
    }

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, _title, $"{_values.Count} of {_all.Count}");
        var p = c.P;
        int value = Sel < _values.Count ? _values[Sel] : -1;
        if (value >= 0 && _preview is not null)
            _preview(c, value);
        else if (value >= 0)
            c.Text(_fields[Sel].Label, 320, 150, 34, p.Ink, Align.Center, bold: true, maxW: 600);
        c.Box(40, 380, 560, 44, p.Chip, _filter.Length > 0 ? p.Edge : p.Rule, 2);
        c.Text(_filter.Length > 0 ? _filter : "Y: search", 56, 391, 18, _filter.Length > 0 ? p.Ink : p.Muted, maxW: 528);
        c.Text("A chooses · B back (and clears the search) · SELECT deletes a letter", 320, 440, 14, p.Muted, Align.Center, maxW: 600);
    }

    protected override void DrawHints(Canvas c, float y)
    {
        float x = 8;
        x += c.Hint("A", "Choose", x, y);
        x += c.Hint("←→", "Page", x, y);
        x += c.Hint("Y", "Search", x, y);
        c.Hint("B", _filter.Length > 0 ? "Clear" : "Back", x, y);
    }
}

/// <summary>The names an editor offers for a Pokémon's format: PKHeX's lists, filtered to what that game has.</summary>
public static class EditSources
{
    private static readonly Dictionary<string, (SaveFile Sav, FilteredGameDataSource Src)> Cache = [];

    /// <summary>A save of <paramref name="pk"/>'s format: the open one when it matches, otherwise a blank one.</summary>
    public static SaveFile SaveFor(App app, PKM pk) =>
        app.Mover.Save?.Sav is { } s && s.PKMType == pk.GetType() ? s : Get(pk.Context).Sav;

    public static FilteredGameDataSource For(App app, PKM pk) => For(SaveFor(app, pk));

    public static FilteredGameDataSource For(SaveFile sav)
    {
        var key = $"{sav.GetType().Name}/{sav.Version}/{GameInfo.CurrentLanguage}";
        lock (Cache)
        {
            if (!Cache.TryGetValue(key, out var e))
            {
                e = (sav, new FilteredGameDataSource(sav, GameInfo.Sources, false));
                Cache[key] = e;
            }
            return e.Src;
        }
    }

    private static (SaveFile Sav, FilteredGameDataSource Src) Get(EntityContext context)
    {
        var key = $"blank/{context}/{GameInfo.CurrentLanguage}";
        lock (Cache)
        {
            if (!Cache.TryGetValue(key, out var e))
            {
                var sav = BlankSaveFile.Get(context, "BANK", LanguageID.English);
                e = (sav, new FilteredGameDataSource(sav, GameInfo.Sources, false));
                Cache[key] = e;
            }
            return e;
        }
    }

    public static List<(int Value, string Text)> List(IReadOnlyList<ComboItem> items, bool withNone = false) =>
        items.Where(i => withNone || i.Value != 0).Select(i => (i.Value, i.Text)).ToList();
}
