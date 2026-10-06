using PKHeX.Core;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// A trade lobby's listing, broadcast on the network with the room's announcement: the Pokémon its host trades away and
/// the species it wants for it. An open lobby also carries its code, so anyone can join with one tap; a closed one
/// needs the code from its host.
/// </summary>
/// <remarks>
/// A listing is an advertisement, nothing more: anyone on the network can broadcast one. What counts is what the host
/// actually offers once connected, which is checked like any offer (structure, legality) and against the listing.
/// An open lobby's code is public, so its encryption keeps out listeners but not other joiners: that's what "open"
/// means. Nothing changes hands until both players accept.
/// </remarks>
public sealed class LobbyListing
{
    public ushort Species { get; set; }
    public byte Form { get; set; }
    public byte Gender { get; set; }
    public bool Shiny { get; set; }
    public byte Level { get; set; }
    /// <summary>The offered Pokémon's format: 3 for a .pk3 and so on.</summary>
    public byte Format { get; set; }
    /// <summary>The species wanted for it; 0: open to any offer.</summary>
    public ushort Want { get; set; }
    /// <summary>Anyone may join; <see cref="Code"/> is then the room's code.</summary>
    public bool Open { get; set; }
    public string? Code { get; set; }

    /// <summary>The highest species number any format has (PKHeX's newest games).</summary>
    public static ushort MaxSpecies => (ushort)(PKHeX.Core.Species.MAX_COUNT - 1);

    public static LobbyListing For(PKM pk, ushort want, bool open, string code) => new()
    {
        Species = pk.Species,
        Form = pk.Form,
        Gender = pk.Gender,
        Shiny = pk.IsShiny,
        Level = pk.CurrentLevel,
        Format = pk.Format,
        Want = want,
        Open = open,
        Code = open ? code : null, // a closed lobby's code is never broadcast
    };

    /// <summary>A listing heard on the network, if it makes sense; anything out of range drops the whole listing.</summary>
    public static LobbyListing? Validate(LobbyListing? l)
    {
        if (l is null)
            return null;
        if (l.Species is 0 || l.Species > MaxSpecies || l.Want > MaxSpecies)
            return null;
        if (l.Level is < 1 or > 100 || l.Format is < 1 or > 9 || l.Gender > 2 || l.Form > 64)
            return null;
        if (l.Open && (l.Code is null || !ShareCode.IsComplete(l.Code)))
            return null;
        return new LobbyListing
        {
            Species = l.Species, Form = l.Form, Gender = l.Gender, Shiny = l.Shiny, Level = l.Level, Format = l.Format,
            Want = l.Want, Open = l.Open, Code = l.Open ? ShareCode.Normalize(l.Code!) : null,
        };
    }

    /// <summary>Whether <paramref name="pk"/> is what the lobby asks for (any Pokémon when it asks for none).</summary>
    public bool Wants(PKM pk) => Want == 0 || (!pk.IsEgg && pk.Species == Want);

    /// <summary>Whether the host's actual offer is the Pokémon the listing advertised.</summary>
    public bool Advertises(PKM pk) => pk.Species == Species && pk.IsShiny == Shiny && pk.CurrentLevel == Level && pk.Format == Format;

    public string WantName => Want == 0 ? "any Pokémon" : Names.Species(Want);
    public string OfferName => Names.Species(Species);
}
