using System.Globalization;
using System.Text;
using PKHeX.Core;
using Rocknixds.Bank.Trade;

namespace Rocknixds.Bank.App;

/// <summary>Opening a lobby: choose the Pokémon to list, the species wanted for it, who may join.</summary>
public static class LobbyFlow
{
    public static void Start(App app)
    {
        app.PopToRoot();
        var box = app.Find<BoxScreen>();
        if (box is null)
            return;
        box.StartPick("Choose the Pokémon your lobby trades away (A). B cancels.", (slot, pk) =>
        {
            var title = MonSummary.From(pk).Title;
            app.Push(new SpeciesPickerScreen(app, $"What do you want for {title}?", want =>
            {
                app.Menu("Who can join your lobby?",
                [
                    new("Anyone on this network (open)", () => Open(app, pk, slot, want, true)),
                    new("Only with a code", () => Open(app, pk, slot, want, false)),
                    new("Cancel", () => { }),
                ], cancelIndex: 2);
            }));
        });
    }

    private static void Open(App app, PKM pk, Slot slot, ushort want, bool open) =>
        app.Push(new LobbyHostScreen(app, new HostedLobby(pk.Clone(), slot, slot.Area == Area.Save ? app.Mover.Save : null, want, open), null));
}

/// <summary>Chooses a species by typing the start of its name: the matches show as you type.</summary>
public sealed class SpeciesPickerScreen : Screen
{
    private const int Shown = 6;
    private static readonly string[] Keys = ["QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM. '"];

    private readonly string _title;
    private readonly Action<ushort> _done;
    private readonly List<(ushort Id, string Name, string Key)> _all;
    private string _text = "";
    private List<ushort> _matches = [];
    private int _row = 1, _col, _match;
    private readonly List<(RectF R, Action A)> _hit = [];

    public SpeciesPickerScreen(App app, string title, Action<ushort> done) : base(app)
    {
        _title = title;
        _done = done;
        var names = GameInfo.Strings.Species;
        _all = Enumerable.Range(1, Math.Min(names.Count - 1, LobbyListing.MaxSpecies))
            .Select(i => ((ushort)i, names[i], Fold(names[i])))
            .Where(x => x.Item2.Length > 0)
            .ToList();
        Search();
    }

    /// <summary>Upper case without accents: "flabebe" finds Flabébé.</summary>
    private static string Fold(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (var ch in s.Normalize(NormalizationForm.FormD))
        {
            if (CharUnicodeInfo.GetUnicodeCategory(ch) != UnicodeCategory.NonSpacingMark)
                sb.Append(char.ToUpperInvariant(ch));
        }
        return sb.ToString();
    }

    private void Search()
    {
        var q = Fold(_text.Trim());
        _matches = q.Length == 0
            ? []
            : _all.Where(x => x.Key.StartsWith(q, StringComparison.Ordinal))
                .Concat(_all.Where(x => !x.Key.StartsWith(q, StringComparison.Ordinal) && x.Key.Contains(q, StringComparison.Ordinal)))
                .Take(Shown).Select(x => x.Id).ToList();
        _match = Math.Clamp(_match, 0, Math.Max(0, _matches.Count - 1));
    }

    private void Type(string s)
    {
        if (_text.Length + s.Length > 16)
            return;
        _text += s;
        _match = 0;
        Search();
    }

    private void Pick(ushort species)
    {
        App.Pop();
        _done(species);
    }

    private void PickSelected()
    {
        if (_matches.Count > 0)
            Pick(_matches[_match]);
        else
            App.ShowToast("Type the start of a name, or choose Any Pokémon.", true);
    }

    // rows: 0 the matches, 1-3 letters, 4 [Del] [Any Pokémon] [OK]
    private int RowLength(int r) => r == 0 ? Math.Max(1, _matches.Count) : r == 4 ? 3 : Keys[r - 1].Length;

    private void Press(int r, int c)
    {
        if (r == 0)
        {
            if (c < _matches.Count)
                Pick(_matches[c]);
            return;
        }
        if (r == 4)
        {
            switch (c)
            {
                case 0:
                    if (_text.Length > 0)
                    {
                        _text = _text[..^1];
                        Search();
                    }
                    break;
                case 1:
                    Pick(0);
                    break;
                default:
                    PickSelected();
                    break;
            }
            return;
        }
        Type(Keys[r - 1][c].ToString());
    }

    public override void Enter() => Sdl.StartTextInput();
    public override void Leave() => Sdl.StopTextInput();

    public override void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.Text)
        {
            Type(e.Text!.ToUpperInvariant());
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
        if (e.Is(Btn.Up)) _row = (_row + 4) % 5;
        else if (e.Is(Btn.Down)) _row = (_row + 1) % 5;
        else if (e.Is(Btn.Left)) _col--;
        else if (e.Is(Btn.Right)) _col++;
        else if (e.Is(Btn.L)) _match = Math.Max(0, _match - 1);
        else if (e.Is(Btn.R)) _match = Math.Min(Math.Max(0, _matches.Count - 1), _match + 1);
        else if (e.Pressed(Btn.A)) Press(_row, Math.Clamp(_col, 0, RowLength(_row) - 1));
        else if (e.Pressed(Btn.Start)) PickSelected();
        else if (e.Pressed(Btn.Y)) Pick(0);
        else if (e.Is(Btn.B) || e.Is(Btn.Select))
        {
            if (_text.Length > 0)
            {
                _text = _text[..^1];
                Search();
            }
            else if (e.Pressed(Btn.B))
            {
                App.Pop();
            }
        }
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
        int len = RowLength(_row);
        _col = (_col % len + len) % len;
        if (_row == 0)
            _match = Math.Min(_col, Math.Max(0, _matches.Count - 1));
    }

    public override bool Animating => true;

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, _title);
        var p = c.P;
        c.Box(40, 56, 560, 64, p.Chip, p.Edge, 2);
        float w = c.Text(_text, 60, 70, 32, p.Ink, bold: true, maxW: 500);
        if ((App.Now / 450) % 2 == 0)
            c.Fill(64 + w, 70, 4, 36, p.Edge);
        if (_matches.Count == 0)
        {
            c.Paragraph(_text.Length == 0 ? "Type the start of the species' name. Y: any Pokémon (open to offers)." : "No species by that name.",
                60, 160, 520, 18, p.Muted);
            return;
        }
        var sel = _matches[_match];
        c.Mon(sel, 0, 0, false, false, 190, 132, 260, 210, 3);
        c.Text(Names.Species(sel), 320, 350, 30, p.Ink, Align.Center, bold: true);
        c.Text($"#{sel:000}", 320, 390, 16, p.Muted, Align.Center);
        c.Text("START or A on it: choose this one    L/R: another match", 320, 440, 14, p.Muted, Align.Center);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        // the matches
        float mw = 100, mx = 8;
        for (int i = 0; i < Shown; i++)
        {
            float x = mx + i * (mw + 4.8f);
            bool sel = i < _matches.Count && i == _match;
            bool cursor = _row == 0 && i == _col;
            c.Box(x, 6, mw, 78, sel ? p.Band : p.Body, cursor ? p.Edge : sel ? p.Line : p.Rule, 2);
            if (i < _matches.Count)
            {
                var id = _matches[i];
                c.Mon(id, 0, 0, false, false, x + 4, 8, mw - 8, 50, 1);
                c.Text(Names.Species(id), x + mw / 2, 60, 12, p.Ink, Align.Center, maxW: mw - 6);
                int idx = i;
                _hit.Add((new RectF(x, 6, mw, 78), () => Pick(_matches[idx])));
            }
        }
        // the letters
        float top = 94, h = 66, gap = 6;
        for (int r = 0; r < 3; r++)
        {
            var keys = Keys[r];
            float kw = (624 - (keys.Length - 1) * gap) / keys.Length;
            for (int k = 0; k < keys.Length; k++)
            {
                float x = 8 + k * (kw + gap), y = top + r * (h + gap);
                bool cur = _row == r + 1 && _col == k;
                c.Box(x, y, kw, h, cur ? p.Band : p.KeyBg, cur ? p.Edge : p.Rule, 2);
                var label = keys[k] == ' ' ? "Space" : keys[k].ToString();
                c.Text(label, x + kw / 2, y + h / 2 - 12, label.Length > 1 ? 13 : 24, p.KeyInk, Align.Center, bold: true);
                int rr = r + 1, kk = k;
                _hit.Add((new RectF(x, y, kw, h), () => Press(rr, kk)));
            }
        }
        string[] last = ["Del", "Any Pokémon", "OK"];
        float[] widths = [150, 300, 162];
        float lx = 8, ly = top + 3 * (h + gap);
        for (int k = 0; k < 3; k++)
        {
            bool cur = _row == 4 && _col == k;
            c.Box(lx, ly, widths[k], 56, cur ? p.Band : p.KeyBg, cur ? p.Edge : p.Rule, 2);
            c.Text(last[k], lx + widths[k] / 2, ly + 16, 20, k == 2 ? p.Good : p.KeyInk, Align.Center, bold: true);
            int kk = k;
            _hit.Add((new RectF(lx, ly, widths[k], 56), () => Press(4, kk)));
            lx += widths[k] + 6;
        }
        float hx = 8;
        hx += c.Hint("A", "Type", hx, 452);
        hx += c.Hint("B", "Delete", hx, 452);
        hx += c.Hint("Y", "Any", hx, 452);
        c.Hint("START", "Choose", hx, 452);
    }
}

/// <summary>
/// A lobby this handheld hosts: listed on the network with its Pokémon and what it wants for it. When someone joins,
/// the trade starts with the Pokémon already offered; when they leave, the lobby opens again, until its Pokémon has
/// been traded away.
/// </summary>
public sealed class LobbyHostScreen(App app, HostedLobby lobby, string? lastVisit) : Screen(app)
{
    private TradeHost? _host;
    private readonly List<(RectF R, Action A)> _hit = [];

    public override void Enter()
    {
        if (_host is not null)
            return;
        if (!lobby.StillThere(App))
        {
            App.Pop();
            App.Message("Lobby closed", $"{MonSummary.From(lobby.Pk).Title} isn't where it was any more, so the lobby closed.");
            return;
        }
        Start();
    }

    private void Start()
    {
        _host?.Dispose();
        _host = new TradeHost(App.Cfg, App.Identity, code => lobby.Listing(code));
        _host.Start();
    }

    public override void Leave()
    {
        if (_host?.Channel is null)
            _host?.Dispose();
    }

    public override void Update()
    {
        if (_host?.Channel is { } ch)
        {
            var session = new TradeSession(ch, App.Cfg, isHost: true);
            App.Trade = new TradeController(App, session) { HostLobby = lobby };
            _host = null;
            App.PopToRoot();
            App.ShowToast($"Someone joined your lobby: {MonSummary.From(lobby.Pk).Title} is on offer.");
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
        if (_host?.Pending is { } req)
        {
            // someone asks to join: A lets them in, B refuses
            if (e.Pressed(Btn.A)) _ = req.LetIn();
            else if (e.Pressed(Btn.B)) _ = req.Refuse();
            return;
        }
        if (e.Pressed(Btn.B)) App.Pop();
        else if (e.Pressed(Btn.X) && !lobby.Open) _host?.NewCode();
        else if (e.Kind == InputKind.Quit) App.QuitRequested = true;
    }

    public override bool Animating => true;

    public override void DrawTop(Canvas c)
    {
        if (_host?.Pending is { } req)
        {
            DrawRequest(c, req);
            return;
        }
        Views.Header(c, App, lobby.Open ? "Your lobby is open" : "Your lobby (with a code)", $"ID {App.Identity.Id}");
        var p = c.P;
        var s = MonSummary.From(lobby.Pk);
        c.Panel(32, 56, 250, 200, false);
        c.Mon(lobby.Pk, 40, 62, 234, 140, 2);
        c.Text(s.Title, 157, 206, 19, p.Ink, Align.Center, bold: true, maxW: 236);
        c.Text($"Lv {s.Level} · {s.Format}", 157, 230, 15, p.Muted, Align.Center);
        c.Icon(Icons.Right, 300, 146, 4, p.Edge);
        c.Panel(358, 56, 250, 200, false);
        if (lobby.Want == 0)
            c.Text("Any Pokémon", 483, 130, 22, p.Muted, Align.Center, bold: true);
        else
            c.Mon(lobby.Want, 0, 0, false, false, 366, 62, 234, 140, 2);
        c.Text(lobby.Want == 0 ? "open to offers" : Names.Species(lobby.Want), 483, 206, 19, p.Ink, Align.Center, bold: true);
        c.Text("wanted", 483, 230, 15, p.Muted, Align.Center);

        if (_host is null)
            return;
        if (lobby.Open)
        {
            c.Text("Anyone on this network can join from their lobby list.", 320, 276, 17, p.Ink, Align.Center);
        }
        else
        {
            c.Text("Code", 200, 272, 16, p.Muted, Align.Center);
            c.Text(ShareCode.Pretty(_host.Code), 200, 292, 40, p.Ink, Align.Center, bold: true);
        }
        var ips = Network.LocalAddresses();
        var port = _host.Port == 47900 ? "" : $":{_host.Port}";
        c.Text(ips.Count > 0 ? ips[0] + port : "No network: connect to Wi-Fi first", lobby.Open ? 320 : 460, lobby.Open ? 306 : 300, 22,
            ips.Count > 0 ? p.Ink : p.Bad, Align.Center, bold: true);
        c.Paragraph(_host.Status, 40, 352, 560, 16, _host.Failed ? p.Bad : p.Muted, 2);
        if (lastVisit is not null)
            c.Paragraph("Last visit: " + lastVisit, 40, 400, 560, 14, p.Muted, 2);
        if (!_host.Failed)
        {
            int dots = (int)(App.Now / 400 % 4);
            for (int i = 0; i < 3; i++)
                c.Fill(296 + i * 18, 452, 10, 10, i < dots ? p.Edge : p.Rule);
        }
    }

    private void DrawRequest(Canvas c, JoinRequest req)
    {
        Views.Header(c, App, $"{req.Name} wants to join", $"ID {req.Id}");
        var p = c.P;
        var trust = App.Trainers.TrustOf(req.Key, req.Name);
        c.Text(req.Name, 320, 60, 30, p.Ink, Align.Center, bold: true, maxW: 600);
        c.Text($"{App.Trainers.Describe(req.Key, req.Name)} · {req.Address}", 320, 102, 17,
            trust switch { Trust.Known => p.Good, Trust.Impostor => p.Bad, _ => p.Muted }, Align.Center, maxW: 600);
        c.Text("Check number", 320, 150, 18, p.Muted, Align.Center);
        c.Text(req.CheckNumber, 320, 176, 80, p.Ink, Align.Center, bold: true);
        c.Paragraph($"{req.Name}'s screen shows a check number too. If you're together, compare them: the same number means " +
                    "you're connected to each other with nobody in between. Let in only someone you want to trade with.",
            60, 290, 520, 17, p.Muted);
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _hit.Clear();
        if (_host?.Pending is { } req)
        {
            c.Text($"Let {req.Name} into your lobby?", 320, 60, 22, p.Ink, Align.Center, bold: true, maxW: 600);
            _hit.Add((c.Button("A  Let in", 40, 200, 270, 64, true, p.Good), () => _ = req.LetIn()));
            _hit.Add((c.Button("B  Refuse", 330, 200, 270, 64, false), () => _ = req.Refuse()));
            c.Paragraph($"Refused handhelds can't ask again in this lobby. Unanswered requests are refused after {TradeHost.RequestTimeout.TotalSeconds:0} seconds.",
                40, 300, 560, 15, p.Muted);
            return;
        }
        c.Paragraph($"Your lobby is in the list of every handheld on this network that opens Trade > Join. When someone joins, " +
                    $"{MonSummary.From(lobby.Pk).Title} is offered to them straight away; you still see their offer, checked, and " +
                    "nothing is traded until you both accept.", 32, 28, 576, 17, p.Ink);
        c.Paragraph(lobby.Open
                ? "Open lobby: no code to type. Whoever wants to join asks first: you see their name and their handheld's ID, and decide."
                : $"Joining needs the code. An address that tries {TradeHost.MaxWrongPerAddress} wrong codes is shut out; after " +
                  $"{TradeHost.MaxWrongPerCode} in all, the code changes by itself.",
            32, 196, 576, 15, p.Muted);
        if (!lobby.Open)
            _hit.Add((c.Button("New code", 32, 330, 270, 56, false), () => _host?.NewCode()));
        _hit.Add((c.Button("Close the lobby", lobby.Open ? 185 : 338, 330, 270, 56, false), App.Pop));
        float x = 8;
        if (!lobby.Open)
            x += c.Hint("X", "New code", x, 446);
        c.Hint("B", "Close", x, 446);
    }
}

/// <summary>
/// Connected and proven: waiting for the host to let us in. A room with a code lets in at once; an open lobby's host
/// sees who's asking, and the same check number as this screen.
/// </summary>
public sealed class AdmissionScreen(App app, SecureChannel channel, string hostName, LobbyListing? lobby, Action admitted) : Screen(app)
{
    private readonly CancellationTokenSource _cts = new();
    private Task? _wait;
    private bool _done;

    public override void Enter() => _wait ??= TradeClient.WaitForAdmissionAsync(channel, _cts.Token);

    public override void Update()
    {
        if (_done || _wait is not { IsCompleted: true } w)
            return;
        _done = true;
        if (w.IsCompletedSuccessfully)
        {
            admitted();
            var session = new TradeSession(channel, App.Cfg, isHost: false);
            App.Trade = new TradeController(App, session) { JoinedLobby = lobby };
            App.PopToRoot();
            App.ShowToast(lobby is { Want: > 0 }
                ? $"Connected. The lobby wants {lobby.WantName}: offer one with A (yours are framed in green)."
                : "Connected. Pick a Pokémon to offer with A.");
            return;
        }
        channel.Dispose();
        App.Pop();
        var why = w.Exception?.InnerException is TradeRefusedException r ? r.Message : "The host closed the connection.";
        App.Message($"{hostName} didn't let you in", why);
    }

    private void Cancel()
    {
        if (_done)
            return;
        _done = true;
        _cts.Cancel();
        channel.Dispose();
        App.Pop();
    }

    public override void Handle(InputEvent e)
    {
        if (e.Pressed(Btn.B) || e.Kind == InputKind.TouchUp)
            Cancel();
        else if (e.Kind == InputKind.Quit)
            App.QuitRequested = true;
    }

    public override bool Animating => true;

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, $"Joining {hostName}", $"ID {Fingerprint.Short(channel.PeerKey)}");
        var p = c.P;
        c.Text($"Waiting for {hostName} to let you in...", 320, 70, 22, p.Ink, Align.Center, bold: true, maxW: 600);
        c.Text("Check number", 320, 140, 18, p.Muted, Align.Center);
        c.Text(channel.CheckNumber, 320, 166, 80, p.Ink, Align.Center, bold: true);
        c.Paragraph($"{hostName}'s screen shows the same number. If you're together, compare them: a different number means " +
                    "someone is in between, so go back.", 60, 290, 520, 17, p.Muted);
        c.Text(App.Trainers.Describe(channel.PeerKey, hostName), 320, 400, 16,
            App.Trainers.TrustOf(channel.PeerKey, hostName) == Trust.Impostor ? p.Bad : p.Muted, Align.Center);
        int dots = (int)(App.Now / 400 % 4);
        for (int i = 0; i < 3; i++)
            c.Fill(296 + i * 18, 450, 10, 10, i < dots ? p.Edge : p.Rule);
    }

    public override void DrawBottom(Canvas c)
    {
        c.Button("Cancel", 220, 200, 200, 56, true);
        c.Hint("B", "Cancel", 8, 446);
    }
}
