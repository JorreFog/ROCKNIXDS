using System.Collections.Concurrent;
using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>The result of PKHeX's legality analysis for one Pokémon.</summary>
public sealed record LegalityVerdict(bool Valid, bool Analysed, IReadOnlyList<string> Problems, string Report)
{
    /// <summary>One line for a badge: "Legal" or the first problem.</summary>
    public string Headline => !Analysed ? "Not checked" : Valid ? "Legal" : Problems.Count > 0 ? Problems[0] : "Not legal";
}

/// <summary>
/// PKHeX's legality analysis (the same checks as PKHeX's own legality report: encounter, moves, ribbons, trainer data,
/// PID/IV correlation...). The first analysis loads PKHeX's encounter tables, a second or two on the handheld.
/// </summary>
public static class Legality
{
    private static readonly ConcurrentDictionary<string, LegalityVerdict> Cache = new();

    public static LegalityVerdict Check(PKM pk)
    {
        if (pk.Species == 0)
            return new LegalityVerdict(true, false, [], "");
        var key = pk.GetType().Name + PkmIO.Hash(pk);
        if (Cache.TryGetValue(key, out var v))
            return v;
        v = Analyse(pk);
        if (Cache.Count > 4000)
            Cache.Clear();
        Cache[key] = v;
        return v;
    }

    private static LegalityVerdict Analyse(PKM pk)
    {
        try
        {
            var la = new LegalityAnalysis(pk);
            var report = la.Report();
            var verbose = la.Report(true);
            var problems = la.Valid
                ? []
                : report.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                    .Where(l => l.Length > 0)
                    .ToList();
            return new LegalityVerdict(la.Valid, true, problems, verbose);
        }
        catch (Exception ex)
        {
            return new LegalityVerdict(false, false, [$"The legality check failed: {ex.Message}"], ex.ToString());
        }
    }

    /// <summary>Loads PKHeX's tables now, so the first real check is quick. Safe to call on a worker thread.</summary>
    public static void WarmUp()
    {
        try
        {
            var pk = new PK4 { Species = (ushort)PKHeX.Core.Species.Bulbasaur, CurrentLevel = 5 };
            _ = new LegalityAnalysis(pk).Valid;
        }
        catch (Exception)
        {
            // only a warm-up
        }
    }
}
