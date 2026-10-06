using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>A game save found on the card.</summary>
public sealed record SaveEntry(
    string Path,
    GameVersion Version,
    byte Generation,
    string Trainer,
    string TrainerId,
    string PlayTime,
    int BoxCount,
    int Pokemon,
    DateTime Modified)
{
    /// <summary>The game, as its box says it ("HeartGold", "FireRed").</summary>
    public string GameName => Names.Game(Version);

    /// <summary>The file's name without its extension: usually the ROM's ("Pokemon - HeartGold Version (USA)").</summary>
    public string FileTitle => System.IO.Path.GetFileNameWithoutExtension(Path);

    public string Folder => System.IO.Path.GetFileName(System.IO.Path.GetDirectoryName(Path) ?? "") ?? "";
}

/// <summary>Finds game saves under the configured folders.</summary>
public static class SaveScanner
{
    // what emulators on ROCKNIX write: DraStic .dsv (next to the ROM), melonDS/mGBA/gpSP .sav, RetroArch .srm,
    // VBA .sa1/.sa2, and .fla/.dat from a few GBA cores. PKHeX recognises the game from the contents, not the name.
    private static readonly HashSet<string> Extensions = new(StringComparer.OrdinalIgnoreCase)
        { ".dsv", ".sav", ".srm", ".sa1", ".sa2", ".fla", ".dat", ".sav1" };

    public static List<SaveEntry> Scan(BankConfig cfg, Action<string>? log = null)
    {
        var found = new List<SaveEntry>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var skip = new HashSet<string>(cfg.SkipFolders, StringComparer.OrdinalIgnoreCase);
        var dataFolder = Path.GetFullPath(cfg.DataFolder).TrimEnd('/');
        foreach (var root in cfg.SaveFolders)
        {
            if (Directory.Exists(root))
                Walk(root, 0);
        }
        found.Sort((a, b) => b.Modified.CompareTo(a.Modified));
        return found;

        void Walk(string dir, int depth)
        {
            IEnumerable<string> files, dirs;
            try
            {
                files = Directory.EnumerateFiles(dir);
                dirs = Directory.EnumerateDirectories(dir);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                return;
            }

            foreach (var f in files)
            {
                if (!Extensions.Contains(Path.GetExtension(f)))
                    continue;
                var full = Path.GetFullPath(f);
                if (!seen.Add(full))
                    continue;
                var entry = TryRead(full, log);
                if (entry is not null)
                    found.Add(entry);
            }

            if (depth >= 4)
                return;
            foreach (var d in dirs)
            {
                var name = Path.GetFileName(d);
                if (name.StartsWith('.') || skip.Contains(name))
                    continue;
                if (Path.GetFullPath(d).TrimEnd('/') == dataFolder)
                    continue; // our own backups are saves too
                Walk(d, depth + 1);
            }
        }
    }

    public static SaveEntry? TryRead(string path, Action<string>? log = null)
    {
        try
        {
            var info = new FileInfo(path);
            if (!info.Exists || !SaveUtil.IsSizeValid(info.Length))
                return null;
            if (!SaveUtil.TryGetSaveFile(path, out var sav) || !sav.HasBox)
                return null;
            return Describe(path, sav, info.LastWriteTimeUtc);
        }
        catch (Exception ex)
        {
            log?.Invoke($"skipped {path}: {ex.Message}");
            return null;
        }
    }

    internal static SaveEntry Describe(string path, SaveFile sav, DateTime modified)
    {
        int count = 0;
        for (int b = 0; b < sav.BoxCount; b++)
        {
            for (int s = 0; s < sav.BoxSlotCount; s++)
            {
                try
                {
                    if (sav.GetBoxSlotAtIndex(b, s).Species != 0)
                        count++;
                }
                catch (Exception)
                {
                    // a corrupt slot doesn't make the save unusable
                }
            }
        }
        return new SaveEntry(path, sav.Version, sav.Generation, sav.OT, Names.TrainerId(sav), sav.PlayTimeString.Replace('ː', ':'),
            sav.BoxCount, count, modified);
    }
}

/// <summary>
/// One game save, opened for moving Pokémon in and out of its boxes. Every change is written to the card straight away
/// (<see cref="Commit"/>), after one backup of the file as it was before this session touched it.
/// </summary>
public sealed class SaveSession
{
    private readonly BankConfig _cfg;
    private DateTime _mtime;
    private long _size;
    private bool _backedUp;

    public SaveFile Sav { get; private set; }
    public string Path { get; }
    public SaveEntry Entry { get; private set; }

    private SaveSession(BankConfig cfg, string path, SaveFile sav)
    {
        _cfg = cfg;
        Path = path;
        Sav = sav;
        var info = new FileInfo(path);
        _mtime = info.LastWriteTimeUtc;
        _size = info.Length;
        Entry = SaveScanner.Describe(path, sav, _mtime);
    }

    public static SaveSession Open(BankConfig cfg, string path)
    {
        if (!SaveUtil.TryGetSaveFile(path, out var sav))
            throw new BankException($"{System.IO.Path.GetFileName(path)} isn't a save PKHeX can read.");
        if (!sav.HasBox)
            throw new BankException($"{Names.Game(sav.Version)} saves have no PC boxes.");
        if (!sav.ChecksumsValid)
            throw new BankException($"{System.IO.Path.GetFileName(path)}: the save's checksums are wrong (a damaged save). It is left alone.");
        return new SaveSession(cfg, path, sav);
    }

    public int BoxCount => Sav.BoxCount;
    public int SlotsPerBox => Sav.BoxSlotCount;

    public string BoxName(int box)
    {
        try
        {
            if (Sav is IBoxDetailNameRead n)
            {
                var name = n.GetBoxName(box).Trim();
                // an unwritten name block reads as question marks or control characters
                if (name.Length > 0 && name.Any(char.IsLetterOrDigit) && !name.Any(char.IsControl))
                    return name;
            }
        }
        catch (Exception)
        {
            // unnamed
        }
        return $"Box {box + 1}";
    }

    public PKM? Get(int box, int slot)
    {
        var pk = Sav.GetBoxSlotAtIndex(box, slot);
        return pk.Species == 0 ? null : pk;
    }

    /// <summary>The slot can't be changed: the game's battle box, a team in use and the like.</summary>
    public bool IsLocked(int box, int slot) => Sav.IsBoxSlotLocked(box, slot) || Sav.IsBoxSlotOverwriteProtected(box, slot);

    public IReadOnlyList<PKM> Party
    {
        get
        {
            var list = new List<PKM>();
            if (!Sav.HasParty)
                return list;
            for (int i = 0; i < Sav.PartyCount; i++)
            {
                var pk = Sav.GetPartySlotAtIndex(i);
                if (pk.Species != 0)
                    list.Add(pk);
            }
            return list;
        }
    }

    /// <summary>Puts <paramref name="pk"/> (already in this game's format) in a slot, as if it had been traded in.</summary>
    internal void Set(int box, int slot, PKM pk)
    {
        if (pk.GetType() != Sav.PKMType)
            throw new InvalidOperationException($"{pk.GetType().Name} into a {Sav.PKMType.Name} save");
        Sav.SetBoxSlotAtIndex(pk, box, slot);
    }

    internal void Clear(int box, int slot) => Sav.SetBoxSlotAtIndex(Sav.BlankPKM, box, slot, EntityImportSettings.None);

    /// <summary>True when something else (the game) wrote the file since it was opened.</summary>
    public bool ChangedOnDisk
    {
        get
        {
            var info = new FileInfo(Path);
            return !info.Exists || info.LastWriteTimeUtc != _mtime || info.Length != _size;
        }
    }

    /// <summary>Writes the save to the card: footer and all (DraStic's .dsv footer stays), checksums fixed.</summary>
    public void Commit()
    {
        if (ChangedOnDisk)
            throw new BankException($"{System.IO.Path.GetFileName(Path)} changed on the card since it was opened. Open it again.");
        if (!_backedUp)
        {
            Backup();
            _backedUp = true;
        }
        var data = Sav.Write();
        FileUtil.WriteAtomic(Path, data.Span);
        var info = new FileInfo(Path);
        _mtime = info.LastWriteTimeUtc;
        _size = info.Length;
        Entry = SaveScanner.Describe(Path, Sav, _mtime);
    }

    /// <summary>Throws away changes that weren't written: reads the file again.</summary>
    public void Reload()
    {
        if (!SaveUtil.TryGetSaveFile(Path, out var sav))
            throw new BankException($"{System.IO.Path.GetFileName(Path)} can't be read any more.");
        Sav = sav;
        var info = new FileInfo(Path);
        _mtime = info.LastWriteTimeUtc;
        _size = info.Length;
        Entry = SaveScanner.Describe(Path, sav, _mtime);
    }

    private void Backup()
    {
        var dir = System.IO.Path.Combine(_cfg.BackupFolder, FileUtil.Sanitize(System.IO.Path.GetFileName(Path)));
        Directory.CreateDirectory(dir);
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        var dest = System.IO.Path.Combine(dir, stamp + System.IO.Path.GetExtension(Path));
        for (int i = 2; File.Exists(dest); i++)
            dest = System.IO.Path.Combine(dir, $"{stamp}-{i}{System.IO.Path.GetExtension(Path)}");
        File.Copy(Path, dest);
        var old = new DirectoryInfo(dir).GetFiles().OrderByDescending(f => f.Name, StringComparer.Ordinal).Skip(_cfg.BackupsPerSave);
        foreach (var f in old)
        {
            try { f.Delete(); } catch (IOException) { /* next time */ }
        }
    }
}

public sealed class BankException(string message) : Exception(message);
