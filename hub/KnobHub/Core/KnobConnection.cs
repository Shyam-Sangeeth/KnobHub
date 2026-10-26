using System.IO;
using System.Net.Sockets;
using System.Text;

namespace KnobHub.Core;

/// <summary>A link to a real knob: one JSON message per line each way. Events arrive on the UI thread.</summary>
internal interface IKnobConnection : IDisposable
{
    /// <summary>How it's connected, for the status line: "USB", "Wi-Fi", "the simulator".</summary>
    string Kind { get; }
    bool IsConnected { get; }
    event Action<bool>? ConnectedChanged;
    event Action<string>? LineReceived;
    void Start();
    void Send(string line);
}

/// <summary>The Knob OS simulator (knob-os, pio run -e sim) standing in for the knob: it listens on 127.0.0.1:5718
/// and Knob Hub keeps trying to connect.</summary>
internal sealed class SimulatorConnection : IKnobConnection
{
    private const int Port = 5718;

    private readonly SynchronizationContext _ui;
    private readonly CancellationTokenSource _stop = new();
    private readonly object _writeLock = new();
    private StreamWriter? _writer;

    public SimulatorConnection()
    {
        _ui = SynchronizationContext.Current ?? throw new InvalidOperationException("Create on the UI thread.");
    }

    public string Kind => "the simulator";
    public bool IsConnected { get; private set; }
    public event Action<bool>? ConnectedChanged;
    public event Action<string>? LineReceived;

    public void Start() => Task.Run(() => RunAsync(_stop.Token));

    public void Send(string line)
    {
        lock (_writeLock)
        {
            try { _writer?.Write(line + "\n"); _writer?.Flush(); }
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
                using var client = new TcpClient { NoDelay = true };
                await client.ConnectAsync("127.0.0.1", Port, stop);
                using var stream = client.GetStream();
                using var reader = new StreamReader(stream, new UTF8Encoding(false));
                lock (_writeLock) _writer = new StreamWriter(stream, new UTF8Encoding(false));
                Post(() => SetConnected(true));
                while (await reader.ReadLineAsync(stop) is { } line)
                {
                    if (line.Length > 0) Post(() => LineReceived?.Invoke(line));
                }
            }
            catch (OperationCanceledException) { return; }
            catch (Exception) { }  // not running, or it closed: try again shortly
            finally
            {
                lock (_writeLock) _writer = null;
                Post(() => SetConnected(false));
            }
            try { await Task.Delay(2000, stop); } catch (OperationCanceledException) { return; }
        }
    }

    private void SetConnected(bool connected)
    {
        if (IsConnected == connected) return;
        IsConnected = connected;
        ConnectedChanged?.Invoke(connected);
    }

    private void Post(Action action) => _ui.Post(_ => action(), null);

    public void Dispose() => _stop.Cancel();
}
