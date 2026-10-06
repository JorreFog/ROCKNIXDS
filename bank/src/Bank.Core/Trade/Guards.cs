using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
using PKHeX.Core;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// Text that comes from the other handheld (its name, its messages) is shown on screen and written to history.log:
/// one line, printable, no direction overrides or invisible characters, and short.
/// </summary>
public static class TextGuard
{
    public static string Clean(string? text, int max)
    {
        if (string.IsNullOrEmpty(text))
            return "";
        var sb = new StringBuilder(Math.Min(text.Length, max));
        foreach (var rune in text.EnumerateRunes())
        {
            if (sb.Length >= max)
                break;
            var cat = Rune.GetUnicodeCategory(rune);
            if (cat is UnicodeCategory.Control or UnicodeCategory.Format or UnicodeCategory.LineSeparator
                or UnicodeCategory.ParagraphSeparator or UnicodeCategory.PrivateUse or UnicodeCategory.Surrogate
                or UnicodeCategory.OtherNotAssigned)
            {
                sb.Append(' '); // newlines, tabs, bidi overrides (U+202E...), zero-width joiners and the like
                continue;
            }
            if (sb.Length + rune.Utf16SequenceLength > max)
                break;
            sb.Append(rune.ToString());
        }
        var s = string.Join(' ', sb.ToString().Split(' ', StringSplitOptions.RemoveEmptyEntries));
        return s;
    }
}

/// <summary>Who may connect to a trade room.</summary>
public static class AddressGuard
{
    /// <summary>
    /// Addresses of the local network: private IPv4 (10/8, 172.16/12, 192.168/16), shared address space (100.64/10, where
    /// Tailscale and carrier NAT live), link-local, loopback; IPv6 unique-local, link-local and loopback. A room is meant
    /// for the handheld next to you: a connection from the internet (a public IPv6 address, a forwarded port) is refused
    /// unless the settings allow any address.
    /// </summary>
    public static bool IsLocal(IPAddress a)
    {
        if (a.IsIPv4MappedToIPv6)
            a = a.MapToIPv4();
        if (IPAddress.IsLoopback(a))
            return true;
        var b = a.GetAddressBytes();
        if (a.AddressFamily == AddressFamily.InterNetwork)
        {
            return b[0] == 10
                   || (b[0] == 172 && (b[1] & 0xF0) == 16)
                   || (b[0] == 192 && b[1] == 168)
                   || (b[0] == 169 && b[1] == 254)
                   || (b[0] == 100 && (b[1] & 0xC0) == 64);
        }
        if (a.AddressFamily == AddressFamily.InterNetworkV6)
            return a.IsIPv6LinkLocal || (b[0] & 0xFE) == 0xFC;
        return false;
    }
}

/// <summary>Checks on a Pokémon that came over the network, before it is shown, checked or kept.</summary>
public static class PokemonGuard
{
    /// <summary>
    /// Structurally sound: its checksum matches, a species and form that exist in its format, a level from 1 to 100, and
    /// the data reads back the same. This is not the legality check (that comes after, and the player decides what to do
    /// with its verdict); it keeps out data no game ever wrote, such as glitch Pokémon that can corrupt an old game's save.
    /// </summary>
    public static string? Problem(PKM pk)
    {
        if (!pk.ChecksumValid)
            return "its data is damaged (bad checksum)";
        if (pk.Species == 0 || pk.Species > pk.MaxSpeciesID)
            return "its species doesn't exist in its format";
        if (!pk.PersonalInfo.IsFormWithinRange(pk.Form) && !FormInfo.IsValidOutOfBoundsForm(pk.Species, pk.Form, pk.Format))
            return "its form doesn't exist";
        var level = pk.CurrentLevel;
        if (level is < 1 or > 100)
            return "its level is out of range";
        for (int i = 0; i < 4; i++)
        {
            if (pk.GetMove(i) > pk.MaxMoveID)
                return "it knows a move that doesn't exist in its format";
        }
        if (pk.HeldItem > pk.MaxItemID)
            return "it holds an item that doesn't exist in its format";
        return null;
    }
}
