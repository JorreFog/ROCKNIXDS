using PKHeX.Core;

namespace Rocknixds.Bank;

/// <summary>
/// Evolution by trade, done when a traded Pokémon arrives, as the games do: Kadabra, Machoke, Graveler, Haunter (and
/// later Boldore, Gurdurr, Phantump, Pumpkaboo), the held-item ones (Onix + Metal Coat, Seadra + Dragon Scale, Porygon +
/// Up-Grade...) and Karrablast traded for Shelmet. An Everstone stops it. Which species evolve, and how, is PKHeX's
/// evolution data for the Pokémon's own generation.
/// </summary>
public static class TradeEvolution
{
    private const ushort Everstone = 229;

    /// <summary>Evolves <paramref name="pk"/> if trading it for <paramref name="partnerSpecies"/> would. Returns the
    /// evolved species' name, or null.</summary>
    public static string? TryEvolve(PKM pk, ushort partnerSpecies)
    {
        if (pk.IsEgg || pk.Species == 0)
            return null;
        if (ModernItem(pk) == Everstone)
            return null;

        var tree = EvolutionTree.GetEvolutionTree(pk.Context);
        foreach (var m in tree.Forward.GetForward(pk.Species, pk.Form).Span)
        {
            if (!m.Method.IsTrade)
                continue;
            bool go = m.Method switch
            {
                EvolutionType.Trade => true,
                EvolutionType.TradeHeldItem => pk.HeldItem != 0 && (pk.HeldItem == m.Argument || ModernItem(pk) == m.Argument),
                EvolutionType.TradeShelmetKarrablast => partnerSpecies is (ushort)Species.Karrablast or (ushort)Species.Shelmet
                                                        && partnerSpecies != pk.Species,
                _ => false,
            };
            if (!go)
                continue;
            if (m.Species > pk.MaxSpeciesID)
                continue; // the evolution doesn't exist in this format
            Apply(pk, m);
            return Names.Species(pk.Species);
        }
        return null;
    }

    private static void Apply(PKM pk, EvolutionMethod m)
    {
        bool nicknamed = pk.IsNicknamed;
        int abilitySlot = pk.AbilityNumber >> 1;
        if (m.Method == EvolutionType.TradeHeldItem)
            pk.HeldItem = 0; // the item is used up
        pk.Form = m.GetDestinationForm(pk.Form);
        pk.Species = m.Species;
        if (!nicknamed)
            pk.ClearNickname();
        if (pk.Format >= 3)
            pk.RefreshAbility(abilitySlot);
        if (pk.PartyStatsPresent)
            pk.ResetPartyStats();
        pk.RefreshChecksum();
    }

    private static int ModernItem(PKM pk) => pk.Format switch
    {
        3 => ItemConverter.GetItemFuture3((ushort)pk.HeldItem),
        2 => ItemConverter.GetItemFuture2((byte)pk.HeldItem),
        1 => 0,
        _ => pk.HeldItem,
    };
}
