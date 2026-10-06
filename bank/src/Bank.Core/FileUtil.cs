using System.Security.Cryptography;
using PKHeX.Core;

namespace Rocknixds.Bank;

internal static class FileUtil
{
    /// <summary>
    /// Writes <paramref name="data"/> to a temporary file next to <paramref name="path"/>, flushes it to the card and
    /// renames it over <paramref name="path"/>: a crash or a pulled battery leaves the old file or the new one, never half
    /// of each.
    /// </summary>
    public static void WriteAtomic(string path, ReadOnlySpan<byte> data)
    {
        var dir = Path.GetDirectoryName(Path.GetFullPath(path))!;
        Directory.CreateDirectory(dir);
        var tmp = Path.Combine(dir, "." + Path.GetFileName(path) + ".rnbank-tmp");
        using (var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            fs.Write(data);
            fs.Flush(true);
        }
        File.Move(tmp, path, true);
    }

    public static string Sanitize(string name)
    {
        var bad = Path.GetInvalidFileNameChars();
        // names can come from a traded Pokémon's nickname: no path separators, no control characters
        var chars = name.Select(c => bad.Contains(c) || char.IsControl(c) || c is '/' or '\\' or ':' or '*' or '?' or '"' or '<' or '>' or '|' ? '_' : c).ToArray();
        var s = new string(chars).Trim().TrimEnd('.');
        return s.Length == 0 ? "_" : s;
    }
}

/// <summary>Reading and writing single Pokémon files (.pk1 ... .pk9), as PKHeX does.</summary>
public static class PkmIO
{
    /// <summary>The bytes of the Pokémon's file: decrypted, with party stats (PKHeX's .pkX export).</summary>
    public static byte[] ToFileBytes(PKM pk)
    {
        var copy = pk.Clone();
        copy.ForcePartyData(); // a box Pokémon has no stats stored yet: write the ones it would have, as PKHeX does
        var data = new byte[copy.SIZE_PARTY];
        copy.WriteDecryptedDataParty(data);
        return data;
    }

    /// <summary>Reads a Pokémon file's bytes. <paramref name="extension"/> (".pk4") picks the format where sizes collide.</summary>
    public static PKM? FromFileBytes(byte[] data, string? extension = null)
    {
        if (!EntityDetection.IsSizePlausible(data.Length))
            return null;
        var prefer = extension is null ? EntityContext.None : EntityFileExtension.GetContextFromExtension(extension.TrimStart('.'));
        var pk = EntityFormat.GetFromBytes(data, prefer);
        if (pk is null || pk.Species == 0 || !pk.Valid)
            return null;
        return pk;
    }

    /// <summary>Identifies one Pokémon's exact data, for trades and caches: SHA-256 of its stored (box) bytes.</summary>
    public static string Hash(PKM pk)
    {
        var data = new byte[pk.SIZE_STORED];
        pk.Clone().WriteDecryptedDataStored(data);
        return Convert.ToHexString(SHA256.HashData(data));
    }

    public static string Extension(PKM pk) => "." + pk.Extension;
}
