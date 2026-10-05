using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>Display names, from PKHeX's tables in the configured language.</summary>
public static class Names
{
    public static void SetLanguage(string lang)
    {
        var known = GameLanguage.AllSupportedLanguages;
        if (!known.Contains(lang))
            lang = GameLanguage.DefaultLanguage;
        GameInfo.CurrentLanguage = lang;
        GameInfo.Strings = GameInfo.GetStrings(lang);
    }

    public static string Game(GameVersion v) => v switch
    {
        GameVersion.RBY or GameVersion.RB => "Red/Blue/Yellow",
        GameVersion.GS => "Gold/Silver",
        GameVersion.GSC => "Gold/Silver/Crystal",
        GameVersion.RS => "Ruby/Sapphire",
        GameVersion.RSE => "Ruby/Sapphire/Emerald",
        GameVersion.FRLG => "FireRed/LeafGreen",
        GameVersion.DP => "Diamond/Pearl",
        GameVersion.DPPt => "Diamond/Pearl/Platinum",
        GameVersion.HGSS => "HeartGold/SoulSilver",
        GameVersion.BW => "Black/White",
        GameVersion.B2W2 => "Black 2/White 2",
        GameVersion.XY => "X/Y",
        GameVersion.ORAS => "Omega Ruby/Alpha Sapphire",
        GameVersion.SM => "Sun/Moon",
        GameVersion.USUM => "Ultra Sun/Ultra Moon",
        GameVersion.Any or GameVersion.Invalid => "Unknown game",
        _ => GameInfo.GetVersionName(v),
    };

    public static string Species(ushort species)
    {
        var list = GameInfo.Strings.Species;
        return species < list.Count ? list[species] : $"#{species}";
    }

    public static string Move(ushort move)
    {
        var list = GameInfo.Strings.Move;
        return move < list.Count ? list[move] : $"Move {move}";
    }

    public static string Item(PKM pk)
    {
        if (pk.HeldItem <= 0)
            return "";
        var items = GameInfo.Strings.GetItemStrings(pk.Context, pk.Version);
        return pk.HeldItem < items.Length ? items[pk.HeldItem] : $"Item {pk.HeldItem}";
    }

    public static string TrainerId(ITrainerID32 tr) =>
        tr.GetDisplayTID().ToString(tr.TrainerIDDisplayFormat == TrainerIDFormat.SixDigit ? "D6" : "D5");

    public static string GenderSymbol(byte gender) => gender switch { 0 => "♂", 1 => "♀", _ => "" };
}

/// <summary>Everything the screens show about one Pokémon, read once.</summary>
public sealed record MonSummary(
    ushort Species,
    byte Form,
    string SpeciesName,
    string Nickname,
    bool IsNicknamed,
    bool IsEgg,
    bool IsShiny,
    byte Gender,
    int Level,
    string Nature,
    string Ability,
    string Item,
    string[] Moves,
    int[] IVs,
    int[] EVs,
    int[] Stats,
    string OT,
    string TrainerId,
    string Origin,
    string Ball,
    string Met,
    string Format,
    uint PID,
    string Language,
    bool Untraded)
{
    /// <summary>The name shown on a slot: the nickname, or the species when it has none.</summary>
    public string Title => IsEgg ? "Egg" : IsNicknamed ? Nickname : SpeciesName;

    public static MonSummary From(PKM pk)
    {
        var s = GameInfo.Strings;
        var gen = pk.Format;
        string nature = gen >= 3 && (int)pk.Nature < s.Natures.Count ? s.Natures[(int)pk.Nature] : "";
        string ability = gen >= 3 && pk.Ability < s.Ability.Count ? s.Ability[pk.Ability] : "";
        string ball = pk.Ball < s.balllist.Length && gen >= 3 ? s.balllist[pk.Ball] : "";
        string met = "";
        try
        {
            if (gen >= 3 || pk.MetLocation != 0)
                met = GameInfo.GetLocationName(false, pk.MetLocation, pk.Format, pk.Generation, pk.Version);
        }
        catch (Exception)
        {
            // an unknown location id
        }
        var moves = new[] { pk.Move1, pk.Move2, pk.Move3, pk.Move4 }.Where(m => m != 0).Select(Names.Move).ToArray();
        var ivs = new[] { pk.IV_HP, pk.IV_ATK, pk.IV_DEF, pk.IV_SPA, pk.IV_SPD, pk.IV_SPE };
        var evs = new[] { pk.EV_HP, pk.EV_ATK, pk.EV_DEF, pk.EV_SPA, pk.EV_SPD, pk.EV_SPE };
        int[] stats;
        try
        {
            var c = pk.Clone();
            c.ForcePartyData();
            stats = [c.Stat_HPMax, c.Stat_ATK, c.Stat_DEF, c.Stat_SPA, c.Stat_SPD, c.Stat_SPE];
        }
        catch (Exception)
        {
            stats = new int[6];
        }
        string lang = gen >= 3 || pk.Japanese ? ((LanguageID)pk.Language).ToString() : "";
        return new MonSummary(pk.Species, pk.Form, Names.Species(pk.Species), pk.Nickname, pk.IsNicknamed, pk.IsEgg, pk.IsShiny,
            pk.Gender, pk.CurrentLevel, nature, ability, Names.Item(pk), moves, ivs, evs, stats, pk.OriginalTrainerName,
            Names.TrainerId(pk), Names.Game(pk.Version), ball, met, pk.Extension.ToUpperInvariant(), pk.PID, lang,
            pk.IsUntraded);
    }
}
