using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// Hosting a trade: listens on the trade port, announces itself on the LAN (signed with this handheld's identity key),
/// and lets one partner in.
/// <list type="bullet">
/// <item>Rooms with a code (private rooms, lobbies with a code): every connection that gets the host's key
/// confirmation is one guess at the code, however it ends. An address gets <see cref="MaxWrongPerAddress"/> wrong
/// guesses, then the room ignores it; after <see cref="MaxWrongPerCode"/> wrong guesses in all, the code changes (the
/// host's screen shows the new one), so nobody can guess a code by trying many times, and nobody can close the room by
/// trying wrong codes either.</item>
/// <item>Open lobbies: the code is public, so the host decides. Someone who completes the handshake becomes a
/// <see cref="Pending"/> join request (its name, its handheld's key, a check number shown on both screens) until the
/// host lets them in or refuses; one request at a time; a refused address waits <see cref="RefusedCooldown"/>, a refused
/// handheld is refused for the rest of the lobby.</item>
/// <item>Handshakes run side by side (at most <see cref="MaxPerAddress"/> per address, <see cref="MaxConcurrent"/> in all,
/// <see cref="SecureChannel.HandshakeTimeout"/> each), so connections that say nothing can't keep the partner out.</item>
/// <item>Only addresses of the local network may connect, unless the settings allow any.</item>
/// </list>
/// </summary>
public sealed class TradeHost : IDisposable
{
    public const int MaxWrongPerAddress = 3;
    public const int MaxWrongPerCode = 20;
    public const int MaxConcurrent = 8;
    public const int MaxPerAddress = 2;
    public static readonly TimeSpan RefusedCooldown = TimeSpan.FromMinutes(2);
    public static TimeSpan RequestTimeout { get; internal set; } = TimeSpan.FromSeconds(60);

    private readonly BankConfig _cfg;
    private readonly DeviceIdentity _me;
    private readonly Func<string, LobbyListing>? _lobbyFor;
    private readonly CancellationTokenSource _cts = new();
    private readonly Lock _lock = new();
    private readonly Dictionary<string, int> _perAddress = [];
    private readonly Dictionary<string, int> _wrongByAddress = [];
    private readonly Dictionary<string, DateTime> _refusedAddresses = [];
    private readonly HashSet<string> _refusedKeys = [];
    private TcpListener? _listener;
    private int _attemptsThisCode, _wrongThisCode, _inFlight;
    private volatile string _code = ShareCode.New();
    private volatile LobbyListing? _lobby;

    public string Code => _code;
    public int Port { get; private set; }
    public volatile string Status = "Waiting for a partner...";
    public volatile bool Failed;

    /// <summary>How many times the code changed after too many wrong guesses (or on the host's request).</summary>
    public int CodeChanges { get; private set; }

    /// <summary>The partner that was let in. Set once, from a background thread.</summary>
    public volatile SecureChannel? Channel;

    /// <summary>An open lobby's join request waiting for the host's answer.</summary>
    public volatile JoinRequest? Pending;

    /// <summary>The lobby this room lists, or null for a private room.</summary>
    public LobbyListing? Lobby => _lobby;

    private bool IsOpenLobby => _lobby is { Open: true };

    public TradeHost(BankConfig cfg, DeviceIdentity me, Func<string, LobbyListing>? lobby = null)
    {
        _cfg = cfg;
        _me = me;
        _lobbyFor = lobby;
        Port = cfg.TradePort;
        _lobby = lobby?.Invoke(_code);
    }

    public void Start()
    {
        try
        {
            _listener = new TcpListener(IPAddress.IPv6Any, Port);
            _listener.Server.DualMode = true;
            _listener.Start();
        }
        catch (Exception)
        {
            try
            {
                _listener = new TcpListener(IPAddress.Any, Port);
                _listener.Start();
            }
            catch (SocketException ex)
            {
                Status = $"Can't listen on port {Port}: {ex.Message}";
                Failed = true;
                return;
            }
        }
        Port = ((IPEndPoint)_listener.LocalEndpoint).Port;
        _ = Task.Run(AcceptLoop);
        _ = Task.Run(AnnounceLoop);
    }

    private async Task AcceptLoop()
    {
        while (!_cts.IsCancellationRequested && Channel is null && !Failed)
        {
            TcpClient client;
            try
            {
                client = await _listener!.AcceptTcpClientAsync(_cts.Token);
            }
            catch (Exception)
            {
                return;
            }
            var ip = (client.Client.RemoteEndPoint as IPEndPoint)?.Address ?? IPAddress.None;
            var remote = (ip.IsIPv4MappedToIPv6 ? ip.MapToIPv4() : ip).ToString();
            if (!_cfg.TradeAllowAnyAddress && !AddressGuard.IsLocal(ip))
            {
                client.Dispose();
                Status = $"Refused a connection from {remote}: not on this network.";
                continue;
            }
            lock (_lock)
            {
                _perAddress.TryGetValue(remote, out var n);
                bool locked = (!IsOpenLobby && _wrongByAddress.GetValueOrDefault(remote) >= MaxWrongPerAddress)
                              || (_refusedAddresses.TryGetValue(remote, out var until) && until > DateTime.UtcNow);
                if (locked || _inFlight >= MaxConcurrent || n >= MaxPerAddress)
                {
                    client.Dispose(); // locked out, or busy: a partner tries again, a flood gets nowhere
                    continue;
                }
                _perAddress[remote] = n + 1;
                _inFlight++;
            }
            client.NoDelay = true;
            _ = Task.Run(() => Handshake(client, remote));
        }
    }

    private bool ReserveAttempt(string remote)
    {
        lock (_lock)
        {
            if (Failed || Channel is not null)
                return false;
            if (IsOpenLobby)
                return true; // nothing secret to guess: the host decides who comes in
            if (_wrongByAddress.GetValueOrDefault(remote) >= MaxWrongPerAddress || _attemptsThisCode >= MaxWrongPerCode + MaxConcurrent)
                return false;
            _attemptsThisCode++;
            return true;
        }
    }

    private async Task Handshake(TcpClient client, string remote)
    {
        try
        {
            var code = _code;
            var ch = await SecureChannel.AcceptAsync(client.GetStream(), code, remote, _me, _cts.Token, () => ReserveAttempt(remote));
            if (IsOpenLobby)
                await Request(ch, remote);
            else
                await Admit(ch, remote);
        }
        catch (WrongCodeException)
        {
            client.Dispose();
            if (IsOpenLobby)
            {
                Status = $"Someone at {remote} couldn't complete the connection.";
                return;
            }
            int byAddress;
            bool changed = false;
            lock (_lock)
            {
                byAddress = _wrongByAddress[remote] = _wrongByAddress.GetValueOrDefault(remote) + 1;
                if (++_wrongThisCode >= MaxWrongPerCode)
                {
                    NewCodeLocked();
                    changed = true;
                }
            }
            Status = changed
                ? "Too many wrong codes were tried, so the code changed: give your partner the new one."
                : byAddress >= MaxWrongPerAddress
                    ? $"{remote} tried {byAddress} wrong codes and can't try again in this room."
                    : $"Someone at {remote} tried a wrong code ({byAddress} of {MaxWrongPerAddress} from there).";
        }
        catch (Exception ex)
        {
            client.Dispose();
            if (ex is TradeRefusedException r && !_cts.IsCancellationRequested)
                Status = r.Message;
        }
        finally
        {
            lock (_lock)
            {
                _inFlight--;
                if (_perAddress.TryGetValue(remote, out var n) && n > 1)
                    _perAddress[remote] = n - 1;
                else
                    _perAddress.Remove(remote);
            }
        }
    }

    /// <summary>Changes the code (the host's choice, or after too many wrong guesses); a lobby's listing follows.</summary>
    public void NewCode()
    {
        lock (_lock)
            NewCodeLocked();
        Status = "New code: waiting for a partner...";
    }

    private void NewCodeLocked()
    {
        _code = ShareCode.New();
        _attemptsThisCode = _wrongThisCode = 0;
        CodeChanges++;
        if (_lobbyFor is not null)
            _lobby = _lobbyFor(_code);
    }

    private async Task Admit(SecureChannel ch, string remote)
    {
        bool first;
        lock (_lock)
        {
            first = Channel is null && !Failed;
            if (first)
                Channel = ch;
        }
        if (!first)
        {
            await Answer(ch, false, "The host is already trading with someone.");
            ch.Dispose();
            return;
        }
        await Answer(ch, true, null);
        Status = $"Connected to {remote}";
        _cts.Cancel(); // the other handshakes in progress, and the announcements
        StopListening();
    }

    private async Task Request(SecureChannel ch, string remote)
    {
        var key = Fingerprint.Full(ch.PeerKey);
        JoinRequest? req = null;
        string? busy = null;
        lock (_lock)
        {
            if (_refusedKeys.Contains(key))
                busy = "The host said no.";
            else if (Pending is not null || Channel is not null)
                busy = "The host is answering someone else: try again in a moment.";
            else
                Pending = req = new JoinRequest(this, ch, remote, ch.PeerName.Length > 0 ? ch.PeerName : remote);
        }
        if (req is null)
        {
            await Answer(ch, false, busy);
            ch.Dispose();
            return;
        }
        Status = $"{req.Name} wants to join.";
        try
        {
            await Task.Delay(RequestTimeout, _cts.Token);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (Pending == req)
            await Decide(req, false, "The host didn't answer.", block: false);
    }

    internal async Task Decide(JoinRequest req, bool letIn, string? why, bool block = true)
    {
        lock (_lock)
        {
            if (Pending != req)
                return;
            Pending = null;
            if (!letIn && block)
            {
                _refusedAddresses[req.Address] = DateTime.UtcNow + RefusedCooldown;
                _refusedKeys.Add(Fingerprint.Full(req.Key));
            }
        }
        if (letIn)
        {
            await Admit(req.Channel, req.Address);
            return;
        }
        await Answer(req.Channel, false, why);
        req.Channel.Dispose();
        Status = block ? $"You refused {req.Name}. Waiting for a partner..." : "Waiting for a partner...";
    }

    private static async Task Answer(SecureChannel ch, bool admit, string? why)
    {
        try
        {
            await ch.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new TradeMsg { T = admit ? "admit" : "refuse", Text = why },
                TradeJson.Default.TradeMsg));
        }
        catch (Exception)
        {
            // they hung up
        }
    }

    /// <summary>Tells handhelds on the same network that a trade room is open (UDP broadcast on port + 1).</summary>
    private async Task AnnounceLoop()
    {
        using var udp = new UdpClient();
        udp.EnableBroadcast = true;
        while (!_cts.IsCancellationRequested && Channel is null && !Failed)
        {
            // made again each time: a new code changes an open lobby's listing
            var payload = Announcement.Make(_me, TextGuard.Clean(_cfg.EffectiveTrainerName, 24), Port, Lobby);
            foreach (var target in Network.BroadcastAddresses())
            {
                try { await udp.SendAsync(payload, new IPEndPoint(target, Port + 1)); }
                catch (SocketException) { /* no network yet */ }
            }
            try { await Task.Delay(1000, _cts.Token); }
            catch (OperationCanceledException) { return; }
        }
    }

    private void StopListening()
    {
        try { _listener?.Stop(); } catch (SocketException) { /* already */ }
    }

    public void Dispose()
    {
        _cts.Cancel();
        StopListening();
    }
}

/// <summary>Joining a trade.</summary>
public static class TradeClient
{
    /// <summary>
    /// Connects and proves the code. <paramref name="expectHost"/>: the key a lobby's listing was signed with; the host
    /// must hold it (<see cref="ImpostorException"/> otherwise). Then call <see cref="WaitForAdmissionAsync"/>.
    /// </summary>
    public static Task<SecureChannel> ConnectAsync(string host, int port, string code, DeviceIdentity me, string myName,
        byte[]? expectHost = null, CancellationToken ct = default) => ConnectFromAsync(null, host, port, code, me, myName, expectHost, ct);

    /// <summary>From a given local address (the tests play several handhelds on 127.0.0.x).</summary>
    internal static async Task<SecureChannel> ConnectFromAsync(IPAddress? local, string host, int port, string code, DeviceIdentity me,
        string myName, byte[]? expectHost = null, CancellationToken ct = default)
    {
        var client = local is null ? new TcpClient() : new TcpClient(new IPEndPoint(local, 0));
        client.NoDelay = true;
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(TimeSpan.FromSeconds(8));
            try
            {
                await client.ConnectAsync(host, port, timeout.Token);
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                throw new IOException($"No answer from {host}:{port}. Is the trade room open, and are both on the same network?");
            }
            catch (SocketException ex)
            {
                throw new IOException($"Can't reach {host}:{port} ({ex.SocketErrorCode}).");
            }
            return await SecureChannel.ConnectAsync(client.GetStream(), code, host, me, myName, expectHost, ct);
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    /// <summary>Waits for the host to let us in (at once for a room with a code; an open lobby's host decides).</summary>
    public static async Task WaitForAdmissionAsync(SecureChannel ch, CancellationToken ct = default)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(TradeHost.RequestTimeout + TimeSpan.FromSeconds(15));
        TradeMsg? msg;
        try
        {
            msg = JsonSerializer.Deserialize(await ch.ReceiveAsync(timeout.Token), TradeJson.Default.TradeMsg);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new TradeRefusedException("The host didn't answer.");
        }
        catch (Exception ex) when (ex is IOException or EndOfStreamException or System.Security.Cryptography.CryptographicException)
        {
            throw new TradeRefusedException("The host closed the connection.");
        }
        if (msg?.T == "admit")
            return;
        var why = TextGuard.Clean(msg?.Text, 120);
        throw new TradeRefusedException(why.Length > 0 ? why : "The host said no.");
    }
}

/// <summary>Someone who wants to join an open lobby, waiting for its host's answer.</summary>
public sealed class JoinRequest(TradeHost host, SecureChannel channel, string address, string name)
{
    internal SecureChannel Channel { get; } = channel;
    public string Address { get; } = address;
    public string Name { get; } = TextGuard.Clean(name, 24);
    public byte[] Key => Channel.PeerKey;
    public string Id => Fingerprint.Short(Channel.PeerKey);
    /// <summary>Shown on both screens: if the joiner's screen shows another number, someone is in between.</summary>
    public string CheckNumber => Channel.CheckNumber;

    public Task LetIn() => host.Decide(this, true, null);
    public Task Refuse() => host.Decide(this, false, "The host said no.");
}

/// <summary>A trade room's LAN announcement.</summary>
public sealed class Announcement
{
    public string App { get; set; } = "";
    public int Proto { get; set; }
    public string Name { get; set; } = "";
    public int Port { get; set; }
    /// <summary>A lobby's listing; null for a private room.</summary>
    public LobbyListing? Lobby { get; set; }
    /// <summary>The announcing handheld's identity key, and its signature over everything else.</summary>
    public byte[]? Id { get; set; }
    public byte[]? Sig { get; set; }

    public static byte[] Make(DeviceIdentity me, string name, int port, LobbyListing? lobby)
    {
        var a = new Announcement { App = "rocknixds-bank", Proto = SecureChannel.ProtocolVersion, Name = name, Port = port, Lobby = lobby, Id = me.PublicKey };
        a.Sig = me.Sign(a.Unsigned());
        return JsonSerializer.SerializeToUtf8Bytes(a, TradeJson.Default.Announcement);
    }

    /// <summary>The bytes the signature covers: the announcement as sent, without its signature.</summary>
    internal byte[] Unsigned()
    {
        var sig = Sig;
        Sig = null;
        try { return JsonSerializer.SerializeToUtf8Bytes(this, TradeJson.Default.Announcement); }
        finally { Sig = sig; }
    }

    /// <summary>Signed by the key it carries. Anyone can sign with a key of their own: what this proves is that the
    /// listing comes whole from the holder of that key, and joining then checks the host holds it.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsSigned => Id is not null && DeviceIdentity.Verify(Id, Unsigned(), Sig);
}

/// <summary>A room heard on the network; <see cref="Lobby"/> when it's a lobby with a listing. <see cref="Key"/>: the
/// handheld's identity key, which it must prove it holds when someone joins.</summary>
public sealed record FoundRoom(string Name, string Address, int Port, DateTime Seen, LobbyListing? Lobby, byte[] Key)
{
    public string Id => Fingerprint.Short(Key);

    /// <summary>The same key announced from more than one address: all but one are copies.</summary>
    public bool Imitated { get; init; }
}

/// <summary>Listens for trade rooms announced on the LAN.</summary>
public sealed class RoomFinder : IDisposable
{
    private readonly CancellationTokenSource _cts = new();
    private readonly Lock _lock = new();
    private readonly Dictionary<string, FoundRoom> _rooms = [];
    private UdpClient? _udp;

    public const int MaxRooms = 16;

    public string? Error { get; private set; }

    private void Prune()
    {
        foreach (var k in _rooms.Where(kv => DateTime.UtcNow - kv.Value.Seen > TimeSpan.FromSeconds(4)).Select(kv => kv.Key).ToList())
            _rooms.Remove(k);
    }

    public void Start(int tradePort)
    {
        try
        {
            _udp = new UdpClient();
            _udp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            _udp.Client.Bind(new IPEndPoint(IPAddress.Any, tradePort + 1));
        }
        catch (SocketException ex)
        {
            Error = $"Can't look for trade rooms: {ex.Message}";
            return;
        }
        _ = Task.Run(async () =>
        {
            while (!_cts.IsCancellationRequested)
            {
                try
                {
                    var r = await _udp.ReceiveAsync(_cts.Token);
                    if (r.Buffer.Length > 1024)
                        continue; // an announcement with a lobby's listing is under 300 bytes
                    var a = JsonSerializer.Deserialize(r.Buffer, TradeJson.Default.Announcement);
                    if (a is null || a.App != "rocknixds-bank" || a.Proto != SecureChannel.ProtocolVersion || a.Port is < 1024 or > 65535)
                        continue;
                    if (!a.IsSigned)
                        continue; // unsigned or altered on the way: not listed
                    var name = TextGuard.Clean(a.Name, 24);
                    var addr = r.RemoteEndPoint.Address.MapToIPv4().ToString();
                    lock (_lock)
                    {
                        Prune();
                        var key = $"{addr}:{a.Port}";
                        // a few rooms at a time, two per address: a flood of made-up announcements can't fill memory or the list
                        if (!_rooms.ContainsKey(key) && (_rooms.Count >= MaxRooms || _rooms.Values.Count(x => x.Address == addr) >= 2))
                            continue;
                        _rooms[key] = new FoundRoom(name.Length > 0 ? name : addr, addr, a.Port, DateTime.UtcNow, LobbyListing.Validate(a.Lobby), a.Id!);
                    }
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                catch (Exception)
                {
                    // a stray packet
                }
            }
        });
    }

    /// <summary>Rooms heard from in the last 4 seconds.</summary>
    public List<FoundRoom> Rooms
    {
        get
        {
            lock (_lock)
            {
                var live = _rooms.Values.Where(r => DateTime.UtcNow - r.Seen < TimeSpan.FromSeconds(4)).ToList();
                var copies = live.GroupBy(r => r.Id).Where(g => g.Select(r => r.Address).Distinct().Count() > 1).Select(g => g.Key).ToHashSet();
                return live.Select(r => copies.Contains(r.Id) ? r with { Imitated = true } : r).OrderBy(r => r.Name).ToList();
            }
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        _udp?.Dispose();
    }
}

public static class Network
{
    private static readonly Lock CacheLock = new();
    private static (DateTime At, List<string> Local, List<IPAddress> Broadcast)? _cache;

    /// <summary>The interfaces, read at most every few seconds: the screens ask every frame, and asking the kernel each
    /// time costs battery. Any failure reading them means "no network", never a crash.</summary>
    private static (List<string> Local, List<IPAddress> Broadcast) Interfaces()
    {
        lock (CacheLock)
        {
            if (_cache is { } c && DateTime.UtcNow - c.At < TimeSpan.FromSeconds(3))
                return (c.Local, c.Broadcast);
            var local = new List<(int Rank, string Ip)>();
            var broadcast = new List<IPAddress> { IPAddress.Broadcast };
            try
            {
                foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
                {
                    if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                        continue;
                    int rank = ni.Name.StartsWith("wlan", StringComparison.Ordinal) ? 0 : ni.Name.StartsWith("eth", StringComparison.Ordinal) ? 1 : 2;
                    foreach (var ua in ni.GetIPProperties().UnicastAddresses)
                    {
                        if (ua.Address.AddressFamily != AddressFamily.InterNetwork)
                            continue;
                        local.Add((rank, ua.Address.ToString()));
                        if (ua.IPv4Mask is null)
                            continue;
                        var ip = ua.Address.GetAddressBytes();
                        var mask = ua.IPv4Mask.GetAddressBytes();
                        var b = new byte[4];
                        for (int i = 0; i < 4; i++)
                            b[i] = (byte)(ip[i] | ~mask[i]);
                        broadcast.Add(new IPAddress(b));
                    }
                }
            }
            catch (Exception)
            {
                // no interfaces we can read: no network
            }
            var l = local.OrderBy(x => x.Rank).Select(x => x.Ip).Distinct().ToList();
            var bc = broadcast.DistinctBy(x => x.ToString()).ToList();
            _cache = (DateTime.UtcNow, l, bc);
            return (l, bc);
        }
    }

    /// <summary>This handheld's IPv4 addresses (Wi-Fi first), for the host screen.</summary>
    public static List<string> LocalAddresses() => [.. Interfaces().Local];

    public static List<IPAddress> BroadcastAddresses() => [.. Interfaces().Broadcast];
}
