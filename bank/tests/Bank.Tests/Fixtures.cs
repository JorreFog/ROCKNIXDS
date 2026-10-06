using System.Text;
using PKHeX.Core;
using static System.Buffers.Binary.BinaryPrimitives;

namespace Rocknixds.Bank.Tests;

/// <summary>
/// Game saves made from scratch, so the tests need no copyrighted data: the block structure each game writes (Gen 3
/// sectors, Gen 4 block footers; Gen 5 from PKHeX's blank save), which PKHeX then reads and writes like a real save.
/// </summary>
internal static class Fixtures
{
    public const string DeSmuMEFooterTail = "|-DESMUME SAVE-|";

    public static byte[] Gen4(GameVersion version)
    {
        var (gSize, sSize, sStart) = version switch
        {
            GameVersion.HG or GameVersion.SS => (SAV4HGSS.GeneralSize, 0x12310, SAV4HGSS.GeneralSize + 0xD8),
            GameVersion.Pt => (SAV4Pt.GeneralSize, 0x121E4, 0xCF2C),
            GameVersion.D or GameVersion.P => (SAV4DP.GeneralSize, 0x121E0, 0xC100),
            _ => throw new ArgumentOutOfRangeException(nameof(version)),
        };
        var d = new byte[0x80000];
        foreach (var part in new[] { 0, 0x40000 })
        {
            foreach (var (start, len) in new[] { (0, gSize), (sStart, sSize) })
            {
                var f = d.AsSpan(part + start + len - 0x14);
                WriteUInt32LittleEndian(f, part == 0 ? 2u : 1u); // save count: the first partition is the newer
                WriteUInt32LittleEndian(f[4..], 1);
                WriteUInt32LittleEndian(f[8..], (uint)len);
                WriteUInt32LittleEndian(f[12..], SAV4.MAGIC_JAPAN_INTL);
            }
        }
        var sav = (SaveFile)(SaveUtil.GetSaveFile(d) ?? throw new InvalidOperationException("synthetic Gen 4 save not recognised"));
        sav.Version = version;
        Trainer(sav);
        return sav.Write().ToArray();
    }

    public static byte[] Gen5(GameVersion version)
    {
        var sav = BlankSaveFile.Get(version, "TESTER");
        Trainer(sav);
        return sav.Write().ToArray();
    }

    /// <summary>FireRed/LeafGreen (key 1 at 0xAC of the small block) or Ruby/Sapphire (0).</summary>
    public static byte[] Gen3(GameVersion version)
    {
        uint key = version is GameVersion.FR or GameVersion.LG ? 1u : 0u;
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
        WriteUInt32LittleEndian(d.AsSpan(0xAC), key);
        d[6] = d[7] = 0xFF; // an international save (a Japanese one leaves these name bytes zero)
        var sav = SaveUtil.GetSaveFile(d) ?? throw new InvalidOperationException("synthetic Gen 3 save not recognised");
        sav.Version = version;
        Trainer(sav);
        return sav.Write().ToArray();
    }

    private static void Trainer(SaveFile sav)
    {
        sav.OT = "TESTER";
        sav.TID16 = 12345;
        sav.SID16 = 54321;
        sav.Language = (int)LanguageID.English;
    }

    public static byte[] WithDeSmuMEFooter(byte[] raw)
    {
        // DeSmuME's (and DraStic's) .dsv: the raw save, then a 122-byte footer ending in its signature
        var footer = new byte[0x7A];
        Encoding.ASCII.GetBytes("|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:").CopyTo(footer, 0);
        Encoding.ASCII.GetBytes(DeSmuMEFooterTail).CopyTo(footer, 0x7A - 16);
        return [.. raw, .. footer];
    }

    /// <summary>A legal Pokémon of <paramref name="species"/> as caught or hatched in <paramref name="version"/>.</summary>
    public static PKM Make(Species species, GameVersion version, string ot = "TESTER", ushort tid = 12345, ushort sid = 54321)
    {
        var tr = new SimpleTrainerInfo(version) { OT = ot, TID16 = tid, SID16 = sid, Language = (int)LanguageID.English };
        var template = EntityBlank.GetBlank(tr.Context);
        template.Species = (ushort)species;
        template.Version = version;
        template.Language = tr.Language;
        foreach (var enc in EncounterMovesetGenerator.GenerateEncounters(template, tr, ReadOnlyMemory<ushort>.Empty, version))
        {
            if (enc.Species != (ushort)species)
                continue; // e.g. the egg of a pre-evolution
            var pk = enc.ConvertToPKM(tr);
            if (pk.IsEgg)
                continue;
            if (new LegalityAnalysis(pk).Valid)
                return pk;
        }
        throw new InvalidOperationException($"no legal {species} found for {version}");
    }

    public sealed class TempDir : IDisposable
    {
        public string Path { get; } = Directory.CreateTempSubdirectory("rnbank-test-").FullName;

        public BankConfig Config()
        {
            var cfg = new BankConfig
            {
                DataFolder = System.IO.Path.Combine(Path, "data"),
                SaveFolders = [System.IO.Path.Combine(Path, "roms")],
                BankBoxes = 4,
                TrainerName = "Tester",
            };
            cfg.FillDefaults();
            Directory.CreateDirectory(cfg.SaveFolders[0]);
            return cfg.WithPath(System.IO.Path.Combine(Path, "bank.json"));
        }

        public string Write(string relative, byte[] data)
        {
            var p = System.IO.Path.Combine(Path, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(p)!);
            File.WriteAllBytes(p, data);
            return p;
        }

        public void Dispose()
        {
            try { Directory.Delete(Path, true); } catch (IOException) { }
        }
    }
}
