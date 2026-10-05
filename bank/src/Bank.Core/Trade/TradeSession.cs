using System.Collections.Concurrent;
using System.Text.Json;
using PKHeX.Core;

namespace Rocknixds.Bank.Trade;

/// <summary>A Pokémon put up for trade.</summary>
public sealed class TradeOffer(PKM pk, string hash)
{
    public PKM Pk { get; } = pk;
    public string Hash { get; } = hash;
    public MonSummary Summary { get; } = MonSummary.From(pk);

    /// <summary>Our own legality check of it (null while it runs).</summary>
    public LegalityVerdict? Verdict { get; internal set; }

    /// <summary>For our own offer: what the partner's check said about it.</summary>
    public bool? PartnerSaysValid { get; internal set; }
    public string? PartnerHeadline { get; internal set; }

    /// <summary>For our own offer: where it is, to take it out once the partner has it.</summary>
    public object? Source { get; init; }
}

/// <summary>What the app does with Pokémon when a trade goes through. Called on the thread that calls <see cref="TradeSession.Pump"/>.</summary>
public interface ITradeStorage
{
    /// <summary>Keeps the received Pokémon (in the bank). Returns where it went, for the player.</summary>
    string StoreReceived(PKM pk);

    /// <summary>Takes our offered Pokémon out of where it is (<see cref="TradeOffer.Source"/>); the partner has it now.</summary>
    void RemoveOffered(TradeOffer offer);
}

public enum TradePhase { Open, Exchanging, Closed }

/// <summary>A finished trade, for the screen that celebrates it. <see cref="Got"/> is the Pokémon as kept (evolved, if it
/// evolved on arrival); <see cref="Arrived"/> as it was sent.</summary>
public sealed record TradeResult(MonSummary Gave, MonSummary Arrived, MonSummary Got, string? EvolvedInto, string StoredWhere, string? Problem);

/// <summary>
/// One trade connection, the same on both handhelds after the handshake. Each side offers one Pokémon; both check the
/// other's with PKHeX's legality analysis; both accept; then the host coordinates the exchange:
/// <list type="number">
/// <item>host: <c>commit</c> (the pair both accepted)</item>
/// <item>joiner: keeps the host's Pokémon, then <c>received</c></item>
/// <item>host: keeps the joiner's Pokémon, takes its own out, then <c>received</c></item>
/// <item>joiner: takes its own out, then <c>complete</c></item>
/// </list>
/// Each side gives its Pokémon away only after the other said it has it, so a dropped connection can leave a copy
/// on both sides, never a Pokémon on neither. Network work runs on background threads; everything that changes state
/// or touches storage runs in <see cref="Pump"/>, on the app's thread.
/// </summary>
public sealed class TradeSession : IDisposable
{
    private static readonly TimeSpan PingEvery = TimeSpan.FromSeconds(4);
    private static readonly TimeSpan GiveUpAfter = TimeSpan.FromSeconds(20);

    private readonly SecureChannel _channel;
    private readonly BankConfig _cfg;
    private readonly ConcurrentQueue<TradeMsg> _inbox = new();
    private readonly BlockingCollection<TradeMsg> _outbox = new();
    private readonly CancellationTokenSource _cts = new();
    private readonly List<Task<(string Hash, LegalityVerdict Verdict)>> _checks = [];
    private DateTime _lastReceived = DateTime.UtcNow;
    private DateTime _lastSent = DateTime.UtcNow;
    private volatile string? _networkError;

    // the exchange in progress: the pair as accepted
    private TradeOffer? _exMine, _exTheirs;
    private bool _stored, _removed;
    private string _storedWhere = "";
    private string? _evolvedInto;
    private MonSummary? _got;

    public bool IsHost { get; }
    public string MyName { get; }
    public string PartnerName { get; private set; } = "Partner";
    public string PartnerAddress => _channel.RemoteAddress;

    public TradeOffer? Mine { get; private set; }
    public TradeOffer? Theirs { get; private set; }
    public bool IAccepted { get; private set; }
    public bool TheyAccepted { get; private set; }
    public TradePhase Phase { get; private set; } = TradePhase.Open;
    public string? CloseReason { get; private set; }

    /// <summary>Set when a trade finishes; the app shows it and clears it.</summary>
    public TradeResult? Result { get; set; }

    /// <summary>Things that happened, newest last ("Partner offered Machop").</summary>
    public List<string> Log { get; } = [];

    public TradeSession(SecureChannel channel, BankConfig cfg, bool isHost)
    {
        _channel = channel;
        _cfg = cfg;
        IsHost = isHost;
        MyName = cfg.EffectiveTrainerName;
        _ = Task.Run(ReceiveLoop);
        _ = Task.Factory.StartNew(SendLoop, TaskCreationOptions.LongRunning);
        Send(new TradeMsg { T = "hello", Name = MyName, App = $"rocknixds-bank {typeof(TradeSession).Assembly.GetName().Version}" });
    }

    public bool CanChangeOffer => Phase == TradePhase.Open;

    /// <summary>Puts <paramref name="pk"/> up for trade (replacing an earlier offer). Both sides' accepts reset.</summary>
    public void Offer(PKM pk, object source)
    {
        if (Phase != TradePhase.Open)
            return;
        var data = PkmIO.ToFileBytes(pk);
        var copy = PkmIO.FromFileBytes(data, PkmIO.Extension(pk)) ?? pk.Clone();
        Mine = new TradeOffer(copy, PkmIO.Hash(copy)) { Source = source };
        IAccepted = TheyAccepted = false;
        Send(new TradeMsg { T = "offer", Data = data, Ext = PkmIO.Extension(pk), Hash = Mine.Hash });
        Note($"You offered {Mine.Summary.Title}.");
    }

    public void Withdraw()
    {
        if (Phase != TradePhase.Open || Mine is null)
            return;
        Mine = null;
        IAccepted = TheyAccepted = false;
        Send(new TradeMsg { T = "withdraw" });
        Note("You took your offer back.");
    }

    /// <summary>Agrees to give our offer for theirs, as they are now.</summary>
    public void Accept()
    {
        if (Phase != TradePhase.Open || Mine is null || Theirs is null || IAccepted)
            return;
        IAccepted = true;
        Send(new TradeMsg { T = "accept", Mine = Mine.Hash, Theirs = Theirs.Hash });
    }

    public void Unaccept()
    {
        if (Phase != TradePhase.Open || !IAccepted)
            return;
        IAccepted = false;
        Send(new TradeMsg { T = "unaccept" });
    }

    public void Leave(string reason = "The partner left the trade.")
    {
        if (Phase == TradePhase.Closed)
            return;
        Send(new TradeMsg { T = "bye", Text = reason });
        _outbox.CompleteAdding();
        Close("You left the trade.");
    }

    /// <summary>Handles what arrived and moves the trade along. Call it every frame.</summary>
    public void Pump(ITradeStorage storage)
    {
        if (Phase == TradePhase.Closed)
            return;

        for (int i = _checks.Count - 1; i >= 0; i--)
        {
            var task = _checks[i];
            if (!task.IsCompleted)
                continue;
            _checks.RemoveAt(i);
            if (!task.IsCompletedSuccessfully)
                continue;
            var (hash, verdict) = task.Result;
            if (Theirs?.Hash == hash)
            {
                Theirs.Verdict = verdict;
                Send(new TradeMsg { T = "verdict", Hash = hash, Valid = verdict.Valid, Text = verdict.Headline });
            }
        }

        while (_inbox.TryDequeue(out var msg))
        {
            _lastReceived = DateTime.UtcNow;
            Handle(msg, storage);
            if (Phase == TradePhase.Closed)
                return;
        }

        if (_networkError is { } err)
        {
            Close(Phase == TradePhase.Exchanging ? InterruptedMessage() : err);
            return;
        }

        if (IsHost && Phase == TradePhase.Open && IAccepted && TheyAccepted && Mine is not null && Theirs is not null)
        {
            _exMine = Mine;
            _exTheirs = Theirs;
            _stored = _removed = false;
            Phase = TradePhase.Exchanging;
            Send(new TradeMsg { T = "commit", Mine = Mine.Hash, Theirs = Theirs.Hash });
        }

        var now = DateTime.UtcNow;
        if (now - _lastSent > PingEvery)
            Send(new TradeMsg { T = "ping" });
        if (now - _lastReceived > GiveUpAfter)
            Close(Phase == TradePhase.Exchanging ? InterruptedMessage() : $"{PartnerName} stopped answering.");
    }

    private void Handle(TradeMsg msg, ITradeStorage storage)
    {
        switch (msg.T)
        {
            case "hello":
                PartnerName = string.IsNullOrWhiteSpace(msg.Name) ? "Partner" : msg.Name.Trim()[..Math.Min(msg.Name.Trim().Length, 24)];
                Note($"Connected to {PartnerName}.");
                break;

            case "offer" when Phase == TradePhase.Open:
                if (msg.Data is null)
                    break;
                var pk = PkmIO.FromFileBytes(msg.Data, msg.Ext);
                if (pk is null)
                {
                    Note($"{PartnerName} offered something that isn't a Pokémon this app can read.");
                    break;
                }
                var offer = new TradeOffer(pk, PkmIO.Hash(pk));
                Theirs = offer;
                IAccepted = TheyAccepted = false;
                _checks.Add(Task.Run(() => (offer.Hash, Legality.Check(offer.Pk))));
                Note($"{PartnerName} offered {offer.Summary.Title} (Lv {offer.Summary.Level}).");
                break;

            case "withdraw" when Phase == TradePhase.Open:
                Theirs = null;
                IAccepted = TheyAccepted = false;
                Note($"{PartnerName} took their offer back.");
                break;

            case "accept" when Phase == TradePhase.Open:
                // only for the pair as it is now: an accept that crossed a changed offer is stale
                if (Mine is not null && Theirs is not null && msg.Mine == Theirs.Hash && msg.Theirs == Mine.Hash)
                    TheyAccepted = true;
                break;

            case "unaccept" when Phase == TradePhase.Open:
                TheyAccepted = false;
                break;

            case "verdict":
                if (Mine is not null && msg.Hash == Mine.Hash)
                {
                    Mine.PartnerSaysValid = msg.Valid;
                    Mine.PartnerHeadline = msg.Text;
                }
                break;

            // joiner: the host says go
            case "commit" when !IsHost:
                if (Phase == TradePhase.Open && IAccepted && Mine is not null && Theirs is not null &&
                    msg.Mine == Theirs.Hash && msg.Theirs == Mine.Hash)
                {
                    _exMine = Mine;
                    _exTheirs = Theirs;
                    _stored = _removed = false;
                    Phase = TradePhase.Exchanging;
                    if (!StoreTheirs(storage))
                        return;
                    Send(new TradeMsg { T = "received", Mine = _exMine.Hash, Theirs = _exTheirs.Hash });
                }
                else
                {
                    Send(new TradeMsg { T = "abort", Mine = msg.Theirs, Theirs = msg.Mine });
                }
                break;

            case "abort" when IsHost && Phase == TradePhase.Exchanging:
                Phase = TradePhase.Open;
                TheyAccepted = false;
                _exMine = _exTheirs = null;
                break;

            case "received" when Phase == TradePhase.Exchanging && _exMine is not null && _exTheirs is not null:
                if (msg.Mine != _exTheirs.Hash || msg.Theirs != _exMine.Hash)
                    break;
                if (IsHost)
                {
                    // the joiner has ours: keep theirs, give ours away, tell them
                    if (!StoreTheirs(storage))
                        return;
                    RemoveMine(storage);
                    Send(new TradeMsg { T = "received", Mine = _exMine.Hash, Theirs = _exTheirs.Hash });
                }
                else
                {
                    RemoveMine(storage);
                    Send(new TradeMsg { T = "complete", Mine = _exMine.Hash, Theirs = _exTheirs.Hash });
                    Finish();
                }
                break;

            case "complete" when IsHost && Phase == TradePhase.Exchanging:
                Finish();
                break;

            case "bye":
                Close(Phase == TradePhase.Exchanging ? InterruptedMessage() : msg.Text ?? $"{PartnerName} left the trade.");
                break;

            case "ping":
                break;
        }
    }

    private bool StoreTheirs(ITradeStorage storage)
    {
        if (_stored)
            return true;
        var pk = _exTheirs!.Pk.Clone();
        _evolvedInto = _cfg.TradeEvolutions ? TradeEvolution.TryEvolve(pk, _exMine!.Pk.Species) : null;
        _got = MonSummary.From(pk);
        try
        {
            _storedWhere = storage.StoreReceived(pk);
            _stored = true;
            return true;
        }
        catch (Exception ex)
        {
            // nothing was given away yet: tell the partner, end the trade
            Send(new TradeMsg { T = "bye", Text = $"{MyName} couldn't keep the Pokémon ({ex.Message}). Nothing was traded." });
            Close($"Couldn't keep {_exTheirs.Summary.Title}: {ex.Message}. Nothing was traded.");
            return false;
        }
    }

    private void RemoveMine(ITradeStorage storage)
    {
        if (_removed)
            return;
        _removed = true;
        try
        {
            storage.RemoveOffered(_exMine!);
        }
        catch (Exception ex)
        {
            Result = new TradeResult(_exMine!.Summary, _exTheirs!.Summary, _got ?? _exTheirs.Summary, _evolvedInto, _storedWhere,
                $"Your {_exMine.Summary.Title} couldn't be taken out of its slot ({ex.Message}), so you have it and the new one.");
        }
    }

    private void Finish()
    {
        Result ??= new TradeResult(_exMine!.Summary, _exTheirs!.Summary, _got ?? _exTheirs.Summary, _evolvedInto, _storedWhere, null);
        Note($"Traded {_exMine!.Summary.Title} for {_exTheirs!.Summary.Title}.");
        Phase = TradePhase.Open;
        Mine = Theirs = null;
        IAccepted = TheyAccepted = false;
        _exMine = _exTheirs = null;
        _evolvedInto = null;
        _got = null;
    }

    private string InterruptedMessage()
    {
        if (_stored && !_removed)
            return $"The connection dropped during the trade. {_exTheirs?.Summary.Title} arrived ({_storedWhere}) and you still have {_exMine?.Summary.Title}.";
        if (_stored)
            return $"The connection dropped at the end of the trade. {_exTheirs?.Summary.Title} arrived ({_storedWhere}).";
        return "The connection dropped before the trade. Nothing was traded.";
    }

    private void Close(string reason)
    {
        if (Phase == TradePhase.Closed)
            return;
        if (Phase == TradePhase.Exchanging && _stored && _exMine is not null && _exTheirs is not null)
            Result ??= new TradeResult(_exMine.Summary, _exTheirs.Summary, _got ?? _exTheirs.Summary, _evolvedInto, _storedWhere, reason);
        Phase = TradePhase.Closed;
        CloseReason = reason;
        Note(reason);
        _cts.Cancel();
        _outbox.CompleteAdding();
        // let the send loop flush a last "bye", then hang up
        _ = Task.Delay(500).ContinueWith(_ => _channel.Dispose());
    }

    private void Note(string line)
    {
        Log.Add(line);
        if (Log.Count > 50)
            Log.RemoveAt(0);
    }

    private void Send(TradeMsg msg)
    {
        _lastSent = DateTime.UtcNow;
        try
        {
            if (!_outbox.IsAddingCompleted)
                _outbox.Add(msg);
        }
        catch (InvalidOperationException)
        {
            // closing
        }
    }

    private async Task ReceiveLoop()
    {
        try
        {
            while (!_cts.IsCancellationRequested)
            {
                var data = await _channel.ReceiveAsync(_cts.Token);
                var msg = JsonSerializer.Deserialize(data, TradeJson.Default.TradeMsg);
                if (msg is not null)
                    _inbox.Enqueue(msg);
            }
        }
        catch (Exception ex) when (!_cts.IsCancellationRequested)
        {
            _networkError = ex is EndOfStreamException or IOException ? $"The connection to {PartnerName} was lost." : $"Connection error: {ex.Message}";
        }
        catch (Exception)
        {
            // closing
        }
    }

    private void SendLoop()
    {
        try
        {
            foreach (var msg in _outbox.GetConsumingEnumerable())
            {
                var data = JsonSerializer.SerializeToUtf8Bytes(msg, TradeJson.Default.TradeMsg);
                _channel.SendAsync(data).GetAwaiter().GetResult();
            }
        }
        catch (Exception ex)
        {
            _networkError ??= ex is IOException or ObjectDisposedException ? $"The connection to {PartnerName} was lost." : $"Connection error: {ex.Message}";
        }
    }

    public void Dispose()
    {
        if (Phase != TradePhase.Closed)
            Leave();
        _cts.Cancel();
    }
}
