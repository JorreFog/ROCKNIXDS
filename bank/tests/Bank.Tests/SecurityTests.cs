using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using PKHeX.Core;
using Rocknixds.Bank.Trade;
using Xunit;

namespace Rocknixds.Bank.Tests;

[CollectionDefinition("network attacks", DisableParallelization = true)]
public class NetworkAttackCollection;

/// <summary>Attacks on a trade room and on a trade, from someone on the network or from a malicious partner.</summary>
[Collection("network attacks")]
public class SecurityTests
{
    private static BankConfig HostCfg()
    {
        var cfg = TradeTests.Cfg("Host");
        cfg.TradePort = 0; // any free port
        return cfg;
    }

    private static async Task WriteFrame(Stream s, byte[] payload)
    {
        var header = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(header, payload.Length);
        await s.WriteAsync(header);
        await s.WriteAsync(payload);
    }

    private static async Task<byte[]?> ReadFrame(Stream s)
    {
        var header = new byte[4];
        try
        {
            await s.ReadExactlyAsync(header);
        }
        catch (EndOfStreamException)
        {
            return null;
        }
        var payload = new byte[BinaryPrimitives.ReadInt32BigEndian(header)];
        await s.ReadExactlyAsync(payload);
        return payload;
    }

    /// <summary>A client's first handshake message, made with <paramref name="guess"/>.</summary>
    private static byte[] Hello(string guess) => JsonSerializer.SerializeToUtf8Bytes(new HandshakeMsg
    {
        T = "spake", App = "rocknixds-bank", Proto = SecureChannel.ProtocolVersion, Msg = new Spake2(true, guess).Message,
    }, TradeJson.Default.HandshakeMsg);

    private static async Task Until(Func<bool> done, int seconds = 10)
    {
        var until = DateTime.UtcNow.AddSeconds(seconds);
        while (!done() && DateTime.UtcNow < until)
            await Task.Delay(20);
    }

    [Fact]
    public async Task GuessersWhoGoQuietStillUseUpTheRoom()
    {
        // The host's key confirmation lets a client test one guess. Going quiet afterwards (instead of sending a wrong
        // confirmation or hanging up) must still count, or the 5-guess limit means nothing.
        var old = SecureChannel.HandshakeTimeout;
        SecureChannel.HandshakeTimeout = TimeSpan.FromMilliseconds(600);
        try
        {
            using var host = new TradeHost(HostCfg());
            host.Start();
            var wrong = host.Code == "AAAAAA" ? "CCCCCC" : "AAAAAA";
            for (int i = 0; i < TradeHost.MaxAttempts; i++)
            {
                using var c = new TcpClient();
                await c.ConnectAsync(IPAddress.Loopback, host.Port);
                var s = c.GetStream();
                await WriteFrame(s, Hello(wrong));
                var reply = JsonSerializer.Deserialize(await ReadFrame(s) ?? [], TradeJson.Default.HandshakeMsg);
                Assert.Equal("spake", reply!.T);           // the guess can be checked now...
                Assert.Null(await ReadFrame(s));            // ...and saying nothing ends in the host hanging up
            }
            await Until(() => host.Failed);
            Assert.True(host.Failed, host.Status);
            await Assert.ThrowsAnyAsync<Exception>(() => TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Code));
        }
        finally
        {
            SecureChannel.HandshakeTimeout = old;
        }
    }

    [Fact]
    public async Task IdleConnectionsDontKeepThePartnerOut()
    {
        using var host = new TradeHost(HostCfg());
        host.Start();
        // someone at another address opens connections and says nothing
        var idle = new List<TcpClient>();
        for (int i = 0; i < 3; i++)
        {
            var c = new TcpClient(new IPEndPoint(IPAddress.Parse("127.0.0.2"), 0));
            await c.ConnectAsync(IPAddress.Loopback, host.Port);
            idle.Add(c);
        }
        await Task.Delay(200);
        // the third is over the per-address limit: dropped straight away
        var buf = new byte[1];
        idle[2].ReceiveTimeout = 2000;
        Assert.Equal(0, await idle[2].GetStream().ReadAsync(buf));

        // the partner gets in anyway, long before the idle ones time out
        var sw = System.Diagnostics.Stopwatch.StartNew();
        using var ch = await TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Code);
        Assert.True(sw.Elapsed < TimeSpan.FromSeconds(3), sw.Elapsed.ToString());
        await Until(() => host.Channel is not null);
        Assert.NotNull(host.Channel);
        host.Channel!.Dispose();
        foreach (var c in idle)
            c.Dispose();
    }

    [Fact]
    public async Task AnOversizedFrameBeforeTheCodeIsProvenIsDropped()
    {
        using var host = new TradeHost(HostCfg());
        host.Start();
        using (var c = new TcpClient())
        {
            await c.ConnectAsync(IPAddress.Loopback, host.Port);
            var s = c.GetStream();
            var header = new byte[4];
            BinaryPrimitives.WriteInt32BigEndian(header, 1 << 20); // "a megabyte follows"
            await s.WriteAsync(header);
            Assert.Null(await ReadFrame(s)); // hung up on, without reading it
        }
        Assert.False(host.Failed); // and it cost no attempt: the partner still gets in
        using var ch = await TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Code);
        await Until(() => host.Channel is not null);
        host.Channel?.Dispose();
    }

    [Theory]
    [InlineData("192.168.1.23", true)]
    [InlineData("10.0.0.5", true)]
    [InlineData("172.20.1.1", true)]
    [InlineData("100.101.102.103", true)]   // Tailscale
    [InlineData("169.254.3.4", true)]
    [InlineData("127.0.0.1", true)]
    [InlineData("::ffff:192.168.0.9", true)]
    [InlineData("fe80::1", true)]
    [InlineData("fd12:3456::1", true)]
    [InlineData("8.8.8.8", false)]
    [InlineData("172.32.0.1", false)]
    [InlineData("100.128.0.1", false)]
    [InlineData("2001:db8::1", false)]
    [InlineData("::ffff:1.2.3.4", false)]
    public void OnlyTheLocalNetworkMayConnect(string ip, bool local) => Assert.Equal(local, AddressGuard.IsLocal(IPAddress.Parse(ip)));

    [Fact]
    public void PartnerTextIsOneShortPrintableLine()
    {
        Assert.Equal("Ash 2026-01-01 TRADE OUT x", TextGuard.Clean("Ash\n2026-01-01 TRADE OUT x", 60));
        Assert.Equal("evil txt.exe", TextGuard.Clean("evil‮txt.exe", 60));
        Assert.Equal("ab", TextGuard.Clean("a​b\u0000", 60).Replace(" ", ""));
        Assert.Equal(24, TextGuard.Clean(new string('x', 500), 24).Length);
        Assert.Equal("", TextGuard.Clean(null, 10));
    }

    [Fact]
    public void TradedNamesCantForgeLogLinesOrPaths()
    {
        using var tmp = new Fixtures.TempDir();
        var log = Path.Combine(tmp.Path, "history.log");
        new History(log).Log("TRADE IN Eevee OT EVIL\n2026-01-01 00:00:00 TRADE OUT everything");
        Assert.Single(File.ReadAllLines(log));
        Assert.Equal("_.._.._etc_passwd_x", FileUtil.Sanitize("/../../etc/passwd\nx"));
        Assert.Equal("_", FileUtil.Sanitize(".."));
    }

    private sealed class Store(PKM mine) : ITradeStorage
    {
        public PKM? Mine = mine;
        public readonly List<PKM> Received = [];
        public string StoreReceived(PKM pk) { Received.Add(pk); return "bank"; }
        public void RemoveOffered(TradeOffer offer) => Mine = null;
    }

    private static Task Send(SecureChannel ch, TradeMsg msg) =>
        ch.SendAsync(JsonSerializer.SerializeToUtf8Bytes(msg, TradeJson.Default.TradeMsg));

    private static void Pump(TradeSession s, ITradeStorage st, int ms)
    {
        var until = DateTime.UtcNow.AddMilliseconds(ms);
        while (DateTime.UtcNow < until)
        {
            s.Pump(st); // never throws, whatever arrives
            Thread.Sleep(5);
        }
    }

    [Fact]
    public async Task AHostilePartnerCantSpoofCrashOrForge()
    {
        var (hc, evil) = await TradeTests.Pair();
        var store = new Store(Fixtures.Make(Species.Pikachu, GameVersion.HG));
        using var host = TradeTests.Session(hc, TradeTests.Cfg("Host"), true);

        await Send(evil, new TradeMsg { T = "hello", Name = "Ash\nTRADE OUT everything‮" });
        await Send(evil, new TradeMsg { T = "hello", Name = "Somebody else" }); // a second hello is ignored
        Pump(host, store, 300);
        Assert.Equal("Ash TRADE OUT everything", host.PartnerName);

        // damaged data: a valid Pokémon with one byte changed, so its checksum no longer matches
        var good = PkmIO.ToFileBytes(Fixtures.Make(Species.Eevee, GameVersion.HG));
        var damaged = (byte[])good.Clone();
        damaged[0x40] ^= 0x5A;
        await Send(evil, new TradeMsg { T = "offer", Data = damaged, Ext = ".pk4" });
        Pump(host, store, 200);
        Assert.Null(host.Theirs);

        // a species that doesn't exist in Gen 4 (with a correct checksum)
        var glitch = Fixtures.Make(Species.Eevee, GameVersion.HG);
        glitch.Species = 600;
        glitch.RefreshChecksum();
        await Send(evil, new TradeMsg { T = "offer", Data = PkmIO.ToFileBytes(glitch), Ext = ".pk4" });
        // and noise of a plausible size
        await Send(evil, new TradeMsg { T = "offer", Data = Enumerable.Range(0, 236).Select(i => (byte)(i * 37)).ToArray(), Ext = "../../x" });
        Pump(host, store, 300);
        Assert.Null(host.Theirs);
        Assert.Contains(host.Log, l => l.Contains("refused", StringComparison.Ordinal));
        Assert.Equal(TradePhase.Open, host.Phase);

        await Send(evil, new TradeMsg { T = "bye", Text = "Your Pokémon were lost.\nCall 555-0100‮" + new string('!', 500) });
        Pump(host, store, 300);
        Assert.Equal(TradePhase.Closed, host.Phase);
        Assert.StartsWith("Ash TRADE OUT everything: ", host.CloseReason);
        Assert.DoesNotContain('\n', host.CloseReason!);
        Assert.True(host.CloseReason!.Length < 220);
        Assert.NotNull(store.Mine);
        evil.Dispose();
    }

    [Fact]
    public async Task AFloodOfMessagesClosesTheConnection()
    {
        var (hc, evil) = await TradeTests.Pair();
        var store = new Store(Fixtures.Make(Species.Pikachu, GameVersion.HG));
        using var host = TradeTests.Session(hc, TradeTests.Cfg("Host"), true);
        for (int i = 0; i < TradeSession.MaxQueued * 3; i++)
            await Send(evil, new TradeMsg { T = "ping" });
        await Task.Delay(500); // all of it arrives before the app's next frame
        host.Pump(store);
        Assert.Equal(TradePhase.Closed, host.Phase);
        Assert.Contains("too many messages", host.CloseReason);
        evil.Dispose();
    }

    [Fact]
    public async Task ASwappedOfferCantBeAcceptedRightAway()
    {
        var (hc, cc) = await TradeTests.Pair();
        var hostStore = new Store(Fixtures.Make(Species.Pikachu, GameVersion.HG));
        var joinStore = new Store(Fixtures.Make(Species.Eevee, GameVersion.HG));
        using var host = new TradeSession(hc, TradeTests.Cfg("Host"), true) { AcceptCooldown = TimeSpan.FromMilliseconds(800) };
        using var join = TradeTests.Session(cc, TradeTests.Cfg("Joiner"), false);
        var sides = new[] { (host, (ITradeStorage)hostStore), (join, (ITradeStorage)joinStore) };

        host.Offer(hostStore.Mine!, "A");
        join.Offer(joinStore.Mine!, "B");
        TradeTests.PumpUntil(() => host.Theirs?.Verdict is not null, sides);
        Thread.Sleep(900);
        // the joiner swaps to its rare one... no: to a different, worse Pokémon, just as the host reaches for START
        var worse = Fixtures.Make(Species.Magikarp, GameVersion.HG);
        join.Offer(worse, "C");
        TradeTests.PumpUntil(() => host.Theirs?.Summary.Species == (ushort)Species.Magikarp && host.Theirs.Verdict is not null, sides);
        Assert.True(host.AcceptWait > TimeSpan.Zero);
        Assert.False(host.Accept());
        Thread.Sleep(900);
        Assert.True(host.Accept()); // after a look at it, fine
    }

    [Fact]
    public async Task APartnerThatStallsTheExchangeLosesNothingForUs()
    {
        var (hc, evil) = await TradeTests.Pair();
        var mine = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        var store = new Store(mine);
        using var host = new TradeSession(hc, TradeTests.Cfg("Host"), true)
        {
            AcceptCooldown = TimeSpan.Zero,
            ExchangeTimeout = TimeSpan.FromSeconds(1),
        };
        var theirs = Fixtures.Make(Species.Eevee, GameVersion.HG);
        await Send(evil, new TradeMsg { T = "hello", Name = "Staller" });
        await Send(evil, new TradeMsg { T = "offer", Data = PkmIO.ToFileBytes(theirs), Ext = ".pk4" });
        host.Offer(mine, "A");
        TradeTests.PumpUntil(() => host.Theirs?.Verdict is not null, (host, store));
        Assert.True(host.Accept());
        await Send(evil, new TradeMsg { T = "accept", Mine = PkmIO.Hash(theirs), Theirs = PkmIO.Hash(mine) });
        TradeTests.PumpUntil(() => host.Phase == TradePhase.Exchanging, (host, store));

        // the partner keeps the line alive but never answers the commit
        var until = DateTime.UtcNow.AddSeconds(5);
        while (host.Phase != TradePhase.Closed && DateTime.UtcNow < until)
        {
            await Send(evil, new TradeMsg { T = "ping" });
            Pump(host, store, 100);
        }
        Assert.Equal(TradePhase.Closed, host.Phase);
        Assert.NotNull(store.Mine);      // still ours
        Assert.Empty(store.Received);    // and nothing half-done
        Assert.Contains("Nothing was traded", host.CloseReason);
        evil.Dispose();
    }
}
