using System.Text.Json;
using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>
/// The bank: boxes of 30 Pokémon kept as plain PKHeX files, one per Pokémon, in the format of the game they came from:
/// <c>bank/01/07 - Pikachu.pk4</c> is box 1, slot 8. The files can be copied off, opened in PKHeX, and dropped back in
/// (into <c>import/</c>).
/// </summary>
public sealed class BankStore
{
    public const int SlotsPerBox = 30;

    private readonly BankConfig _cfg;
    private readonly PKM?[][] _slots;
    private readonly string?[][] _files;
    private List<string> _names = [];

    public int BoxCount { get; }
    public string Folder => _cfg.BankFolder;

    /// <summary>What happened while loading: files that couldn't be read, duplicates moved to free slots.</summary>
    public List<string> LoadNotes { get; } = [];

    private BankStore(BankConfig cfg)
    {
        _cfg = cfg;
        BoxCount = cfg.BankBoxes;
        _slots = new PKM?[BoxCount][];
        _files = new string?[BoxCount][];
        for (int i = 0; i < BoxCount; i++)
        {
            _slots[i] = new PKM?[SlotsPerBox];
            _files[i] = new string?[SlotsPerBox];
        }
    }

    public static BankStore Open(BankConfig cfg)
    {
        var store = new BankStore(cfg);
        Directory.CreateDirectory(cfg.BankFolder);
        Directory.CreateDirectory(cfg.ImportFolder);
        Directory.CreateDirectory(cfg.ExportFolder);
        store.LoadNames();
        store.Load();
        return store;
    }

    public PKM? Get(int box, int slot) => _slots[box][slot];

    public string BoxName(int box) => box < _names.Count && !string.IsNullOrWhiteSpace(_names[box]) ? _names[box] : $"Bank {box + 1}";

    public int Count => _slots.Sum(b => b.Count(p => p is not null));

    public int CountInBox(int box) => _slots[box].Count(p => p is not null);

    public IEnumerable<(int Box, int Slot, PKM Pk)> All()
    {
        for (int b = 0; b < BoxCount; b++)
        {
            for (int s = 0; s < SlotsPerBox; s++)
            {
                if (_slots[b][s] is { } pk)
                    yield return (b, s, pk);
            }
        }
    }

    public bool TryFindFree(out int box, out int slot, int startBox = 0)
    {
        for (int i = 0; i < BoxCount; i++)
        {
            box = (startBox + i) % BoxCount;
            for (slot = 0; slot < SlotsPerBox; slot++)
            {
                if (_slots[box][slot] is null)
                    return true;
            }
        }
        box = slot = -1;
        return false;
    }

    /// <summary>Stores <paramref name="pk"/> (any format) in an empty slot.</summary>
    public void Put(int box, int slot, PKM pk)
    {
        if (_slots[box][slot] is not null)
            throw new InvalidOperationException($"bank slot {box + 1}/{slot + 1} is taken");
        var path = FilePath(box, slot, pk);
        FileUtil.WriteAtomic(path, PkmIO.ToFileBytes(pk));
        _slots[box][slot] = pk.Clone();
        _files[box][slot] = path;
    }

    /// <summary>Takes the Pokémon out of its slot and deletes its file.</summary>
    public PKM Take(int box, int slot)
    {
        var pk = _slots[box][slot] ?? throw new InvalidOperationException($"bank slot {box + 1}/{slot + 1} is empty");
        var path = _files[box][slot];
        if (path is not null && File.Exists(path))
            File.Delete(path);
        _slots[box][slot] = null;
        _files[box][slot] = null;
        return pk;
    }

    /// <summary>Moves (or swaps) two bank slots by renaming their files.</summary>
    public void Move(int fromBox, int fromSlot, int toBox, int toSlot)
    {
        if (fromBox == toBox && fromSlot == toSlot)
            return;
        var a = _slots[fromBox][fromSlot] ?? throw new InvalidOperationException("empty slot");
        var b = _slots[toBox][toSlot];
        // The new files are written before the old ones go. Until they are renamed into place they are named without a
        // slot number, so a crash in between leaves them as files the next start puts in free slots: nothing is lost.
        var stagedA = Stage(a);
        var stagedB = b is null ? null : Stage(b);
        DeleteFile(fromBox, fromSlot);
        Place(toBox, toSlot, a, stagedA);
        if (b is not null)
            Place(fromBox, fromSlot, b, stagedB!);
    }

    /// <summary>
    /// Writes a copy of <paramref name="pk"/> into the bank folder with no slot of its own yet. A start after a crash puts
    /// such a file in a free slot, so a Pokémon on its way somewhere is never only in memory.
    /// </summary>
    internal string Stage(PKM pk)
    {
        var path = Path.Combine(Folder, $"moving-{Guid.NewGuid():N}{PkmIO.Extension(pk)}");
        FileUtil.WriteAtomic(path, PkmIO.ToFileBytes(pk));
        return path;
    }

    internal static void Unstage(string staged)
    {
        try { File.Delete(staged); } catch (IOException) { /* re-homed at the next start */ }
    }

    /// <summary>Puts a staged Pokémon in a slot, replacing (deleting) what was there.</summary>
    internal void Place(int box, int slot, PKM pk, string staged)
    {
        DeleteFile(box, slot);
        var dest = FilePath(box, slot, pk);
        Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
        File.Move(staged, dest, true);
        _slots[box][slot] = pk.Clone();
        _files[box][slot] = dest;
    }

    private void DeleteFile(int box, int slot)
    {
        var path = _files[box][slot];
        if (path is not null && File.Exists(path))
            File.Delete(path);
        _slots[box][slot] = null;
        _files[box][slot] = null;
    }

    public void RenameBox(int box, string name)
    {
        while (_names.Count <= box)
            _names.Add("");
        _names[box] = name.Trim();
        var json = JsonSerializer.Serialize(new BankBoxNames { Names = _names }, BankJson.Default.BankBoxNames);
        FileUtil.WriteAtomic(Path.Combine(Folder, "boxes.json"), System.Text.Encoding.UTF8.GetBytes(json));
    }

    /// <summary>Writes a copy of a bank Pokémon to <c>export/</c> (for PKHeX, or another bank).</summary>
    public string Export(int box, int slot)
    {
        var pk = Get(box, slot) ?? throw new InvalidOperationException("empty slot");
        var name = FileUtil.Sanitize(EntityFileNamer.GetName(pk)) + PkmIO.Extension(pk);
        var path = Path.Combine(_cfg.ExportFolder, name);
        FileUtil.WriteAtomic(path, PkmIO.ToFileBytes(pk));
        return path;
    }

    /// <summary>
    /// Moves Pokémon files dropped into <c>import/</c> (.pk1 ... .pk9, as PKHeX exports them) into free bank slots.
    /// Imported files go to <c>import/done/</c>; unreadable ones stay where they are.
    /// </summary>
    public List<string> ImportDropped()
    {
        var notes = new List<string>();
        if (!Directory.Exists(_cfg.ImportFolder))
            return notes;
        var done = Path.Combine(_cfg.ImportFolder, "done");
        foreach (var file in Directory.EnumerateFiles(_cfg.ImportFolder).OrderBy(f => f, StringComparer.Ordinal))
        {
            PKM? pk = null;
            try
            {
                var info = new FileInfo(file);
                if (EntityDetection.IsSizePlausible(info.Length))
                    pk = PkmIO.FromFileBytes(File.ReadAllBytes(file), Path.GetExtension(file));
            }
            catch (IOException)
            {
                // left in import/
            }
            if (pk is null)
            {
                notes.Add($"{Path.GetFileName(file)}: not a Pokémon file");
                continue;
            }
            if (!TryFindFree(out var box, out var slot))
            {
                notes.Add("The bank is full: the rest stays in import/.");
                break;
            }
            Put(box, slot, pk);
            Directory.CreateDirectory(done);
            File.Move(file, UniquePath(Path.Combine(done, Path.GetFileName(file))));
            notes.Add($"Imported {Names.Species(pk.Species)} into {BoxName(box)}, slot {slot + 1}");
        }
        return notes;
    }

    private string FilePath(int box, int slot, PKM pk)
    {
        var dir = Path.Combine(Folder, (box + 1).ToString("00"));
        var name = pk.IsEgg ? "Egg" : SpeciesName.GetSpeciesName(pk.Species, (int)LanguageID.English);
        return Path.Combine(dir, $"{slot:00} - {FileUtil.Sanitize(name)}{PkmIO.Extension(pk)}");
    }

    private void LoadNames()
    {
        var path = Path.Combine(Folder, "boxes.json");
        try
        {
            if (File.Exists(path))
                _names = JsonSerializer.Deserialize(File.ReadAllText(path), BankJson.Default.BankBoxNames)?.Names ?? [];
        }
        catch (Exception)
        {
            _names = [];
        }
    }

    private void Load()
    {
        var homeless = new List<(string Path, PKM Pk)>();
        foreach (var dir in Directory.EnumerateDirectories(Folder))
        {
            if (!int.TryParse(Path.GetFileName(dir), out var boxNumber))
                continue;
            int box = boxNumber - 1;
            foreach (var file in Directory.EnumerateFiles(dir).OrderBy(f => f, StringComparer.Ordinal))
            {
                var fileName = Path.GetFileName(file);
                if (fileName.StartsWith('.'))
                    continue;
                PKM? pk;
                try
                {
                    var len = new FileInfo(file).Length;
                    pk = EntityDetection.IsSizePlausible(len) ? PkmIO.FromFileBytes(File.ReadAllBytes(file), Path.GetExtension(file)) : null;
                }
                catch (IOException ex)
                {
                    LoadNotes.Add($"{fileName}: {ex.Message}");
                    continue;
                }
                if (pk is null)
                {
                    LoadNotes.Add($"bank/{Path.GetFileName(dir)}/{fileName}: not a Pokémon file, left alone");
                    continue;
                }
                var digits = new string(fileName.TakeWhile(char.IsAsciiDigit).ToArray());
                if ((uint)box < BoxCount && int.TryParse(digits, out var slot) && (uint)slot < SlotsPerBox && _slots[box][slot] is null)
                {
                    _slots[box][slot] = pk;
                    _files[box][slot] = file;
                }
                else
                {
                    homeless.Add((file, pk)); // a second file for one slot, or a box past the configured count
                }
            }
        }

        // loose files in bank/ itself: a move interrupted halfway, or files copied there by hand
        foreach (var file in Directory.EnumerateFiles(Folder).OrderBy(f => f, StringComparer.Ordinal))
        {
            var fileName = Path.GetFileName(file);
            if (fileName.StartsWith('.') || fileName.Equals("boxes.json", StringComparison.OrdinalIgnoreCase))
                continue;
            try
            {
                var len = new FileInfo(file).Length;
                if (EntityDetection.IsSizePlausible(len) && PkmIO.FromFileBytes(File.ReadAllBytes(file), Path.GetExtension(file)) is { } pk)
                    homeless.Add((file, pk));
            }
            catch (IOException)
            {
                // left alone
            }
        }

        foreach (var (path, pk) in homeless)
        {
            if (!TryFindFree(out var box, out var slot))
            {
                LoadNotes.Add($"The bank is full: {Path.GetFileName(path)} has no slot (raise bankBoxes in the settings).");
                continue;
            }
            var dest = FilePath(box, slot, pk);
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            File.Move(path, dest);
            _slots[box][slot] = pk;
            _files[box][slot] = dest;
            LoadNotes.Add($"{Path.GetFileName(path)} moved to {BoxName(box)}, slot {slot + 1}");
        }
    }

    private static string UniquePath(string path)
    {
        if (!File.Exists(path))
            return path;
        var dir = Path.GetDirectoryName(path)!;
        var stem = Path.GetFileNameWithoutExtension(path);
        var ext = Path.GetExtension(path);
        for (int i = 2; ; i++)
        {
            var p = Path.Combine(dir, $"{stem} ({i}){ext}");
            if (!File.Exists(p))
                return p;
        }
    }
}
