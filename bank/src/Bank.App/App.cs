using System.Collections.Concurrent;
using PKHeX.Core;
using Rocknixds.Bank.Trade;

namespace Rocknixds.Bank.App;

/// <summary>One screen of the app: it draws both panels and gets the input.</summary>
public abstract class Screen(App app)
{
    protected App App { get; } = app;
    protected Palette P => App.Palette;

    public virtual void Enter() { }
    public virtual void Leave() { }
    public abstract void Handle(InputEvent e);
    public virtual void Update() { }
    public abstract void DrawTop(Canvas c);
    public abstract void DrawBottom(Canvas c);

    /// <summary>The screen is moving: draw again soon.</summary>
    public virtual bool Animating => false;
}

/// <summary>The app's state: settings, bank, open save, screens, the trade, the background worker.</summary>
public sealed class App
{
    private readonly ConcurrentQueue<Action> _mainThread = new();
    private readonly BlockingCollection<PKM> _legalityQueue = new(new ConcurrentQueue<PKM>());
    private readonly ConcurrentDictionary<string, LegalityVerdict> _verdicts = new();
    private readonly ConcurrentDictionary<string, byte> _queued = new();
    private readonly List<Screen> _screens = [];

    public BankConfig Cfg { get; }
    public History History { get; }
    public BankStore Bank { get; private set; } = null!;
    public Mover Mover { get; private set; } = null!;
    public List<SaveEntry> Saves { get; private set; } = [];
    public bool SavesScanned { get; private set; }
    public Palette Palette { get; private set; } = Palette.Dark;
    public Action<string> Log { get; }
    public uint Now { get; set; }
    public bool QuitRequested { get; set; }

    public TradeController? Trade { get; set; }

    /// <summary>This handheld's identity key for trading (made on first use).</summary>
    public DeviceIdentity Identity => _identity ??= DeviceIdentity.LoadOrCreate(Cfg.IdentityPath);
    private DeviceIdentity? _identity;

    /// <summary>The handhelds traded with before.</summary>
    public TrainerBook Trainers => _trainers ??= new TrainerBook(Cfg.TrainersPath);
    private TrainerBook? _trainers;

    // modal
    public Dialog? Dialog { get; private set; }

    // the background job
    public string? BusyLabel { get; private set; }
    public uint BusySince { get; private set; }

    // the message line
    public string? Toast { get; private set; }
    public bool ToastIsError { get; private set; }
    private uint _toastUntil;

    public App(BankConfig cfg, Action<string> log)
    {
        Cfg = cfg;
        Log = log;
        History = new History(cfg.HistoryPath);
        ApplyPalette();
        var worker = new Thread(LegalityWorker) { IsBackground = true, Name = "legality", Priority = ThreadPriority.BelowNormal };
        worker.Start();
    }

    public void Load()
    {
        _ = Identity; // made (or read) now, off the main thread
        _ = Trainers;
        Bank = BankStore.Open(Cfg);
        Mover = new Mover(Cfg, Bank, History);
        foreach (var n in Bank.LoadNotes)
            Log(n);
        var imported = Bank.ImportDropped();
        foreach (var n in imported)
            Log(n);
        if (imported.Count(n => n.StartsWith("Imported", StringComparison.Ordinal)) is var k and > 0)
            ShowToast($"Imported {k} Pokémon from the import folder.");
    }

    public void ApplyPalette()
    {
        var mode = Cfg.Palette;
        if (mode == "auto")
        {
            // follow the menu: ROCKNIXDS Pixel light, or anything else (dark themes)
            try
            {
                var es = File.ReadAllText("/storage/.config/emulationstation/es_settings.cfg");
                mode = es.Contains("value=\"rocknixds-pixel-light\"", StringComparison.Ordinal) ? "light" : "dark";
            }
            catch (Exception)
            {
                mode = "dark";
            }
        }
        Palette = mode == "light" ? Palette.Light : Palette.Dark;
    }

    // ---- screens ----

    public Screen Top => _screens[^1];

    public void SetSaves(List<SaveEntry> list)
    {
        Saves = list;
        SavesScanned = true;
    }

    /// <summary>Replaces the current screen.</summary>
    public void Replace(Screen s)
    {
        var old = _screens[^1];
        _screens[^1] = s;
        old.Leave();
        s.Enter();
    }

    /// <summary>A message for when the player is back on the boxes (a trade that ended behind another screen).</summary>
    public string? PendingMessage { get; set; }

    /// <summary>A hosted lobby to open again once the player is back on the boxes, with what happened last.</summary>
    public (HostedLobby Lobby, string Status)? PendingLobby { get; set; }

    public void Push(Screen s)
    {
        _screens.Add(s);
        s.Enter();
    }

    public void Pop()
    {
        if (_screens.Count <= 1)
            return;
        var s = _screens[^1];
        _screens.RemoveAt(_screens.Count - 1);
        s.Leave();
        _screens[^1].Enter();
    }

    /// <summary>Back to the first screen (the boxes).</summary>
    public void PopToRoot()
    {
        while (_screens.Count > 1)
        {
            var s = _screens[^1];
            _screens.RemoveAt(_screens.Count - 1);
            s.Leave();
        }
        _screens[0].Enter();
    }

    public T? Find<T>() where T : Screen => _screens.OfType<T>().FirstOrDefault();

    public void ShowDialog(Dialog d) => Dialog = d;

    public void CloseDialog() => Dialog = null;

    public void Message(string title, string text, Action? then = null) =>
        ShowDialog(new Dialog(this, title, text, [new("OK", () => then?.Invoke())]));

    public void Confirm(string title, string text, string yes, Action onYes, string no = "Cancel", Action? onNo = null) =>
        ShowDialog(new Dialog(this, title, text, [new(yes, onYes), new(no, () => onNo?.Invoke())]) { CancelIndex = 1 });

    public void Menu(string title, IReadOnlyList<DialogChoice> choices, int cancelIndex = -1) =>
        ShowDialog(new Dialog(this, title, null, choices) { CancelIndex = cancelIndex, Vertical = true });

    public void ShowToast(string text, bool error = false, int ms = 4500)
    {
        Toast = text;
        ToastIsError = error;
        _toastUntil = Now + (uint)ms;
    }

    // ---- background work ----

    public bool Busy => BusyLabel is not null;

    /// <summary>Runs <paramref name="work"/> off the main thread; input waits until it's done.</summary>
    public void Run<T>(string label, Func<T> work, Action<T> done, Action<Exception>? failed = null)
    {
        if (Busy)
            return;
        BusyLabel = label;
        BusySince = Now;
        Task.Run(() =>
        {
            try
            {
                var r = work();
                Post(() =>
                {
                    BusyLabel = null;
                    done(r);
                });
            }
            catch (Exception ex)
            {
                Log($"{label}: {ex}");
                Post(() =>
                {
                    BusyLabel = null;
                    if (failed is not null)
                        failed(ex);
                    else
                        Message("Something went wrong", ex is BankException or IOException ? ex.Message : $"{ex.GetType().Name}: {ex.Message}");
                });
            }
        });
    }

    public void Post(Action a) => _mainThread.Enqueue(a);

    public void ScanSaves(Action? then = null)
    {
        Run("Looking for game saves...", () => SaveScanner.Scan(Cfg, Log), list =>
        {
            Saves = list;
            SavesScanned = true;
            then?.Invoke();
        });
    }

    /// <summary>Opens a save for the left box panel.</summary>
    public void OpenSave(SaveEntry entry, Action? then = null)
    {
        Run($"Opening {entry.GameName}...", () => SaveSession.Open(Cfg, entry.Path), session =>
        {
            Mover.Save = session;
            Cfg.LastSave = entry.Path;
            TrySaveConfig();
            then?.Invoke();
        });
    }

    public void TrySaveConfig()
    {
        try { Cfg.Save(); } catch (Exception ex) { Log($"settings not saved: {ex.Message}"); }
    }

    // ---- legality, checked in the background ----

    private static string Key(PKM pk) => pk.GetType().Name + PkmIO.Hash(pk);

    /// <summary>The verdict for <paramref name="pk"/>, or null while it's being checked (it is queued).</summary>
    public LegalityVerdict? VerdictFor(PKM pk, string? key = null)
    {
        key ??= Key(pk);
        if (_verdicts.TryGetValue(key, out var v))
            return v;
        if (_queued.TryAdd(key, 0))
            _legalityQueue.Add(pk.Clone());
        return null;
    }

    public int PendingChecks => _queued.Count;

    private void LegalityWorker()
    {
        Legality.WarmUp();
        foreach (var pk in _legalityQueue.GetConsumingEnumerable())
        {
            var key = Key(pk);
            _verdicts[key] = Legality.Check(pk);
            _queued.TryRemove(key, out _);
            Post(() => { }); // wakes the screen
        }
    }

    // ---- every frame ----

    public void Update()
    {
        while (_mainThread.TryDequeue(out var a))
            a();
        if (Toast is not null && Now > _toastUntil)
            Toast = null;
        Trade?.Update();
        Top.Update();
        if (PendingLobby is { } pl && Dialog is null && Top is BoxScreen)
        {
            PendingLobby = null;
            Push(new LobbyHostScreen(this, pl.Lobby, pl.Status));
        }
        if (PendingMessage is { } m && Dialog is null && Top is BoxScreen)
        {
            PendingMessage = null;
            Message("Trade over", m);
        }
    }

    public void Handle(InputEvent e)
    {
        if (Busy)
            return;
        if (Dialog is not null)
        {
            Dialog.Handle(e);
            return;
        }
        Top.Handle(e);
    }

    public bool Animating => Busy || Top.Animating || Trade is not null || Toast is not null || PendingChecks > 0 || !_mainThread.IsEmpty;

    public void Draw(Canvas top, Canvas bottom, bool drawTop)
    {
        if (drawTop)
        {
            Top.DrawTop(top);
            Dialog?.DrawTop(top);
        }
        else
        {
            Top.DrawBottom(bottom);
            if (Dialog is not null)
                Dialog.DrawBottom(bottom);
            if (Busy && Now - BusySince > 150)
                DrawBusy(bottom);
        }
    }

    private void DrawBusy(Canvas c)
    {
        c.Shade(Palette.Shade);
        c.Panel(120, 190, 400, 100, true);
        int dots = (int)((Now / 300) % 4);
        c.Text(BusyLabel!, 320, 214, 20, Palette.Ink, Align.Center, true, 370);
        for (int i = 0; i < 3; i++)
            c.Fill(296 + i * 18, 254, 10, 10, i < dots ? Palette.Edge : Palette.Rule);
    }
}

public sealed record DialogChoice(string Label, Action Action, bool Enabled = true);

/// <summary>A question or a list of choices over the bottom screen.</summary>
public sealed class Dialog(App app, string title, string? text, IReadOnlyList<DialogChoice> choices)
{
    private int _focus;
    private readonly List<RectF> _hit = [];

    public int CancelIndex { get; init; } = -1;
    public bool Vertical { get; init; }

    /// <summary>Optional extra: drawn on the top screen while the dialog is open.</summary>
    public Action<Canvas>? Top { get; init; }

    public void Handle(InputEvent e)
    {
        if (e.Kind == InputKind.TouchDown || e.Kind == InputKind.TouchUp)
        {
            for (int i = 0; i < _hit.Count; i++)
            {
                if (_hit[i].Contains(e.X, e.Y) && choices[i].Enabled)
                {
                    _focus = i;
                    if (e.Kind == InputKind.TouchUp)
                        Choose(i);
                    return;
                }
            }
            return;
        }
        int n = choices.Count;
        if (e.Is(Vertical ? Btn.Up : Btn.Left))
            Step(-1);
        else if (e.Is(Vertical ? Btn.Down : Btn.Right))
            Step(1);
        else if (e.Pressed(Btn.A))
            Choose(_focus);
        else if (e.Pressed(Btn.B) && CancelIndex >= 0)
            Choose(CancelIndex);
        else if (e.Pressed(Btn.B) && n == 1)
            Choose(0);

        void Step(int d)
        {
            for (int k = 0; k < n; k++)
            {
                _focus = (_focus + d + n) % n;
                if (choices[_focus].Enabled)
                    break;
            }
        }
    }

    private void Choose(int i)
    {
        if (!choices[i].Enabled)
            return;
        app.CloseDialog();
        choices[i].Action();
    }

    public void DrawTop(Canvas c) => Top?.Invoke(c);

    public void DrawBottom(Canvas c)
    {
        var p = app.Palette;
        c.Shade(p.Shade);
        float w = 560, x = (Canvas.W - w) / 2;
        var lines = text is null ? [] : c.Wrap(text, 17, w - 40);
        if (lines.Count > 9)
            lines = [.. lines.Take(9).SkipLast(1), c.Ellipsize(lines[8] + "…", 17, false, w - 40)];
        float textH = lines.Count * 23;
        float buttonsH = Vertical ? choices.Count * 46 : 54;
        float h = 56 + textH + (textH > 0 ? 12 : 0) + buttonsH + 12;
        h = Math.Min(h, 470);
        float y = (Canvas.H - h) / 2;
        c.Panel(x, y, w, h, true, 40);
        c.Text(title, x + 20, y + 11, 20, p.Ink, bold: true, maxW: w - 40);
        float ty = y + 54;
        foreach (var l in lines)
        {
            c.Text(l, x + 20, ty, 17, p.Ink);
            ty += 23;
        }
        _hit.Clear();
        float by = ty + (textH > 0 ? 12 : 0);
        if (Vertical)
        {
            for (int i = 0; i < choices.Count; i++)
            {
                var r = c.Button(choices[i].Label, x + 20, by + i * 46, w - 40, 40, i == _focus, size: 18);
                if (!choices[i].Enabled)
                    c.Fill(r.X + 2, r.Y + 2, r.W - 4, r.H - 4, p.Body.WithAlpha(150));
                _hit.Add(r);
            }
        }
        else
        {
            float bw = (w - 40 - (choices.Count - 1) * 12) / choices.Count;
            for (int i = 0; i < choices.Count; i++)
            {
                var label = choices.Count == 2 ? (i == 0 ? "A  " : "B  ") + choices[i].Label : choices[i].Label;
                _hit.Add(c.Button(label, x + 20 + i * (bw + 12), by, bw, 44, i == _focus, i == 0 ? p.Edge : p.Line));
            }
        }
    }
}

/// <summary>What the app does with Pokémon in a trade: received ones go to the bank, given ones leave their slot.</summary>
public sealed class TradeStorage(App app) : ITradeStorage
{
    public string StoreReceived(PKM pk)
    {
        var start = app.Find<BoxScreen>()?.BankBox ?? 0;
        if (!app.Bank.TryFindFree(out var box, out var slot, start))
            throw new BankException("the bank is full");
        app.Bank.Put(box, slot, pk);
        app.History.Log($"TRADE IN {Names.Species(pk.Species)} Lv{pk.CurrentLevel} {pk.Extension} PID {pk.PID:X8} OT {pk.OriginalTrainerName} from {app.Trade?.Session.PartnerName}: {app.Bank.BoxName(box)}, slot {slot + 1}");
        app.Find<BoxScreen>()?.Refresh();
        return $"{app.Bank.BoxName(box)}, slot {slot + 1}";
    }

    public void RemoveOffered(TradeOffer offer)
    {
        var (slot, save) = ((Slot, SaveSession?))offer.Source!;
        if (slot.Area == Area.Save && save != app.Mover.Save)
            throw new BankException("its game isn't open any more");
        var pk = app.Mover.Read(slot);
        if (pk is null || PkmIO.Hash(pk) != offer.Hash)
            throw new BankException("it isn't in its slot any more");
        app.Mover.Remove(slot, $"TRADE OUT to {app.Trade?.Session.PartnerName}:");
        app.Find<BoxScreen>()?.Refresh();
    }
}

/// <summary>The trade in progress: its session, pumped every frame.</summary>
public sealed class TradeController(App app, TradeSession session)
{
    private readonly TradeStorage _storage = new(app);
    private bool _lobbyOffered;

    public TradeSession Session { get; } = session;
    public bool ShowPartnerReport { get; set; }

    /// <summary>The lobby we host: its Pokémon is offered as soon as someone joins, and the lobby opens again when
    /// they leave (unless that Pokémon was traded).</summary>
    public HostedLobby? HostLobby { get; init; }

    /// <summary>The listing of the lobby we joined: what its host advertised and wants.</summary>
    public LobbyListing? JoinedLobby { get; init; }

    public void Update()
    {
        if (HostLobby is { } lobby && !_lobbyOffered)
        {
            _lobbyOffered = true;
            if (lobby.StillThere(app))
                Session.Offer(lobby.Pk, (lobby.Slot, lobby.Save));
        }
        Session.Pump(_storage);
        if (Session.Result is { } r)
        {
            Session.Result = null;
            if (r.Problem is null)
                app.Trainers.RecordTrade(Session.PartnerKey, Session.PartnerName);
            app.Push(new TradeDoneScreen(app, r));
        }
        if (Session.Phase == TradePhase.Closed)
        {
            var reason = Session.CloseReason ?? "The trade ended.";
            app.Trade = null;
            Session.Dispose();
            app.Find<BoxScreen>()?.Refresh();
            if (HostLobby is { } l && l.StillThere(app))
                app.PendingLobby = (l, reason); // its Pokémon is still here: the lobby opens again
            else if (HostLobby is { } gone)
                app.PendingMessage = $"{reason}\n\nYour lobby for {MonSummary.From(gone.Pk).Title} closed: it was traded.";
            else
                app.PendingMessage = reason;
        }
    }
}

/// <summary>A lobby this handheld hosts: the Pokémon it lists (and where it is), and what it wants for it.</summary>
public sealed record HostedLobby(PKM Pk, Slot Slot, SaveSession? Save, ushort Want, bool Open)
{
    public string Hash { get; } = PkmIO.Hash(Pk);

    /// <summary>The listed Pokémon is still in its slot (it wasn't traded or moved).</summary>
    public bool StillThere(App app)
    {
        if (Slot.Area == Area.Save && Save != app.Mover.Save)
            return false;
        var now = app.Mover.Read(Slot);
        return now is not null && PkmIO.Hash(now) == Hash;
    }

    public LobbyListing Listing(string code) => LobbyListing.For(Pk, Want, Open, code);
}
