using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Rocknixds.Bank.Trade;

/// <summary>
/// A connection between two handhelds after the share code was proven: length-prefixed frames, each sealed with
/// AES-256-GCM under a key per direction and a counter nonce (so frames can't be replayed, reordered or altered).
/// </summary>
public sealed class SecureChannel : IDisposable
{
    public const int ProtocolVersion = 1;
    /// <summary>Before the code is proven a frame is a few hundred bytes of JSON: nobody gets to make us allocate more.</summary>
    private const int MaxHandshakeFrame = 4096;
    /// <summary>After it: a Pokémon file is under 400 bytes, so 64 KiB is a generous bound.</summary>
    public const int MaxFrame = 64 * 1024;
    public static TimeSpan HandshakeTimeout { get; internal set; } = TimeSpan.FromSeconds(10);

    private readonly Stream _stream;
    private readonly AesGcm _send;
    private readonly AesGcm _receive;
    private readonly byte[] _sendLabel;
    private readonly byte[] _receiveLabel;
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private ulong _sendCounter;
    private ulong _receiveCounter;
    private bool _disposed;

    public string RemoteAddress { get; }

    private SecureChannel(Stream stream, byte[] sendKey, byte[] receiveKey, bool isClient, string remote)
    {
        _stream = stream;
        _send = new AesGcm(sendKey, 16);
        _receive = new AesGcm(receiveKey, 16);
        _sendLabel = Encoding.ASCII.GetBytes(isClient ? "rnbank c2s" : "rnbank s2c");
        _receiveLabel = Encoding.ASCII.GetBytes(isClient ? "rnbank s2c" : "rnbank c2s");
        RemoteAddress = remote;
    }

    /// <summary>The joining side's handshake. Throws <see cref="WrongCodeException"/> when the codes differ.</summary>
    public static async Task<SecureChannel> ConnectAsync(Stream stream, string code, string remote, CancellationToken ct = default)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(HandshakeTimeout);
        var t = timeout.Token;

        var spake = new Spake2(isClient: true, code);
        await WriteJsonAsync(stream, new HandshakeMsg { T = "spake", App = "rocknixds-bank", Proto = ProtocolVersion, Msg = spake.Message }, t);
        var reply = await ReadJsonAsync(stream, t);
        if (reply.T == "refused")
            throw new TradeRefusedException(TextGuard.Clean(reply.Text, 160) is { Length: > 0 } why ? $"The host refused: {why}" : "The host refused the connection.");
        if (reply.T != "spake" || reply.Msg is null || reply.Confirm is null)
            throw new IOException("unexpected handshake reply");
        if (reply.Proto != ProtocolVersion)
            throw new TradeRefusedException("The other handheld runs a different version of ROCKNIXDS Bank. Update both.");

        var keys = Keys.Derive(spake.Finish(reply.Msg));
        if (!CryptographicOperations.FixedTimeEquals(reply.Confirm, Confirm(keys.ConfirmServer, "server")))
            throw new WrongCodeException();
        await WriteJsonAsync(stream, new HandshakeMsg { T = "confirm", Confirm = Confirm(keys.ConfirmClient, "client") }, t);
        return new SecureChannel(stream, keys.ClientToServer, keys.ServerToClient, isClient: true, remote);
    }

    /// <summary>
    /// The host's handshake with one incoming connection. <paramref name="reserveAttempt"/> is asked before the host
    /// reveals anything that depends on the code (its key confirmation): every connection that gets that far is one guess
    /// at the code, whether it then confirms, sends garbage, hangs up or just goes quiet. Returning false refuses it.
    /// </summary>
    public static async Task<SecureChannel> AcceptAsync(Stream stream, string code, string remote, CancellationToken ct = default,
        Func<bool>? reserveAttempt = null)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(HandshakeTimeout);
        var t = timeout.Token;

        var hello = await ReadJsonAsync(stream, t);
        if (hello.T != "spake" || hello.App != "rocknixds-bank" || hello.Msg is null)
            throw new IOException("not a ROCKNIXDS Bank client");
        if (hello.Proto != ProtocolVersion)
        {
            await WriteJsonAsync(stream, new HandshakeMsg { T = "refused", Proto = ProtocolVersion,
                Text = "The host runs a different version of ROCKNIXDS Bank. Update both." }, t);
            throw new TradeRefusedException("A partner with a different version tried to connect.");
        }
        // a well-formed message counts as an attempt from here on, valid group element or not: a real client never
        // sends a bad one, and an attacker shouldn't get free modular exponentiations
        if (reserveAttempt is not null && !reserveAttempt())
        {
            await WriteJsonAsync(stream, new HandshakeMsg { T = "refused", Proto = ProtocolVersion,
                Text = "Too many wrong codes: this trade room is closed." }, t);
            throw new TradeRefusedException("A connection was refused: the room has no attempts left.");
        }
        var spake = new Spake2(isClient: false, code);
        Keys keys;
        try
        {
            keys = Keys.Derive(spake.Finish(hello.Msg));
        }
        catch (CryptographicException)
        {
            throw new WrongCodeException();
        }
        try
        {
            await WriteJsonAsync(stream, new HandshakeMsg { T = "spake", Proto = ProtocolVersion, Msg = spake.Message,
                Confirm = Confirm(keys.ConfirmServer, "server") }, t);
            var confirm = await ReadJsonAsync(stream, t);
            if (confirm.T != "confirm" || confirm.Confirm is null ||
                !CryptographicOperations.FixedTimeEquals(confirm.Confirm, Confirm(keys.ConfirmClient, "client")))
                throw new WrongCodeException();
        }
        catch (Exception ex) when (ex is not WrongCodeException && !ct.IsCancellationRequested)
        {
            // hung up, timed out, garbage: after our confirmation went out, all of these are a failed guess
            throw new WrongCodeException();
        }
        return new SecureChannel(stream, keys.ServerToClient, keys.ClientToServer, isClient: false, remote);
    }

    public async Task SendAsync(byte[] plaintext, CancellationToken ct = default)
    {
        if (plaintext.Length > MaxFrame)
            throw new ArgumentException("message too large", nameof(plaintext));
        await _sendLock.WaitAsync(ct);
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            var counter = _sendCounter++;
            var frame = new byte[plaintext.Length + 16];
            Span<byte> nonce = stackalloc byte[12];
            BinaryPrimitives.WriteUInt64BigEndian(nonce[4..], counter);
            _send.Encrypt(nonce, plaintext, frame.AsSpan(0, plaintext.Length), frame.AsSpan(plaintext.Length), Aad(_sendLabel, counter));
            await WriteFrameAsync(_stream, frame, ct);
        }
        finally
        {
            _sendLock.Release();
        }
    }

    /// <summary>The next frame, decrypted. Throws on a frame that was tampered with or replayed.</summary>
    public async Task<byte[]> ReceiveAsync(CancellationToken ct = default)
    {
        var frame = await ReadFrameAsync(_stream, MaxFrame + 16, ct);
        if (frame.Length < 16)
            throw new CryptographicException("short frame");
        var counter = _receiveCounter++;
        var plain = new byte[frame.Length - 16];
        Span<byte> nonce = stackalloc byte[12];
        BinaryPrimitives.WriteUInt64BigEndian(nonce[4..], counter);
        _receive.Decrypt(nonce, frame.AsSpan(0, plain.Length), frame.AsSpan(plain.Length), plain, Aad(_receiveLabel, counter));
        return plain;
    }

    private static byte[] Aad(byte[] label, ulong counter)
    {
        var aad = new byte[label.Length + 8];
        label.CopyTo(aad, 0);
        BinaryPrimitives.WriteUInt64BigEndian(aad.AsSpan(label.Length), counter);
        return aad;
    }

    private static byte[] Confirm(byte[] key, string who) => HMACSHA256.HashData(key, Encoding.ASCII.GetBytes("rnbank confirm " + who));

    private static async Task WriteFrameAsync(Stream s, byte[] payload, CancellationToken ct)
    {
        var header = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(header, payload.Length);
        await s.WriteAsync(header, ct);
        await s.WriteAsync(payload, ct);
        await s.FlushAsync(ct);
    }

    private static async Task<byte[]> ReadFrameAsync(Stream s, int max, CancellationToken ct)
    {
        var header = new byte[4];
        await s.ReadExactlyAsync(header, ct);
        var len = BinaryPrimitives.ReadInt32BigEndian(header);
        if (len is < 0 || len > max)
            throw new IOException($"frame of {len} bytes");
        var payload = new byte[len];
        await s.ReadExactlyAsync(payload, ct);
        return payload;
    }

    private static Task WriteJsonAsync(Stream s, HandshakeMsg msg, CancellationToken ct) =>
        WriteFrameAsync(s, JsonSerializer.SerializeToUtf8Bytes(msg, TradeJson.Default.HandshakeMsg), ct);

    private static async Task<HandshakeMsg> ReadJsonAsync(Stream s, CancellationToken ct)
    {
        var frame = await ReadFrameAsync(s, MaxHandshakeFrame, ct);
        return JsonSerializer.Deserialize(frame, TradeJson.Default.HandshakeMsg) ?? throw new IOException("empty handshake message");
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        try { _stream.Dispose(); } catch (IOException) { /* closing anyway */ }
        _send.Dispose();
        _receive.Dispose();
    }

    private sealed record Keys(byte[] ClientToServer, byte[] ServerToClient, byte[] ConfirmClient, byte[] ConfirmServer)
    {
        public static Keys Derive(byte[] secret)
        {
            var okm = HKDF.DeriveKey(HashAlgorithmName.SHA256, secret, 128, info: Encoding.ASCII.GetBytes("rocknixds-bank session keys v1"));
            return new Keys(okm[..32], okm[32..64], okm[64..96], okm[96..]);
        }
    }
}

public sealed class WrongCodeException() : Exception("The code doesn't match. Check it and try again.");

public sealed class TradeRefusedException(string message) : Exception(message);

internal sealed class HandshakeMsg
{
    public string T { get; set; } = "";
    public string? App { get; set; }
    public int Proto { get; set; }
    public byte[]? Msg { get; set; }
    public byte[]? Confirm { get; set; }
    public string? Text { get; set; }
}

/// <summary>One message of a trade, inside the encrypted channel.</summary>
public sealed class TradeMsg
{
    /// <summary>hello, offer, withdraw, accept, unaccept, verdict, received, complete, ping, bye.</summary>
    public string T { get; set; } = "";
    public string? Name { get; set; }
    public string? App { get; set; }
    /// <summary>offer: the Pokémon's file (as <see cref="PkmIO.ToFileBytes"/>), and its file extension.</summary>
    public byte[]? Data { get; set; }
    public string? Ext { get; set; }
    /// <summary>offer, verdict: the offered Pokémon (<see cref="PkmIO.Hash"/>).</summary>
    public string? Hash { get; set; }
    /// <summary>accept, received, complete: the sender's own offer and the one it gets, by hash.</summary>
    public string? Mine { get; set; }
    public string? Theirs { get; set; }
    public bool? Valid { get; set; }
    public string? Text { get; set; }
}

[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase, DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
[JsonSerializable(typeof(HandshakeMsg))]
[JsonSerializable(typeof(TradeMsg))]
[JsonSerializable(typeof(Announcement))]
internal sealed partial class TradeJson : JsonSerializerContext;
