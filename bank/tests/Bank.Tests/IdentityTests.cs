using System.Net;
using System.Text.Json;
using PKHeX.Core;
using Rocknixds.Bank.Trade;
using Xunit;

namespace Rocknixds.Bank.Tests;

/// <summary>Imitated hosts, copied listings, men in the middle, guessing a code, and who gets into an open lobby.</summary>
[Collection("network attacks")]
public class IdentityTests
{
    private static BankConfig HostCfg()
    {
        var cfg = TradeTests.Cfg("Jorre");
        cfg.TradePort = 0;
        return cfg;
    }

    private static async Task<T> Soon<T>(Func<T?> get, int seconds = 5) where T : class
    {
        var until = DateTime.UtcNow.AddSeconds(seconds);
        while (get() is null && DateTime.UtcNow < until)
            await Task.Delay(20);
        return get() ?? throw new TimeoutException();
    }

    [Fact]
    public void TheIdentityKeyStaysAndOnlyRootCanReadIt()
    {
        using var tmp = new Fixtures.TempDir();
        var path = Path.Combine(tmp.Path, "bank-identity.key");
        using var a = DeviceIdentity.LoadOrCreate(path);
        using var b = DeviceIdentity.LoadOrCreate(path);
        Assert.Equal(a.PublicKey, b.PublicKey);
        Assert.Equal(UnixFileMode.UserRead | UnixFileMode.UserWrite, File.GetUnixFileMode(path));
        var data = "listing"u8.ToArray();
        var sig = a.Sign(data);
        Assert.True(DeviceIdentity.Verify(a.PublicKey, data, sig));
        Assert.False(DeviceIdentity.Verify(a.PublicKey, "listinG"u8.ToArray(), sig));
        Assert.False(DeviceIdentity.Verify(TradeTests.HostId.PublicKey, data, sig));
        Assert.False(DeviceIdentity.Verify([1, 2, 3], data, sig));
    }

    [Fact]
    public void AnAlteredListingIsNotListed()
    {
        var me = TradeTests.HostId;
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        var bytes = Announcement.Make(me, "Jorre", 47900, LobbyListing.For(pk, (ushort)Species.Mareep, true, "ACDEFG"));
        var a = JsonSerializer.Deserialize(bytes, TradeJson.Default.Announcement)!;
        Assert.True(a.IsSigned);

        a.Lobby!.Want = (ushort)Species.Magikarp; // someone rewrites what the lobby wants...
        Assert.False(a.IsSigned);
        a.Lobby.Want = (ushort)Species.Mareep;
        a.Name = "Jorre (official)";             // ...or its name...
        Assert.False(a.IsSigned);
        a.Name = "Jorre";
        a.Id = TradeTests.JoinId.PublicKey;       // ...or claims it under another key
        Assert.False(a.IsSigned);
    }

    [Fact]
    public async Task AnImitatorCantPassForTheHostItCopied()
    {
        // Mallory copies Jorre's listing (and its open code) and announces it from her own handheld. The joiner connects
        // to her expecting Jorre's key: she can't prove it, whatever she knows.
        using var mallory = DeviceIdentity.Ephemeral();
        using var host = new TradeHost(HostCfg(), mallory);
        host.Start();
        await Assert.ThrowsAsync<ImpostorException>(() =>
            TradeClient.ConnectAsync("127.0.0.1", host.Port, host.Code, TradeTests.JoinId, "Ash", expectHost: TradeTests.HostId.PublicKey));
        Assert.Null(host.Channel);
    }

    [Fact]
    public async Task NobodyCanSitBetweenTwoHandhelds()
    {
        // Mallory relays: she answers the joiner herself (an open lobby's code is public, so she knows it) and would
        // forward to the real host. The joiner expects the host's key, which only the host can sign with.
        using var mallory = DeviceIdentity.Ephemeral();
        var listener = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var middle = Task.Run(async () =>
        {
            using var c = await listener.AcceptTcpClientAsync();
            try { return await SecureChannel.AcceptAsync(c.GetStream(), "ACDEFG", "joiner", mallory); }
            catch (Exception) { return null; }
        });
        await Assert.ThrowsAsync<ImpostorException>(() =>
            TradeClient.ConnectAsync("127.0.0.1", port, "ACDEFG", TradeTests.JoinId, "Ash", expectHost: TradeTests.HostId.PublicKey));
        Assert.Null(await middle); // and she gets no session out of it either
        listener.Stop();
    }

    [Fact]
    public async Task ACodeChangesAfterTooManyWrongGuessesInsteadOfClosingTheRoom()
    {
        using var host = new TradeHost(HostCfg(), TradeTests.HostId);
        host.Start();
        var first = host.Code;
        var wrong = first == "AAAAAA" ? "CCCCCC" : "AAAAAA";
        // many addresses, 3 wrong guesses each, until the room has seen 20
        int tried = 0;
        for (int a = 2; tried < TradeHost.MaxWrongPerCode; a++)
        {
            for (int i = 0; i < TradeHost.MaxWrongPerAddress && tried < TradeHost.MaxWrongPerCode; i++, tried++)
            {
                await Assert.ThrowsAnyAsync<Exception>(() =>
                    TradeClient.ConnectFromAsync(IPAddress.Parse($"127.0.0.{a}"), "127.0.0.1", host.Port, wrong, TradeTests.JoinId, "M"));
            }
        }
        var until = DateTime.UtcNow.AddSeconds(5);
        while (host.CodeChanges == 0 && DateTime.UtcNow < until)
            await Task.Delay(20);
        Assert.Equal(1, host.CodeChanges);
        Assert.NotEqual(first, host.Code);
        Assert.False(host.Failed);
        // the old code is worthless now; the partner, with the new one, gets in
        await Assert.ThrowsAsync<WrongCodeException>(() =>
            TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.200"), "127.0.0.1", host.Port, first, TradeTests.JoinId, "J"));
        using var ch = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.201"), "127.0.0.1", host.Port, host.Code, TradeTests.JoinId, "J");
        await TradeClient.WaitForAdmissionAsync(ch);
        Assert.NotNull(host.Channel);
        host.Channel!.Dispose();
    }

    private static TradeHost OpenLobby(DeviceIdentity me)
    {
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        var host = new TradeHost(HostCfg(), me, code => LobbyListing.For(pk, (ushort)Species.Mareep, true, code));
        host.Start();
        return host;
    }

    [Fact]
    public async Task AnOpenLobbysHostDecidesWhoComesIn()
    {
        using var host = OpenLobby(TradeTests.HostId);
        using var eve = DeviceIdentity.Ephemeral();
        using var ash = DeviceIdentity.Ephemeral();

        // Eve asks; both screens show the same check number; the host refuses
        using var evech = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.5"), "127.0.0.1", host.Port, host.Lobby!.Code!, eve, "Eve",
            TradeTests.HostId.PublicKey);
        var eveWait = TradeClient.WaitForAdmissionAsync(evech);
        var req = await Soon(() => host.Pending);
        Assert.Equal("Eve", req.Name);
        Assert.Equal(evech.CheckNumber, req.CheckNumber);
        Assert.Equal(Fingerprint.Short(eve.PublicKey), req.Id);
        Assert.False(eveWait.IsCompleted); // nothing until the host answers
        await req.Refuse();
        var refused = await Assert.ThrowsAsync<TradeRefusedException>(() => eveWait);
        Assert.Contains("said no", refused.Message);
        Assert.Null(host.Channel);

        // Eve can't simply ask again: not from her address...
        await Assert.ThrowsAnyAsync<Exception>(async () =>
        {
            using var again = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.5"), "127.0.0.1", host.Port, host.Lobby!.Code!, eve, "Eve");
            await TradeClient.WaitForAdmissionAsync(again);
        });
        // ...nor from another one with the same handheld
        var other = await Assert.ThrowsAsync<TradeRefusedException>(async () =>
        {
            using var again = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.6"), "127.0.0.1", host.Port, host.Lobby!.Code!, eve, "Not Eve");
            await TradeClient.WaitForAdmissionAsync(again);
        });
        Assert.Contains("said no", other.Message);

        // Ash asks and is let in
        using var ashch = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.7"), "127.0.0.1", host.Port, host.Lobby!.Code!, ash, "Ash",
            TradeTests.HostId.PublicKey);
        var ashWait = TradeClient.WaitForAdmissionAsync(ashch);
        await (await Soon(() => host.Pending)).LetIn();
        await ashWait;
        Assert.Equal(ash.PublicKey, host.Channel!.PeerKey);
        host.Channel.Dispose();
    }

    [Fact]
    public async Task OneRequestAtATimeAndUnansweredOnesLapse()
    {
        var old = TradeHost.RequestTimeout;
        TradeHost.RequestTimeout = TimeSpan.FromSeconds(1);
        try
        {
            using var host = OpenLobby(TradeTests.HostId);
            using var first = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.8"), "127.0.0.1", host.Port, host.Lobby!.Code!,
                DeviceIdentity.Ephemeral(), "First");
            var firstWait = TradeClient.WaitForAdmissionAsync(first);
            await Soon(() => host.Pending);
            // a second asker while the host is deciding: told to wait, not queued
            var busy = await Assert.ThrowsAsync<TradeRefusedException>(async () =>
            {
                using var second = await TradeClient.ConnectFromAsync(IPAddress.Parse("127.0.0.9"), "127.0.0.1", host.Port, host.Lobby!.Code!,
                    DeviceIdentity.Ephemeral(), "Second");
                await TradeClient.WaitForAdmissionAsync(second);
            });
            Assert.Contains("someone else", busy.Message);
            // nobody answers the first: it lapses, and the lobby is free again
            var lapsed = await Assert.ThrowsAsync<TradeRefusedException>(() => firstWait);
            Assert.Contains("didn't answer", lapsed.Message);
            Assert.Null(host.Pending);
            Assert.Null(host.Channel);
        }
        finally
        {
            TradeHost.RequestTimeout = old;
        }
    }

    [Fact]
    public void TheTrainerBookRecognisesHandheldsNotNames()
    {
        using var tmp = new Fixtures.TempDir();
        var path = Path.Combine(tmp.Path, "trainers.json");
        using var jorre = DeviceIdentity.Ephemeral();
        using var fake = DeviceIdentity.Ephemeral();
        var book = new TrainerBook(path);
        Assert.Equal(Trust.New, book.TrustOf(jorre.PublicKey, "Jorre"));
        book.RecordTrade(jorre.PublicKey, "Jorre");
        book.RecordTrade(jorre.PublicKey, "Jorre");

        var again = new TrainerBook(path); // remembered across starts
        Assert.Equal(Trust.Known, again.TrustOf(jorre.PublicKey, "Jorre"));
        Assert.Equal(Trust.Known, again.TrustOf(jorre.PublicKey, "J-man")); // a new name, the same handheld
        Assert.Equal("Known · 2 trades", again.Describe(jorre.PublicKey, "J-man"));
        Assert.Equal(Trust.Impostor, again.TrustOf(fake.PublicKey, "jorre")); // the name, another handheld
        Assert.Equal(Trust.New, again.TrustOf(fake.PublicKey, "Mallory"));
    }

    [Fact]
    public async Task ATradeKnowsWhoItsWith()
    {
        var (hc, cc) = await TradeTests.Pair();
        using var host = TradeTests.Session(hc, TradeTests.Cfg("Host"), true);
        using var join = TradeTests.Session(cc, TradeTests.Cfg("Joiner"), false);
        Assert.Equal(TradeTests.JoinId.PublicKey, host.PartnerKey);
        Assert.Equal(TradeTests.HostId.PublicKey, join.PartnerKey);
        Assert.Equal(host.CheckNumber, join.CheckNumber);
        Assert.Matches("^[0-9]{4}$", host.CheckNumber);
    }
}
