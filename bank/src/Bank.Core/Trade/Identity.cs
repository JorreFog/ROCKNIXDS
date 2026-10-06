using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// This handheld's identity: an ECDSA P-256 key made on first use and kept next to the settings (readable by root only).
/// It signs the lobby listings this handheld broadcasts and every trade handshake, so a partner can tell it's the
/// same handheld as before, and nobody else can pose as it: not by copying its listing, not by sitting between two
/// handhelds.
/// </summary>
public sealed class DeviceIdentity : IDisposable
{
    private readonly ECDsa _key;

    /// <summary>The public key (SubjectPublicKeyInfo, DER).</summary>
    public byte[] PublicKey { get; }

    public string Id => Fingerprint.Short(PublicKey);

    private DeviceIdentity(ECDsa key)
    {
        _key = key;
        PublicKey = key.ExportSubjectPublicKeyInfo();
    }

    public static DeviceIdentity LoadOrCreate(string path)
    {
        if (File.Exists(path))
        {
            try
            {
                var key = ECDsa.Create();
                key.ImportPkcs8PrivateKey(File.ReadAllBytes(path), out _);
                if (key.KeySize == 256)
                    return new DeviceIdentity(key);
            }
            catch (CryptographicException)
            {
                // unreadable: keep it aside and make a new one
            }
            File.Move(path, path + ".broken", true);
        }
        var created = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        FileUtil.WriteAtomic(path, created.ExportPkcs8PrivateKey());
        if (!OperatingSystem.IsWindows())
            File.SetUnixFileMode(path, UnixFileMode.UserRead | UnixFileMode.UserWrite);
        return new DeviceIdentity(created);
    }

    /// <summary>A key that lives only in memory (the tests).</summary>
    public static DeviceIdentity Ephemeral() => new(ECDsa.Create(ECCurve.NamedCurves.nistP256));

    public byte[] Sign(byte[] data) => _key.SignData(data, HashAlgorithmName.SHA256, DSASignatureFormat.IeeeP1363FixedFieldConcatenation);

    /// <summary>Checks a signature by the P-256 key <paramref name="publicKey"/>. Anything malformed is false.</summary>
    public static bool Verify(byte[]? publicKey, byte[] data, byte[]? signature)
    {
        if (publicKey is null || signature is null || publicKey.Length is < 32 or > 200 || signature.Length != 64)
            return false;
        try
        {
            using var key = ECDsa.Create();
            key.ImportSubjectPublicKeyInfo(publicKey, out int read);
            if (read != publicKey.Length || key.KeySize != 256)
                return false;
            return key.VerifyData(data, signature, HashAlgorithmName.SHA256, DSASignatureFormat.IeeeP1363FixedFieldConcatenation);
        }
        catch (CryptographicException)
        {
            return false;
        }
    }

    public void Dispose() => _key.Dispose();
}

public static class Fingerprint
{
    /// <summary>The key's SHA-256, in full: what the trainer book remembers.</summary>
    public static string Full(byte[] publicKey) => Convert.ToHexString(SHA256.HashData(publicKey));

    /// <summary>"4F2A-9C1B": shown next to a name, to tell two handhelds apart at a glance.</summary>
    public static string Short(byte[] publicKey)
    {
        var h = Full(publicKey);
        return $"{h[..4]}-{h[4..8]}";
    }
}

/// <summary>How much a partner is known: <see cref="Known"/> after a trade with that handheld, <see cref="Impostor"/>
/// when its name is that of a known trainer but its key isn't.</summary>
public enum Trust { New, Known, Impostor }

/// <summary>
/// The handhelds this one has traded with (trainers.json in the data folder): trust on first use. A partner seen
/// before is recognised by its key, whatever name it uses; a new key that uses a known trainer's name is flagged.
/// </summary>
public sealed class TrainerBook
{
    private readonly string _path;
    private readonly Lock _lock = new();
    private Dictionary<string, KnownTrainer> _known = [];

    public TrainerBook(string path)
    {
        _path = path;
        try
        {
            if (File.Exists(path))
                _known = JsonSerializer.Deserialize(File.ReadAllText(path), IdentityJson.Default.DictionaryStringKnownTrainer) ?? [];
        }
        catch (Exception)
        {
            _known = [];
        }
    }

    public KnownTrainer? Lookup(byte[] publicKey)
    {
        lock (_lock)
            return _known.GetValueOrDefault(Fingerprint.Full(publicKey));
    }

    public Trust TrustOf(byte[] publicKey, string name)
    {
        lock (_lock)
        {
            var fp = Fingerprint.Full(publicKey);
            if (_known.TryGetValue(fp, out var k) && k.Trades > 0)
                return Trust.Known;
            var n = name.Trim();
            if (n.Length > 0 && _known.Any(kv => kv.Key != fp && kv.Value.Trades > 0 && string.Equals(kv.Value.Name, n, StringComparison.OrdinalIgnoreCase)))
                return Trust.Impostor;
            return Trust.New;
        }
    }

    /// <summary>Remembers a finished trade with this handheld (and the name it used).</summary>
    public void RecordTrade(byte[] publicKey, string name)
    {
        lock (_lock)
        {
            var fp = Fingerprint.Full(publicKey);
            var k = _known.GetValueOrDefault(fp) ?? new KnownTrainer { FirstTrade = DateTime.UtcNow };
            k.Name = TextGuard.Clean(name, 24);
            k.Trades++;
            k.LastTrade = DateTime.UtcNow;
            _known[fp] = k;
            try
            {
                FileUtil.WriteAtomic(_path, JsonSerializer.SerializeToUtf8Bytes(_known, IdentityJson.Default.DictionaryStringKnownTrainer));
            }
            catch (IOException)
            {
                // remembered for this session
            }
        }
    }

    /// <summary>One line for the screens: "Known · 3 trades", "New handheld", "Not the Jorre you traded with".</summary>
    public string Describe(byte[] publicKey, string name) => TrustOf(publicKey, name) switch
    {
        Trust.Known => $"Known · {Lookup(publicKey)!.Trades} trade{(Lookup(publicKey)!.Trades == 1 ? "" : "s")}",
        Trust.Impostor => $"Not the {TextGuard.Clean(name, 24)} you traded with before!",
        _ => "New handheld",
    };
}

public sealed class KnownTrainer
{
    public string Name { get; set; } = "";
    public int Trades { get; set; }
    public DateTime FirstTrade { get; set; }
    public DateTime LastTrade { get; set; }
}

[JsonSourceGenerationOptions(WriteIndented = true, PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase)]
[JsonSerializable(typeof(Dictionary<string, KnownTrainer>))]
internal sealed partial class IdentityJson : JsonSerializerContext;
