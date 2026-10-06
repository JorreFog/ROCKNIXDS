using PKHeX.Core;
using Xunit;

namespace Rocknixds.Bank.Tests;

public class SaveTests
{
    [Theory]
    [InlineData(GameVersion.HG)]
    [InlineData(GameVersion.Pt)]
    [InlineData(GameVersion.D)]
    public void Gen4FixturesRoundTrip(GameVersion v)
    {
        var data = Fixtures.Gen4(v);
        Assert.True(SaveUtil.TryGetSaveFile(data, out var sav));
        Assert.True(sav.ChecksumsValid);
        Assert.Equal(4, sav.Generation);
    }

    [Fact]
    public void ScannerFindsSavesAndSkipsOtherFiles()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        tmp.Write("roms/nds/Pokemon - HeartGold Version (USA).dsv", Fixtures.WithDeSmuMEFooter(Fixtures.Gen4(GameVersion.HG)));
        tmp.Write("roms/nds/Pokemon - Black Version 2 (USA).sav", Fixtures.Gen5(GameVersion.B2));
        tmp.Write("roms/gba/Pokemon - FireRed Version (USA).srm", Fixtures.Gen3(GameVersion.FR));
        tmp.Write("roms/nds/notes.sav", new byte[1234]);                     // wrong size
        tmp.Write("roms/nds/random.sav", new byte[0x80000]);                  // right size, no save inside
        tmp.Write("roms/images/Pokemon - Platinum.sav", Fixtures.Gen4(GameVersion.Pt)); // a skipped folder

        var found = SaveScanner.Scan(cfg);

        Assert.Equal(3, found.Count);
        var hg = Assert.Single(found, e => e.Path.EndsWith(".dsv"));
        Assert.Equal(GameVersion.HG, hg.Version);
        Assert.Equal("TESTER", hg.Trainer);
        Assert.Equal("Pokemon - HeartGold Version (USA)", hg.FileTitle);
        Assert.Contains(found, e => e.Version == GameVersion.B2);
        Assert.Contains(found, e => e.Generation == 3);
    }

    [Fact]
    public void DsvFooterSurvivesAWrite()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var path = tmp.Write("roms/nds/HG.dsv", Fixtures.WithDeSmuMEFooter(Fixtures.Gen4(GameVersion.HG)));
        var before = File.ReadAllBytes(path);

        var session = SaveSession.Open(cfg, path);
        session.Set(2, 7, Fixtures.Make(Species.Pikachu, GameVersion.HG));
        session.Commit();

        var after = File.ReadAllBytes(path);
        Assert.Equal(before.Length, after.Length);
        Assert.Equal(before[^0x7A..], after[^0x7A..]); // DraStic's footer, unchanged
        var reread = SaveSession.Open(cfg, path);
        Assert.Equal((ushort)Species.Pikachu, reread.Get(2, 7)!.Species);

        // the save as it was before is in the backups
        var backup = Assert.Single(Directory.GetFiles(Path.Combine(cfg.BackupFolder, "HG.dsv")));
        Assert.Equal(before, File.ReadAllBytes(backup));
    }

    [Fact]
    public void OnlyOneBackupPerSessionAndOldOnesArePruned()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        cfg.BackupsPerSave = 2;
        var path = tmp.Write("roms/nds/B2.sav", Fixtures.Gen5(GameVersion.B2));
        for (int i = 0; i < 4; i++)
        {
            var s = SaveSession.Open(cfg, path);
            s.Set(0, i, Fixtures.Make(Species.Patrat, GameVersion.B2));
            s.Commit();
            s.Set(0, i + 10, Fixtures.Make(Species.Patrat, GameVersion.B2));
            s.Commit();
            Thread.Sleep(1100); // backups are named by the second
        }
        Assert.Equal(2, Directory.GetFiles(Path.Combine(cfg.BackupFolder, "B2.sav")).Length);
    }

    [Fact]
    public void RefusesToOverwriteASaveTheGameChanged()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var path = tmp.Write("roms/nds/B2.sav", Fixtures.Gen5(GameVersion.B2));
        var session = SaveSession.Open(cfg, path);
        File.SetLastWriteTimeUtc(path, DateTime.UtcNow.AddMinutes(5)); // the emulator saved meanwhile
        session.Set(0, 0, Fixtures.Make(Species.Patrat, GameVersion.B2));
        Assert.Throws<BankException>(session.Commit);
    }
}

public class BankStoreTests
{
    [Fact]
    public void KeepsEveryFormatAcrossRestarts()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var mons = new PKM[]
        {
            Fixtures.Make(Species.Treecko, GameVersion.E),
            Fixtures.Make(Species.Chikorita, GameVersion.HG),
            Fixtures.Make(Species.Snivy, GameVersion.B2),
            Fixtures.Make(Species.Bulbasaur, GameVersion.FR),
        };
        var bank = BankStore.Open(cfg);
        for (int i = 0; i < mons.Length; i++)
            bank.Put(1, i * 3, mons[i]);

        var again = BankStore.Open(cfg);
        Assert.Empty(again.LoadNotes);
        for (int i = 0; i < mons.Length; i++)
        {
            var pk = again.Get(1, i * 3);
            Assert.NotNull(pk);
            Assert.Equal(mons[i].GetType(), pk.GetType());
            Assert.Equal(PkmIO.Hash(mons[i]), PkmIO.Hash(pk));
        }
        Assert.True(File.Exists(Path.Combine(cfg.BankFolder, "02", "00 - Treecko.pk3")));
    }

    [Fact]
    public void MoveAndSwap()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var bank = BankStore.Open(cfg);
        var a = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        var b = Fixtures.Make(Species.Eevee, GameVersion.HG);
        bank.Put(0, 0, a);
        bank.Put(0, 1, b);
        bank.Move(0, 0, 0, 1); // swap
        bank.Move(0, 0, 3, 29); // move Eevee to the last slot

        var again = BankStore.Open(cfg);
        Assert.Null(again.Get(0, 0));
        Assert.Equal((ushort)Species.Pikachu, again.Get(0, 1)!.Species);
        Assert.Equal((ushort)Species.Eevee, again.Get(3, 29)!.Species);
        Assert.Equal(2, Directory.EnumerateFiles(cfg.BankFolder, "*.pk4", SearchOption.AllDirectories).Count());
    }

    [Fact]
    public void ALooseFileFromAnInterruptedMoveGetsASlot()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var bank = BankStore.Open(cfg);
        bank.Put(0, 0, Fixtures.Make(Species.Pikachu, GameVersion.HG));
        var staged = bank.Stage(Fixtures.Make(Species.Eevee, GameVersion.HG)); // ... and then the battery died

        var again = BankStore.Open(cfg);
        Assert.False(File.Exists(staged));
        Assert.Equal((ushort)Species.Eevee, again.Get(0, 1)!.Species);
    }

    [Fact]
    public void ImportsDroppedFiles()
    {
        using var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var bank = BankStore.Open(cfg);
        bank.Put(0, 0, Fixtures.Make(Species.Pikachu, GameVersion.HG));
        File.WriteAllBytes(Path.Combine(cfg.ImportFolder, "0133 - EEVEE.pk4"), PkmIO.ToFileBytes(Fixtures.Make(Species.Eevee, GameVersion.HG)));
        File.WriteAllBytes(Path.Combine(cfg.ImportFolder, "readme.txt"), "hello"u8.ToArray());

        var notes = bank.ImportDropped(new History(cfg.HistoryPath));

        Assert.Equal((ushort)Species.Eevee, bank.Get(0, 1)!.Species);
        Assert.True(File.Exists(Path.Combine(cfg.ImportFolder, "done", "0133 - EEVEE.pk4")));
        Assert.True(File.Exists(Path.Combine(cfg.ImportFolder, "readme.txt")));
        Assert.Contains(notes, n => n.Contains("Eevee"));
        // an import is a line in the history, as a move is; the file that wasn't a Pokémon is not
        var history = File.ReadAllLines(cfg.HistoryPath);
        Assert.Single(history);
        Assert.Contains("IMPORT Eevee", history[0]);
        Assert.Contains("0133 - EEVEE.pk4 -> ", history[0]);
        Assert.EndsWith("slot 2", history[0]);
    }
}

public class MoverTests
{
    private static (Mover Mover, BankConfig Cfg, Fixtures.TempDir Tmp) Setup(byte[] save, string name)
    {
        var tmp = new Fixtures.TempDir();
        var cfg = tmp.Config();
        var path = tmp.Write("roms/" + name, save);
        var mover = new Mover(cfg, BankStore.Open(cfg), new History(cfg.HistoryPath)) { Save = SaveSession.Open(cfg, path) };
        return (mover, cfg, tmp);
    }

    [Fact]
    public void Gen4ToBankToGen5ConvertsLikePokeTransfer()
    {
        var (mover, cfg, tmp) = Setup(Fixtures.WithDeSmuMEFooter(Fixtures.Gen4(GameVersion.HG)), "nds/HG.dsv");
        using var _ = tmp;
        mover.Save!.Set(0, 0, Fixtures.Make(Species.Cyndaquil, GameVersion.HG));
        mover.Save.Commit();

        var r = mover.Move(Slot.InSave(0, 0), Slot.InBank(0, 0));
        Assert.True(r.Done, r.Message);
        Assert.Null(mover.Save.Get(0, 0));
        Assert.IsType<PK4>(mover.Bank.Get(0, 0));
        Assert.Null(SaveSession.Open(cfg, mover.Save.Path).Get(0, 0)); // on the card too

        // now into Black 2
        mover.Save = SaveSession.Open(cfg, tmp.Write("roms/nds/B2.sav", Fixtures.Gen5(GameVersion.B2)));
        r = mover.Move(Slot.InBank(0, 0), Slot.InSave(1, 5));
        Assert.True(r.Done, r.Message);
        var pk5 = Assert.IsType<PK5>(mover.Save.Get(1, 5));
        Assert.Equal((ushort)Species.Cyndaquil, pk5.Species);
        Assert.True(new LegalityAnalysis(pk5).Valid, new LegalityAnalysis(pk5).Report());
        Assert.Null(mover.Bank.Get(0, 0));
        Assert.Contains("Converted", r.Message);
        Assert.Contains("MOVE Cyndaquil", File.ReadAllText(cfg.HistoryPath));
    }

    [Fact]
    public void Gen3ToGen4ViaTheBank()
    {
        var (mover, cfg, tmp) = Setup(Fixtures.Gen3(GameVersion.FR), "gba/FR.srm");
        using var _ = tmp;
        mover.Save!.Set(0, 3, Fixtures.Make(Species.Charmander, GameVersion.FR));
        mover.Save.Commit();
        Assert.True(mover.Move(Slot.InSave(0, 3), Slot.InBank(2, 2)).Done);

        mover.Save = SaveSession.Open(cfg, tmp.Write("roms/nds/Pt.dsv", Fixtures.WithDeSmuMEFooter(Fixtures.Gen4(GameVersion.Pt))));
        var r = mover.Move(Slot.InBank(2, 2), Slot.InSave(0, 0));
        Assert.True(r.Done, r.Message);
        var pk4 = Assert.IsType<PK4>(mover.Save.Get(0, 0));
        Assert.Equal((ushort)Species.Charmander, pk4.Species);
    }

    [Fact]
    public void NeverBackwards()
    {
        var (mover, cfg, tmp) = Setup(Fixtures.Gen3(GameVersion.FR), "gba/FR.srm");
        using var _ = tmp;
        mover.Bank.Put(0, 0, Fixtures.Make(Species.Snivy, GameVersion.B2));
        var r = mover.Move(Slot.InBank(0, 0), Slot.InSave(0, 0));
        Assert.False(r.Done);
        Assert.NotNull(r.Blocked);
        Assert.NotNull(mover.Bank.Get(0, 0)); // still there
        Assert.Null(mover.Save!.Get(0, 0));
    }

    [Fact]
    public void IllegalPokemonNeedsConfirmationOrIsBlocked()
    {
        var (mover, cfg, tmp) = Setup(Fixtures.Gen5(GameVersion.B2), "nds/B2.sav");
        using var _ = tmp;
        var hacked = Fixtures.Make(Species.Snivy, GameVersion.B2);
        hacked.CurrentLevel = 3;
        hacked.SetMove(0, (ushort)Move.HydroPump); // Snivy can't learn Hydro Pump
        hacked.RefreshChecksum();
        Assert.False(Legality.Check(hacked).Valid);
        mover.Bank.Put(0, 0, hacked);

        var r = mover.Move(Slot.InBank(0, 0), Slot.InSave(0, 0));
        Assert.True(r.NeedsConfirm);
        Assert.Null(mover.Save!.Get(0, 0));

        r = mover.Move(Slot.InBank(0, 0), Slot.InSave(0, 0), confirmed: true);
        Assert.True(r.Done);
        Assert.NotNull(mover.Save.Get(0, 0));

        cfg.BlockIllegalIntoSaves = true;
        r = mover.Move(Slot.InSave(0, 0), Slot.InBank(0, 0)); // out is always fine
        Assert.True(r.Done, r.Message);
        r = mover.Move(Slot.InBank(0, 0), Slot.InSave(0, 0), confirmed: true);
        Assert.NotNull(r.Blocked);
    }

    [Fact]
    public void SwapBetweenSaveAndBank()
    {
        var (mover, cfg, tmp) = Setup(Fixtures.Gen5(GameVersion.B2), "nds/B2.sav");
        using var _ = tmp;
        mover.Save!.Set(0, 0, Fixtures.Make(Species.Oshawott, GameVersion.B2));
        mover.Save.Commit();
        mover.Bank.Put(0, 0, Fixtures.Make(Species.Pikachu, GameVersion.HG));

        var r = mover.Move(Slot.InBank(0, 0), Slot.InSave(0, 0));
        Assert.True(r.Done, r.Message);
        Assert.IsType<PK5>(mover.Save.Get(0, 0));
        Assert.Equal((ushort)Species.Pikachu, mover.Save.Get(0, 0)!.Species);
        Assert.Equal((ushort)Species.Oshawott, mover.Bank.Get(0, 0)!.Species);
        Assert.Single(Directory.EnumerateFiles(cfg.BankFolder, "*.pk*", SearchOption.AllDirectories));
    }
}

public class LegalityAndEvolutionTests
{
    [Fact]
    public void LegalAndIllegal()
    {
        var pk = Fixtures.Make(Species.Abra, GameVersion.HG);
        Assert.True(Legality.Check(pk).Valid);
        var bad = pk.Clone();
        bad.SetMove(0, (ushort)Move.HydroPump); // not a move Abra can learn
        bad.RefreshChecksum();
        var v = Legality.Check(bad);
        Assert.False(v.Valid);
        Assert.NotEmpty(v.Problems);
    }

    [Fact]
    public void KadabraEvolvesWhenTraded()
    {
        var pk = Fixtures.Make(Species.Kadabra, GameVersion.HG);
        Assert.Equal("Alakazam", TradeEvolution.TryEvolve(pk, (ushort)Species.Machop));
        Assert.Equal((ushort)Species.Alakazam, pk.Species);
        Assert.Equal("ALAKAZAM", pk.Nickname.ToUpperInvariant());
        Assert.True(Legality.Check(pk).Valid, Legality.Check(pk).Report);
    }

    [Fact]
    public void OnixNeedsTheMetalCoatAndUsesItUp()
    {
        var pk = Fixtures.Make(Species.Onix, GameVersion.HG);
        Assert.Null(TradeEvolution.TryEvolve(pk.Clone(), 1));
        pk.HeldItem = 233; // Metal Coat
        Assert.Equal("Steelix", TradeEvolution.TryEvolve(pk, 1));
        Assert.Equal(0, pk.HeldItem);
        Assert.True(Legality.Check(pk).Valid, Legality.Check(pk).Report);
    }

    [Fact]
    public void EverstoneStopsIt()
    {
        var pk = Fixtures.Make(Species.Machoke, GameVersion.HG);
        pk.HeldItem = 229;
        Assert.Null(TradeEvolution.TryEvolve(pk, 1));
        Assert.Equal((ushort)Species.Machoke, pk.Species);
    }

    [Fact]
    public void KarrablastForShelmet()
    {
        var k = Fixtures.Make(Species.Karrablast, GameVersion.B2);
        var s = Fixtures.Make(Species.Shelmet, GameVersion.B2);
        Assert.Null(TradeEvolution.TryEvolve(k.Clone(), (ushort)Species.Pikachu));
        Assert.Equal("Escavalier", TradeEvolution.TryEvolve(k, (ushort)Species.Shelmet));
        Assert.Equal("Accelgor", TradeEvolution.TryEvolve(s, (ushort)Species.Karrablast));
    }
}
