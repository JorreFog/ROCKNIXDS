using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>
/// Changes to one Pokémon, as PKHeX's own editor makes them: the PID-bound values of Gen 3-5 (nature, gender, shininess)
/// are changed by finding a new PID that keeps the others, so the Pokémon stays consistent.
/// </summary>
public static class MonEdit
{
    /// <summary>The versions a new Pokémon for a game of <paramref name="gen"/> can come from, newest first: the game's
    /// own, then the ones it can be moved up from (Pal Park, Poké Transfer, the Time Capsule).</summary>
    private static IEnumerable<GameVersion> Origins(GameVersion own, byte gen)
    {
        yield return own;
        GameVersion[][] byGen =
        [
            [GameVersion.RD, GameVersion.BU, GameVersion.YW, GameVersion.GN],
            [GameVersion.C, GameVersion.GD, GameVersion.SI],
            [GameVersion.E, GameVersion.FR, GameVersion.LG, GameVersion.R, GameVersion.S],
            [GameVersion.HG, GameVersion.SS, GameVersion.Pt, GameVersion.D, GameVersion.P],
            [GameVersion.B2, GameVersion.W2, GameVersion.B, GameVersion.W],
        ];
        // Gen 1 and 2 trade with each other; Gen 3 on only takes Gen 3 and newer
        int low = gen <= 2 ? 1 : 3;
        for (int g = Math.Min(gen, (byte)5); g >= low; g--)
        {
            foreach (var v in byGen[g - 1])
            {
                if (v != own)
                    yield return v;
            }
        }
    }

    /// <summary>
    /// A new Pokémon of <paramref name="species"/> for a slot in <paramref name="target"/>'s format, caught by
    /// <paramref name="trainer"/>: a real encounter from the game (or one it can be moved up from), so it passes the
    /// legality check. When no game has one, a plain level-5 Pokémon that may not pass. <paramref name="note"/> says which.
    /// </summary>
    public static PKM Create(ushort species, Type target, GameVersion version, ITrainerInfo trainer, out string note)
    {
        var lang = trainer.Language is > 0 ? trainer.Language : (int)LanguageID.English;
        var gen = trainer.Generation;
        foreach (var v in Origins(version, gen))
        {
            var tr = new SimpleTrainerInfo(v)
            {
                OT = trainer.OT, TID16 = trainer.TID16, SID16 = trainer.SID16, Gender = trainer.Gender, Language = lang,
            };
            var template = EntityBlank.GetBlank(tr.Context);
            template.Species = species;
            template.Version = v;
            template.Language = lang;
            IEnumerable<IEncounterable> encounters;
            try
            {
                encounters = EncounterMovesetGenerator.GenerateEncounters(template, tr, ReadOnlyMemory<ushort>.Empty, v).Take(40).ToList();
            }
            catch (Exception)
            {
                continue;
            }
            foreach (var enc in encounters)
            {
                if (enc.Species != species)
                    continue;
                PKM pk;
                try
                {
                    pk = enc.ConvertToPKM(tr);
                }
                catch (Exception)
                {
                    continue;
                }
                if (pk.IsEgg)
                    continue;
                if (pk.GetType() != target)
                {
                    var converted = EntityConverter.ConvertToType(pk, target, out var result);
                    if (converted is null || !result.IsSuccess)
                        continue;
                    pk = converted;
                }
                if (new LegalityAnalysis(pk).Valid)
                {
                    pk.ResetPartyStats();
                    note = v == version ? $"Caught in {Names.Game(v)}, as the game makes it." : $"Caught in {Names.Game(v)} and moved up, as the games allow.";
                    return pk;
                }
            }
        }

        // nothing legal: a plain one in the target's format
        var blank = EntityBlank.GetBlank(target);
        blank.Species = species;
        blank.Version = version;
        blank.Language = lang;
        blank.OriginalTrainerName = trainer.OT;
        blank.TID16 = trainer.TID16;
        blank.SID16 = trainer.SID16;
        blank.OriginalTrainerGender = trainer.Gender;
        blank.CurrentLevel = 5;
        blank.MetLevel = 5;
        blank.Ball = (byte)Ball.Poke;
        blank.ClearNickname();
        blank.PID = Util.Rand32();
        if (blank.Format >= 3)
            SetGender(blank, Genders(blank)[0]);
        blank.RefreshAbility(0);
        blank.SetMoveset(false);
        blank.HealPP();
        blank.CurrentFriendship = blank.PersonalInfo.BaseFriendship;
        blank.ResetPartyStats();
        blank.RefreshChecksum();
        note = $"No game has {Names.Species(species)} as an encounter that can reach {Names.Game(version)}: this one may not pass the legality check.";
        return blank;
    }

    /// <summary>Changes the species (its form back to the first, the name to the new species' unless nicknamed).</summary>
    public static void SetSpecies(PKM pk, ushort species)
    {
        pk.Species = species;
        pk.Form = 0;
        if (!pk.IsNicknamed)
            pk.ClearNickname();
        pk.RefreshAbility(Math.Max(0, AbilityIndex(pk)));
        if (!pk.IsGenderValid())
            SetGender(pk, Genders(pk)[0]);
    }

    /// <summary>The genders the species can have: 0 male, 1 female, 2 genderless.</summary>
    public static IReadOnlyList<byte> Genders(PKM pk)
    {
        var pi = pk.PersonalInfo;
        if (pi.Genderless)
            return [2];
        if (pi.OnlyMale)
            return [0];
        if (pi.OnlyFemale)
            return [1];
        return [0, 1];
    }

    /// <summary>Sets the gender; in Gen 3-5 it comes from the PID, so a new PID is found that keeps the nature and
    /// shininess. Gen 1-2 genders come from the Attack IV and aren't changed here.</summary>
    public static void SetGender(PKM pk, byte gender)
    {
        if (pk.Format <= 2)
            return;
        if (pk.Format <= 5)
            pk.SetPIDGender(gender);
        pk.Gender = gender;
    }

    public static int AbilityIndex(PKM pk) => pk.AbilityNumber switch { 1 => 0, 2 => 1, 4 => 2, _ => -1 };

    /// <summary>The abilities the species has: (index, ability ID).</summary>
    public static IReadOnlyList<(int Index, int Ability)> Abilities(PKM pk)
    {
        var pi = pk.PersonalInfo;
        var list = new List<(int, int)>();
        int count = pk.Format <= 4 ? Math.Min(2, pi.AbilityCount) : pi.AbilityCount;
        for (int i = 0; i < count; i++)
        {
            int a = pi.GetAbilityAtIndex(i);
            if (a == 0 || (i == 1 && a == pi.GetAbilityAtIndex(0)))
                continue; // a species with one ability lists it once
            list.Add((i, a));
        }
        return list;
    }

    public static void SetAbility(PKM pk, int index)
    {
        if (pk.Format is >= 3 and <= 5 && index < 2)
        {
            // Gen 3-5: the ability slot is a bit of the PID (the lowest; Gen 5's seventeenth): find a PID with that bit
            // that keeps the nature (Gen 3-4), the gender and the shininess
            int Bit(uint pid) => (int)(pk.Format == 5 ? (pid >> 16) & 1 : pid & 1);
            if (Bit(pk.PID) != index)
            {
                var nature = pk.Nature;
                byte gender = pk.Gender;
                RerollPid(pk, pid => Bit(pid) == index && (pk.Format >= 5 || (Nature)(pid % 25) == nature)
                                     && (gender == 2 || EntityGender.GetFromPID(pk.Species, pid) == gender));
                if (pk is PK3 pk3)
                    pk3.AbilityBit = index == 1;
            }
        }
        if (pk.Format is >= 3 and <= 5)
        {
            // not CommonEdits.SetAbilityIndex: it rolls a new PID of its own, and the Pokémon stopped being shiny
            if (pk is PK5 pk5)
                pk5.HiddenAbility = index == 2;
            pk.RefreshAbility(index);
        }
        else
        {
            CommonEdits.SetAbilityIndex(pk, index);
        }
    }

    /// <summary>A new PID that <paramref name="ok"/> accepts and keeps the Pokémon shiny or not as it was (a shiny one is
    /// built shiny: the trainer IDs XOR the PID's halves to under 8). False when none was found.</summary>
    public static bool RerollPid(PKM pk, Func<uint, bool> ok)
    {
        bool shiny = pk.IsShiny;
        var old = pk.PID;
        for (int i = 0; i < 1_000_000; i++)
        {
            uint pid;
            if (shiny)
            {
                uint low = Util.Rand32() & 0xFFFF;
                uint high = ((uint)pk.TID16 ^ pk.SID16 ^ low ^ (Util.Rand32() & 7)) & 0xFFFF;
                pid = (high << 16) | low;
            }
            else
            {
                pid = Util.Rand32();
            }
            if (!ok(pid))
                continue;
            pk.PID = pid;
            if (pk.IsShiny == shiny)
                return true;
        }
        pk.PID = old;
        return false;
    }

    public static void SetLevel(PKM pk, int level)
    {
        pk.CurrentLevel = (byte)Math.Clamp(level, 1, 100);
        if (pk.MetLevel > pk.CurrentLevel && pk.Format >= 3)
            pk.MetLevel = pk.CurrentLevel;
    }

    /// <summary>
    /// Fixes what PKHeX can suggest a legal value for: moves the Pokémon can know, relearn moves, a ball and the met data
    /// of an encounter that matches, a name, the gender its PID gives, EVs over the limit, an item it can't hold. What it
    /// can't fix (an impossible species, a PID that no game makes) stays. Returns what was changed.
    /// </summary>
    public static List<string> TryMakeLegal(PKM pk)
    {
        var changed = new List<string>();
        for (int pass = 0; pass < 2; pass++)
        {
            var la = new LegalityAnalysis(pk);
            if (la.Valid)
                break;
            bool Bad(CheckIdentifier id) => la.Results.Any(r => !r.Valid && r.Identifier == id);

            if (la.Info.Moves.Any(m => !m.Valid))
            {
                pk.SetMoveset(false);
                pk.HealPP();
                Add("moves");
            }
            if (pk.Format >= 6 && la.Info.Relearn.Any(m => !m.Valid))
            {
                pk.SetRelearnMoves(la);
                Add("relearn moves");
            }
            if (Bad(CheckIdentifier.Encounter) || Bad(CheckIdentifier.Level))
            {
                var met = EncounterSuggestion.GetSuggestedMetInfo(pk);
                if (met is not null)
                {
                    if (pk.Format >= 3)
                    {
                        pk.MetLocation = met.Location;
                        pk.MetLevel = met.GetSuggestedMetLevel(pk);
                    }
                    if (pk.CurrentLevel < met.LevelMin)
                        pk.CurrentLevel = met.LevelMin;
                    Add("met data");
                }
            }
            if (Bad(CheckIdentifier.Ball))
            {
                BallApplicator.ApplyBallLegalByColor(pk);
                Add("ball");
            }
            if (Bad(CheckIdentifier.Gender) && pk.Format >= 3 && pk.Format <= 5)
            {
                pk.Gender = EntityGender.GetFromPID(pk.Species, pk.PID);
                Add("gender");
            }
            if (Bad(CheckIdentifier.Nickname))
            {
                pk.SetDefaultNickname(la);
                Add("name");
            }
            if (Bad(CheckIdentifier.EVs))
            {
                pk.SetEVs([0, 0, 0, 0, 0, 0]);
                Add("EVs (reset)");
            }
            if (Bad(CheckIdentifier.HeldItem))
            {
                pk.HeldItem = 0;
                Add("held item (removed)");
            }
            if (Bad(CheckIdentifier.Ability))
            {
                pk.RefreshAbility(Math.Max(0, AbilityIndex(pk)));
                Add("ability");
            }
            if (Bad(CheckIdentifier.Ribbon))
            {
                RibbonApplicator.RemoveAllValidRibbons(pk);
                RibbonApplicator.SetAllValidRibbons(pk);
                Add("ribbons");
            }
            pk.RefreshChecksum();
        }
        return changed;

        void Add(string what)
        {
            if (!changed.Contains(what))
                changed.Add(what);
        }
    }

    /// <summary>All IVs to the most the format allows.</summary>
    public static void MaxIVs(PKM pk)
    {
        int m = pk.MaxIV;
        pk.SetIVs([m, m, m, m, m, m]);
    }

    public static int GetIV(PKM pk, int stat) => stat switch
    {
        0 => pk.IV_HP, 1 => pk.IV_ATK, 2 => pk.IV_DEF, 3 => pk.IV_SPA, 4 => pk.IV_SPD, _ => pk.IV_SPE,
    };

    public static void SetIV(PKM pk, int stat, int value)
    {
        value = Math.Clamp(value, 0, pk.MaxIV);
        switch (stat)
        {
            case 0: pk.IV_HP = value; break;
            case 1: pk.IV_ATK = value; break;
            case 2: pk.IV_DEF = value; break;
            case 3: pk.IV_SPA = value; break;
            case 4: pk.IV_SPD = value; break;
            default: pk.IV_SPE = value; break;
        }
    }

    public static int GetEV(PKM pk, int stat) => stat switch
    {
        0 => pk.EV_HP, 1 => pk.EV_ATK, 2 => pk.EV_DEF, 3 => pk.EV_SPA, 4 => pk.EV_SPD, _ => pk.EV_SPE,
    };

    /// <summary>Sets an EV, within the format's limit for one stat and (Gen 3 on) 510 for all six.</summary>
    public static void SetEV(PKM pk, int stat, int value)
    {
        int others = pk.EVTotal - GetEV(pk, stat);
        int max = pk.MaxEV;
        if (pk.Format >= 3)
            max = Math.Min(max, Math.Max(0, 510 - others));
        value = Math.Clamp(value, 0, max);
        switch (stat)
        {
            case 0: pk.EV_HP = value; break;
            case 1: pk.EV_ATK = value; break;
            case 2: pk.EV_DEF = value; break;
            case 3: pk.EV_SPA = value; break;
            case 4: pk.EV_SPD = value; break;
            default: pk.EV_SPE = value; break;
        }
    }

    public static ushort GetMove(PKM pk, int i) => i switch { 0 => pk.Move1, 1 => pk.Move2, 2 => pk.Move3, _ => pk.Move4 };

    public static int GetPPUps(PKM pk, int i) => i switch { 0 => pk.Move1_PPUps, 1 => pk.Move2_PPUps, 2 => pk.Move3_PPUps, _ => pk.Move4_PPUps };

    public static void SetMove(PKM pk, int i, ushort move)
    {
        pk.SetMove(i, move);
        if (move == 0)
            SetPPUps(pk, i, 0);
        pk.FixMoves();
        pk.HealPP();
    }

    public static void SetPPUps(PKM pk, int i, int ups)
    {
        ups = Math.Clamp(ups, 0, 3);
        if (GetMove(pk, i) == 0)
            ups = 0;
        switch (i)
        {
            case 0: pk.Move1_PPUps = ups; break;
            case 1: pk.Move2_PPUps = ups; break;
            case 2: pk.Move3_PPUps = ups; break;
            default: pk.Move4_PPUps = ups; break;
        }
        pk.HealPP();
    }

    /// <summary>Pokérus: 0 never had it, 1 infected (spreads), 2 cured (keeps the EV bonus).</summary>
    public static int PokerusState(PKM pk) => pk.PokerusStrain == 0 ? 0 : pk.PokerusDays > 0 ? 1 : 2;

    public static void SetPokerus(PKM pk, int state)
    {
        switch (state)
        {
            case 0: pk.PokerusStrain = 0; pk.PokerusDays = 0; break;
            case 1: pk.PokerusStrain = 1; pk.PokerusDays = 2; break; // strain 1 lasts 1 + (1 % 4) days
            default: pk.PokerusStrain = Math.Max(1, pk.PokerusStrain); pk.PokerusDays = 0; break;
        }
    }
}

/// <summary>The trainer's badges, in each game's own place for them.</summary>
public static class TrainerEdit
{
    /// <summary>How many badges the game has: 8, or 16 where Kanto's come after Johto's (Gold/Silver/Crystal, HGSS).</summary>
    public static int BadgeCount(SaveFile s) => s switch
    {
        SAV2 or SAV4HGSS => 16,
        SAV1 or SAV3 or SAV4 or SAV5 => 8,
        _ => 0,
    };

    public static int GetBadges(SaveFile s) => s switch
    {
        SAV1 a => a.Badges,
        SAV2 b => b.Badges,
        SAV3 c => c.Badges,
        SAV4HGSS h => h.Badges | ((h.Badges16 & 0xFF) << 8),
        SAV4 d => d.Badges,
        SAV5 e => e.Misc.Badges,
        _ => 0,
    };

    public static void SetBadges(SaveFile s, int mask)
    {
        switch (s)
        {
            case SAV1 a: a.Badges = mask & 0xFF; break;
            case SAV2 b: b.Badges = mask & 0xFFFF; break;
            case SAV3 c: c.Badges = mask & 0xFF; break;
            case SAV4HGSS h: h.Badges = (byte)(mask & 0xFF); h.Badges16 = (mask >> 8) & 0xFF; break;
            case SAV4 d: d.Badges = (byte)(mask & 0xFF); break;
            case SAV5 e: e.Misc.Badges = mask & 0xFF; break;
        }
    }

    public static string BadgeName(SaveFile s, int i) => BadgeCount(s) == 16 ? (i < 8 ? $"Johto badge {i + 1}" : $"Kanto badge {i - 7}") : $"Badge {i + 1}";
}

/// <summary>The Pokédex: seen and caught, each generation in its own blocks (Gen 4's and 5's carry gender, form and
/// language flags too, set as the game sets them for a Pokémon it registers).</summary>
public static class DexEdit
{
    public static bool Seen(SaveFile s, ushort species) => s switch
    {
        SAV4 d => d.Dex.GetSeen(species),
        SAV5 f => f.Zukan.GetSeen(species),
        _ => s.GetSeen(species),
    };

    public static bool Caught(SaveFile s, ushort species) => s switch
    {
        SAV4 d => d.Dex.GetCaught(species),
        SAV5 f => f.Zukan.GetCaught(species),
        _ => s.GetCaught(species),
    };

    /// <summary>Sets one species: caught implies seen.</summary>
    public static void Set(SaveFile s, ushort species, bool seen, bool caught)
    {
        seen |= caught;
        switch (s)
        {
            case SAV4 d:
                if (seen)
                {
                    if (!d.Dex.GetSeen(species))
                        d.Dex.SetDexEntryAll(species, false);
                    d.Dex.SetCaught(species, caught);
                }
                else
                {
                    d.Dex.ClearSeen(species);
                    d.Dex.SetCaught(species, false);
                }
                break;
            case SAV5 f:
                if (seen)
                {
                    if (!f.Zukan.GetSeen(species))
                        f.Zukan.GiveAll(species, true, false, (LanguageID)Math.Max(1, s.Language), false);
                    f.Zukan.SetCaught(species, caught);
                }
                else
                {
                    f.Zukan.ClearSeen(species);
                    f.Zukan.SetCaught(species, false);
                }
                break;
            default:
                s.SetSeen(species, seen);
                s.SetCaught(species, caught);
                break;
        }
    }
}
