using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>Where a slot is: the bank, the open game's boxes, or the open game's party (its Box is 0).</summary>
public enum Area { Bank, Save, Party }

/// <summary>A slot: in the bank, in the open game save's boxes, or in its party.</summary>
public readonly record struct Slot(Area Area, int Box, int Index)
{
    public static Slot InBank(int box, int index) => new(Area.Bank, box, index);
    public static Slot InSave(int box, int index) => new(Area.Save, box, index);
    public static Slot InParty(int index) => new(Area.Party, 0, index);

    /// <summary>In the open game (its boxes or its party).</summary>
    public bool InGame => Area != Area.Bank;
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

    public PKM? Read(Slot s) => s.Area switch
    {
        Area.Bank => Bank.Get(s.Box, s.Index),
        Area.Party => Save?.GetParty(s.Index),
        _ => Save?.Get(s.Box, s.Index),
    };

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

    /// <summary>
    /// Why the party can't take these changes (null when it can): the game needs at least one Pokémon in the party that
    /// isn't an egg, and the party holds six at most. <paramref name="writes"/> are (slot, Pokémon or null for empty).
    /// </summary>
    public string? PartyProblem(params (Slot Slot, PKM? Pk)[] writes)
    {
        if (!writes.Any(w => w.Slot.Area == Area.Party))
            return null;
        var save = Save ?? throw new InvalidOperationException("no save open");
        if (!save.HasParty)
            return $"{save.Entry.GameName} saves have no party.";
        var after = PartyAfter(save, writes);
        if (after.Count > SaveSession.PartySize)
            return "The party is full: six Pokémon at most.";
        if (!after.Any(p => !p.IsEgg))
            return "The party can't be left without a Pokémon that can battle: the game needs at least one that isn't an egg.";
        return null;
    }

    private static List<PKM> PartyAfter(SaveSession save, IEnumerable<(Slot Slot, PKM? Pk)> writes)
    {
        var slots = new List<PKM?>();
        for (int i = 0; i < save.PartyCount; i++)
            slots.Add(save.GetParty(i));
        foreach (var (s, pk) in writes)
        {
            if (s.Area != Area.Party)
                continue;
            while (slots.Count <= s.Index)
                slots.Add(null);
            slots[s.Index] = pk;
        }
        return slots.Where(p => p is not null).Select(p => p!).ToList();
    }

    /// <summary>Where <paramref name="pk"/>, written into <paramref name="s"/> with the other <paramref name="writes"/>, ends
    /// up: the party closes its gaps, so a Pokémon put past the last one lands in the first free slot.</summary>
    private Slot Landing(Slot s, PKM pk, params (Slot Slot, PKM? Pk)[] writes)
    {
        if (s.Area != Area.Party || Save is not { } save)
            return s;
        int i = PartyAfter(save, writes).FindIndex(p => ReferenceEquals(p, pk));
        return i < 0 ? s : Slot.InParty(i);
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
        if ((from.InGame || to.InGame) && Save is null)
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
        var aNew = checkA.Result!;
        var bNew = checkB?.Result;
        if (PartyProblem((to, aNew), (from, bNew)) is { } partyWhy)
            return Refuse(partyWhy);
        var warnings = checkA.Warnings.Concat(checkB?.Warnings ?? []).ToList();
        if (warnings.Count > 0 && !confirmed)
            return new MoveResult(false, null, warnings, "");

        var landing = Landing(to, aNew, (to, aNew), (from, bNew));
        switch (from.InGame, to.InGame)
        {
            case (false, false):
                Bank.Move(from.Box, from.Index, to.Box, to.Index);
                break;
            case (true, true):
                ApplyToGame((to, aNew), (from, bNew));
                break;
            case (true, false):
                SaveToBank(from, to, a, bNew);
                break;
            case (false, true):
                BankToSave(from, to, aNew, b);
                break;
        }

        var what = Describe(a, from, landing);
        history.Log(b is null ? $"MOVE {what}" : $"SWAP {what} <-> {Describe(b, to, from)}");
        var msg = b is null ? $"{Names.Species(a.Species)} moved to {Where(landing)}." : $"{Names.Species(a.Species)} and {Names.Species(b.Species)} swapped places.";
        var notes = string.Join(" ", new[] { checkA.Note, checkB?.Note }.Where(n => n is not null));
        return new MoveResult(true, null, warnings, notes.Length > 0 ? $"{msg} {notes}" : msg);

        MoveResult Refuse(string why) => new(false, why, [], why);
    }

    /// <summary>
    /// Puts a copy of the Pokémon in <paramref name="from"/> into the empty slot <paramref name="to"/> (converted when it
    /// goes into a newer game, as a move would). The original stays where it is.
    /// </summary>
    public MoveResult Copy(Slot from, Slot to, bool confirmed = false)
    {
        var a = Read(from) ?? throw new InvalidOperationException("nothing to copy");
        if (Read(to) is not null)
            return Refuse("That slot isn't empty: a copy goes into an empty slot.");
        if (IsLocked(to))
            return Refuse("That slot is locked by the game (its battle box or a team in use).");
        if (to.InGame && Save is null)
            throw new InvalidOperationException("no save open");
        var check = Check(a, to.Area);
        if (!check.Ok)
            return Refuse(check.Blocked!);
        var copy = check.Result!;
        if (PartyProblem((to, copy)) is { } partyWhy)
            return Refuse(partyWhy);
        if (check.Warnings.Count > 0 && !confirmed)
            return new MoveResult(false, null, check.Warnings, "");

        var landing = Landing(to, copy, (to, copy));
        if (to.InGame)
            ApplyToGame((to, copy));
        else
            Bank.Put(to.Box, to.Index, copy);
        history.Log($"COPY {Describe(a, from, landing)}");
        var msg = $"A copy of {Names.Species(a.Species)} is in {Where(landing)}.";
        return new MoveResult(true, null, check.Warnings, check.Note is null ? msg : $"{msg} {check.Note}");

        MoveResult Refuse(string why) => new(false, why, [], why);
    }

    /// <summary>The first empty slot for a copy of what's in <paramref name="from"/>: in the same place (its box, then
    /// the boxes after it; the party's next free slot), or null when that place is full.</summary>
    public Slot? NextFree(Slot from)
    {
        switch (from.Area)
        {
            case Area.Bank:
                return Bank.TryFindFree(out var box, out var slot, from.Box) ? Slot.InBank(box, slot) : null;
            case Area.Party:
                return Save is { } p && p.PartyCount < SaveSession.PartySize ? Slot.InParty(p.PartyCount) : null;
            default:
                var save = Save ?? throw new InvalidOperationException("no save open");
                for (int i = 0; i < save.BoxCount; i++)
                {
                    int b = (from.Box + i) % save.BoxCount;
                    for (int s = 0; s < save.SlotsPerBox; s++)
                    {
                        if (save.Get(b, s) is null && !save.IsLocked(b, s))
                            return Slot.InSave(b, s);
                    }
                }
                return null;
        }
    }

    /// <summary>
    /// Writes an edited (or newly made) Pokémon into <paramref name="s"/>, replacing what's there. It must already be in
    /// the slot's format (the editor works in it). Saves are written at once, after their one backup per session.
    /// </summary>
    public void Replace(Slot s, PKM pk, string why = "EDIT")
    {
        if (IsLocked(s))
            throw new BankException("That slot is locked by the game.");
        var old = Read(s);
        var landing = Landing(s, pk, (s, pk));
        if (s.Area == Area.Bank)
        {
            if (old is null)
                Bank.Put(s.Box, s.Index, pk);
            else
                Bank.Replace(s.Box, s.Index, pk);
        }
        else
        {
            var save = Save ?? throw new InvalidOperationException("no save open");
            if (pk.GetType() != save.Sav.PKMType)
                throw new BankException($"A {pk.Extension.ToUpperInvariant()} Pokémon can't be written into {save.Entry.GameName} as it is.");
            if (PartyProblem((s, pk)) is { } partyWhy)
                throw new BankException(partyWhy);
            ApplyToGame((s, pk));
        }
        history.Log($"{why} {Describe(pk, landing, null)}" + (old is null ? "" : $" (was {Names.Species(old.Species)} Lv{old.CurrentLevel} PID {old.PID:X8})"));
    }

    // save -> bank: the bank copy is written first; the save loses its Pokémon only once the bank has it.
    private void SaveToBank(Slot from, Slot to, PKM a, PKM? bNew)
    {
        var staged = Bank.Stage(a);
        try
        {
            ApplyToGame((from, bNew));
        }
        catch
        {
            BankStore.Unstage(staged);
            throw;
        }
        Bank.Place(to.Box, to.Index, a, staged); // replaces b's file, which is in the save now
    }

    // bank -> save: the save gets its Pokémon first; the bank's file goes only once the save is on the card.
    private void BankToSave(Slot from, Slot to, PKM aNew, PKM? b)
    {
        string? stagedB = b is null ? null : Bank.Stage(b); // the save's Pokémon, on its way into the bank
        try
        {
            ApplyToGame((to, aNew));
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

    /// <summary>Writes Pokémon (or empties, for null) into slots of the open game, then the save to the card. The party's
    /// slots are written as one list with its gaps closed, as the game keeps it.</summary>
    private void ApplyToGame(params (Slot Slot, PKM? Pk)[] writes)
    {
        var save = Save!;
        var party = writes.Any(w => w.Slot.Area == Area.Party) ? PartyAfter(save, writes) : null;
        ApplyToSave(() =>
        {
            foreach (var (s, pk) in writes)
            {
                if (s.Area != Area.Save)
                    continue;
                if (pk is null)
                    save.Clear(s.Box, s.Index);
                else
                    save.Set(s.Box, s.Index, pk);
            }
            if (party is not null)
                save.SetParty(party);
        });
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

    /// <summary>
    /// Changes the open save itself (its trainer, bag, Pokédex, box names) and writes it to the card, after the session's
    /// one backup; when anything fails the save is read again from the card, so nothing half-done stays in memory.
    /// </summary>
    public void EditSave(string what, Action<SaveFile> change)
    {
        var save = Save ?? throw new InvalidOperationException("no save open");
        ApplyToSave(() => change(save.Sav));
        history.Log($"SAVE EDIT {what} [{Path.GetFileName(save.Path)}]");
    }

    /// <summary>Why <paramref name="s"/> can't be emptied (null when it can): the party's last Pokémon that can battle.</summary>
    public string? CantEmpty(Slot s) => s.Area == Area.Party ? PartyProblem((s, null)) : null;

    /// <summary>Takes a Pokémon out of its slot for good (traded away or released).</summary>
    public void Remove(Slot s, string why)
    {
        var pk = Read(s) ?? throw new InvalidOperationException("empty slot");
        if (CantEmpty(s) is { } problem)
            throw new BankException(problem);
        if (s.Area == Area.Bank)
            Bank.Take(s.Box, s.Index);
        else
            ApplyToGame((s, null));
        history.Log($"{why} {Describe(pk, s, null)}");
    }

    public string Where(Slot s) => s.Area switch
    {
        Area.Bank => $"{Bank.BoxName(s.Box)}, slot {s.Index + 1}",
        Area.Party => $"{Save?.Entry.GameName} party, slot {s.Index + 1}",
        _ => $"{Save?.Entry.GameName} {Save?.BoxName(s.Box)}, slot {s.Index + 1}",
    };

    private string Describe(PKM pk, Slot from, Slot? to) =>
        $"{Names.Species(pk.Species)} Lv{pk.CurrentLevel} {pk.Extension} PID {pk.PID:X8} OT {pk.OriginalTrainerName}/{Names.TrainerId(pk)}: " +
        $"{Where(from)}{(to is { } t ? " -> " + Where(t) : "")}" +
        (from.InGame || to?.InGame == true ? $" [{Path.GetFileName(Save?.Path)}]" : "");
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
