using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// Hosting a trade: listens on the trade port, announces itself on the LAN, and takes the first partner that proves the
/// share code. Wrong codes are counted; after <see cref="MaxWrongCodes"/> the host stops so the code can't be guessed.
/// </summary>
public sealed class TradeHost : IDisposable
{
    public const int MaxWrongCodes = 5;

    private readonly BankConfig _cfg;
    private readonly CancellationTokenSource _cts = new();
    private TcpListener? _listener;
    private int _wrong;

    public string Code { get; } = ShareCode.New();
    public int Port { get; }
    public volatile string Status = "Waiting for a partner...";
    public volatile bool Failed;

    /// <summary>The connected, verified partner. Set once, from a background thread.</summary>
    public volatile SecureChannel? Channel;

    public TradeHost(BankConfig cfg)
    {
        _cfg = cfg;
        Port = cfg.TradePort;
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
        _ = Task.Run(AcceptLoop);
        _ = Task.Run(AnnounceLoop);
    }

    private async Task AcceptLoop()
    {
        while (!_cts.IsCancellationRequested && Channel is null)
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
            var remote = (client.Client.RemoteEndPoint as IPEndPoint)?.Address.MapToIPv4().ToString() ?? "?";
            client.NoDelay = true;
            try
            {
                var ch = await SecureChannel.AcceptAsync(client.GetStream(), Code, remote, _cts.Token);
                Channel = ch;
                Status = $"Connected to {remote}";
                StopListening();
                return;
            }
            catch (WrongCodeException)
            {
                client.Dispose();
                _wrong++;
                if (_wrong >= MaxWrongCodes)
                {
                    Status = $"{_wrong} wrong codes were tried, so this trade room closed. Start a new one for a new code.";
                    Failed = true;
                    StopListening();
                    return;
                }
                Status = $"Someone at {remote} tried a wrong code ({_wrong} of {MaxWrongCodes}).";
            }
            catch (Exception ex) when (ex is not OperationCanceledException || !_cts.IsCancellationRequested)
            {
                client.Dispose();
                if (ex is TradeRefusedException r)
                    Status = r.Message;
            }
        }
    }

    /// <summary>Tells handhelds on the same network that a trade room is open (UDP broadcast on port + 1).</summary>
    private async Task AnnounceLoop()
    {
        using var udp = new UdpClient();
        udp.EnableBroadcast = true;
        var payload = JsonSerializer.SerializeToUtf8Bytes(new Announcement
        {
            App = "rocknixds-bank",
            Proto = SecureChannel.ProtocolVersion,
            Name = _cfg.EffectiveTrainerName,
            Port = Port,
        }, TradeJson.Default.Announcement);
        while (!_cts.IsCancellationRequested && Channel is null && !Failed)
        {
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
    public static async Task<SecureChannel> ConnectAsync(string host, int port, string code, CancellationToken ct = default)
    {
        var client = new TcpClient { NoDelay = true };
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
            return await SecureChannel.ConnectAsync(client.GetStream(), code, host, ct);
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }
}

/// <summary>A trade room's LAN announcement.</summary>
public sealed class Announcement
{
    public string App { get; set; } = "";
    public int Proto { get; set; }
    public string Name { get; set; } = "";
    public int Port { get; set; }
}

public sealed record FoundRoom(string Name, string Address, int Port, DateTime Seen);

/// <summary>Listens for trade rooms announced on the LAN.</summary>
public sealed class RoomFinder : IDisposable
{
    private readonly CancellationTokenSource _cts = new();
    private readonly Lock _lock = new();
    private readonly Dictionary<string, FoundRoom> _rooms = [];
    private UdpClient? _udp;

    public string? Error { get; private set; }

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
                    var a = JsonSerializer.Deserialize(r.Buffer, TradeJson.Default.Announcement);
                    if (a is null || a.App != "rocknixds-bank" || a.Port is <= 0 or > 65535)
                        continue;
                    var addr = r.RemoteEndPoint.Address.MapToIPv4().ToString();
                    lock (_lock)
                        _rooms[$"{addr}:{a.Port}"] = new FoundRoom(a.Name.Length > 24 ? a.Name[..24] : a.Name, addr, a.Port, DateTime.UtcNow);
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
                return _rooms.Values.Where(r => DateTime.UtcNow - r.Seen < TimeSpan.FromSeconds(4)).OrderBy(r => r.Name).ToList();
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
    /// <summary>This handheld's IPv4 addresses (Wi-Fi first), for the host screen.</summary>
    public static List<string> LocalAddresses()
    {
        var list = new List<(int Rank, string Ip)>();
        try
        {
            foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;
                int rank = ni.Name.StartsWith("wlan", StringComparison.Ordinal) ? 0 : ni.Name.StartsWith("eth", StringComparison.Ordinal) ? 1 : 2;
                foreach (var ua in ni.GetIPProperties().UnicastAddresses)
                {
                    if (ua.Address.AddressFamily == AddressFamily.InterNetwork)
                        list.Add((rank, ua.Address.ToString()));
                }
            }
        }
        catch (NetworkInformationException)
        {
            // no addresses
        }
        return list.OrderBy(x => x.Rank).Select(x => x.Ip).Distinct().ToList();
    }

    public static List<IPAddress> BroadcastAddresses()
    {
        var list = new List<IPAddress> { IPAddress.Broadcast };
        try
        {
            foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;
                foreach (var ua in ni.GetIPProperties().UnicastAddresses)
                {
                    if (ua.Address.AddressFamily != AddressFamily.InterNetwork || ua.IPv4Mask is null)
                        continue;
                    var ip = ua.Address.GetAddressBytes();
                    var mask = ua.IPv4Mask.GetAddressBytes();
                    var b = new byte[4];
                    for (int i = 0; i < 4; i++)
                        b[i] = (byte)(ip[i] | ~mask[i]);
                    list.Add(new IPAddress(b));
                }
            }
        }
        catch (NetworkInformationException)
        {
            // the global broadcast only
        }
        return list.DistinctBy(a => a.ToString()).ToList();
    }
}
