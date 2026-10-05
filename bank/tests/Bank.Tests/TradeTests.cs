using System.Net;
using System.Net.Sockets;
using PKHeX.Core;
using Rocknixds.Bank.Trade;
using Xunit;

namespace Rocknixds.Bank.Tests;

public class Spake2Tests
{
    [Fact]
    public void SameCodeSameKey()
    {
        var c = new Spake2(true, "K7P-2QX");
        var s = new Spake2(false, "k7p2qx");
        Assert.Equal(c.Finish(s.Message), s.Finish(c.Message));
    }

    [Fact]
    public void DifferentCodeDifferentKey()
    {
        var c = new Spake2(true, "K7P2QX");
        var s = new Spake2(false, "K7P2QY");
        Assert.NotEqual(c.Finish(s.Message), s.Finish(c.Message));
    }

    [Fact]
    public void FreshKeysEverySession()
    {
        var c1 = new Spake2(true, "AAAAAA");
        var s1 = new Spake2(false, "AAAAAA");
        var c2 = new Spake2(true, "AAAAAA");
        var s2 = new Spake2(false, "AAAAAA");
        Assert.NotEqual(c1.Message, c2.Message);
        Assert.NotEqual(c1.Finish(s1.Message), c2.Finish(s2.Message));
    }

    [Fact]
    public void RejectsElementsOutsideTheGroup()
    {
        var c = new Spake2(true, "AAAAAA");
        var one = new byte[Spake2.ElementBytes];
        one[^1] = 1;
        Assert.ThrowsAny<System.Security.Cryptography.CryptographicException>(() => c.Finish(one));
        Assert.ThrowsAny<System.Security.Cryptography.CryptographicException>(() => c.Finish(new byte[10]));
    }

    [Fact]
    public void CodesUseTheUnambiguousAlphabet()
    {
        for (int i = 0; i < 200; i++)
        {
            var code = ShareCode.New();
            Assert.True(ShareCode.IsComplete(code));
            Assert.DoesNotContain('0', code);
            Assert.DoesNotContain('O', code);
            Assert.DoesNotContain('1', code);
            Assert.DoesNotContain('I', code);
            Assert.DoesNotContain('S', code);
            Assert.DoesNotContain('Z', code);
            Assert.DoesNotContain('B', code);
        }
        Assert.Equal("ABC-DEF", ShareCode.Pretty("abc def"));
        Assert.False(ShareCode.IsComplete("ABCDE"));
    }
}

/// <summary>A bank in memory, standing in for the app's storage.</summary>
internal sealed class MemoryStorage(PKM mine) : ITradeStorage
{
    public PKM? Mine = mine;
    public List<PKM> Received = [];

    public string StoreReceived(PKM pk)
    {
        Received.Add(pk);
        return "the bank";
    }

    public void RemoveOffered(TradeOffer offer)
    {
        Assert.Equal(PkmIO.Hash(Mine!), offer.Hash);
        Mine = null;
    }
}

public class TradeTests
{
    private static async Task<(SecureChannel Host, SecureChannel Client)> Pair(string hostCode = "ABCDEF", string clientCode = "ABCDEF")
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var accept = Task.Run(async () =>
        {
            var c = await listener.AcceptTcpClientAsync();
            return await SecureChannel.AcceptAsync(c.GetStream(), hostCode, "test");
        });
        try
        {
            var client = await TradeClient.ConnectAsync("127.0.0.1", port, clientCode);
            return (await accept, client);
        }
        finally
        {
            listener.Stop();
        }
    }

    [Fact]
    public async Task WrongCodeIsRejectedOnBothSides()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var accept = Task.Run(async () =>
        {
            var c = await listener.AcceptTcpClientAsync();
            return await SecureChannel.AcceptAsync(c.GetStream(), "ABCDEF", "test");
        });
        await Assert.ThrowsAsync<WrongCodeException>(() => TradeClient.ConnectAsync("127.0.0.1", port, "ABCDEG"));
        await Assert.ThrowsAsync<WrongCodeException>(() => accept);
        listener.Stop();
    }

    [Fact]
    public async Task ChannelCarriesMessagesBothWays()
    {
        var (host, client) = await Pair();
        await client.SendAsync("hello"u8.ToArray());
        await host.SendAsync("hi"u8.ToArray());
        await client.SendAsync("again"u8.ToArray());
        Assert.Equal("hello"u8.ToArray(), await host.ReceiveAsync());
        Assert.Equal("again"u8.ToArray(), await host.ReceiveAsync());
        Assert.Equal("hi"u8.ToArray(), await client.ReceiveAsync());
        host.Dispose();
        client.Dispose();
    }

    private static void PumpUntil(Func<bool> done, params (TradeSession S, ITradeStorage St)[] sides)
    {
        var until = DateTime.UtcNow.AddSeconds(30);
        while (!done())
        {
            if (DateTime.UtcNow > until)
                throw new TimeoutException("the trade didn't get there");
            foreach (var (s, st) in sides)
                s.Pump(st);
            Thread.Sleep(5);
        }
    }

    private static BankConfig Cfg(string name)
    {
        var cfg = new BankConfig { TrainerName = name, DataFolder = "/nonexistent" };
        cfg.FillDefaults();
        return cfg;
    }

    [Fact]
    public async Task AFullTrade()
    {
        var (hc, cc) = await Pair();
        var kadabra = Fixtures.Make(Species.Kadabra, GameVersion.HG, "HOST", 11111, 22222);
        var machop = Fixtures.Make(Species.Machop, GameVersion.Pt, "JOINER", 33333, 44444);
        var hostStore = new MemoryStorage(kadabra);
        var joinStore = new MemoryStorage(machop);
        using var host = new TradeSession(hc, Cfg("Host"), isHost: true);
        using var join = new TradeSession(cc, Cfg("Joiner"), isHost: false);
        var sides = new[] { (host, (ITradeStorage)hostStore), (join, (ITradeStorage)joinStore) };

        PumpUntil(() => host.PartnerName == "Joiner" && join.PartnerName == "Host", sides);
        host.Offer(kadabra, "slot A");
        join.Offer(machop, "slot B");
        PumpUntil(() => host.Theirs?.Verdict is not null && join.Theirs?.Verdict is not null
                        && host.Mine?.PartnerSaysValid is not null && join.Mine?.PartnerSaysValid is not null, sides);
        Assert.True(host.Theirs!.Verdict!.Valid);
        Assert.True(join.Theirs!.Verdict!.Valid);
        Assert.True(host.Mine!.PartnerSaysValid);

        host.Accept();
        join.Accept();
        PumpUntil(() => host.Result is not null && join.Result is not null, sides);

        Assert.Null(hostStore.Mine);
        Assert.Null(joinStore.Mine);
        Assert.Equal((ushort)Species.Machop, Assert.Single(hostStore.Received).Species);
        // Kadabra evolves when it arrives
        Assert.Equal((ushort)Species.Alakazam, Assert.Single(joinStore.Received).Species);
        Assert.Equal("Alakazam", join.Result!.EvolvedInto);
        Assert.Equal("Kadabra", join.Result.Arrived.SpeciesName);
        Assert.Equal("Alakazam", join.Result.Got.SpeciesName);
        var alakazam = joinStore.Received[0];
        Assert.True(Legality.Check(alakazam).Valid, Legality.Check(alakazam).Report);
        Assert.Null(join.Result.Problem);
        Assert.Equal(TradePhase.Open, host.Phase);
        Assert.Null(host.Mine);
    }

    [Fact]
    public async Task ChangingAnOfferResetsTheAccepts()
    {
        var (hc, cc) = await Pair();
        var a = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        var b = Fixtures.Make(Species.Eevee, GameVersion.HG);
        var c = Fixtures.Make(Species.Vulpix, GameVersion.HG);
        var hostStore = new MemoryStorage(a);
        var joinStore = new MemoryStorage(b);
        using var host = new TradeSession(hc, Cfg("Host"), true);
        using var join = new TradeSession(cc, Cfg("Joiner"), false);
        var sides = new[] { (host, (ITradeStorage)hostStore), (join, (ITradeStorage)joinStore) };

        host.Offer(a, "A");
        join.Offer(b, "B");
        PumpUntil(() => host.Theirs is not null && join.Theirs is not null, sides);
        join.Accept();
        PumpUntil(() => host.TheyAccepted, sides);
        join.Offer(c, "C"); // changes its mind before the host accepts
        joinStore.Mine = c;
        PumpUntil(() => host.Theirs?.Summary.Species == (ushort)Species.Vulpix, sides);
        Assert.False(host.TheyAccepted);
        Assert.False(join.IAccepted);

        host.Accept();
        for (int i = 0; i < 40; i++)
        {
            host.Pump(hostStore);
            join.Pump(joinStore);
            Thread.Sleep(5);
        }
        Assert.Null(host.Result); // nothing traded: the joiner hasn't accepted Vulpix
        Assert.Empty(hostStore.Received);
        Assert.Empty(joinStore.Received);
    }

    [Fact]
    public async Task AnUnacceptThatCrossesTheCommitAborts()
    {
        var (hc, cc) = await Pair();
        var a = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        var b = Fixtures.Make(Species.Eevee, GameVersion.HG);
        var hostStore = new MemoryStorage(a);
        var joinStore = new MemoryStorage(b);
        using var host = new TradeSession(hc, Cfg("Host"), true);
        using var join = new TradeSession(cc, Cfg("Joiner"), false);
        var sides = new[] { (host, (ITradeStorage)hostStore), (join, (ITradeStorage)joinStore) };

        host.Offer(a, "A");
        join.Offer(b, "B");
        PumpUntil(() => host.Theirs is not null && join.Theirs is not null, sides);
        host.Accept();
        join.Accept();
        PumpUntil(() => host.TheyAccepted, sides);
        join.Unaccept();     // the joiner's unaccept is on its way...
        host.Pump(hostStore); // ...while the host commits
        Assert.Equal(TradePhase.Exchanging, host.Phase);
        PumpUntil(() => host.Phase == TradePhase.Open, sides);

        Assert.Empty(hostStore.Received);
        Assert.Empty(joinStore.Received);
        Assert.NotNull(hostStore.Mine);
        Assert.NotNull(joinStore.Mine);
    }

    [Fact]
    public async Task ADroppedConnectionNeverLosesAPokemon()
    {
        var (hc, cc) = await Pair();
        var a = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        var b = Fixtures.Make(Species.Eevee, GameVersion.HG);
        var hostStore = new MemoryStorage(a);
        var joinStore = new MemoryStorage(b);
        using var host = new TradeSession(hc, Cfg("Host"), true);
        using var join = new TradeSession(cc, Cfg("Joiner"), false);
        var sides = new[] { (host, (ITradeStorage)hostStore), (join, (ITradeStorage)joinStore) };

        host.Offer(a, "A");
        join.Offer(b, "B");
        PumpUntil(() => host.Theirs is not null && join.Theirs is not null, sides);
        host.Accept();
        join.Accept();
        // run the joiner until it has stored the host's Pokémon, then cut the line before the host hears about it
        PumpUntil(() => join.Phase == TradePhase.Exchanging, sides);
        PumpUntil(() => joinStore.Received.Count == 1, (join, joinStore));
        hc.Dispose();
        PumpUntil(() => host.Phase == TradePhase.Closed && join.Phase == TradePhase.Closed, sides);

        // Whether the host heard the joiner's "received" before the line went is up to the network threads. Either
        // way each Pokémon is still somewhere: Pikachu with the host or (also) with the joiner, Eevee with the joiner.
        Assert.True(hostStore.Mine is not null || joinStore.Received.Any(p => p.Species == (ushort)Species.Pikachu));
        Assert.NotNull(joinStore.Mine); // the joiner never heard that the host has Eevee, so it kept it
        Assert.Single(joinStore.Received);
        Assert.Contains("dropped", join.CloseReason);
    }

    [Fact]
    public async Task HostClosesAfterTooManyWrongCodes()
    {
        var cfg = Cfg("Host");
        cfg.TradePort = 47000 + Random.Shared.Next(1000);
        using var host = new TradeHost(cfg);
        host.Start();
        Assert.False(host.Failed, host.Status);
        for (int i = 0; i < TradeHost.MaxWrongCodes; i++)
        {
            var wrong = host.Code == "AAAAAA" ? "BBBBBB" : "AAAAAA";
            await Assert.ThrowsAnyAsync<Exception>(() => TradeClient.ConnectAsync("127.0.0.1", host.Port, wrong));
        }
        var until = DateTime.UtcNow.AddSeconds(10);
        while (!host.Failed && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.True(host.Failed);
        await Assert.ThrowsAnyAsync<Exception>(() => TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Code));
    }

    [Fact]
    public async Task RoomsAreFoundOnTheNetwork()
    {
        if (Network.BroadcastAddresses().Count < 2)
            return; // no network interface with a broadcast address here: nothing to find rooms on
        var cfg = Cfg("Room host");
        cfg.TradePort = 46000 + Random.Shared.Next(1000);
        using var finder = new RoomFinder();
        finder.Start(cfg.TradePort);
        using var host = new TradeHost(cfg);
        host.Start();
        var until = DateTime.UtcNow.AddSeconds(8);
        while (finder.Rooms.Count == 0 && DateTime.UtcNow < until)
            await Task.Delay(50);
        var room = Assert.Single(finder.Rooms);
        Assert.Equal("Room host", room.Name);
        Assert.Equal(cfg.TradePort, room.Port);
    }

    [Fact]
    public async Task HostAcceptsTheRightCode()
    {
        var cfg = Cfg("Host");
        cfg.TradePort = 48000 + Random.Shared.Next(1000);
        using var host = new TradeHost(cfg);
        host.Start();
        using var ch = await TradeClient.ConnectAsync("127.0.0.1", host.Port, ShareCode.Pretty(host.Code).ToLowerInvariant());
        var until = DateTime.UtcNow.AddSeconds(10);
        while (host.Channel is null && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.NotNull(host.Channel);
        host.Channel!.Dispose();
    }
}
