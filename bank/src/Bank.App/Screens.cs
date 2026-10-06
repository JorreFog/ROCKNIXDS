using System.Net;
using PKHeX.Core;
using Rocknixds.Bank.Trade;

namespace Rocknixds.Bank.App;

/// <summary>The first screen: opens the bank and looks for saves, then hands over to the boxes.</summary>
public sealed class LoadingScreen(App app) : Screen(app)
{
    private bool _started;

    public override void Enter()
    {
        if (_started)
            return;
        _started = true;
        App.Run("Opening the bank...", () =>
        {
            App.Load();
            return SaveScanner.Scan(App.Cfg, App.Log);
        }, saves =>
        {
            App.SetSaves(saves);
            App.Replace(new BoxScreen(App));
            var last = saves.FirstOrDefault(s => s.Path == App.Cfg.LastSave);
            if (last is not null)
                App.OpenSave(last, () => App.Find<BoxScreen>()?.Refresh());
        }, ex => App.Message("The bank can't open", ex.Message, () => App.QuitRequested = true));
    }

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.Quit)
            App.QuitRequested = true;
    }

    public override void DrawTop(Canvas c)
    {
        var p = c.P;
        c.Fill(0, 0, Canvas.W, Canvas.H, p.Bg);
        c.Fill(220, 170, 24, 24, p.Red);
        c.Text("ROCKNIXDS", 256, 160, 40, p.Ink, bold: true);
        c.Text("Bank & Trade", 256, 206, 24, p.Muted);
        c.Text("Pokémon storage, legality checks and trading for the RG DS", 320, 300, 16, p.Muted, Align.Center);
    }

    public override void DrawBottom(Canvas c) { }
}

/// <summary>The list of game saves found on the card.</summary>
public sealed class SavePickerScreen(App app) : Screen(app)
{
    private const float RowH = 66, ListY = 50;
    private const int Visible = 5;
    private int _sel, _scroll;
    private float _touchY = -1;
    private int _touchRow = -1;
    private bool _swiped;
    private readonly List<(RectF R, Action A)> _touch = [];

    public override void Enter()
    {
        if (!App.SavesScanned)
            App.ScanSaves();
        var open = App.Mover?.Save?.Path;
        var i = App.Saves.FindIndex(s => s.Path == open);
        if (i >= 0)
            _sel = i;
    }

    private void Open()
    {
        if (App.Saves.Count == 0)
            return;
        var e = App.Saves[_sel];
        App.OpenSave(e, () =>
        {
            App.Pop();
            App.Find<BoxScreen>()?.Refresh();
            App.ShowToast($"{e.GameName} ({e.Trainer}) is open on the left.");
        });
    }

    public override void Handle(InputEvent e)
    {
        int n = App.Saves.Count;
        if (e.Kind == InputKind.TouchDown)
        {
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
            _touchRow = e.Y >= ListY && e.Y < ListY + Visible * RowH ? _scroll + (int)((e.Y - ListY) / RowH) : -1;
            return;
        }
        if (e.Kind == InputKind.TouchMove && _touchY >= 0)
        {
            float dy = e.Y - _touchY;
            if (MathF.Abs(dy) > RowH * 0.6f)
            {
                _scroll = Math.Clamp(_scroll - Math.Sign(dy), 0, Math.Max(0, n - Visible));
                _touchY = e.Y;
                _swiped = true;
            }
            return;
        }
        if (e.Kind == InputKind.TouchUp)
        {
            if (!_swiped && _touchRow >= 0 && _touchRow < n)
            {
                int row = _scroll + (int)((e.Y - ListY) / RowH);
                if (row == _touchRow)
                {
                    _sel = row;
                    Open();
                }
            }
            _touchY = -1;
            return;
        }
        if (e.Is(Btn.Up) && n > 0) _sel = (_sel - 1 + n) % n;
        else if (e.Is(Btn.Down) && n > 0) _sel = (_sel + 1) % n;
        else if (e.Is(Btn.L)) _sel = Math.Max(0, _sel - Visible);
        else if (e.Is(Btn.R)) _sel = Math.Min(Math.Max(0, n - 1), _sel + Visible);
        else if (e.Pressed(Btn.A)) Open();
        else if (e.Pressed(Btn.B) || e.Pressed(Btn.Select)) App.Pop();
        else if (e.Pressed(Btn.X)) App.ScanSaves(() => _sel = 0);
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
        if (_sel < _scroll) _scroll = _sel;
        if (_sel >= _scroll + Visible) _scroll = _sel - Visible + 1;
    }

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Game saves");
        var p = c.P;
        if (App.Saves.Count == 0)
        {
            c.Text(App.SavesScanned ? "No game saves found" : "Looking for game saves...", 320, 140, 26, p.Ink, Align.Center, bold: true);
            c.Paragraph("Saves are looked for in " + string.Join(", ", App.Cfg.SaveFolders) +
                        " and its folders: DraStic's .dsv next to the DS game, .sav from melonDS and the GBA emulators, " +
                        ".srm from RetroArch. Play a game until it has saved once, then look again (X).", 70, 200, 500, 17, p.Muted);
            return;
        }
        Views.SaveDetails(c, App, App.Saves[Math.Clamp(_sel, 0, App.Saves.Count - 1)], null);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _touch.Clear();
        c.Fill(0, 0, Canvas.W, 44, p.Band);
        c.Text("Choose a game", 16, 11, 20, p.Ink, bold: true);
        c.Text($"{App.Saves.Count} found", 624, 13, 16, p.Muted, Align.Right);
        var open = App.Mover?.Save?.Path;
        for (int i = 0; i < Visible && _scroll + i < App.Saves.Count; i++)
        {
            int idx = _scroll + i;
            var e = App.Saves[idx];
            float y = ListY + i * RowH;
            bool sel = idx == _sel;
            c.Box(8, y + 2, 600, RowH - 6, sel ? p.Band : p.Body, sel ? p.Edge : p.Rule, 2);
            c.Fill(10, y + 4, 8, RowH - 10, Views.GameColor(e.Version));
            c.Text(e.GameName, 30, y + 9, 19, p.Ink, bold: true, maxW: 300);
            c.Text(string.Join(" · ", new[] { e.Trainer, e.PlayTime, $"{e.Pokemon} in boxes" }.Where(t => t.Length > 0)), 30, y + 35, 14, p.Muted, maxW: 330);
            c.Text(e.FileTitle, 596, y + 11, 13, p.Muted, Align.Right, maxW: 250);
            if (e.Path == open)
                c.Tag("Open", 596 - c.Measure("Open", 12, true) - 10, y + 34, p.Good, 12);
        }
        // scroll bar
        if (App.Saves.Count > Visible)
        {
            float h = Visible * RowH, th = h * Visible / App.Saves.Count;
            float ty = ListY + (h - th) * _scroll / Math.Max(1, App.Saves.Count - Visible);
            c.Fill(618, ListY, 8, h, p.Chip);
            c.Fill(618, ty, 8, th, p.Line);
        }
        _touch.Add((c.Button("Look again", 8, 392, 196, 40, false, size: 16), () => App.ScanSaves(() => _sel = 0)));
        _touch.Add((c.Button("Back", 214, 392, 120, 40, false, size: 16), App.Pop));
        float x = 8;
        x += c.Hint("A", "Open", x, 446);
        x += c.Hint("B", "Back", x, 446);
        c.Hint("X", "Look again", x, 446);
    }
}

public static class MainMenu
{
    public static void Show(App app)
    {
        app.Menu("Menu",
        [
            new("Choose a game save", () => app.Push(new SavePickerScreen(app)), app.Trade is null),
            new("Trade with another handheld", () => app.Push(new TradeMenuScreen(app)), app.Trade is null),
            new("Import files from the import folder", () =>
            {
                var notes = app.Bank.ImportDropped();
                app.Find<BoxScreen>()?.Refresh();
                app.Message("Import", notes.Count == 0 ? $"Nothing to import. Put PKHeX files (.pk1 to .pk9) in {app.Cfg.ImportFolder}." : string.Join("\n", notes.Take(8)));
            }),
            new("Settings", () => app.Push(new SettingsScreen(app))),
            new("About", () => app.Push(new AboutScreen(app))),
            new("Quit", () => app.QuitRequested = true),
            new("Close", () => { }),
        ], cancelIndex: 6);
    }
}

public sealed class SettingsScreen(App app) : Screen(app)
{
    private int _sel;
    private readonly List<RectF> _rows = [];

    private static readonly string[] Languages = ["en", "ja", "fr", "it", "de", "es", "ko", "zh-Hans", "zh-Hant"];

    private (string Label, string Value, string Help, Action Change)[] Items
    {
        get
        {
            var c = App.Cfg;
            return
            [
                ("Colours", c.Palette switch { "dark" => "Dark", "light" => "Light", _ => "Follow the menu" },
                    "Follow the menu: light with ROCKNIXDS Pixel light, dark otherwise.",
                    () =>
                    {
                        c.Palette = c.Palette switch { "auto" => "dark", "dark" => "light", _ => "auto" };
                        App.ApplyPalette();
                    }),
                ("Trade evolutions", c.TradeEvolutions ? "On" : "Off",
                    "Kadabra, Machoke, Graveler, Haunter, the held-item ones and Karrablast with Shelmet evolve when they arrive in a trade, as in the games. An Everstone stops it.",
                    () => c.TradeEvolutions = !c.TradeEvolutions),
                ("Pokémon that fail the check, into games", c.BlockIllegalIntoSaves ? "Never" : "Ask first",
                    "Putting a Pokémon that fails PKHeX's legality check into a game: ask first, or never allow it. Taking Pokémon out of games is always allowed.",
                    () => c.BlockIllegalIntoSaves = !c.BlockIllegalIntoSaves),
                ("Trading for Pokémon that fail the check", c.BlockIllegalTrades ? "Never" : "Ask first",
                    "Accepting a partner's Pokémon that fails the legality check: ask first, or never allow it.",
                    () => c.BlockIllegalTrades = !c.BlockIllegalTrades),
                ("Your name in trades", c.EffectiveTrainerName,
                    "What trade partners see, in their room list and on their screen.",
                    () => App.Push(new KeyboardScreen(App, "Your name in trades", KeyboardScreen.Kind.Text, c.EffectiveTrainerName, 16, name =>
                    {
                        c.TrainerName = name;
                        App.TrySaveConfig();
                    }))),
                ("Names of species, moves and items", c.Language,
                    "The language of PKHeX's names: en, ja, fr, it, de, es, ko, zh-Hans, zh-Hant.",
                    () =>
                    {
                        var i = Array.IndexOf(Languages, c.Language);
                        c.Language = Languages[(i + 1) % Languages.Length];
                        Names.SetLanguage(c.Language);
                    }),
                ("Swap A and B", c.SwapAB ? "On" : "Off",
                    "For pads labelled the other way round. Takes effect at the next start.",
                    () => c.SwapAB = !c.SwapAB),
            ];
        }
    }

    public override void Handle(InputEvent e)
    {
        var items = Items;
        if (e.Kind == InputKind.TouchUp)
        {
            for (int i = 0; i < _rows.Count; i++)
            {
                if (_rows[i].Contains(e.X, e.Y))
                {
                    _sel = i;
                    Change(items[i]);
                    return;
                }
            }
            if (e.Y > 420)
                App.Pop();
            return;
        }
        if (e.Is(Btn.Up)) _sel = (_sel - 1 + items.Length) % items.Length;
        else if (e.Is(Btn.Down)) _sel = (_sel + 1) % items.Length;
        else if (e.Pressed(Btn.A) || e.Is(Btn.Right) || e.Is(Btn.Left)) Change(items[_sel]);
        else if (e.Pressed(Btn.B)) App.Pop();
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
    }

    private void Change((string Label, string Value, string Help, Action Change) item)
    {
        item.Change();
        App.TrySaveConfig();
    }

    public override void Leave() => App.TrySaveConfig();

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Settings");
        var p = c.P;
        var item = Items[_sel];
        c.Text(item.Label, 32, 80, 24, p.Ink, bold: true, maxW: 576);
        c.Text(item.Value, 32, 120, 20, p.Accent, maxW: 576);
        c.Paragraph(item.Help, 32, 170, 576, 17, p.Muted);
        c.Paragraph($"Settings file: {App.Cfg.ConfigPath}", 32, 400, 576, 14, p.Muted);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _rows.Clear();
        var items = Items;
        for (int i = 0; i < items.Length; i++)
        {
            float y = 10 + i * 56;
            bool sel = i == _sel;
            c.Box(8, y, 624, 50, sel ? p.Band : p.Body, sel ? p.Edge : p.Rule, 2);
            c.Text(items[i].Label, 22, y + 14, 17, p.Ink, bold: sel, maxW: 400);
            c.Text(items[i].Value, 618, y + 14, 17, sel ? p.Accent : p.Muted, Align.Right, maxW: 190);
            _rows.Add(new RectF(8, y, 624, 50));
        }
        float x = 8;
        x += c.Hint("A", "Change", x, 446);
        c.Hint("B", "Back", x, 446);
    }
}

public sealed class AboutScreen(App app) : Screen(app)
{
    public override void Handle(InputEvent e)
    {
        if (e.Pressed(Btn.B) || e.Pressed(Btn.A) || e.Kind == InputKind.TouchUp)
            App.Pop();
        else if (e.Kind == InputKind.Quit)
            App.QuitRequested = true;
    }

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "About");
        var p = c.P;
        c.Text("ROCKNIXDS Bank & Trade", 32, 64, 28, p.Ink, bold: true);
        c.Text($"Version {typeof(App).Assembly.GetName().Version?.ToString(3)}", 32, 104, 18, p.Muted);
        float y = 150;
        Line("PKHeX.Core", typeof(PKM).Assembly.GetName().Version?.ToString(3) ?? "?");
        Line("SDL", $"{Sdl.Version} ({Sdl.VideoDriver})");
        Line("Pad", Program.PadDescription);
        Line("Bank", $"{App.Bank.Count} Pokémon, {App.Bank.BoxCount} boxes");
        Line("Data", App.Cfg.DataFolder);
        Line("Network", string.Join(", ", Network.LocalAddresses().DefaultIfEmpty("not connected")));

        void Line(string label, string value)
        {
            c.Text(label, 32, y, 16, p.Muted);
            c.Text(value, 160, y, 16, p.Ink, maxW: 450);
            y += 30;
        }
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        c.Paragraph(
            "Save reading and writing, the conversions between generations and the legality checks are PKHeX.Core " +
            "by Kaphotics and the PKHeX contributors (GPL-3.0), which makes this app GPL-3.0 too. The Pokémon box sprites " +
            "come with PKHeX. Font: Pixelify Sans (SIL Open Font License).\n\n" +
            "ROCKNIXDS does not support piracy: use only games you dumped from cartridges you own. Pokémon, Nintendo and the " +
            "other names are trademarks of their owners; this fan project isn't affiliated with them. The terms of use " +
            "are in LEGAL.md in the ROCKNIXDS repository.\n\n" +
            "Saves are backed up the first time they change in a session (the backups folder in the data folder), and every " +
            "move and trade is written to history.log there.",
            24, 24, 592, 16, p.Ink);
        c.Hint("B", "Back", 8, 446);
    }
}

/// <summary>An on-screen keyboard on the bottom screen, the text on the top one.</summary>
public sealed class KeyboardScreen : Screen
{
    public enum Kind { Code, Address, Text }

    private readonly string _title;
    private readonly Kind _kind;
    private readonly int _max;
    private readonly Action<string> _done;
    private readonly Action? _cancel;
    private string _text;
    private int _row, _col;
    private bool _lower;
    private readonly List<(RectF R, string Key)> _hit = [];

    public KeyboardScreen(App app, string title, Kind kind, string initial, int max, Action<string> done, Action? cancel = null) : base(app)
    {
        _title = title;
        _kind = kind;
        _text = initial;
        _max = max;
        _done = done;
        _cancel = cancel;
    }

    private string[][] Rows => _kind switch
    {
        Kind.Code =>
        [
            // ShareCode.Alphabet: no 0/O, 1/I, and none of B, S, Z (G, 5, 2 in the pixel font)
            ["A", "C", "D", "E", "F", "G", "H", "J"],
            ["K", "L", "M", "N", "P", "Q", "R", "T"],
            ["U", "V", "W", "X", "Y", "2", "3", "4"],
            ["5", "6", "7", "8", "9"],
            ["Del", "OK"],
        ],
        Kind.Address =>
        [
            ["1", "2", "3"],
            ["4", "5", "6"],
            ["7", "8", "9"],
            [".", "0", ":"],
            ["Del", "OK"],
        ],
        _ =>
        [
            ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
            (_lower ? "qwertyuiop" : "QWERTYUIOP").Select(ch => ch.ToString()).ToArray(),
            (_lower ? "asdfghjkl'" : "ASDFGHJKL'").Select(ch => ch.ToString()).ToArray(),
            (_lower ? "zxcvbnm-._" : "ZXCVBNM-._").Select(ch => ch.ToString()).ToArray(),
            ["Shift", "Space", "Del", "OK"],
        ],
    };

    private void Press(string key)
    {
        switch (key)
        {
            case "Del":
                if (_text.Length > 0)
                    _text = _text[..^1];
                break;
            case "OK":
                Finish();
                break;
            case "Shift":
                _lower = !_lower;
                break;
            case "Space":
                Type(" ");
                break;
            default:
                Type(key);
                break;
        }
    }

    private void Type(string s)
    {
        foreach (var ch in s)
        {
            if (_text.Length >= _max)
                return;
            var c = _kind == Kind.Code ? char.ToUpperInvariant(ch) : ch;
            bool ok = _kind switch
            {
                Kind.Code => ShareCode.Alphabet.Contains(c),
                Kind.Address => char.IsAsciiLetterOrDigit(c) || c is '.' or ':' or '-',
                _ => !char.IsControl(c),
            };
            if (ok)
                _text += c;
        }
    }

    private void Finish()
    {
        var t = _text.Trim();
        if (_kind == Kind.Code && !ShareCode.IsComplete(t))
        {
            App.ShowToast($"The code has {ShareCode.Length} characters.", true);
            return;
        }
        if (t.Length == 0)
            return;
        App.Pop();
        _done(t);
    }

    public override void Enter() => Sdl.StartTextInput();
    public override void Leave() => Sdl.StopTextInput();

    public override void Handle(InputEvent e)
    {
        var rows = Rows;
        if (e.Kind == InputKind.Text)
        {
            Type(e.Text!);
            return;
        }
        if (e.Kind == InputKind.TouchUp)
        {
            foreach (var (r, k) in _hit)
            {
                if (r.Contains(e.X, e.Y))
                {
                    Press(k);
                    return;
                }
            }
            return;
        }
        if (e.Is(Btn.Up)) _row = (_row - 1 + rows.Length) % rows.Length;
        else if (e.Is(Btn.Down)) _row = (_row + 1) % rows.Length;
        else if (e.Is(Btn.Left)) _col--;
        else if (e.Is(Btn.Right)) _col++;
        else if (e.Pressed(Btn.A)) Press(rows[_row][Math.Clamp(_col, 0, rows[_row].Length - 1)]);
        else if (e.Is(Btn.B) || e.Is(Btn.Select))
        {
            if (_text.Length > 0)
                _text = _text[..^1];
            else if (e.Pressed(Btn.B))
            {
                App.Pop();
                _cancel?.Invoke();
            }
        }
        else if (e.Pressed(Btn.Start)) Finish();
        else if (e.Pressed(Btn.Y) && _kind == Kind.Text) _lower = !_lower;
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
        _col = (_col % rows[_row].Length + rows[_row].Length) % rows[_row].Length;
    }

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, _title);
        var p = c.P;
        c.Box(40, 150, 560, 90, p.Chip, p.Edge, 2);
        var shown = _kind == Kind.Code ? ShareCode.Pretty(_text) : _text;
        float w = c.Text(shown, 320, 170, 44, p.Ink, Align.Center, bold: true, maxW: 520);
        if ((App.Now / 450) % 2 == 0)
            c.Fill(320 + w / 2 + 4, 170, 4, 48, p.Edge);
        string hint = _kind switch
        {
            Kind.Code => "The code the host shows: 6 letters and digits.",
            Kind.Address => "The host's address, like 192.168.1.23 (a port after a colon if it isn't the usual one).",
            _ => $"Up to {_max} characters.",
        };
        c.Paragraph(hint, 60, 270, 520, 17, p.Muted);
    }

    public override bool Animating => true;

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        var rows = Rows;
        float top = 14, h = 62, gap = 8;
        for (int r = 0; r < rows.Length; r++)
        {
            var keys = rows[r];
            float kw = (624 - (keys.Length - 1) * gap) / keys.Length;
            for (int k = 0; k < keys.Length; k++)
            {
                float x = 8 + k * (kw + gap), y = top + r * (h + gap);
                bool sel = r == _row && k == _col;
                var key = keys[k];
                c.Box(x, y, kw, h, sel ? p.Band : p.KeyBg, sel ? p.Edge : p.Rule, 2);
                var label = key == "Shift" ? (_lower ? "ABC" : "abc") : key;
                c.Text(label, x + kw / 2, y + h / 2 - 13, key.Length > 1 ? 20 : 26, key == "OK" ? p.Good : p.KeyInk, Align.Center, bold: true);
                _hit.Add((new RectF(x, y, kw, h), key));
            }
        }
        float hx = 8;
        hx += c.Hint("A", "Type", hx, 446);
        hx += c.Hint("B", "Delete", hx, 446);
        c.Hint("START", "OK", hx, 446);
    }
}

public sealed class TradeMenuScreen(App app) : Screen(app)
{
    private int _sel;
    private readonly List<RectF> _hit = [];

    private static readonly (string Label, string Sub)[] Items =
    [
        ("Open a lobby", "List a Pokémon and what you want for it"),
        ("Open a private room", "Show an address and a code for your partner"),
        ("Join a lobby or room", "See what's on offer on this network"),
        ("Back", ""),
    ];

    private void Choose(int i)
    {
        switch (i)
        {
            case 0:
                LobbyFlow.Start(App);
                break;
            case 1:
                App.Push(new TradeHostScreen(App));
                break;
            case 2:
                App.Push(new TradeJoinScreen(App));
                break;
            default:
                App.Pop();
                break;
        }
    }

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.TouchUp)
        {
            for (int i = 0; i < _hit.Count; i++)
            {
                if (_hit[i].Contains(e.X, e.Y))
                {
                    Choose(i);
                    return;
                }
            }
            return;
        }
        if (e.Is(Btn.Up)) _sel = (_sel + Items.Length - 1) % Items.Length;
        else if (e.Is(Btn.Down)) _sel = (_sel + 1) % Items.Length;
        else if (e.Pressed(Btn.A)) Choose(_sel);
        else if (e.Pressed(Btn.B)) App.Pop();
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
    }

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Trade");
        var p = c.P;
        c.Text("Trade with another handheld", 32, 64, 26, p.Ink, bold: true);
        c.Paragraph("Both handhelds on the same Wi-Fi. Open a lobby to list a Pokémon and the one you want for it: everyone " +
                    "here sees it in their list. Or open a private room and give your partner its code. Each side offers a " +
                    "Pokémon, both are checked with PKHeX, and the trade happens when both accept. What you receive goes " +
                    "into your bank.", 32, 110, 576, 17, p.Muted);
        var ips = Network.LocalAddresses();
        c.Text("This handheld", 32, 330, 16, p.Muted);
        c.Text(App.Cfg.EffectiveTrainerName, 180, 330, 17, p.Ink, maxW: 420);
        c.Text("Address", 32, 360, 16, p.Muted);
        c.Text(ips.Count > 0 ? string.Join(", ", ips) : "No network. Connect to Wi-Fi first (ROCKNIX's network settings).", 180, 360, 17,
            ips.Count > 0 ? p.Ink : p.Bad, maxW: 420);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        for (int i = 0; i < Items.Length; i++)
        {
            bool back = i == Items.Length - 1;
            float y = 16 + i * 96;
            var r = c.Button("", 40, y, 560, back ? 56 : 84, i == _sel);
            c.Text(Items[i].Label, 320, y + (back ? 16 : 14), 22, p.Ink, Align.Center, bold: true);
            if (Items[i].Sub.Length > 0)
                c.Text(Items[i].Sub, 320, y + 50, 15, p.Muted, Align.Center);
            _hit.Add(r);
        }
    }
}

public sealed class TradeHostScreen(App app) : Screen(app)
{
    private TradeHost? _host;
    private readonly List<(RectF R, Action A)> _hit = [];

    public override void Enter()
    {
        if (_host is null)
            Start();
    }

    private void Start()
    {
        _host?.Dispose();
        _host = new TradeHost(App.Cfg, App.Identity);
        _host.Start();
        WriteCodeFile();
    }

    private void NewCode()
    {
        _host?.NewCode();
        WriteCodeFile();
    }

    private string? _codeWritten;

    // scripted runs (two instances trading for the screenshots): tell the joining script the code
    private void WriteCodeFile()
    {
        if (_host is null || _codeWritten == _host.Code)
            return;
        _codeWritten = _host.Code;
        if (Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_CODE_FILE") is { Length: > 0 } codeFile)
            File.WriteAllText(codeFile, _host.Code);
    }

    public override void Leave()
    {
        if (App.Trade is null || _host?.Channel is null)
            _host?.Dispose();
    }

    public override void Update()
    {
        WriteCodeFile();
        if (_host?.Channel is { } ch)
        {
            var session = new TradeSession(ch, App.Cfg, isHost: true);
            App.Trade = new TradeController(App, session);
            _host = null;
            App.PopToRoot();
            App.ShowToast("Connected. Pick a Pokémon to offer with A.");
        }
    }

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.TouchUp)
        {
            foreach (var (r, a) in _hit)
            {
                if (r.Contains(e.X, e.Y))
                    a();
            }
            return;
        }
        if (e.Pressed(Btn.B)) App.Pop();
        else if (e.Pressed(Btn.X)) NewCode();
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
    }

    public override bool Animating => true;

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Trade room open", $"ID {App.Identity.Id}");
        var p = c.P;
        if (_host is null)
            return;
        c.Text("Code", 320, 62, 18, p.Muted, Align.Center);
        c.Text(ShareCode.Pretty(_host.Code), 320, 86, 72, p.Ink, Align.Center, bold: true);
        var ips = Network.LocalAddresses();
        c.Text("Address", 320, 196, 18, p.Muted, Align.Center);
        if (ips.Count == 0)
        {
            c.Text("No network: connect to Wi-Fi first", 320, 222, 24, p.Bad, Align.Center);
        }
        else
        {
            var port = _host.Port == 47900 ? "" : $":{_host.Port}";
            for (int i = 0; i < Math.Min(2, ips.Count); i++)
                c.Text(ips[i] + port, 320, 222 + i * 44, i == 0 ? 40 : 24, i == 0 ? p.Ink : p.Muted, Align.Center, bold: i == 0);
        }
        c.Text(App.Cfg.EffectiveTrainerName, 320, 330, 18, p.Muted, Align.Center);
        var status = _host.Status;
        c.Paragraph(status, 60, 380, 520, 17, _host.Failed ? p.Bad : p.Ink, 3);
        if (!_host.Failed)
        {
            int dots = (int)(App.Now / 400 % 4);
            for (int i = 0; i < 3; i++)
                c.Fill(296 + i * 18, 450, 10, 10, i < dots ? p.Edge : p.Rule);
        }
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        c.Paragraph("On the other handheld: Trade > Join a trade room. This room shows up in its list; pick it and type the code. " +
                    "Not in the list (another network, a VPN)? Type the address instead.", 32, 32, 576, 18, p.Ink);
        c.Paragraph("Only someone with the code can connect, and the code can't be worked out from the network traffic. " +
                    $"An address that tries {TradeHost.MaxWrongPerAddress} wrong codes is shut out, and after {TradeHost.MaxWrongPerCode} " +
                    $"wrong codes in all the code changes by itself. Your handheld's ID, which your partner sees next to your name: " +
                    $"{App.Identity.Id}.", 32, 170, 576, 15, p.Muted);
        _hit.Add((c.Button("New code", 32, 330, 270, 56, false), NewCode));
        _hit.Add((c.Button("Close the room", 338, 330, 270, 56, false), App.Pop));
        float x = 8;
        x += c.Hint("X", "New code", x, 446);
        c.Hint("B", "Close", x, 446);
    }
}

/// <summary>
/// The lobbies and rooms open on this network. A lobby shows what its host trades away and what it wants for it; an
/// open lobby is joined with one tap, the rest need their host's code.
/// </summary>
public sealed class TradeJoinScreen(App app) : Screen(app)
{
    private const int Visible = 5;
    private const float RowH = 64, ListY = 50;
    private readonly RoomFinder _finder = new();
    private bool _started;
    private int _sel, _scroll;
    private readonly List<(RectF R, int Index)> _hit = [];
    private RectF _back;
    private string? _error;
    private HashSet<ushort> _owned = [];

    public override void Enter()
    {
        if (_started)
            return;
        _started = true;
        _finder.Start(App.Cfg.TradePort);
        _owned = OwnedSpecies(App);
    }

    /// <summary>The species in the bank and the open game's boxes: a lobby that wants one of them is marked.</summary>
    internal static HashSet<ushort> OwnedSpecies(App app)
    {
        var set = new HashSet<ushort>(app.Bank.All().Where(x => !x.Pk.IsEgg).Select(x => x.Pk.Species));
        if (app.Mover.Save is { } save)
        {
            for (int b = 0; b < save.BoxCount; b++)
            {
                for (int i = 0; i < save.SlotsPerBox; i++)
                {
                    if (save.Get(b, i) is { IsEgg: false } pk)
                        set.Add(pk.Species);
                }
            }
        }
        return set;
    }

    // lobbies first (open ones first), then private rooms
    private List<FoundRoom> Rooms => _finder.Rooms
        .OrderBy(r => r.Lobby is null ? 2 : r.Lobby.Open ? 0 : 1).ThenBy(r => r.Name, StringComparer.OrdinalIgnoreCase).ToList();

    private void Choose(int i)
    {
        var rooms = Rooms;
        if (i < rooms.Count)
        {
            var r = rooms[i];
            void Go()
            {
                if (r.Lobby is { Open: true, Code: { } code } open)
                    Connect(r.Address, r.Port, code, open, r.Key, r.Name);
                else
                    AskCode(r.Address, r.Port, r.Name, r.Lobby, r.Key);
            }
            var warning = r.Imitated
                ? $"Two handhelds on this network announce {r.Name} with the same ID: one of them is copying the other. Joining checks which one is real, so a copy can't get in your way; go ahead only if you expected this room."
                : App.Trainers.TrustOf(r.Key, r.Name) == Trust.Impostor
                    ? $"You traded with a handheld called {r.Name} before, and this isn't it (its ID is {r.Id}). It may be someone using the same name."
                    : null;
            if (warning is null)
                Go();
            else
                App.Confirm("Careful", warning, "Join anyway", Go);
        }
        else
        {
            App.Push(new KeyboardScreen(App, "Host address", KeyboardScreen.Kind.Address, App.Cfg.LastTradeAddress, 40, addr =>
            {
                var (host, port) = ParseAddress(addr, App.Cfg.TradePort);
                App.Cfg.LastTradeAddress = addr;
                App.TrySaveConfig();
                AskCode(host, port, host, null, null);
            }));
        }
    }

    private static (string Host, int Port) ParseAddress(string addr, int defaultPort)
    {
        var a = addr.Trim();
        int colon = a.LastIndexOf(':');
        if (colon > 0 && a.IndexOf(':') == colon && int.TryParse(a[(colon + 1)..], out var port) && port is > 0 and < 65536)
            return (a[..colon], port);
        return (a, defaultPort);
    }

    private void AskCode(string host, int port, string name, LobbyListing? lobby, byte[]? key) =>
        App.Push(new KeyboardScreen(App, $"Code for {name}", KeyboardScreen.Kind.Code, "", ShareCode.Length,
            code => Connect(host, port, code, lobby, key, name)));

    /// <summary>Connects; <paramref name="key"/> is the ID the room was announced with, which the host must prove it
    /// holds (null for a typed address: then whatever answers there is shown with its own ID).</summary>
    private void Connect(string host, int port, string code, LobbyListing? lobby, byte[]? key, string name)
    {
        _error = null;
        App.Run($"Connecting to {name}...",
            () => TradeClient.ConnectAsync(host, port, code, App.Identity, App.Cfg.EffectiveTrainerName, key).GetAwaiter().GetResult(),
            ch => App.Push(new AdmissionScreen(App, ch, name, lobby, () => _finder.Dispose())),
            ex =>
        {
            _error = ex switch
            {
                ImpostorException imp => imp.Message,
                WrongCodeException => "Wrong code. Check it on the host's screen and try again.",
                TradeRefusedException r => r.Message,
                IOException io => io.Message,
                OperationCanceledException => "The host didn't answer in time.",
                _ => ex.Message,
            };
            App.Message("Couldn't join", _error);
        });
    }

    public override void Handle(InputEvent e)
    {
        int n = Rooms.Count + 1;
        if (e.Kind == InputKind.TouchUp)
        {
            if (_back.Contains(e.X, e.Y))
            {
                Close();
                return;
            }
            foreach (var (r, i) in _hit)
            {
                if (r.Contains(e.X, e.Y))
                {
                    _sel = i;
                    Choose(i);
                    return;
                }
            }
            return;
        }
        if (e.Is(Btn.Up)) _sel = (_sel - 1 + n) % n;
        else if (e.Is(Btn.Down)) _sel = (_sel + 1) % n;
        else if (e.Is(Btn.L)) _sel = Math.Max(0, _sel - Visible);
        else if (e.Is(Btn.R)) _sel = Math.Min(n - 1, _sel + Visible);
        else if (e.Pressed(Btn.A)) Choose(Math.Min(_sel, n - 1));
        else if (e.Pressed(Btn.B)) Close();
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
    }

    private void Close()
    {
        _finder.Dispose();
        App.Pop();
    }

    public override bool Animating => true;

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Join a lobby or room");
        var p = c.P;
        var rooms = Rooms;
        if (_sel < rooms.Count && rooms[_sel] is { Lobby: { } l } room)
        {
            // the selected lobby, large: what's offered, what's wanted
            c.Text($"{room.Name}'s lobby", 320, 54, 20, p.Ink, Align.Center, bold: true, maxW: 600);
            c.Panel(32, 86, 250, 210, false);
            c.Mon(l.Species, l.Form, l.Gender, l.Shiny, false, 40, 92, 234, 150, 2);
            c.Text($"{l.OfferName}{(l.Shiny ? " ★" : "")}", 157, 246, 19, p.Ink, Align.Center, bold: true, maxW: 236);
            c.Text($"Lv {l.Level} · Gen {l.Format}", 157, 270, 15, p.Muted, Align.Center);
            c.Icon(Icons.Right, 300, 178, 4, p.Edge);
            c.Panel(358, 86, 250, 210, false);
            if (l.Want == 0)
                c.Text("Any Pokémon", 483, 170, 22, p.Muted, Align.Center, bold: true);
            else
                c.Mon(l.Want, 0, 0, false, false, 366, 92, 234, 150, 2);
            c.Text(l.Want == 0 ? "open to offers" : l.WantName, 483, 246, 19, p.Ink, Align.Center, bold: true, maxW: 236);
            c.Text(l.Want != 0 && _owned.Contains(l.Want) ? "You have one" : l.Want == 0 ? "" : "You have none", 483, 270, 15,
                l.Want != 0 && _owned.Contains(l.Want) ? p.Good : p.Muted, Align.Center);
            c.Paragraph(l.Open
                    ? "Open lobby: A asks to join, and its host decides. The listing is signed by the host's handheld, which must prove it's the one when you connect; its real offer is checked against it, and nothing is traded until you both accept."
                    : "This lobby needs its host's code to join.",
                32, 316, 576, 15, p.Muted);
            c.Text($"{room.Address}{(room.Port == App.Cfg.TradePort ? "" : $":{room.Port}")} · ID {room.Id} · {App.Trainers.Describe(room.Key, room.Name)}",
                32, 420, 15, room.Imitated || App.Trainers.TrustOf(room.Key, room.Name) == Trust.Impostor ? p.Bad : p.Muted, maxW: 576);
            return;
        }
        c.Paragraph("Lobbies list a Pokémon their host trades away and what they want for it: open ones are joined with one tap. " +
                    "Private rooms need the code their host's screen shows. Can't see one? Both handhelds must be on the same Wi-Fi, " +
                    "or type the host's address.", 32, 70, 576, 17, p.Muted);
        if (_finder.Error is { } err)
            c.Paragraph(err, 32, 230, 576, 16, p.Bad);
        var ips = Network.LocalAddresses();
        c.Text("This handheld: " + (ips.Count > 0 ? ips[0] : "no network"), 32, 420, 16, ips.Count > 0 ? p.Muted : p.Bad);
    }

    /// <summary>"Jorre · ID 4F2A-9C1B · Known · 3 trades", red when it's a copy or not who it claims to be.</summary>
    private void TrustLine(Canvas c, FoundRoom r, float x, float y, float maxW, string prefix = "")
    {
        var p = c.P;
        var trust = App.Trainers.TrustOf(r.Key, r.Name);
        string label = r.Imitated ? "copied ID!" : trust switch
        {
            Trust.Known => App.Trainers.Describe(r.Key, r.Name),
            Trust.Impostor => "not who you traded with!",
            _ => "new",
        };
        var col = r.Imitated || trust == Trust.Impostor ? p.Bad : trust == Trust.Known ? p.Good : p.Muted;
        c.Text($"{prefix}{r.Name} · ID {r.Id} · {label}", x, y, 13, col, maxW: maxW);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        var rooms = Rooms;
        int lobbies = rooms.Count(r => r.Lobby is not null);
        c.Fill(0, 0, Canvas.W, 44, p.Band);
        c.Text(rooms.Count == 0 ? "Looking for lobbies and rooms..." : $"{lobbies} lobb{(lobbies == 1 ? "y" : "ies")}, {rooms.Count - lobbies} room{(rooms.Count - lobbies == 1 ? "" : "s")}",
            16, 11, 19, p.Ink, bold: true);
        int n = rooms.Count + 1;
        _sel = Math.Clamp(_sel, 0, n - 1);
        if (_sel < _scroll) _scroll = _sel;
        if (_sel >= _scroll + Visible) _scroll = _sel - Visible + 1;
        _scroll = Math.Clamp(_scroll, 0, Math.Max(0, n - Visible));
        for (int k = 0; k < Visible && _scroll + k < n; k++)
        {
            int i = _scroll + k;
            float y = ListY + k * RowH;
            bool sel = i == _sel;
            c.Box(8, y, 610, RowH - 6, sel ? p.Band : p.Body, sel ? p.Edge : p.Rule, 2);
            if (i < rooms.Count && rooms[i].Lobby is { } l)
            {
                // [offer] Name Lv → [want] Name   host · open/code · you have one
                c.Mon(l.Species, l.Form, l.Gender, l.Shiny, false, 12, y + 3, 52, 52, 1);
                if (l.Shiny)
                    c.Icon(Icons.Star, 14, y + 6, 1, Color.Hex(0xffc93c));
                c.Text($"{l.OfferName} Lv {l.Level}", 68, y + 7, 17, p.Ink, bold: true, maxW: 190);
                c.Icon(Icons.Right, 268, y + 13, 2, p.Muted);
                if (l.Want != 0)
                    c.Mon(l.Want, 0, 0, false, false, 282, y + 3, 52, 52, 1);
                c.Text(l.Want == 0 ? "Any Pokémon" : l.WantName, l.Want == 0 ? 286 : 338, y + 7, 17, p.Ink, bold: true, maxW: 150);
                TrustLine(c, rooms[i], 68, y + 33, 200);
                float tx = 604;
                tx -= c.Measure(l.Open ? "Open" : "Code", 12, true) + 10;
                c.Tag(l.Open ? "Open" : "Code", tx, y + 8, l.Open ? p.Good : p.Muted, 12);
                if (l.Want != 0 && _owned.Contains(l.Want))
                {
                    tx -= c.Measure("You have one", 12, true) + 18;
                    c.Tag("You have one", tx, y + 32, p.Accent, 12);
                }
            }
            else if (i < rooms.Count)
            {
                c.Icon(Icons.Lock, 26, y + 20, 3, p.Muted);
                c.Text(rooms[i].Name, 68, y + 8, 18, p.Ink, bold: true, maxW: 380);
                TrustLine(c, rooms[i], 68, y + 33, 520, "Private room · ");
            }
            else
            {
                c.Text("Type an address...", 24, y + 18, 19, p.Ink);
            }
            _hit.Add((new RectF(8, y, 610, RowH - 6), i));
        }
        if (n > Visible)
        {
            float h = Visible * RowH - 6, th = h * Visible / n, ty = ListY + (h - th) * _scroll / Math.Max(1, n - Visible);
            c.Fill(624, ListY, 8, h, p.Chip);
            c.Fill(624, ty, 8, th, p.Line);
        }
        _back = c.Button("Back", 8, 384, 160, 44, false);
        float x = 8;
        x += c.Hint("A", "Join", x, 446);
        x += c.Hint("L/R", "Page", x, 446);
        c.Hint("B", "Back", x, 446);
    }
}

/// <summary>A finished trade: what arrived (and what it evolved into), what left.</summary>
public sealed class TradeDoneScreen(App app, TradeResult result) : Screen(app)
{
    private const uint EvolveAt = 1400, FlashFor = 500;
    private readonly uint _start = app.Now;

    private uint Age => App.Now - _start;

    public override void Handle(InputEvent e)
    {
        if (Age < 600)
            return;
        if (e.Pressed(Btn.A) || e.Pressed(Btn.B) || e.Kind == InputKind.TouchUp)
            App.Pop();
    }

    public override bool Animating => Age < EvolveAt + FlashFor + 1000;

    public override void DrawTop(Canvas c)
    {
        var p = c.P;
        Views.Header(c, App, "Trade complete");
        float t = Math.Min(1, Age / 700f);
        bool evolves = result.EvolvedInto is not null;
        bool after = !evolves || Age >= EvolveAt + FlashFor / 2;
        var shown = after ? result.Got : result.Arrived;
        c.Text("You received", 320, 56, 18, p.Muted, Align.Center);
        c.Mon(shown.Species, shown.Form, shown.Gender, shown.IsShiny, shown.IsEgg, 170, 80 + (1 - t) * 40, 300, 200, 3, (byte)(255 * t));
        if (evolves && Age >= EvolveAt && Age < EvolveAt + FlashFor)
        {
            // the evolution's white flash
            float k = 1 - MathF.Abs((Age - EvolveAt) / (float)FlashFor * 2 - 1);
            c.Fill(0, 42, Canvas.W, 250, new Color(255, 255, 255, (byte)(230 * k)));
        }
        c.Text(shown.Title, 320, 290, 32, p.Ink, Align.Center, bold: true);
        if (evolves && after)
            c.Text($"{result.Arrived.Title} evolved into {result.EvolvedInto}!", 320, 334, 22, p.Good, Align.Center, bold: true);
        else if (evolves)
            c.Text($"What? {result.Arrived.Title} is evolving!", 320, 334, 22, p.Ink, Align.Center);
        c.Text($"It's in the bank: {result.StoredWhere}", 320, 376, 17, p.Muted, Align.Center);
        if (result.Problem is not null)
            c.Paragraph(result.Problem, 40, 408, 560, 15, p.Bad, 3);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        var gave = result.Gave;
        c.Text("You sent", 320, 40, 18, p.Muted, Align.Center);
        c.Mon(gave.Species, gave.Form, gave.Gender, gave.IsShiny, gave.IsEgg, 220, 70, 200, 150, 2);
        c.Text(gave.Title, 320, 232, 24, p.Ink, Align.Center, bold: true);
        c.Button("OK", 220, 330, 200, 56, true);
        c.Hint("A", "OK", 8, 446);
    }
}
