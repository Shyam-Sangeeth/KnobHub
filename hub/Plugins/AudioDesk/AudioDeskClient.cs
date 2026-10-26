using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace AudioDesk.KnobApp;

/// <summary>AudioDesk's channels as its local API reports them (AudioDesk's LocalApi.cs).</summary>
internal sealed record AudioDeskState(
    bool SonarAvailable,
    bool Sonar,
    string? Profile,
    List<ChannelState> Channels)
{
    public ChannelState? Channel(string id) => Channels.FirstOrDefault(c => c.Id == id);
}

internal sealed record ChannelState(
    string Id,
    double Volume,
    bool Muted,
    bool SonarAvailable,
    bool ThroughSonar,
    bool Switching,
    string? Device,
    List<DeviceState> Devices)
{
    public string DeviceName => Devices.FirstOrDefault(d => d.Id == Device)?.Name ?? "No device";
}

internal sealed record DeviceState(string Id, string Name, string Format);

/// <summary>
/// Talks to AudioDesk over its named pipe ("AudioDesk.Api", one JSON object per line), reconnecting while AudioDesk
/// isn't running. Events come from a background thread.
/// </summary>
internal sealed class AudioDeskClient : IDisposable
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true,
    };

    private readonly CancellationTokenSource _stop = new();
    private readonly object _writeLock = new();
    private StreamWriter? _writer;

    public AudioDeskState? State { get; private set; }
    public bool Connected => State != null;

    /// <summary>A new state arrived (or the connection went: <see cref="State"/> is null).</summary>
    public event Action? Changed;
    public event Action<string>? Error;

    public void Start() => Task.Run(() => RunAsync(_stop.Token));

    public void SetVolume(string channel, double value) => Send(new JsonObject { ["t"] = "volume", ["channel"] = channel, ["value"] = Math.Round(value, 3) });
    public void SetMuted(string channel, bool muted) => Send(new JsonObject { ["t"] = "mute", ["channel"] = channel, ["muted"] = muted });
    public void SetDevice(string channel, string id) => Send(new JsonObject { ["t"] = "device", ["channel"] = channel, ["id"] = id });
    public void SetSonar(bool on) => Send(new JsonObject { ["t"] = "sonar", ["on"] = on });
    public void SetChannelSonar(string channel, bool on) => Send(new JsonObject { ["t"] = "channel_sonar", ["channel"] = channel, ["on"] = on });
    public void SetProfile(string profile) => Send(new JsonObject { ["t"] = "profile", ["profile"] = profile });

    private void Send(JsonObject msg)
    {
        lock (_writeLock)
        {
            try { _writer?.WriteLine(msg.ToJsonString()); }
            catch (IOException) { }
            catch (ObjectDisposedException) { }
        }
    }

    private async Task RunAsync(CancellationToken stop)
    {
        while (!stop.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(".", "AudioDesk.Api", PipeDirection.InOut, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(2000, stop);
                using var reader = new StreamReader(pipe, new UTF8Encoding(false));
                lock (_writeLock) _writer = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
                while (await reader.ReadLineAsync(stop) is { } line) Receive(line);
            }
            catch (OperationCanceledException) when (stop.IsCancellationRequested) { return; }
            catch (Exception) { }  // AudioDesk isn't running (connect timed out) or it closed
            lock (_writeLock) _writer = null;
            if (State != null)
            {
                State = null;
                Changed?.Invoke();
            }
            try { await Task.Delay(2000, stop); } catch (OperationCanceledException) { return; }
        }
    }

    private void Receive(string line)
    {
        try
        {
            if (JsonNode.Parse(line) is not JsonObject msg) return;
            switch (msg["t"]?.GetValue<string>())
            {
                case "state":
                    State = msg.Deserialize<AudioDeskState>(Options);
                    Changed?.Invoke();
                    break;
                case "error":
                    Error?.Invoke(msg["text"]?.GetValue<string>() ?? "AudioDesk couldn't do that");
                    break;
            }
        }
        catch (JsonException) { }
    }

    public void Dispose() => _stop.Cancel();
}
