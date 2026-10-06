using PKHeX.Core;

namespace Rocknixds.Bank;

public enum Area { Bank, Save }

/// <summary>A box slot: in the bank, or in the open game save.</summary>
public readonly record struct Slot(Area Area, int Box, int Index)
{
    public static Slot InBank(int box, int index) => new(Area.Bank, box, index);
    public static Slot InSave(int box, int index) => new(Area.Save, box, index);
}

/// <summary>Whether a Pokémon can go into a slot, and in what shape (converted to that game's format).</summary>
public sealed record PlaceCheck(PKM? Result, string? Blocked, IReadOnlyList<string> Warnings, string? Note)
{
    public bool Ok => Blocked is null;
}

public sealed record MoveResult(bool Done, string? Blocked, IReadOnlyList<string> Warnings, string Message)
{
    /// <summary>The move waits for the player to accept <see cref="Warnings"/> (a Pokémon that fails the legality check).</summary>
    public bool NeedsConfirm => !Done && Blocked is null && Warnings.Count > 0;
}

/// <summary>
/// Moves Pokémon between the bank and the open save, converting them where they change generation (as Pal Park and
/// Poké Transfer did), and refusing what the destination game can't hold. Every move is written to the card before it
/// returns, and in an order where a crash can at worst leave a copy in both places, never lose a Pokémon.
/// </summary>
public sealed class Mover(BankConfig cfg, BankStore bank, History history)
{
    public BankStore Bank { get; } = bank;
    public SaveSession? Save { get; set; }

    public PKM? Read(Slot s) => s.Area == Area.Bank ? Bank.Get(s.Box, s.Index) : Save?.Get(s.Box, s.Index);

    public bool IsLocked(Slot s) => s.Area == Area.Save && Save is not null && Save.IsLocked(s.Box, s.Index);

    /// <summary>Checks <paramref name="pk"/> for a slot in <paramref name="area"/>.</summary>
    public PlaceCheck Check(PKM pk, Area area)
    {
        if (area == Area.Bank)
            return new PlaceCheck(pk, null, [], null); // the bank keeps every format as it is
        var sav = Save ?? throw new InvalidOperationException("no save open");
        return CheckForSave(cfg, pk, sav.Sav);
    }

    public static PlaceCheck CheckForSave(BankConfig cfg, PKM pk, SaveFile sav)
    {
        var name = Names.Species(pk.Species);
        var game = Names.Game(sav.Version);
        string? note = null;
        PKM? converted;
        if (pk.GetType() == sav.PKMType)
        {
            converted = pk.Clone();
        }
        else
        {
            if (!EntityConverter.IsConvertibleToFormat(pk, sav.Generation))
                return new PlaceCheck(null, $"{name} can't go back to {game}: Pokémon only travel forward to newer games.", [], null);
            converted = EntityConverter.ConvertToType(pk, sav.PKMType, out var result);
            if (converted is null || !result.IsSuccess)
                return new PlaceCheck(null, $"{name} can't go to {game}: {result.GetDisplayString(pk, sav.PKMType)}", [], null);
            note = $"Converted from {pk.Extension.ToUpperInvariant()} for {game}.";
        }

        if (sav is ILangDeviantSave il && !EntityConverter.IsCompatibleGB(converted, il.Japanese, converted.Japanese))
            return new PlaceCheck(null, $"{name} is from a {(converted.Japanese ? "Japanese" : "non-Japanese")} game; {game} can't hold it.", [], null);

        var errata = sav.EvaluateCompatibility(converted);
        if (errata.Count > 0)
            return new PlaceCheck(null, $"{name} can't go to {game}: {string.Join(" ", errata)}", [], null);

        var warnings = new List<string>();
        var verdict = Legality.Check(converted);
        if (verdict.Analysed && !verdict.Valid)
        {
            var why = $"{name} fails the legality check: {verdict.Headline}";
            if (cfg.BlockIllegalIntoSaves)
                return new PlaceCheck(null, why, [], null);
            warnings.Add(why);
        }
        return new PlaceCheck(converted, null, warnings, note);
    }

    /// <summary>Moves the Pokémon in <paramref name="from"/> to <paramref name="to"/>, swapping with what's there.</summary>
    public MoveResult Move(Slot from, Slot to, bool confirmed = false)
    {
        if (from == to)
            return new MoveResult(false, null, [], "");
        var a = Read(from) ?? throw new InvalidOperationException("nothing to move");
        var b = Read(to);
        if (IsLocked(from) || IsLocked(to))
            return Refuse("That slot is locked by the game (its battle box or a team in use).");
        if ((from.Area == Area.Save || to.Area == Area.Save) && Save is null)
            throw new InvalidOperationException("no save open");

        var checkA = Check(a, to.Area);
        if (!checkA.Ok)
            return Refuse(checkA.Blocked!);
        PlaceCheck? checkB = null;
        if (b is not null)
        {
            checkB = Check(b, from.Area);
            if (!checkB.Ok)
                return Refuse(checkB.Blocked!);
        }
        var warnings = checkA.Warnings.Concat(checkB?.Warnings ?? []).ToList();
        if (warnings.Count > 0 && !confirmed)
            return new MoveResult(false, null, warnings, "");

        var aNew = checkA.Result!;
        var bNew = checkB?.Result;
        switch (from.Area, to.Area)
        {
            case (Area.Bank, Area.Bank):
                Bank.Move(from.Box, from.Index, to.Box, to.Index);
                break;
            case (Area.Save, Area.Save):
                ApplyToSave(() =>
                {
                    Save!.Set(to.Box, to.Index, aNew);
                    if (bNew is not null)
                        Save.Set(from.Box, from.Index, bNew);
                    else
                        Save.Clear(from.Box, from.Index);
                });
                break;
            case (Area.Save, Area.Bank):
                SaveToBank(from, to, a, b, bNew);
                break;
            case (Area.Bank, Area.Save):
                BankToSave(from, to, aNew, b);
                break;
        }

        var what = Describe(a, from, to);
        history.Log(b is null ? $"MOVE {what}" : $"SWAP {what} <-> {Describe(b, to, from)}");
        var msg = b is null ? $"{Names.Species(a.Species)} moved to {Where(to)}." : $"{Names.Species(a.Species)} and {Names.Species(b.Species)} swapped places.";
        var notes = string.Join(" ", new[] { checkA.Note, checkB?.Note }.Where(n => n is not null));
        return new MoveResult(true, null, warnings, notes.Length > 0 ? $"{msg} {notes}" : msg);

        MoveResult Refuse(string why) => new(false, why, [], why);
    }

    // save -> bank: the bank copy is written first; the save loses its Pokémon only once the bank has it.
    private void SaveToBank(Slot from, Slot to, PKM a, PKM? b, PKM? bNew)
    {
        var staged = Bank.Stage(a);
        try
        {
            ApplyToSave(() =>
            {
                if (bNew is not null)
                    Save!.Set(from.Box, from.Index, bNew);
                else
                    Save!.Clear(from.Box, from.Index);
            });
        }
        catch
        {
            BankStore.Unstage(staged);
            throw;
        }
        Bank.Place(to.Box, to.Index, a, staged); // replaces b's file, which is in the save now
        _ = b;
    }

    // bank -> save: the save gets its Pokémon first; the bank's file goes only once the save is on the card.
    private void BankToSave(Slot from, Slot to, PKM aNew, PKM? b)
    {
        string? stagedB = b is null ? null : Bank.Stage(b); // the save's Pokémon, on its way into the bank
        try
        {
            ApplyToSave(() => Save!.Set(to.Box, to.Index, aNew));
        }
        catch
        {
            if (stagedB is not null)
                BankStore.Unstage(stagedB);
            throw;
        }
        if (stagedB is not null)
            Bank.Place(from.Box, from.Index, b!, stagedB);
        else
            Bank.Take(from.Box, from.Index);
    }

    private void ApplyToSave(Action change)
    {
        try
        {
            change();
            Save!.Commit();
        }
        catch
        {
            try { Save!.Reload(); } catch (BankException) { /* the caller reports the first error */ }
            throw;
        }
    }

    /// <summary>Takes a Pokémon out of its slot for good (it was traded away).</summary>
    public void Remove(Slot s, string why)
    {
        var pk = Read(s) ?? throw new InvalidOperationException("empty slot");
        if (s.Area == Area.Bank)
            Bank.Take(s.Box, s.Index);
        else
            ApplyToSave(() => Save!.Clear(s.Box, s.Index));
        history.Log($"{why} {Describe(pk, s, null)}");
    }

    public string Where(Slot s) => s.Area == Area.Bank
        ? $"{Bank.BoxName(s.Box)}, slot {s.Index + 1}"
        : $"{Save?.Entry.GameName} {Save?.BoxName(s.Box)}, slot {s.Index + 1}";

    private string Describe(PKM pk, Slot from, Slot? to) =>
        $"{Names.Species(pk.Species)} Lv{pk.CurrentLevel} {pk.Extension} PID {pk.PID:X8} OT {pk.OriginalTrainerName}/{Names.TrainerId(pk)}: " +
        $"{Where(from)}{(to is { } t ? " -> " + Where(t) : "")}" +
        (from.Area == Area.Save || to?.Area == Area.Save ? $" [{Path.GetFileName(Save?.Path)}]" : "");
}

/// <summary>An append-only log of every move and trade, in the data folder (history.log).</summary>
public sealed class History(string path)
{
    private readonly Lock _lock = new();

    public void Log(string line)
    {
        lock (_lock)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                // one line per event, whatever a traded Pokémon's names or a partner's name contain
                File.AppendAllText(path, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {Trade.TextGuard.Clean(line, 1000)}\n");
            }
            catch (IOException)
            {
                // the log is a convenience
            }
        }
    }
}
