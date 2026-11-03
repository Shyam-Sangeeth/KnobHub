using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;

namespace KnobHub.Core;

/// <summary>
/// The paired knob over Wi-Fi: Knob OS listens on port 5719 at its network name (knob-xxxx.local, found by Windows'
/// mDNS). Each side first proves it holds the key Knob Hub gave the knob over USB, then every line is sealed (see
/// <see cref="SealedStream"/> and knob-os/src/core/link.h). Nothing is tried until a knob has been paired.
/// </summary>
internal sealed class WifiConnection(Func<PairedKnob?> paired) : LineConnection
{
    private const int Port = 5719;
    private static readonly TimeSpan ConnectWithin = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan RestAfterMiss = TimeSpan.FromSeconds(10);

    private DateTime _restUntil;

    public override string Kind => "Wi-Fi";

    protected override string? Heartbeat(string session) => HubMessage(session);

    protected override async Task<(Stream Stream, IDisposable Owner)?> OpenAsync(CancellationToken stop)
    {
        if (DateTime.UtcNow < _restUntil || paired() is not { } knob || knob.Host.Length == 0) return null;
        byte[] key;
        try { key = Convert.FromBase64String(knob.Key); }
        catch (FormatException) { return null; }

        var client = new TcpClient { NoDelay = true };
        try
        {
            using var within = CancellationTokenSource.CreateLinkedTokenSource(stop);
            within.CancelAfter(ConnectWithin);
            await client.ConnectAsync(knob.Host + ".local", Port, within.Token);
            if (await SealedStream.OpenAsync(client.GetStream(), key, within.Token) is { } link) return (link, client);
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !stop.IsCancellationRequested) { }
        client.Dispose();
        _restUntil = DateTime.UtcNow + RestAfterMiss;  // not on the network now (or not the paired knob)
        return null;
    }
}

/// <summary>
/// The Wi-Fi link after the handshake. Each line written is sealed with AES-256-GCM and sent as base64; each line read
/// is opened, so <see cref="LineConnection"/> reads and writes plain lines as it does over USB. The key is new for
/// each connection, and nonces count up each way (way 1 Knob Hub to knob, 2 knob to Knob Hub), so a line can't be
/// changed, replayed or moved.
/// </summary>
internal sealed class SealedStream : Stream
{
    private const int TagSize = 16;

    private readonly Stream _inner;
    private readonly StreamReader _reader;
    private readonly AesGcm _aes;
    private ulong _sent, _received;
    private byte[] _readBuffer = [];
    private int _readAt;
    private readonly List<byte> _writing = [];

    private SealedStream(Stream inner, StreamReader reader, byte[] session)
    {
        _inner = inner;
        _reader = reader;
        _aes = new AesGcm(session, TagSize);
    }

    /// <summary>The handshake: Knob Hub's nonce, the knob's nonce and proof, Knob Hub's proof. Null if the knob
    /// can't prove it holds the key (or says it isn't paired).</summary>
    public static async Task<SealedStream?> OpenAsync(Stream inner, byte[] key, CancellationToken cancel)
    {
        var reader = new StreamReader(inner, new UTF8Encoding(false));
        byte[] hubNonce = RandomNumberGenerator.GetBytes(16);
        await WriteLineAsync(inner, new JsonObject { ["t"] = "auth", ["nonce"] = Convert.ToBase64String(hubNonce) }, cancel);
        if (JsonNode.Parse(await reader.ReadLineAsync(cancel) ?? "{}") is not JsonObject reply) return null;
        byte[] knobNonce, proof;
        try
        {
            knobNonce = Convert.FromBase64String(reply.Str("nonce"));
            proof = Convert.FromBase64String(reply.Str("proof"));
        }
        catch (FormatException) { return null; }
        if (knobNonce.Length != 16 || !CryptographicOperations.FixedTimeEquals(proof, Mac(key, "knob", hubNonce, knobNonce)))
            return null;
        string hubProof = Convert.ToBase64String(Mac(key, "hub", hubNonce, knobNonce));
        await WriteLineAsync(inner, new JsonObject { ["t"] = "auth", ["proof"] = hubProof }, cancel);
        return new SealedStream(inner, reader, Mac(key, "session", hubNonce, knobNonce));
    }

    private static byte[] Mac(byte[] key, string label, byte[] hubNonce, byte[] knobNonce) =>
        HMACSHA256.HashData(key, (byte[])[.. Encoding.ASCII.GetBytes(label), .. hubNonce, .. knobNonce]);

    private static async Task WriteLineAsync(Stream stream, JsonObject msg, CancellationToken cancel)
    {
        await stream.WriteAsync(Encoding.UTF8.GetBytes(msg.ToJsonString() + "\n"), cancel);
        await stream.FlushAsync(cancel);
    }

    private static byte[] Nonce(uint way, ulong count)
    {
        var nonce = new byte[12];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32BigEndian(nonce, way);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(nonce.AsSpan(4), count);
        return nonce;
    }

    public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancel = default)
    {
        while (_readAt >= _readBuffer.Length)
        {
            if (await _reader.ReadLineAsync(cancel) is not { } line) return 0;
            if (line.Length == 0) continue;
            byte[] sealedLine = Convert.FromBase64String(line);  // a bad line throws: the link is closed
            if (sealedLine.Length < TagSize) throw new IOException("A line from the knob was cut short.");
            int length = sealedLine.Length - TagSize;
            var plain = new byte[length + 1];
            _aes.Decrypt(Nonce(2, _received++), sealedLine.AsSpan(0, length), sealedLine.AsSpan(length), plain.AsSpan(0, length));
            plain[length] = (byte)'\n';
            _readBuffer = plain;
            _readAt = 0;
        }
        int n = Math.Min(buffer.Length, _readBuffer.Length - _readAt);
        _readBuffer.AsMemory(_readAt, n).CopyTo(buffer);
        _readAt += n;
        return n;
    }

    public override int Read(byte[] buffer, int offset, int count) =>
        ReadAsync(buffer.AsMemory(offset, count)).AsTask().GetAwaiter().GetResult();

    // Writes come a line at a time (LineConnection's), under its lock.
    public override void Write(byte[] buffer, int offset, int count)
    {
        for (int i = offset; i < offset + count; i++)
        {
            if (buffer[i] != '\n')
            {
                _writing.Add(buffer[i]);
                continue;
            }
            byte[] plain = [.. _writing];
            _writing.Clear();
            var sealedLine = new byte[plain.Length + TagSize];
            _aes.Encrypt(Nonce(1, _sent++), plain, sealedLine.AsSpan(0, plain.Length), sealedLine.AsSpan(plain.Length));
            _inner.Write(Encoding.ASCII.GetBytes(Convert.ToBase64String(sealedLine) + "\n"));
        }
    }

    public override void Flush() => _inner.Flush();

    public override bool CanRead => true;
    public override bool CanWrite => true;
    public override bool CanSeek => false;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();

    protected override void Dispose(bool disposing)
    {
        if (disposing) _aes.Dispose();
        base.Dispose(disposing);
    }
}
