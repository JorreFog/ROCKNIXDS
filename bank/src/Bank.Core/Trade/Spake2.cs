using System.Numerics;
using System.Security.Cryptography;
using System.Text;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// SPAKE2 (Abdalla-Pointcheval) over the 2048-bit MODP group of RFC 3526: both handhelds turn the short share code into
/// the same session key, and someone listening on the network learns nothing they could test codes against offline. A
/// wrong guess costs a connection attempt, and the host stops taking them after a few.
/// </summary>
/// <remarks>
/// p is a safe prime (p = 2q + 1, p ≡ 7 mod 8), so 2 generates the subgroup of quadratic residues, of prime order q.
/// M and N are fixed members of that subgroup made by hashing two labels and squaring, so nobody knows their discrete
/// logarithms. Exponents are 512 bits: short exponents, as is usual for Diffie-Hellman in safe-prime groups.
/// </remarks>
public sealed class Spake2
{
    private const string Label = "rocknixds-bank spake2 v1";

    private static readonly BigInteger P = BigInteger.Parse(
        "0FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD" +
        "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F" +
        "24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED52907" +
        "7096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF69558" +
        "17183995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF",
        System.Globalization.NumberStyles.HexNumber);

    private static readonly BigInteger Q = (P - 1) / 2;
    private static readonly BigInteger G = 2;
    private static readonly BigInteger M = HashToGroup("M");
    private static readonly BigInteger N = HashToGroup("N");

    public const int ElementBytes = 256;

    private readonly bool _isClient;
    private readonly BigInteger _w;
    private readonly BigInteger _secret;

    /// <summary>The message to send: T (client) or S (server), 256 bytes big-endian.</summary>
    public byte[] Message { get; }

    public Spake2(bool isClient, string code)
    {
        _isClient = isClient;
        _w = PasswordScalar(code);
        _secret = RandomScalar();
        var blind = BigInteger.ModPow(isClient ? M : N, _w, P);
        var element = BigInteger.ModPow(G, _secret, P) * blind % P;
        Message = ToBytes(element);
    }

    /// <summary>
    /// Combines the peer's message with ours. Both sides get the same 32-byte secret only if both used the same code.
    /// Throws <see cref="CryptographicException"/> for a message that isn't a member of the group.
    /// </summary>
    public byte[] Finish(byte[] peerMessage)
    {
        var peer = FromBytes(peerMessage);
        if (peer <= 1 || peer >= P - 1 || BigInteger.ModPow(peer, Q, P) != 1)
            throw new CryptographicException("the partner's key exchange message is not valid");
        // remove the peer's blinding: N^w from the server's S, M^w from the client's T (an element of order q: x^-w = x^(q-w))
        var unblind = BigInteger.ModPow(_isClient ? N : M, Q - (_w % Q), P);
        var shared = BigInteger.ModPow(peer * unblind % P, _secret, P);

        var t = _isClient ? Message : peerMessage;
        var s = _isClient ? peerMessage : Message;
        using var h = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        Append(h, Encoding.UTF8.GetBytes(Label));
        Append(h, t);
        Append(h, s);
        Append(h, ToBytes(shared));
        Append(h, ToBytes(_w));
        return h.GetHashAndReset();
    }

    private static void Append(IncrementalHash h, byte[] data)
    {
        Span<byte> len = stackalloc byte[4];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(len, data.Length);
        h.AppendData(len);
        h.AppendData(data);
    }

    /// <summary>The code as an exponent: 512 bits of SHA-256 over the label and the normalised code.</summary>
    private static BigInteger PasswordScalar(string code)
    {
        var norm = ShareCode.Normalize(code);
        var bytes = Expand($"{Label} password {norm}", 64);
        return new BigInteger(bytes, isUnsigned: true, isBigEndian: true) % Q;
    }

    private static BigInteger RandomScalar()
    {
        Span<byte> b = stackalloc byte[64];
        BigInteger x;
        do
        {
            RandomNumberGenerator.Fill(b);
            x = new BigInteger(b, isUnsigned: true, isBigEndian: true);
        } while (x.IsZero);
        return x;
    }

    private static BigInteger HashToGroup(string name)
    {
        var bytes = Expand($"{Label} element {name}", ElementBytes + 32);
        var x = new BigInteger(bytes, isUnsigned: true, isBigEndian: true) % P;
        return x * x % P; // a square: in the subgroup of order q
    }

    /// <summary>SHA-256 in counter mode, <paramref name="length"/> bytes.</summary>
    private static byte[] Expand(string seed, int length)
    {
        var output = new byte[length];
        var input = Encoding.UTF8.GetBytes(seed);
        var block = new byte[input.Length + 4];
        input.CopyTo(block, 0);
        for (int i = 0, pos = 0; pos < length; i++, pos += 32)
        {
            System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(block.AsSpan(input.Length), i);
            var hash = SHA256.HashData(block);
            hash.AsSpan(0, Math.Min(32, length - pos)).CopyTo(output.AsSpan(pos));
        }
        return output;
    }

    private static byte[] ToBytes(BigInteger x)
    {
        var raw = x.ToByteArray(isUnsigned: true, isBigEndian: true);
        if (raw.Length > ElementBytes)
            throw new CryptographicException("element too large");
        var padded = new byte[ElementBytes];
        raw.CopyTo(padded, ElementBytes - raw.Length);
        return padded;
    }

    private static BigInteger FromBytes(byte[] data)
    {
        if (data.Length != ElementBytes)
            throw new CryptographicException("the partner's key exchange message has the wrong size");
        return new BigInteger(data, isUnsigned: true, isBigEndian: true);
    }
}

/// <summary>
/// The short code a host shows and a partner types: 6 characters from 29 that can't be mixed up, on paper or in the
/// app's pixel font (no 0/O, 1/I; B, S and Z look like G, 5 and 2 there). About 29 bits: plenty, since SPAKE2 allows
/// one guess per connection and the host closes after a few wrong ones.
/// </summary>
public static class ShareCode
{
    public const string Alphabet = "ACDEFGHJKLMNPQRTUVWXY23456789";
    public const int Length = 6;

    public static string New()
    {
        Span<char> c = stackalloc char[Length];
        for (int i = 0; i < Length; i++)
            c[i] = Alphabet[RandomNumberGenerator.GetInt32(Alphabet.Length)];
        return new string(c);
    }

    /// <summary>Upper case, without the spaces and dashes people type.</summary>
    public static string Normalize(string code)
    {
        var sb = new StringBuilder(code.Length);
        foreach (var c in code.ToUpperInvariant())
        {
            if (char.IsAsciiLetterOrDigit(c))
                sb.Append(c);
        }
        return sb.ToString();
    }

    public static bool IsComplete(string code) => Normalize(code).Length == Length && Normalize(code).All(Alphabet.Contains);

    /// <summary>"K7P2QX" as "K7P-2QX".</summary>
    public static string Pretty(string code)
    {
        var n = Normalize(code);
        return n.Length == Length ? $"{n[..3]}-{n[3..]}" : n;
    }
}
