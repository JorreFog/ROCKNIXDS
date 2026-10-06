using PKHeX.Core;
using Rocknixds.Bank.Trade;

namespace Rocknixds.Bank.App;

/// <summary>
/// The main screen, laid out like Pokémon Bank: the open game's box on the left of the bottom screen, the bank's on
/// the right, the Pokémon under the cursor in full on the top screen. A picks a Pokémon up and puts it down (moving it,
/// converting it, checking it); touch does the same with a tap or a drag. In a trade, A offers the Pokémon instead.
/// </summary>
public sealed class BoxScreen(App app) : Screen(app)
{
    private sealed class Pane(Area area)
    {
        public Area Area { get; } = area;
        public int Box;
        public int BoxCount;
        public int SlotCount = 30;
        public string Name = "";
        public readonly PKM?[] Slots = new PKM?[30];
        public readonly string?[] Keys = new string?[30];
        public readonly bool[] Locked = new bool[30];
        public int Count => Slots.Count(s => s is not null);
    }

    private const float GridY = 48, Cell = 50, PaneW = 308, PaneY = 6, PaneH = 298;
    private static readonly float[] PaneX = [6, 326];

    private readonly Pane _game = new(Area.Save);
    private readonly Pane _bank = new(Area.Bank);
    private int _pane = 1, _col, _row;
    private (Slot From, PKM Pk)? _held;
    private bool _report;
    private int _reportScroll;
    private bool _dragging;
    private float _dragX, _dragY;
    private int _touchDownCell = -1, _touchDownPane = -1;
    private readonly List<(RectF R, Action A)> _touch = [];

    public int BankBox => _bank.Box;

    // choosing a Pokémon for something else (a lobby's listing): A picks, B cancels
    private (string Prompt, Action<Slot, PKM> Picked)? _pick;

    /// <summary>Lets the player choose a Pokémon from the boxes, then calls <paramref name="picked"/>.</summary>
    public void StartPick(string prompt, Action<Slot, PKM> picked)
    {
        _held = null;
        _report = false;
        _pick = (prompt, picked);
    }

    private Pane[] Panes => [_game, _bank];
    private Pane Cur => Panes[_pane];
    private bool Trading => App.Trade is not null;
    private TradeSession? Session => App.Trade?.Session;

    public override void Enter() => Refresh();

    public void Refresh()
    {
        Load(_bank);
        Load(_game);
        if (_held is { } h)
        {
            // what's held must still be where it was picked up (a trade or another screen may have changed that)
            var now = App.Mover.Read(h.From);
            if (now is null || PkmIO.Hash(now) != PkmIO.Hash(h.Pk))
                _held = null;
        }
    }

    private void Load(Pane pane)
    {
        if (pane.Area == Area.Bank)
        {
            pane.BoxCount = App.Bank.BoxCount;
            pane.SlotCount = BankStore.SlotsPerBox;
        }
        else
        {
            var save = App.Mover.Save;
            pane.BoxCount = save?.BoxCount ?? 0;
            pane.SlotCount = save?.SlotsPerBox ?? 30;
        }
        pane.Box = pane.BoxCount == 0 ? 0 : Math.Clamp(pane.Box, 0, pane.BoxCount - 1);
        pane.Name = pane.BoxCount == 0 ? "" : pane.Area == Area.Bank ? App.Bank.BoxName(pane.Box) : App.Mover.Save!.BoxName(pane.Box);
        for (int i = 0; i < 30; i++)
        {
            PKM? pk = null;
            bool locked = false;
            if (pane.BoxCount > 0 && i < pane.SlotCount)
            {
                try
                {
                    var slot = new Slot(pane.Area, pane.Box, i);
                    pk = App.Mover.Read(slot);
                    locked = App.Mover.IsLocked(slot);
                }
                catch (Exception ex)
                {
                    App.Log($"slot {i}: {ex.Message}");
                }
            }
            pane.Slots[i] = pk;
            pane.Keys[i] = pk is null ? null : pk.GetType().Name + PkmIO.Hash(pk);
            pane.Locked[i] = locked;
        }
    }

    private Slot SlotOf(Pane pane, int index) => new(pane.Area, pane.Box, index);

    private PKM? UnderCursor => _row >= 0 ? Cur.Slots[_row * 6 + _col] : null;

    // ---- input ----

    public override void Handle(InputEvent e)
    {
        if (e.Kind is InputKind.TouchDown or InputKind.TouchMove or InputKind.TouchUp)
        {
            Touch(e);
            return;
        }
        if (e.Kind == InputKind.Quit)
        {
            App.QuitRequested = true;
            return;
        }
        if (_report)
        {
            if (e.Is(Btn.Up)) _reportScroll = Math.Max(0, _reportScroll - 1);
            else if (e.Is(Btn.Down)) _reportScroll++;
            else if (e.Pressed(Btn.B) || e.Pressed(Btn.X)) _report = false;
            return;
        }
        if (e.Is(Btn.Left)) Move(-1, 0);
        else if (e.Is(Btn.Right)) Move(1, 0);
        else if (e.Is(Btn.Up)) Move(0, -1);
        else if (e.Is(Btn.Down)) Move(0, 1);
        else if (e.Is(Btn.L)) ChangeBox(Cur, -1);
        else if (e.Is(Btn.R)) ChangeBox(Cur, 1);
        else if (e.Pressed(Btn.A)) Activate(_pane, _row < 0 ? -1 : _row * 6 + _col);
        else if (e.Pressed(Btn.B)) Back();
        else if (e.Pressed(Btn.X)) ToggleReport();
        else if (e.Pressed(Btn.Y)) Options();
        else if (e.Pressed(Btn.Select)) SelectButton();
        else if (e.Pressed(Btn.Start)) StartButton();
        else if (e.Pressed(Btn.Menu)) MainMenu.Show(App);
    }

    private void Move(int dx, int dy)
    {
        if (dx != 0)
        {
            int col = _pane * 6 + _col + dx;
            if (_row < 0)
            {
                // on a box name: left/right turn the box
                ChangeBox(Cur, dx);
                return;
            }
            col = Math.Clamp(col, 0, 11);
            _pane = col / 6;
            _col = col % 6;
        }
        if (dy != 0)
            _row = Math.Clamp(_row + dy, -1, 4);
        _reportScroll = 0;
    }

    private void ChangeBox(Pane pane, int d)
    {
        if (pane.BoxCount == 0)
            return;
        pane.Box = (pane.Box + d + pane.BoxCount) % pane.BoxCount;
        Load(pane);
    }

    private void Back()
    {
        if (_pick is not null)
        {
            _pick = null;
            App.ShowToast("Cancelled.");
        }
        else if (_held is not null)
            _held = null;
        else if (Trading && Session!.Mine is not null && Session.CanChangeOffer)
            Session.Withdraw();
    }

    private void ToggleReport()
    {
        if (Trading && App.Trade!.Session.Theirs is not null && (_held is null && UnderCursor is null || App.Trade.ShowPartnerReport))
        {
            App.Trade.ShowPartnerReport = !App.Trade.ShowPartnerReport;
            _report = App.Trade.ShowPartnerReport;
            _reportScroll = 0;
            return;
        }
        if ((_held?.Pk ?? UnderCursor) is null)
            return;
        _report = !_report;
        _reportScroll = 0;
    }

    private void SelectButton()
    {
        if (Trading)
        {
            LeaveTrade();
            return;
        }
        App.Push(new SavePickerScreen(App));
    }

    private void StartButton()
    {
        if (!Trading)
        {
            MainMenu.Show(App);
            return;
        }
        var s = Session!;
        if (s.IAccepted)
        {
            s.Unaccept();
            return;
        }
        if (s.Mine is null || s.Theirs is null)
        {
            App.ShowToast(s.Mine is null ? "Offer a Pokémon first: pick it with A." : $"Waiting for {s.PartnerName} to offer something.");
            return;
        }
        if (s.AcceptWait > TimeSpan.Zero)
        {
            App.ShowToast($"{s.PartnerName} just changed their offer: check it, then accept.", true);
            return;
        }
        if (App.Trade?.JoinedLobby is { } joined && !joined.Advertises(s.Theirs.Pk))
        {
            // the host signed a listing for something else: a bait and switch, or a host that moved on; either way, say so
            App.Confirm("Not what the lobby listed",
                $"{s.PartnerName}'s lobby listed {joined.OfferName} Lv {joined.Level}{(joined.Shiny ? " (shiny)" : "")}, but offers {s.Theirs.Summary.Title} Lv {s.Theirs.Summary.Level}. Accept {s.Theirs.Summary.Title} anyway?",
                "Accept anyway", () => AcceptChecked(s));
            return;
        }
        AcceptChecked(s);
    }

    private void AcceptChecked(TradeSession s)
    {
        if (s.Theirs is null)
            return;
        var v = s.Theirs.Verdict;
        if (v is null)
        {
            App.ShowToast("Still checking their Pokémon...");
            return;
        }
        if (!v.Valid)
        {
            if (App.Cfg.BlockIllegalTrades)
            {
                App.Message("Not accepted", $"{s.Theirs.Summary.Title} fails the legality check ({v.Headline}). Trading for Pokémon that fail it is switched off in the settings.");
                return;
            }
            App.Confirm("Accept a Pokémon that isn't legal?", $"{s.Theirs.Summary.Title} fails the legality check: {v.Headline}", "Accept anyway", () => TryAccept(s));
            return;
        }
        TryAccept(s);
    }

    private void TryAccept(TradeSession s)
    {
        if (!s.Accept())
            App.ShowToast($"Not accepted: {s.PartnerName}'s offer changed. Check it and accept again.", true);
    }

    private void LeaveTrade() => App.Confirm("Leave the trade?", $"The connection to {Session!.PartnerName} closes. Nothing that wasn't traded yet changes hands.",
        "Leave", () => Session?.Leave("The partner left the trade."));

    private void Activate(int paneIndex, int index)
    {
        _pane = paneIndex;
        var pane = Panes[paneIndex];
        if (pane.Area == Area.Save && pane.BoxCount == 0)
        {
            if (!Trading)
                App.Push(new SavePickerScreen(App));
            return;
        }
        if (index < 0)
            return;
        _row = index / 6;
        _col = index % 6;
        if (index >= pane.SlotCount)
        {
            App.ShowToast("This game's boxes are smaller: that space doesn't exist.");
            return;
        }
        var slot = SlotOf(pane, index);
        var pk = pane.Slots[index];
        if (_pick is { } pick)
        {
            if (pk is null)
                return;
            if (pane.Locked[index])
            {
                App.ShowToast("The game has locked that slot.", true);
                return;
            }
            _pick = null;
            pick.Picked(slot, pk);
            return;
        }
        if (Trading)
        {
            Offer(slot, pk);
            return;
        }
        if (_held is null)
        {
            if (pk is null)
                return;
            if (pane.Locked[index])
            {
                App.ShowToast("The game has locked that slot (its battle box or a team in use).", true);
                return;
            }
            _held = (slot, pk);
            return;
        }
        var from = _held.Value.From;
        if (from == slot)
        {
            _held = null;
            return;
        }
        if (pane.Locked[index])
        {
            App.ShowToast("The game has locked that slot.", true);
            return;
        }
        DoMove(from, slot, false);
    }

    private void DoMove(Slot from, Slot to, bool confirmed)
    {
        App.Run("Moving...", () => App.Mover.Move(from, to, confirmed), r =>
        {
            if (r.Done)
            {
                _held = null;
                Refresh();
                App.ShowToast(r.Message);
            }
            else if (r.NeedsConfirm)
            {
                App.Confirm("This Pokémon isn't legal", string.Join("\n", r.Warnings) + "\n\nPut it in the game anyway?", "Move anyway",
                    () => DoMove(from, to, true));
            }
            else if (r.Blocked is not null)
            {
                App.Message("Can't go there", r.Blocked);
            }
        }, ex =>
        {
            Refresh();
            App.Message("Couldn't move it", ex.Message);
        });
    }

    private void Offer(Slot slot, PKM? pk)
    {
        var s = Session!;
        if (!s.CanChangeOffer)
            return;
        if (pk is null)
            return;
        if (slot.Area == Area.Save && App.Mover.Save!.IsLocked(slot.Box, slot.Index))
        {
            App.ShowToast("The game has locked that slot.", true);
            return;
        }
        s.Offer(pk, (slot, slot.Area == Area.Save ? App.Mover.Save : null));
        App.ShowToast(s.Theirs is null
            ? $"You offered {MonSummary.From(pk).Title}. START accepts once {s.PartnerName} offers too."
            : $"You offered {MonSummary.From(pk).Title}. Press START to accept the trade.");
    }

    private void Options()
    {
        var pk = _held?.Pk ?? UnderCursor;
        var choices = new List<DialogChoice>();
        if (Trading && UnderCursor is { } u)
            choices.Add(new("Offer this Pokémon", () => Offer(SlotOf(Cur, _row * 6 + _col), u), Session!.CanChangeOffer));
        if (pk is not null)
            choices.Add(new("Legality report", () => { _report = true; _reportScroll = 0; }));
        if (pk is not null && _held is null && Cur.Area == Area.Bank)
        {
            choices.Add(new("Export a copy (export folder)", () =>
            {
                try
                {
                    var path = App.Bank.Export(Cur.Box, _row * 6 + _col);
                    App.ShowToast($"Saved {Path.GetFileName(path)} in the export folder.");
                }
                catch (Exception ex)
                {
                    App.ShowToast(ex.Message, true);
                }
            }));
        }
        if (Cur.Area == Area.Bank)
        {
            choices.Add(new("Rename this bank box", () => App.Push(new KeyboardScreen(App, "Box name", KeyboardScreen.Kind.Text, _bank.Name, 16,
                name =>
                {
                    App.Bank.RenameBox(_bank.Box, name);
                    Load(_bank);
                }))));
        }
        if (Trading)
            choices.Add(new("Leave the trade", LeaveTrade));
        choices.Add(new("Close", () => { }));
        App.Menu(pk is null ? "Options" : MonSummary.From(pk).Title, choices, choices.Count - 1);
    }

    // ---- touch ----

    private (int Pane, int Index)? CellAt(float x, float y)
    {
        for (int p = 0; p < 2; p++)
        {
            float gx = PaneX[p] + 4;
            if (x < gx || x >= gx + 6 * Cell || y < GridY || y >= GridY + 5 * Cell)
                continue;
            int col = (int)((x - gx) / Cell), row = (int)((y - GridY) / Cell);
            return (p, row * 6 + col);
        }
        return null;
    }

    private void Touch(InputEvent e)
    {
        var cell = CellAt(e.X, e.Y);
        switch (e.Kind)
        {
            case InputKind.TouchDown:
                _report = false;
                foreach (var (r, a) in _touch)
                {
                    if (r.Contains(e.X, e.Y))
                    {
                        a();
                        return;
                    }
                }
                if (cell is { } c)
                {
                    _pane = c.Pane;
                    _row = c.Index / 6;
                    _col = c.Index % 6;
                    _touchDownPane = c.Pane;
                    _touchDownCell = c.Index;
                    var pane = Panes[c.Pane];
                    // press on a Pokémon with nothing held: pick it up and follow the finger
                    if (!Trading && _pick is null && _held is null && c.Index < pane.SlotCount && pane.Slots[c.Index] is { } pk && !pane.Locked[c.Index])
                    {
                        _held = (SlotOf(pane, c.Index), pk);
                        _dragging = true;
                        _dragX = e.X;
                        _dragY = e.Y;
                    }
                }
                else if (e.Y < GridY)
                {
                    // the box name bar: arrows at its ends turn the box
                    for (int p = 0; p < 2; p++)
                    {
                        if (e.X >= PaneX[p] && e.X < PaneX[p] + PaneW)
                        {
                            _pane = p;
                            _row = -1;
                            if (e.X < PaneX[p] + 50) ChangeBox(Panes[p], -1);
                            else if (e.X > PaneX[p] + PaneW - 50) ChangeBox(Panes[p], 1);
                            else if (p == 0 && !Trading) App.Push(new SavePickerScreen(App));
                        }
                    }
                }
                else if (_game.BoxCount == 0 && e.X < PaneX[0] + PaneW && e.Y < PaneY + PaneH && !Trading)
                {
                    App.Push(new SavePickerScreen(App));
                }
                break;

            case InputKind.TouchMove when _dragging:
                _dragX = e.X;
                _dragY = e.Y;
                if (cell is { } m)
                {
                    _pane = m.Pane;
                    _row = m.Index / 6;
                    _col = m.Index % 6;
                }
                break;

            case InputKind.TouchUp:
                if (_dragging)
                {
                    _dragging = false;
                    if (cell is { } d && (d.Pane != _touchDownPane || d.Index != _touchDownCell))
                        Activate(d.Pane, d.Index); // dropped somewhere else: move there
                    // released where it was picked up: keep holding it, a tap elsewhere puts it down
                }
                else if (cell is { } t && t.Pane == _touchDownPane && t.Index == _touchDownCell)
                {
                    Activate(t.Pane, t.Index);
                }
                _touchDownCell = _touchDownPane = -1;
                break;
        }
    }

    // ---- drawing ----

    public override bool Animating => _held is not null || _dragging || Trading;

    public override void DrawTop(Canvas c)
    {
        if (Trading)
        {
            DrawTradeTop(c);
            return;
        }
        Views.Header(c, App, "ROCKNIXDS Bank");
        var pk = _held?.Pk ?? UnderCursor;
        if (_report && pk is not null)
        {
            DrawReport(c, pk, App.VerdictFor(pk));
            return;
        }
        if (pk is not null)
        {
            string where = _held is { } h ? "Holding: from " + App.Mover.Where(h.From) : App.Mover.Where(SlotOf(Cur, _row * 6 + _col));
            Views.MonDetails(c, App, pk, App.VerdictFor(pk), where);
            return;
        }
        if (Cur.Area == Area.Save && App.Mover.Save is { } save)
        {
            Views.SaveDetails(c, App, save.Entry, save.Party);
            return;
        }
        if (Cur.Area == Area.Save)
        {
            DrawNoSave(c);
            return;
        }
        DrawBankOverview(c);
    }

    private void DrawReport(Canvas c, PKM pk, LegalityVerdict? v)
    {
        var p = c.P;
        var s = MonSummary.From(pk);
        c.Fill(0, 42, Canvas.W, 50, p.Body);
        c.Mon(pk, 12, 44, 60, 46, 1);
        c.Text($"{s.Title}  Lv {s.Level}", 80, 50, 20, p.Ink, bold: true, maxW: 360);
        Views.Badge(c, v, 620 - 130, 52);
        var text = v is null ? "Checking..." : v.Report;
        var lines = c.Wrap(text, 15, 600);
        int visible = 17;
        _reportScroll = Math.Clamp(_reportScroll, 0, Math.Max(0, lines.Count - visible));
        for (int i = 0; i < visible && i + _reportScroll < lines.Count; i++)
        {
            var line = lines[i + _reportScroll];
            var col = line.StartsWith("Invalid", StringComparison.OrdinalIgnoreCase) ? p.Bad
                : line.StartsWith("Fishy", StringComparison.OrdinalIgnoreCase) ? p.Warn : p.Ink;
            c.Text(line, 20, 100 + i * 21, 15, col);
        }
        if (lines.Count > visible)
            c.Text($"Up/Down to scroll  ({_reportScroll + 1}-{Math.Min(lines.Count, _reportScroll + visible)} of {lines.Count})", 620, 458, 13, p.Muted, Align.Right);
    }

    private void DrawNoSave(Canvas c)
    {
        var p = c.P;
        c.Text("No game open", 320, 150, 30, p.Ink, Align.Center, bold: true);
        var hint = !App.SavesScanned ? "Looking for game saves..." : App.Saves.Count == 0
            ? "No game saves were found. Saves are looked for in " + string.Join(", ", App.Cfg.SaveFolders) + "."
            : $"{App.Saves.Count} game save{(App.Saves.Count == 1 ? "" : "s")} found. Press SELECT (or tap the left box) to open one.";
        c.Paragraph(hint, 80, 200, 480, 18, p.Muted);
        c.Paragraph("Close the game before moving Pokémon: the emulator writes its own copy of the save when it quits.", 80, 300, 480, 15, p.Muted);
    }

    private void DrawBankOverview(Canvas c)
    {
        var p = c.P;
        c.Panel(16, 52, 608, 416, false, 0);
        c.Text("Bank", 36, 72, 30, p.Ink, bold: true);
        int total = App.Bank.Count;
        c.Text($"{total} Pokémon in {App.Bank.BoxCount} boxes of 30", 36, 114, 18, p.Ink);
        var byFormat = App.Bank.All().GroupBy(x => x.Pk.Extension.ToUpperInvariant()).OrderBy(g => g.Key).ToList();
        float x = 36;
        foreach (var g in byFormat)
            x += c.Tag($"{g.Key}  {g.Count()}", x, 148, p.Accent) + 8;
        c.Text($"This box: {_bank.Name}, {_bank.Count} of 30", 36, 186, 17, p.Muted);
        c.Paragraph("Pokémon keep the format of the game they came from, and change it only when they move into a newer game: " +
                    "Gen 3 to Gen 4 as with Pal Park, Gen 4 to Gen 5 as with Poké Transfer. They never go back to an older game.",
            36, 226, 568, 15, p.Muted);
        c.Paragraph($"Files: {App.Cfg.BankFolder}. Put PKHeX files (.pk3, .pk4...) in {App.Cfg.ImportFolder} to add them.",
            36, 330, 568, 15, p.Muted);
        c.Text("Legality is checked with PKHeX " + typeof(PKM).Assembly.GetName().Version?.ToString(3), 36, 430, 13, p.Muted);
    }

    private void DrawTradeTop(Canvas c)
    {
        var s = Session!;
        var p = c.P;
        Views.Header(c, App, $"Trading with {s.PartnerName}", $"ID {s.PartnerId} · {App.Trainers.Describe(s.PartnerKey, s.PartnerName)}");
        if (App.Trade!.ShowPartnerReport && s.Theirs is not null)
        {
            DrawReport(c, s.Theirs.Pk, s.Theirs.Verdict);
            return;
        }
        string? partnerCheck = s.Mine?.PartnerSaysValid is { } ok ? $"{s.PartnerName}'s check: {(ok ? "legal" : s.Mine.PartnerHeadline)}" : null;
        var lobbyLine = LobbyLine(s);
        float cardH = lobbyLine is null ? 366 : 340;
        Views.OfferCard(c, 16, 52, 296, cardH, "You offer", s.Mine, s.Mine is null ? null : App.VerdictFor(s.Mine.Pk), s.IAccepted,
            partnerCheck, s.Mine?.PartnerSaysValid ?? true);
        Views.OfferCard(c, 328, 52, 296, cardH, $"{s.PartnerName} offers", s.Theirs, s.Theirs?.Verdict, s.TheyAccepted, null, true);
        if (lobbyLine is { } ll)
            c.Text(ll.Text, 320, 398, 15, ll.Ok ? p.Good : p.Warn, Align.Center, bold: true, maxW: 600);
        string status = s.Phase == TradePhase.Exchanging ? "Trading..."
            : s.Mine is null ? "Pick the Pokémon to offer with A."
            : s.Theirs is null ? $"Waiting for {s.PartnerName} to offer a Pokémon."
            : s.IAccepted && s.TheyAccepted ? "Both accepted: trading..."
            : s.IAccepted ? $"Waiting for {s.PartnerName} to accept."
            : s.AcceptWait > TimeSpan.Zero ? $"{s.PartnerName} changed their offer. Check it before accepting."
            : s.TheyAccepted ? $"{s.PartnerName} accepted. Press START to accept the trade."
            : "Press START to accept this trade.";
        c.Fill(16, 428, 608, 40, p.Chip);
        c.Text(status, 320, 437, 18, p.Ink, Align.Center, bold: true, maxW: 590);
    }

    /// <summary>The lobby's terms against the offers on the table: what was asked for, what was listed.</summary>
    private (string Text, bool Ok)? LobbyLine(TradeSession s)
    {
        var t = App.Trade!;
        if (t.HostLobby is { } host)
        {
            if (host.Want == 0)
                return ("Your lobby is open to any offer.", true);
            var want = Names.Species(host.Want);
            if (s.Theirs is null)
                return ($"Your lobby asks for {want}.", true);
            return s.Theirs.Pk.Species == host.Want && !s.Theirs.Pk.IsEgg
                ? ($"Their offer is the {want} you asked for.", true)
                : ($"You asked for {want}; they offer {s.Theirs.Summary.Title} instead.", false);
        }
        if (t.JoinedLobby is { } joined)
        {
            if (s.Theirs is not null && !joined.Advertises(s.Theirs.Pk))
                return ($"Not what the lobby listed: it listed {joined.OfferName} Lv {joined.Level}{(joined.Shiny ? " (shiny)" : "")}.", false);
            if (joined.Want == 0)
                return ("This lobby takes any offer.", true);
            if (s.Mine is null)
                return ($"The lobby wants {joined.WantName}: yours are framed in green.", true);
            return joined.Wants(s.Mine.Pk)
                ? ($"Your offer is the {joined.WantName} the lobby wants.", true)
                : ($"The lobby wants {joined.WantName}, not {s.Mine.Summary.Title}.", false);
        }
        return null;
    }

    public override void DrawBottom(Canvas c)
    {
        var p = c.P;
        _touch.Clear();
        for (int i = 0; i < 2; i++)
            DrawPane(c, i);
        DrawTray(c);

        if (_held is { } h)
        {
            // the held Pokémon floats over the cursor (or the finger)
            float hx, hy;
            if (_dragging)
            {
                hx = _dragX - 28;
                hy = _dragY - 40;
            }
            else
            {
                var (cx, cy) = CellPos(_pane, Math.Max(0, _row), _col);
                float bob = MathF.Sin(App.Now / 180f) * 2;
                hx = cx;
                hy = cy - 22 + bob;
            }
            c.Fill(hx + 10, hy + 50, 36, 5, p.Shade);
            c.Mon(h.Pk, hx, hy, Cell + 6, Cell + 6, 1);
        }
    }

    private (float X, float Y) CellPos(int pane, int row, int col) => (PaneX[pane] + 4 + col * Cell, GridY + row * Cell);

    private void DrawPane(Canvas c, int i)
    {
        var p = c.P;
        var pane = Panes[i];
        float x = PaneX[i];
        bool focusedPane = _pane == i;
        c.Box(x, PaneY, PaneW, PaneH, p.Body, focusedPane ? p.Line : p.Rule);
        // the box name bar
        bool headerFocus = focusedPane && _row < 0;
        c.Fill(x + 2, PaneY + 2, PaneW - 4, 36, headerFocus ? p.Edge.WithAlpha(60) : p.Band);
        if (pane.BoxCount > 0)
        {
            c.Icon(Icons.Left, x + 14, PaneY + 13, 2, p.Muted);
            c.Icon(Icons.Right, x + PaneW - 22, PaneY + 13, 2, p.Muted);
            string title = pane.Area == Area.Bank ? pane.Name : $"{App.Mover.Save!.Entry.GameName}: {pane.Name}";
            c.Text(title, x + PaneW / 2, PaneY + 7, 16, p.Ink, Align.Center, bold: true, maxW: PaneW - 80);
            c.Text($"{pane.Count}/{pane.SlotCount}", x + PaneW / 2, PaneY + 25, 11, p.Muted, Align.Center);
        }
        else
        {
            c.Text(pane.Area == Area.Save ? "No game open" : "Bank", x + PaneW / 2, PaneY + 10, 16, p.Muted, Align.Center, bold: true);
        }

        if (pane.Area == Area.Save && pane.BoxCount == 0)
        {
            c.Paragraph(Trading ? "Leave the trade to open a game." : "Tap here or press SELECT to open a game save.",
                x + 24, GridY + 80, PaneW - 48, 17, p.Muted);
            if (focusedPane && _row >= 0)
                c.Frame(x + 2, GridY - 4, PaneW - 4, 5 * Cell + 6, p.Edge, 2);
            return;
        }

        var offered = Session?.Mine?.Source is ValueTuple<Slot, SaveSession?> src ? src.Item1 : (Slot?)null;
        for (int idx = 0; idx < 30; idx++)
        {
            int row = idx / 6, col = idx % 6;
            var (cx, cy) = CellPos(i, row, col);
            bool cursor = focusedPane && _row == row && _col == col;
            if (idx >= pane.SlotCount)
            {
                c.Fill(cx + 2, cy + 2, Cell - 4, Cell - 4, p.Low.WithAlpha(90));
                continue;
            }
            c.Fill(cx + 1, cy + 1, Cell - 2, Cell - 2, p.Chip);
            var pk = pane.Slots[idx];
            var slot = SlotOf(pane, idx);
            bool isHeldSource = _held is { } h && h.From == slot;
            if (pk is not null)
            {
                c.Mon(pk, cx + 2, cy + 2, Cell - 4, Cell - 4, 1, isHeldSource ? (byte)60 : (byte)255);
                if (pk.IsShiny)
                    c.Icon(Icons.Star, cx + 3, cy + 3, 1, Color.Hex(0xffc93c));
                var v = App.VerdictFor(pk, pane.Keys[idx]);
                if (v is { Analysed: true, Valid: false })
                {
                    c.Fill(cx + Cell - 13, cy + 3, 10, 10, p.Bad);
                    c.Icon(Icons.Warn, cx + Cell - 10, cy + 5, 1, Color.Hex(0xffffff));
                }
                if (offered == slot)
                    c.Tag("T", cx + 2, cy + Cell - 20, p.Accent, 10, filled: true);
                else if (App.Trade?.JoinedLobby is { Want: > 0 } joined && joined.Wants(pk))
                    c.Frame(cx + 2, cy + 2, Cell - 4, Cell - 4, p.Good, 2); // what the lobby's host wants
            }
            if (pane.Locked[idx])
                c.Icon(Icons.Lock, cx + Cell - 9, cy + Cell - 11, 1, p.Muted);
            if (cursor)
            {
                var col2 = _held is not null ? p.Red : p.Edge;
                c.Frame(cx, cy, Cell, Cell, col2, 3);
            }
        }
    }

    private void DrawTray(Canvas c)
    {
        var p = c.P;
        // the message line
        float y = 310;
        c.Box(4, y, 632, 44, p.Chip, p.Rule, 2);
        string msg;
        Color col = p.Ink;
        if (App.Toast is { } t)
        {
            msg = t;
            col = App.ToastIsError ? p.Bad : p.Ink;
        }
        else if (Trading)
        {
            var s = Session!;
            msg = s.Log.Count > 0 ? s.Log[^1] : $"Connected to {s.PartnerName}.";
        }
        else if (_pick is { } pick)
        {
            msg = pick.Prompt;
            col = p.Accent;
        }
        else if (_held is { } h)
        {
            msg = $"Holding {MonSummary.From(h.Pk).Title}: put it in an empty slot, or on another Pokémon to swap.";
        }
        else
        {
            msg = App.Mover.Save is null ? "Open a game save to move Pokémon between it and the bank." : "Pick a Pokémon up with A (or touch it) to move it.";
        }
        c.Text(msg, 16, y + 13, 16, col, maxW: 608);

        // buttons for touch
        y = 362;
        float bw = 151, gap = 9;
        string[] labels;
        Action[] actions;
        if (Trading)
        {
            var s = Session!;
            labels = [s.IAccepted ? "Unaccept" : "Accept", "Withdraw", "Their report", "Leave"];
            actions = [StartButton, () => { if (s.Mine is not null) s.Withdraw(); }, () => { if (s.Theirs is not null) { App.Trade!.ShowPartnerReport = !App.Trade.ShowPartnerReport; _report = App.Trade.ShowPartnerReport; } }, LeaveTrade];
        }
        else
        {
            labels = ["Games", "Trade", "Report", "Menu"];
            actions = [() => App.Push(new SavePickerScreen(App)), () => App.Push(new TradeMenuScreen(App)), ToggleReport, () => MainMenu.Show(App)];
        }
        for (int i = 0; i < 4; i++)
        {
            var r = c.Button(labels[i], 4 + i * (bw + gap), y, bw, 46, false, size: 17);
            _touch.Add((r, actions[i]));
        }

        // the buttons' meaning
        y = 424;
        float x = 8;
        if (Trading)
        {
            x += c.Hint("A", "Offer", x, y);
            x += c.Hint("START", Session!.IAccepted ? "Unaccept" : "Accept", x, y);
            x += c.Hint("X", "Their report", x, y);
            c.Hint("SELECT", "Leave", x, y);
        }
        else if (_pick is not null)
        {
            x += c.Hint("A", "Choose", x, y);
            x += c.Hint("B", "Cancel", x, y);
            c.Hint("L/R", "Box", x, y);
        }
        else
        {
            x += c.Hint("A", _held is null ? "Pick up" : "Put down", x, y);
            x += c.Hint("B", "Cancel", x, y);
            x += c.Hint("L/R", "Box", x, y);
            x += c.Hint("X", "Report", x, y);
            c.Hint("SELECT", "Games", x, y);
        }
        if (App.PendingChecks > 0)
            c.Text($"checking {App.PendingChecks}...", 632, 456, 12, p.Muted, Align.Right);
    }
}
