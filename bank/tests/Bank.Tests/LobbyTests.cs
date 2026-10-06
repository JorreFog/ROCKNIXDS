using System.Text.Json;
using PKHeX.Core;
using Rocknixds.Bank.Trade;
using Xunit;

namespace Rocknixds.Bank.Tests;

[Collection("network attacks")]
public class LobbyTests
{
    private static LobbyListing Good() => new()
    {
        Species = (ushort)Species.Kadabra, Level = 30, Format = 4, Gender = 0, Want = (ushort)Species.Mareep, Open = true, Code = "acd-efg",
    };

    [Fact]
    public void ListingsFromTheNetworkAreChecked()
    {
        var ok = LobbyListing.Validate(Good());
        Assert.NotNull(ok);
        Assert.Equal("ACDEFG", ok.Code); // normalised

        LobbyListing Bad(Action<LobbyListing> change)
        {
            var l = Good();
            change(l);
            return l;
        }
        Assert.Null(LobbyListing.Validate(Bad(l => l.Species = 0)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Species = 60000)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Want = 60000)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Level = 0)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Level = 250)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Format = 0)));
        Assert.Null(LobbyListing.Validate(Bad(l => l.Code = null)));       // open, but no code to join with
        Assert.Null(LobbyListing.Validate(Bad(l => l.Code = "ABC")));
        var closed = LobbyListing.Validate(Bad(l => l.Open = false));
        Assert.NotNull(closed);
        Assert.Null(closed.Code); // whatever a closed listing carries, no code is taken from it
    }

    [Fact]
    public void AClosedLobbyNeverBroadcastsItsCode()
    {
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        var closed = LobbyListing.For(pk, (ushort)Species.Mareep, open: false, code: "ACDEFG");
        var json = JsonSerializer.Serialize(new Announcement { App = "rocknixds-bank", Lobby = closed }, TradeJson.Default.Announcement);
        Assert.DoesNotContain("ACDEFG", json);
        var open = LobbyListing.For(pk, (ushort)Species.Mareep, open: true, code: "ACDEFG");
        Assert.Equal("ACDEFG", open.Code);
    }

    [Fact]
    public void WhatTheLobbyWantsAndWhatItListed()
    {
        var kadabra = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        var mareep = Fixtures.Make(Species.Mareep, GameVersion.HG);
        var l = LobbyListing.For(kadabra, (ushort)Species.Mareep, true, "ACDEFG");
        Assert.True(l.Wants(mareep));
        Assert.False(l.Wants(kadabra));
        Assert.True(l.Advertises(kadabra));
        Assert.False(l.Advertises(mareep));
        var any = LobbyListing.For(kadabra, 0, true, "ACDEFG");
        Assert.True(any.Wants(kadabra));
        Assert.Equal("any Pokémon", any.WantName);
    }

    [Fact]
    public async Task AnOpenLobbyIsntClosedByFailedConnections()
    {
        var cfg = TradeTests.Cfg("Lobby host");
        cfg.TradePort = 0;
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        using var host = new TradeHost(cfg, code => LobbyListing.For(pk, (ushort)Species.Mareep, true, code));
        host.Start();
        var wrong = host.Code == "AAAAAA" ? "CCCCCC" : "AAAAAA";
        for (int i = 0; i < TradeHost.MaxAttempts + 2; i++)
            await Assert.ThrowsAsync<WrongCodeException>(() => TradeClient.ConnectAsync("127.0.0.1", host.Port, wrong));
        Assert.False(host.Failed);
        // the code it lists works
        using var ch = await TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Lobby!.Code!);
        var until = DateTime.UtcNow.AddSeconds(5);
        while (host.Channel is null && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.NotNull(host.Channel);
        host.Channel!.Dispose();
    }

    [Fact]
    public async Task AClosedLobbyStillLocksAfterWrongCodes()
    {
        var cfg = TradeTests.Cfg("Lobby host");
        cfg.TradePort = 0;
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        using var host = new TradeHost(cfg, code => LobbyListing.For(pk, 0, false, code));
        host.Start();
        Assert.Null(host.Lobby!.Code);
        var wrong = host.Code == "AAAAAA" ? "CCCCCC" : "AAAAAA";
        for (int i = 0; i < TradeHost.MaxAttempts; i++)
            await Assert.ThrowsAnyAsync<Exception>(() => TradeClient.ConnectAsync("127.0.0.1", host.Port, wrong));
        var until = DateTime.UtcNow.AddSeconds(5);
        while (!host.Failed && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.True(host.Failed);
    }

    [Fact]
    public async Task LobbiesAreListedWithWhatTheyOfferAndWant()
    {
        if (Network.BroadcastAddresses().Count < 2)
            return; // no network interface with a broadcast address here
        var cfg = TradeTests.Cfg("Jorre");
        cfg.TradePort = 45000 + Random.Shared.Next(1000);
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        using var finder = new RoomFinder();
        finder.Start(cfg.TradePort);
        using var host = new TradeHost(cfg, code => LobbyListing.For(pk, (ushort)Species.Mareep, true, code));
        host.Start();
        var until = DateTime.UtcNow.AddSeconds(8);
        while (finder.Rooms.Count == 0 && DateTime.UtcNow < until)
            await Task.Delay(50);
        var room = Assert.Single(finder.Rooms);
        var l = Assert.IsType<LobbyListing>(room.Lobby);
        Assert.Equal((ushort)Species.Kadabra, l.Species);
        Assert.Equal(pk.CurrentLevel, l.Level);
        Assert.Equal((ushort)Species.Mareep, l.Want);
        Assert.True(l.Open);
        Assert.Equal(host.Code, l.Code);
        // and a joiner gets in with the listed code, nothing typed
        // (over loopback: this test machine's own address may not be a private one, which the host would refuse)
        using var ch = await TradeClient.ConnectAsync("127.0.0.1", room.Port, l.Code!);
        until = DateTime.UtcNow.AddSeconds(5);
        while (host.Channel is null && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.NotNull(host.Channel);
        host.Channel!.Dispose();
    }
}
