using PKHeX.Core;
using static System.Buffers.Binary.BinaryPrimitives;

namespace Rocknixds.Bank.App;

/// <summary>
/// <c>--demo DIR</c>: a sandbox to try the app (and take its screenshots) without real saves: game saves made from scratch
/// for HeartGold, Black 2 and FireRed with legal Pokémon in their boxes, a bank with a few more (one of them edited
/// into something illegal), and the settings that point at all that.
/// </summary>
public static class Demo
{
    public static string Make(string dir)
    {
        dir = Path.GetFullPath(dir);
        var roms = Path.Combine(dir, "roms");
        var data = Path.Combine(dir, "data");
        if (Directory.Exists(data))
            Directory.Delete(data, true);
        Directory.CreateDirectory(Path.Combine(roms, "nds"));
        Directory.CreateDirectory(Path.Combine(roms, "gba"));

        var hg = Gen4HGSS();
        Fill(hg, GameVersion.HG, "JORRE", [Species.Cyndaquil, Species.Kadabra, Species.Togepi, Species.Mareep, Species.Onix, Species.Eevee,
            Species.Pikachu, Species.Heracross, Species.Wooper, Species.Magikarp, Species.Gastly, Species.Sneasel]);
        File.WriteAllBytes(Path.Combine(roms, "nds", "Pokemon - HeartGold Version (USA).dsv"), WithDsvFooter(hg.Write().ToArray()));

        var b2 = BlankSaveFile.Get(GameVersion.B2, "JORRE");
        Trainer(b2, "JORRE");
        Fill(b2, GameVersion.B2, "JORRE", [Species.Snivy, Species.Karrablast, Species.Riolu, Species.Zorua, Species.Litwick, Species.Deino]);
        File.WriteAllBytes(Path.Combine(roms, "nds", "Pokemon - Black Version 2 (USA, Europe).sav"), b2.Write().ToArray());

        var fr = Gen3FRLG();
        Fill(fr, GameVersion.FR, "JORRE", [Species.Charmander, Species.Abra, Species.Machop, Species.Eevee, Species.Snorlax]);
        File.WriteAllBytes(Path.Combine(roms, "gba", "Pokemon - FireRed Version (USA).srm"), fr.Write().ToArray());

        var cfg = new BankConfig { SaveFolders = [roms], DataFolder = data, TrainerName = "Jorre's RG DS", BankBoxes = 12 };
        cfg.FillDefaults();
        cfg.WithPath(Path.Combine(dir, "bank.json"));
        var bank = BankStore.Open(cfg);
        int slot = 0;
        foreach (var (s, v) in new[] { (Species.Treecko, GameVersion.E), (Species.Bulbasaur, GameVersion.LG), (Species.Chimchar, GameVersion.Pt),
                     (Species.Lapras, GameVersion.HG), (Species.Haunter, GameVersion.SS), (Species.Shelmet, GameVersion.B2), (Species.Dratini, GameVersion.HG) })
        {
            bank.Put(0, slot++, Make(s, v, "ASH", 24680, shiny: s == Species.Lapras));
        }
        var hacked = Make(Species.Pikachu, GameVersion.HG, "HAXX", 1);
        hacked.CurrentLevel = 5;
        hacked.SetMove(0, (ushort)Move.HydroPump);
        hacked.SetMove(1, (ushort)Move.Fly);
        hacked.RefreshChecksum();
        bank.Put(0, 9, hacked);
        bank.RenameBox(0, "Favourites");
        cfg.LastSave = Path.Combine(roms, "nds", "Pokemon - HeartGold Version (USA).dsv");
        cfg.Save();
        return cfg.ConfigPath;
    }

    private static void Fill(SaveFile sav, GameVersion v, string ot, Species[] species)
    {
        for (int i = 0; i < species.Length; i++)
        {
            var pk = Make(species[i], v, ot, sav.TID16, sav.SID16);
            var converted = EntityConverter.ConvertToType(pk, sav.PKMType, out _) ?? pk;
            sav.SetBoxSlotAtIndex(converted, i / 6 == 0 ? 0 : 1, i % 6 + (i / 6 == 0 ? 0 : 6));
        }
    }

    private static PKM Make(Species species, GameVersion version, string ot, ushort tid = 12345, ushort sid = 54321, bool shiny = false)
    {
        var criteria = shiny ? new EncounterCriteria { Shiny = Shiny.Always } : EncounterCriteria.Unrestricted;
        var tr = new SimpleTrainerInfo(version) { OT = ot, TID16 = tid, SID16 = sid, Language = (int)LanguageID.English };
        var template = EntityBlank.GetBlank(tr.Context);
        template.Species = (ushort)species;
        template.Version = version;
        template.Language = tr.Language;
        foreach (var enc in EncounterMovesetGenerator.GenerateEncounters(template, tr, ReadOnlyMemory<ushort>.Empty, version))
        {
            if (enc.Species != (ushort)species)
                continue;
            var pk = enc.ConvertToPKM(tr, criteria);
            if (pk.IsEgg || (shiny && !pk.IsShiny))
                continue;
            var plain = pk.Clone();
            if (pk.CurrentLevel < 12)
            {
                // a little grown up: a few levels and the moves that go with them
                pk.CurrentLevel = (byte)(12 + Random.Shared.Next(25));
                pk.SetMoveset();
                pk.ResetPartyStats();
            }
            if (new LegalityAnalysis(pk).Valid)
                return pk;
            if (new LegalityAnalysis(plain).Valid)
                return plain;
        }
        throw new InvalidOperationException($"no legal {species} for {version}");
    }

    private static void Trainer(SaveFile sav, string ot)
    {
        sav.OT = ot;
        sav.TID16 = 12345;
        sav.SID16 = 54321;
        sav.Language = (int)LanguageID.English;
        sav.PlayedHours = 42;
        sav.PlayedMinutes = 17;
    }

    private static SaveFile Gen4HGSS()
    {
        var d = new byte[0x80000];
        const int gSize = SAV4HGSS.GeneralSize, sSize = 0x12310, sStart = SAV4HGSS.GeneralSize + 0xD8;
        foreach (var part in new[] { 0, 0x40000 })
        {
            foreach (var (start, len) in new[] { (0, gSize), (sStart, sSize) })
            {
                var f = d.AsSpan(part + start + len - 0x14);
                WriteUInt32LittleEndian(f, part == 0 ? 2u : 1u);
                WriteUInt32LittleEndian(f[4..], 1);
                WriteUInt32LittleEndian(f[8..], (uint)len);
                WriteUInt32LittleEndian(f[12..], SAV4.MAGIC_JAPAN_INTL);
            }
        }
        var sav = SaveUtil.GetSaveFile(d)!;
        sav.Version = GameVersion.HG;
        Trainer(sav, "JORRE");
        return sav;
    }

    private static SaveFile Gen3FRLG()
    {
        var d = new byte[0x20000];
        foreach (var (slot, counter) in new[] { (0, 2u), (1, 1u) })
        {
            for (int i = 0; i < 14; i++)
            {
                var s = d.AsSpan(slot * 0xE000 + i * 0x1000);
                WriteInt16LittleEndian(s[0xFF4..], (short)i);
                WriteUInt32LittleEndian(s[0xFF8..], 0x08012025);
                WriteUInt32LittleEndian(s[0xFFC..], counter);
            }
        }
        WriteUInt32LittleEndian(d.AsSpan(0xAC), 1);
        d[6] = d[7] = 0xFF; // an international save: a Japanese one leaves these two bytes of the name zero
        var sav = SaveUtil.GetSaveFile(d)!;
        sav.Version = GameVersion.FR;
        Trainer(sav, "JORRE");
        return sav;
    }

    private static byte[] WithDsvFooter(byte[] raw)
    {
        var footer = new byte[0x7A];
        System.Text.Encoding.ASCII.GetBytes("|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:").CopyTo(footer, 0);
        System.Text.Encoding.ASCII.GetBytes("|-DESMUME SAVE-|").CopyTo(footer, 0x7A - 16);
        return [.. raw, .. footer];
    }
}
