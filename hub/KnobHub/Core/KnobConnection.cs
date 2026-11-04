using System.IO;
using System.IO.Ports;
using System.Net.Sockets;
using System.Text;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace KnobHub.Core;

/// <summary>A link to a real knob: one JSON message per line each way. Events arrive on the UI thread.</summary>
internal interface IKnobConnection : IDisposable
{
    /// <summary>How it's connected, for the status line: "USB", "Wi-Fi", "the simulator".</summary>
    string Kind { get; }
    bool IsConnected { get; }
    /// <summary>While paused it lets go of the knob and doesn't look for it (another link has priority).</summary>
    bool Paused { get; set; }
    event Action<bool>? ConnectedChanged;
    event Action<string>? LineReceived;
    void Start();
    void Send(string line);
}

/// <summary>
/// A link over a stream (a TCP socket, a serial port), opened again and again in a loop. It counts as connected once
/// the knob has said hello: lines before that (an ESP32's boot messages) are skipped, and a stream that doesn't say
/// hello within a few seconds is closed (<see cref="NoHello"/>).
/// </summary>
internal abstract class LineConnection : IKnobConnection
{
    private static readonly TimeSpan HelloWithin = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan RetryEvery = TimeSpan.FromSeconds(2);

    private readonly SynchronizationContext _ui;
    private readonly CancellationTokenSource _stop = new();
    private readonly object _writeLock = new();
    private Stream? _writer;
    private CancellationTokenSource? _current;  // the open stream's: cancelled to let go of it
    private volatile bool _paused;

    protected LineConnection()
    {
        _ui = SynchronizationContext.Current ?? throw new InvalidOperationException("Create on the UI thread.");
    }

    public abstract string Kind { get; }
    public bool IsConnected { get; private set; }
    public event Action<bool>? ConnectedChanged;
    public event Action<string>? LineReceived;

    public bool Paused
    {
        get => _paused;
        set
        {
            _paused = value;
            if (value) _current?.Cancel();
        }
    }

    public void Start() => Task.Run(() => RunAsync(_stop.Token));

    /// <summary>Opens the stream to a knob, or returns null when there's none to try now. Disposing the owner closes
    /// the stream.</summary>
    protected abstract Task<(Stream Stream, IDisposable Owner)?> OpenAsync(CancellationToken stop);

    /// <summary>The stream opened by the last <see cref="OpenAsync"/> didn't say hello in time.</summary>
    protected virtual void NoHello() { }

    /// <summary>The stream opened by the last <see cref="OpenAsync"/> said hello.</summary>
    protected virtual void SaidHello() { }

    /// <summary>A message sent as soon as the stream is open and every <see cref="BeatEvery"/> after, or null for none:
    /// how a knob on a link that can't tell when it's opened (USB) knows Knob Hub is there. <paramref name="session"/>
    /// is new each time the stream is opened.</summary>
    protected virtual string? Heartbeat(string session) => null;

    /// <summary>The heartbeat a real knob gets (over USB and Wi-Fi): the session, and the time for its clock (Unix
    /// seconds, and the offset from UTC in minutes).</summary>
    protected static string HubMessage(string session)
    {
        var now = DateTimeOffset.Now;
        return new JsonObject
        {
            ["t"] = "hub",
            ["session"] = session,
            ["time"] = now.ToUnixTimeSeconds(),
            ["offset"] = (int)now.Offset.TotalMinutes,
        }.ToJsonString();
    }

    private static readonly TimeSpan BeatEvery = TimeSpan.FromSeconds(2);

    public void Send(string line) => Write(_writer, line);

    private void Write(Stream? stream, string line)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
        lock (_writeLock)
        {
            try { stream?.Write(bytes); stream?.Flush(); }
            catch (Exception ex) when (ex is IOException or ObjectDisposedException or InvalidOperationException or TimeoutException) { }
        }
    }

    private async Task BeatAsync(Stream stream, string session, CancellationToken stop)
    {
        try
        {
            while (!stop.IsCancellationRequested && Heartbeat(session) is { } line)
            {
                Write(stream, line);
                await Task.Delay(BeatEvery, stop);
            }
        }
        catch (OperationCanceledException) { }
    }

    private async Task RunAsync(CancellationToken stop)
    {
        while (!stop.IsCancellationRequested)
        {
            if (!_paused)
            {
                try { await UseAsync(stop); }
                catch (OperationCanceledException) when (stop.IsCancellationRequested) { return; }
                catch (Exception) { }  // gone (unplugged, closed, let go of): look again shortly
            }
            try { await Task.Delay(RetryEvery, stop); } catch (OperationCanceledException) { return; }
        }
    }

    private async Task UseAsync(CancellationToken stop)
    {
        if (await OpenAsync(stop) is not { } opened) return;
        var (stream, owner) = opened;
        using var current = CancellationTokenSource.CreateLinkedTokenSource(stop);
        _current = current;
        // Reads may not watch the token (a serial port's don't), so letting go closes the stream under them.
        using var closeOnCancel = current.Token.Register(owner.Dispose);
        try
        {
            using (owner)
            {
                if (_paused) return;
                _ = BeatAsync(stream, Guid.NewGuid().ToString("N")[..8], current.Token);
                var reader = new StreamReader(stream, new UTF8Encoding(false));
                string? hello = await WaitForHelloAsync(reader, current);
                if (hello == null)
                {
                    if (!_paused && !stop.IsCancellationRequested) NoHello();
                    return;
                }
                SaidHello();
                lock (_writeLock) _writer = stream;
                Post(() => SetConnected(true));
                Post(() => LineReceived?.Invoke(hello));
                while (await reader.ReadLineAsync(current.Token) is { } line)
                {
                    if (line.Length > 0) Post(() => LineReceived?.Invoke(line));
                }
            }
        }
        finally
        {
            lock (_writeLock) _writer = null;
            _current = null;
            Post(() => SetConnected(false));
        }
    }

    /// <summary>The first line that's a hello message, skipping anything else; null if none comes in time or the
    /// stream ends.</summary>
    private static async Task<string?> WaitForHelloAsync(StreamReader reader, CancellationTokenSource current)
    {
        using var timeout = new CancellationTokenSource(HelloWithin);
        using var closeOnTimeout = timeout.Token.Register(current.Cancel);
        try
        {
            while (await reader.ReadLineAsync(current.Token) is { } line)
            {
                if (IsHello(line)) return line;
            }
        }
        catch (Exception) when (timeout.IsCancellationRequested) { }  // nothing in time: the stream was closed
        return null;
    }

    private static bool IsHello(string line)
    {
        if (!line.StartsWith('{')) return false;
        try { return JsonNode.Parse(line) is JsonObject msg && msg["t"]?.GetValue<string>() == "hello"; }
        catch (Exception) { return false; }
    }

    private void SetConnected(bool connected)
    {
        if (IsConnected == connected) return;
        IsConnected = connected;
        ConnectedChanged?.Invoke(connected);
    }

    protected void Post(Action action) => _ui.Post(_ => action(), null);

    public void Dispose()
    {
        _stop.Cancel();
        _current?.Cancel();
    }
}

/// <summary>The Knob OS simulator (knob-os, pio run -e sim) standing in for the knob: it listens on 127.0.0.1:5718
/// and Knob Hub keeps trying to connect.</summary>
internal sealed class SimulatorConnection : LineConnection
{
    private const int Port = 5718;

    public override string Kind => "the simulator";

    protected override async Task<(Stream Stream, IDisposable Owner)?> OpenAsync(CancellationToken stop)
    {
        var client = new TcpClient { NoDelay = true };
        try
        {
            await client.ConnectAsync("127.0.0.1", Port, stop);
            return (client.GetStream(), client);
        }
        catch (Exception)
        {
            client.Dispose();
            return null;  // not running
        }
    }
}

/// <summary>
/// A knob plugged in over USB: the ESP32-S3's own USB (Espressif's vendor id 303A), which Windows shows as a COM
/// port. Ports are found by that id in the registry, so other serial devices are never opened. The port is opened
/// with RTS off (RTS can reset the ESP32-S3). Knob OS can't see the port being opened, so Knob Hub sends
/// it a "hub" message every 2 seconds, with the time for its clock; the knob counts the link as up while they come
/// (see knob-os/src/hal/esp32/main.cpp). A port that doesn't say hello is left alone for 30 seconds before it's tried
/// again: it's an ESP32 that isn't running Knob OS (yet), on which Knob Hub can install it.
/// </summary>
internal sealed class UsbConnection : LineConnection
{
    /// <summary>USB vendor ids of knobs' USB serial ports.</summary>
    private static readonly string[] Vendors = ["303A"];
    private static readonly TimeSpan RestAfterNoHello = TimeSpan.FromSeconds(30);

    private readonly Dictionary<string, DateTime> _resting = [];  // ports that didn't say hello, until when
    private readonly HashSet<string> _silent = new(StringComparer.OrdinalIgnoreCase);  // ... and haven't since
    private volatile string? _tried;

    public override string Kind => "USB";

    /// <summary>The knob's port while one is connected.</summary>
    public string? Port => IsConnected ? _tried : null;

    /// <summary>Ports there now with a knob's vendor id that didn't say hello: not running Knob OS.</summary>
    public IReadOnlyList<string> SilentPorts
    {
        get
        {
            lock (_silent)
            {
                if (_silent.Count == 0) return [];
                return KnobPorts().Where(_silent.Contains).ToList();
            }
        }
    }

    /// <summary>Tries <paramref name="port"/> again straight away (Knob OS was just installed on it).</summary>
    public void Retry(string port)
    {
        lock (_silent)
        {
            _resting.Remove(port);
            _silent.Remove(port);
        }
    }

    protected override Task<(Stream Stream, IDisposable Owner)?> OpenAsync(CancellationToken stop)
    {
        foreach (string name in KnobPorts())
        {
            lock (_silent)
            {
                if (_resting.TryGetValue(name, out var until) && until > DateTime.UtcNow) continue;
            }
            var port = new SerialPort(name, 115200)
            {
                DtrEnable = true,
                RtsEnable = false,
                Handshake = Handshake.None,
                WriteTimeout = 2000,
            };
            try
            {
                port.Open();
                _tried = name;
                return Task.FromResult<(Stream, IDisposable)?>((port.BaseStream, port));
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException)
            {
                port.Dispose();  // in use (e.g. a firmware upload) or gone
            }
        }
        return Task.FromResult<(Stream, IDisposable)?>(null);
    }

    protected override void NoHello()
    {
        if (_tried is not { } port) return;
        lock (_silent)
        {
            _resting[port] = DateTime.UtcNow + RestAfterNoHello;
            _silent.Add(port);
        }
    }

    protected override void SaidHello()
    {
        if (_tried is not { } port) return;
        lock (_silent) _silent.Remove(port);
    }

    protected override string? Heartbeat(string session) => HubMessage(session);

    /// <summary>The COM ports there now whose USB device has a knob's vendor id.</summary>
    internal static List<string> KnobPorts()
    {
        var present = new HashSet<string>(SerialPort.GetPortNames(), StringComparer.OrdinalIgnoreCase);
        var ports = new List<string>();
        if (present.Count == 0) return ports;
        try
        {
            using var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB");
            if (usb == null) return ports;
            foreach (string device in usb.GetSubKeyNames())
            {
                // e.g. VID_303A&PID_1001, or VID_303A&PID_1001&MI_00 for one interface of a composite device
                if (!Vendors.Any(v => device.StartsWith($"VID_{v}&", StringComparison.OrdinalIgnoreCase))) continue;
                using var deviceKey = usb.OpenSubKey(device);
                if (deviceKey == null) continue;
                foreach (string instance in deviceKey.GetSubKeyNames())
                {
                    using var parameters = deviceKey.OpenSubKey($@"{instance}\Device Parameters");
                    if (parameters?.GetValue("PortName") is string name && present.Contains(name) && !ports.Contains(name))
                        ports.Add(name);
                }
            }
        }
        catch (Exception ex) when (ex is System.Security.SecurityException or UnauthorizedAccessException or IOException) { }
        return ports;
    }
}

/// <summary>
/// Every way a knob can be connected, highest priority first (USB, Wi-Fi, the simulator): the knob in use is the first
/// one connected. While one is in use, the ones after it are paused, so when it goes the next one connects afresh
/// and says hello again (Knob Hub learns its board). Pausing this pauses them all (while Knob OS is installed).
/// </summary>
internal sealed class KnobLinks : IKnobConnection
{
    private readonly IKnobConnection[] _links;
    private IKnobConnection? _active;

    public KnobLinks(params IKnobConnection[] links)
    {
        _links = links;
        foreach (var link in links)
        {
            link.ConnectedChanged += _ => Update();
            link.LineReceived += line =>
            {
                if (link == _active) LineReceived?.Invoke(line);
            };
        }
    }

    public string Kind => _active?.Kind ?? "";
    public bool IsConnected => _active != null;
    public bool Paused
    {
        get => _paused;
        set
        {
            _paused = value;
            Update();
        }
    }
    private bool _paused;

    public event Action<bool>? ConnectedChanged;
    public event Action<string>? LineReceived;

    public void Start()
    {
        foreach (var link in _links) link.Start();
    }

    public void Send(string line) => _active?.Send(line);

    private void Update()
    {
        var best = _links.FirstOrDefault(l => l.IsConnected);
        int index = best == null ? _links.Length : Array.IndexOf(_links, best);
        for (int i = 0; i < _links.Length; i++) _links[i].Paused = _paused || i > index;
        if (best == _active) return;
        if (_active != null)
        {
            _active = null;
            ConnectedChanged?.Invoke(false);
        }
        if (best != null)
        {
            _active = best;
            ConnectedChanged?.Invoke(true);
        }
    }

    public void Dispose()
    {
        foreach (var link in _links) link.Dispose();
    }
}
