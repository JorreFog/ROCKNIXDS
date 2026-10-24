using PKHeX.Core;
using Xunit;

namespace Rocknixds.Bank.Tests;

/// <summary>The party as a place to move Pokémon to and from, and copies (duplicates) of Pokémon.</summary>
public class PartyTests
{
    private static (Mover Mover, BankConfig Cfg, Fixtures.TempDir Tmp) Setup(byte[] save, string name, GameVersion v, params Species[] party)
    {
        var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var path = tmp.Write("roms/" + name, save);
        var mover = new Mover(cfg, BankStore.Open(cfg), new History(cfg.HistoryPath)) { Save = SaveSession.Open(cfg, path) };
        if (party.Length > 0)
        {
            var sav = mover.Save.Sav;
            mover.Save.SetParty(party.Select(s => EntityConverter.ConvertToType(Fixtures.Make(s, v), sav.PKMType, out _)!).ToList());
            mover.Save.Commit();
        }
        return (mover, cfg, tmp);
    }

    private static SaveSession Reopen(Mover m, BankConfig cfg) => SaveSession.Open(cfg, m.Save!.Path);

    // Gen 1 and 2 from PKHeX's blank saves, as Gen 5
    public static TheoryData<GameVersion> Games => [GameVersion.RD, GameVersion.C, GameVersion.FR, GameVersion.E, GameVersion.HG, GameVersion.Pt, GameVersion.B2];

    // Gen 1 and 2 boxes are lists that close their gaps, so their box slots don't stay put
    public static TheoryData<GameVersion> BoxedGames => [GameVersion.FR, GameVersion.E, GameVersion.HG, GameVersion.Pt, GameVersion.B2];

    private static byte[] SaveFor(GameVersion v) => v switch
    {
        GameVersion.RD or GameVersion.C => Fixtures.Gen12(v),
        GameVersion.FR or GameVersion.LG or GameVersion.E or GameVersion.R or GameVersion.S => Fixtures.Gen3(v),
        GameVersion.HG or GameVersion.SS or GameVersion.Pt or GameVersion.D or GameVersion.P => Fixtures.WithDeSmuMEFooter(Fixtures.Gen4(v)),
        _ => Fixtures.Gen5(v),
    };

    private static Species Starter(GameVersion v) => v switch
    {
        GameVersion.RD or GameVersion.FR or GameVersion.LG => Species.Charmander,
        GameVersion.C => Species.Cyndaquil,
        GameVersion.E => Species.Treecko,
        GameVersion.HG or GameVersion.SS => Species.Cyndaquil,
        GameVersion.Pt => Species.Chimchar,
        _ => Species.Snivy,
    };

    [Theory]
    [MemberData(nameof(Games))]
    public void PartyToBankAndBackKeepsThePartyValid(GameVersion v)
    {
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.sav", v, Starter(v), Species.Pikachu);
        using var cleanup = tmp;
        Assert.Equal(2, mover.Save!.PartyCount);

        // the first party Pokémon into the bank: the second moves up
        var r = mover.Move(Slot.InParty(0), Slot.InBank(0, 3));
        Assert.True(r.Done, r.Message);
        Assert.Equal((ushort)Starter(v), mover.Bank.Get(0, 3)!.Species);
        var disk = Reopen(mover, cfg);
        Assert.Equal(1, disk.PartyCount);
        Assert.Equal((ushort)Species.Pikachu, disk.GetParty(0)!.Species);
        Assert.True(disk.Sav.ChecksumsValid);

        // the last one can't go: the party would be empty
        r = mover.Move(Slot.InParty(0), Slot.InBank(0, 4));
        Assert.False(r.Done);
        Assert.Contains("party", r.Blocked);
        Assert.Equal(1, Reopen(mover, cfg).PartyCount);

        // back from the bank into the party's sixth slot: it lands in the first free one, with party stats
        r = mover.Move(Slot.InBank(0, 3), Slot.InParty(5));
        Assert.True(r.Done, r.Message);
        Assert.Contains("party, slot 2", r.Message);
        disk = Reopen(mover, cfg);
        Assert.Equal(2, disk.PartyCount);
        var back = disk.GetParty(1)!;
        Assert.Equal((ushort)Starter(v), back.Species);
        Assert.True(back.Stat_HPMax > 0 && back.Stat_HPCurrent == back.Stat_HPMax);
        Assert.Null(mover.Bank.Get(0, 3));
        Assert.True(new LegalityAnalysis(back).Valid, new LegalityAnalysis(back).Report());
    }

    [Theory]
    [MemberData(nameof(BoxedGames))]
    public void BoxToPartyAndSwaps(GameVersion v)
    {
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.sav", v, Starter(v));
        using var cleanup = tmp;
        var sav = mover.Save!.Sav;
        mover.Save.Set(0, 0, EntityConverter.ConvertToType(Fixtures.Make(Species.Eevee, v), sav.PKMType, out _)!);
        mover.Save.Commit();

        // box -> party (an empty party slot)
        Assert.True(mover.Move(Slot.InSave(0, 0), Slot.InParty(3)).Done);
        var disk = Reopen(mover, cfg);
        Assert.Equal(2, disk.PartyCount);
        Assert.Equal((ushort)Species.Eevee, disk.GetParty(1)!.Species);
        Assert.Null(disk.Get(0, 0));

        // party <-> party swap, then party <-> box swap
        Assert.True(mover.Move(Slot.InParty(0), Slot.InParty(1)).Done);
        Assert.Equal((ushort)Species.Eevee, Reopen(mover, cfg).GetParty(0)!.Species);
        mover.Save.Set(0, 7, EntityConverter.ConvertToType(Fixtures.Make(Species.Pikachu, v), sav.PKMType, out _)!);
        mover.Save.Commit();
        Assert.True(mover.Move(Slot.InParty(1), Slot.InSave(0, 7)).Done);
        disk = Reopen(mover, cfg);
        Assert.Equal(2, disk.PartyCount);
        Assert.Equal((ushort)Species.Pikachu, disk.GetParty(1)!.Species);
        Assert.Equal((ushort)Starter(v), disk.Get(0, 7)!.Species);

        // party -> party's empty end: it moves to the last place
        Assert.True(mover.Move(Slot.InParty(0), Slot.InParty(5)).Done);
        disk = Reopen(mover, cfg);
        Assert.Equal((ushort)Species.Pikachu, disk.GetParty(0)!.Species);
        Assert.Equal((ushort)Species.Eevee, disk.GetParty(1)!.Species);
    }

    [Fact]
    public void AFullPartyOnlySwaps()
    {
        var v = GameVersion.HG;
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.dsv", v, Species.Cyndaquil, Species.Pikachu, Species.Eevee, Species.Togepi, Species.Mareep, Species.Onix);
        using var cleanup = tmp;
        mover.Bank.Put(0, 0, Fixtures.Make(Species.Wooper, v));
        Assert.Null(mover.NextFree(Slot.InParty(0)));
        var r = mover.Move(Slot.InBank(0, 0), Slot.InParty(2));
        Assert.True(r.Done, r.Message);
        Assert.Equal((ushort)Species.Eevee, mover.Bank.Get(0, 0)!.Species);
        Assert.Equal(6, Reopen(mover, cfg).PartyCount);
    }

    [Fact]
    public void AnEggCantBeTheLastOneLeft()
    {
        var v = GameVersion.HG;
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.dsv", v, Species.Cyndaquil);
        using var cleanup = tmp;
        var egg = Fixtures.Make(Species.Togepi, v);
        egg.IsEgg = true;
        mover.Bank.Put(0, 0, egg);
        var r = mover.Move(Slot.InBank(0, 0), Slot.InParty(0), confirmed: true); // swap the only Pokémon for an egg
        Assert.False(r.Done);
        Assert.NotNull(r.Blocked);
        Assert.Equal((ushort)Species.Cyndaquil, Reopen(mover, cfg).GetParty(0)!.Species);
    }

    [Fact]
    public void MovesFromAGen3BankIntoAGen4Party()
    {
        var (mover, cfg, tmp) = Setup(SaveFor(GameVersion.Pt), "Pt.dsv", GameVersion.Pt, Species.Chimchar);
        using var cleanup = tmp;
        mover.Bank.Put(1, 1, Fixtures.Make(Species.Treecko, GameVersion.E));
        var r = mover.Move(Slot.InBank(1, 1), Slot.InParty(1));
        Assert.True(r.Done, r.Message);
        Assert.Contains("Converted", r.Message);
        var pk = Assert.IsType<PK4>(Reopen(mover, cfg).GetParty(1));
        Assert.Equal((ushort)Species.Treecko, pk.Species);
    }

    [Theory]
    [MemberData(nameof(Games))]
    public void DuplicatesGoIntoTheNextFreeSlot(GameVersion v)
    {
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.sav", v, Starter(v));
        using var cleanup = tmp;
        var sav = mover.Save!.Sav;
        mover.Save.Set(2, 0, EntityConverter.ConvertToType(Fixtures.Make(Species.Pikachu, v), sav.PKMType, out _)!);
        mover.Save.Commit();

        // a box Pokémon: next to it
        var to = mover.NextFree(Slot.InSave(2, 0));
        Assert.Equal(Slot.InSave(2, 1), to);
        Assert.True(mover.Copy(Slot.InSave(2, 0), to!.Value).Done);
        var disk = Reopen(mover, cfg);
        Assert.Equal(PkmIO.Hash(disk.Get(2, 0)!), PkmIO.Hash(disk.Get(2, 1)!));

        // a party Pokémon: into the party
        to = mover.NextFree(Slot.InParty(0));
        Assert.Equal(Slot.InParty(1), to);
        Assert.True(mover.Copy(Slot.InParty(0), to!.Value).Done);
        Assert.Equal(2, Reopen(mover, cfg).PartyCount);

        // into the bank, and in the bank
        Assert.True(mover.Copy(Slot.InParty(1), Slot.InBank(0, 0)).Done);
        Assert.Equal((ushort)Starter(v), mover.Bank.Get(0, 0)!.Species);
        Assert.Equal(Slot.InBank(0, 1), mover.NextFree(Slot.InBank(0, 0)));
        Assert.True(mover.Copy(Slot.InBank(0, 0), Slot.InBank(0, 1)).Done);
        Assert.Equal(2, BankStore.Open(cfg).Count);

        // never over another Pokémon
        var r = mover.Copy(Slot.InBank(0, 0), Slot.InSave(2, 0));
        Assert.False(r.Done);
        Assert.Contains("COPY", File.ReadAllText(cfg.HistoryPath));
        Assert.Equal(2, BankStore.Open(cfg).Count);
    }

    [Fact]
    public void ACopyIntoAnOlderGameIsRefused()
    {
        var (mover, cfg, tmp) = Setup(SaveFor(GameVersion.FR), "FR.srm", GameVersion.FR, Species.Charmander);
        using var cleanup = tmp;
        mover.Bank.Put(0, 0, Fixtures.Make(Species.Snivy, GameVersion.B2));
        var r = mover.Copy(Slot.InBank(0, 0), Slot.InSave(0, 0));
        Assert.False(r.Done);
        Assert.NotNull(r.Blocked);
        Assert.Null(Reopen(mover, cfg).Get(0, 0));
    }

    [Fact]
    public void EditsAreWrittenWithABackup()
    {
        var v = GameVersion.HG;
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.dsv", v, Species.Cyndaquil);
        using var cleanup = tmp;
        var pk = mover.Read(Slot.InParty(0))!.Clone();
        pk.CurrentLevel = 50;
        mover.Replace(Slot.InParty(0), pk);
        var disk = Reopen(mover, cfg);
        Assert.Equal(50, disk.GetParty(0)!.CurrentLevel);
        Assert.Equal(50, disk.GetParty(0)!.Stat_Level);
        Assert.NotEmpty(Directory.GetFiles(cfg.BackupFolder, "*", SearchOption.AllDirectories));

        mover.Bank.Put(0, 0, Fixtures.Make(Species.Eevee, v));
        var b = mover.Bank.Get(0, 0)!.Clone();
        b.Nickname = "FLUFFY";
        b.IsNicknamed = true;
        mover.Replace(Slot.InBank(0, 0), b);
        Assert.Equal("FLUFFY", BankStore.Open(cfg).Get(0, 0)!.Nickname);
        Assert.Single(Directory.EnumerateFiles(cfg.BankFolder, "*.pk4", SearchOption.AllDirectories));

        Assert.Throws<BankException>(() => mover.Replace(Slot.InSave(0, 0), Fixtures.Make(Species.Snivy, GameVersion.B2)));
    }

    [Fact]
    public void ReleaseRespectsTheParty()
    {
        var v = GameVersion.HG;
        var (mover, cfg, tmp) = Setup(SaveFor(v), "game.dsv", v, Species.Cyndaquil, Species.Pikachu);
        using var cleanup = tmp;
        mover.Remove(Slot.InParty(0), "RELEASE");
        Assert.Equal(1, Reopen(mover, cfg).PartyCount);
        Assert.NotNull(mover.CantEmpty(Slot.InParty(0)));
        Assert.Throws<BankException>(() => mover.Remove(Slot.InParty(0), "RELEASE"));
    }
}
