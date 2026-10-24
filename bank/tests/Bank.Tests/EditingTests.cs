using PKHeX.Core;
using Xunit;

namespace Rocknixds.Bank.Tests;

/// <summary>The save editor's changes: new Pokémon, edits, "make legal", trainer, Pokédex and bag, written and read back.</summary>
public class EditingTests
{
    private static SaveFile Open(byte[] data) => SaveUtil.GetSaveFile(data) ?? throw new InvalidOperationException("not a save");

    private static SaveFile RoundTrip(SaveFile sav) => Open(sav.Write().ToArray());

    [Theory]
    [InlineData(GameVersion.FR, Species.Bulbasaur)]
    [InlineData(GameVersion.HG, Species.Cyndaquil)]
    [InlineData(GameVersion.HG, Species.Treecko)] // not in HeartGold: from a Gen 3 game, as Pal Park brings it
    [InlineData(GameVersion.Pt, Species.Pikachu)]
    [InlineData(GameVersion.HG, Species.Pikachu)] // the wild one, not the event's (its OT is the event's)
    [InlineData(GameVersion.B2, Species.Snivy)]
    [InlineData(GameVersion.B2, Species.Mudkip)]
    public void NewPokemonPassTheLegalityCheck(GameVersion v, Species species)
    {
        var sav = Open(v switch
        {
            GameVersion.FR => Fixtures.Gen3(v),
            GameVersion.HG or GameVersion.Pt => Fixtures.Gen4(v),
            _ => Fixtures.Gen5(v),
        });
        var pk = MonEdit.Create((ushort)species, sav.PKMType, sav.Version, sav, out var note);
        Assert.Equal(sav.PKMType, pk.GetType());
        Assert.Equal((ushort)species, pk.Species);
        Assert.Equal("TESTER", pk.OriginalTrainerName);
        var la = new LegalityAnalysis(pk);
        Assert.True(la.Valid, note + "\n" + la.Report());
        Assert.True(pk.Stat_HPMax > 0);
    }

    [Fact]
    public void AnEventSpeciesIsMadeInTheGamesFormat()
    {
        var sav = Open(Fixtures.Gen3(GameVersion.FR));
        // Mew (an event, or Emerald's Faraway Island): legal or not, it's the species asked for, in the game's format,
        // with a note saying where it came from
        var pk = MonEdit.Create((ushort)Species.Mew, sav.PKMType, sav.Version, sav, out var note);
        Assert.Equal((ushort)Species.Mew, pk.Species);
        Assert.IsType<PK3>(pk);
        Assert.NotEmpty(note);
    }

    [Fact]
    public void PidBoundEditsKeepTheOtherValues()
    {
        var pk = Fixtures.Make(Species.Eevee, GameVersion.HG);
        var gender = pk.Gender;
        var shiny = pk.IsShiny;
        CommonEdits.SetNature(pk, Nature.Timid);
        Assert.Equal(Nature.Timid, pk.Nature);
        Assert.Equal(gender, pk.Gender);
        Assert.Equal(shiny, pk.IsShiny);

        byte other = (byte)(1 - gender);
        MonEdit.SetGender(pk, other);
        Assert.Equal(other, pk.Gender);
        Assert.Equal(Nature.Timid, pk.Nature);
        Assert.True(pk.IsGenderValid());

        CommonEdits.SetIsShiny(pk, true);
        Assert.True(pk.IsShiny);
        Assert.Equal(other, pk.Gender);

        var abilities = MonEdit.Abilities(pk);
        Assert.Equal(2, abilities.Count); // Run Away, Adaptability
        MonEdit.SetAbility(pk, 1);
        Assert.Equal(abilities[1].Ability, pk.Ability);
        Assert.Equal(1u, pk.PID & 1);
        Assert.Equal(Nature.Timid, pk.Nature);
        Assert.True(pk.IsShiny);
    }

    [Fact]
    public void MakeLegalFixesMoves()
    {
        var pk = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        pk.SetMove(0, (ushort)Move.HydroPump);
        pk.SetMove(1, (ushort)Move.Fly);
        pk.RefreshChecksum();
        Assert.False(new LegalityAnalysis(pk).Valid);

        var changed = MonEdit.TryMakeLegal(pk);
        Assert.Contains("moves", changed);
        var la = new LegalityAnalysis(pk);
        Assert.True(la.Valid, la.Report());
    }

    [Fact]
    public void IVsAndEVsStayWithinTheLimits()
    {
        var pk = Fixtures.Make(Species.Pikachu, GameVersion.HG);
        MonEdit.MaxIVs(pk);
        Assert.Equal(31 * 6, pk.IVTotal);
        MonEdit.SetEV(pk, 0, 255);
        MonEdit.SetEV(pk, 1, 255);
        MonEdit.SetEV(pk, 2, 255); // only 0 left of 510
        Assert.Equal(510, pk.EVTotal);
        Assert.Equal(0, pk.EV_DEF);
        MonEdit.SetIV(pk, 5, 99);
        Assert.Equal(31, pk.IV_SPE);
    }

    [Theory]
    [InlineData(GameVersion.FR)]
    [InlineData(GameVersion.E)]
    [InlineData(GameVersion.HG)]
    [InlineData(GameVersion.Pt)]
    [InlineData(GameVersion.B2)]
    public void TrainerDexAndBagAreWrittenAndReadBack(GameVersion v)
    {
        var sav = Open(v switch
        {
            GameVersion.FR or GameVersion.E => Fixtures.Gen3(v),
            GameVersion.HG or GameVersion.Pt => Fixtures.Gen4(v),
            _ => Fixtures.Gen5(v),
        });
        sav.OT = "RED";
        sav.Money = 123456;
        sav.PlayedHours = 77;
        int badges = TrainerEdit.BadgeCount(sav) == 16 ? 0x81FF : 0xA5;
        TrainerEdit.SetBadges(sav, badges);
        DexEdit.Set(sav, (ushort)Species.Pikachu, true, true);
        DexEdit.Set(sav, (ushort)Species.Bulbasaur, true, false);
        var bag = sav.Inventory;
        var pouch = bag.Pouches.First(p => p.Type != InventoryType.KeyItems && p.MaxCount > 1 && p.GetAllItems().Length > 0);
        var ball = pouch.GetAllItems()[0];
        pouch.GiveItem(bag, ball, 42);
        bag.CopyTo(sav);

        var back = RoundTrip(sav);
        Assert.True(back.ChecksumsValid);
        Assert.Equal("RED", back.OT);
        Assert.Equal(123456u, back.Money);
        Assert.Equal(77, back.PlayedHours);
        Assert.Equal(badges, TrainerEdit.GetBadges(back));
        Assert.True(DexEdit.Caught(back, (ushort)Species.Pikachu));
        Assert.True(DexEdit.Seen(back, (ushort)Species.Bulbasaur));
        Assert.False(DexEdit.Caught(back, (ushort)Species.Bulbasaur));
        Assert.False(DexEdit.Seen(back, (ushort)Species.Mew));
        var item = back.Inventory.Pouches.First(p => p.Type == pouch.Type).Items.First(i => i.Index == ball);
        Assert.Equal(42, item.Count);

        DexEdit.Set(back, (ushort)Species.Pikachu, false, false);
        Assert.False(DexEdit.Seen(RoundTrip(back), (ushort)Species.Pikachu));
    }
}
