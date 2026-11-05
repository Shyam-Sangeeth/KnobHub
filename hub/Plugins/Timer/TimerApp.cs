using KnobHub.Sdk;

namespace Timer.KnobApp;

/// <summary>
/// A countdown timer. Turn the knob to set it, press to start; the ring empties as the time runs down and the knob's
/// screen stays on until it's done. Press to pause (the screen may sleep then) and again to carry on, or turn to set
/// it afresh. It keeps running when you leave the app, and "Time's up" shows over whatever the knob shows at the end.
/// </summary>
public sealed class TimerApp : KnobHub.Sdk.KnobApp
{
    private const string Screen = "timer";
    private const string Accent = "#F59E0B";

    /// <summary>The times a click can set, in minutes: fine steps for short times, bigger ones for long ones.</summary>
    private static readonly int[] Times =
    [
        .. Enumerable.Range(1, 15),                    // 1 to 15 minutes, a minute a click
        .. Enumerable.Range(4, 9).Select(i => i * 5),  // 20 to 60, five minutes a click
        .. Enumerable.Range(5, 8).Select(i => i * 15), // 75 to 180, a quarter of an hour a click
    ];

    private readonly object _lock = new();  // Input runs on Knob Hub's UI thread, Tick on a timer thread
    private int _time = Array.IndexOf(Times, 5);  // the set time, an index into Times
    private DateTime? _endsAt;      // while it runs
    private TimeSpan? _pausedLeft;  // while it's paused
    private System.Threading.Timer? _tick;
    private string _shown = "";     // what the screen last showed, so a tick only updates it when that changes

    private TimeSpan SetTime => TimeSpan.FromMinutes(Times[_time]);

    public override void Open()
    {
        lock (_lock) _shown = "";
        Host.Show(Screen, Describe());
    }

    public override void Input(KnobAppInput input)
    {
        switch (input.Kind)
        {
            case InputKind.Back:
                Host.Close();  // back to Home; a running timer keeps going
                return;
            case InputKind.Rotate:
                lock (_lock)
                {
                    if (_endsAt != null) return;  // not while it runs
                    _pausedLeft = null;           // turning a paused timer sets it afresh
                    _time = Math.Clamp(_time + input.Delta, 0, Times.Length - 1);
                }
                break;
            case InputKind.Press:
                lock (_lock)
                {
                    if (_endsAt is { } end)  // running: pause
                    {
                        if (end <= DateTime.UtcNow) return;  // it ran out just now: the next tick says so
                        _pausedLeft = end - DateTime.UtcNow;
                        StopTicking();
                    }
                    else  // set or paused: (carry on) counting down
                    {
                        _endsAt = DateTime.UtcNow + (_pausedLeft ?? SetTime);
                        _pausedLeft = null;
                        _tick = new System.Threading.Timer(_ => Tick(), null, 250, 250);
                    }
                }
                Host.KeepAwake(Running);
                break;
            default:
                return;
        }
        Update();
    }

    // Knob Hub is turning the app off or exiting: nothing may keep running.
    public override void Stop()
    {
        lock (_lock)
        {
            StopTicking();
            _pausedLeft = null;
        }
    }

    private bool Running
    {
        get { lock (_lock) return _endsAt != null; }
    }

    private void Tick()
    {
        // This runs on a timer thread, where an exception would end Knob Hub: catch everything.
        try
        {
            bool done;
            lock (_lock)
            {
                done = _endsAt != null && DateTime.UtcNow >= _endsAt;
                if (done) StopTicking();
            }
            if (done)
            {
                Host.KeepAwake(false);
                Host.Toast("Time's up");
            }
            Update();
        }
        catch (Exception ex)
        {
            Host.Log("The timer failed: " + ex.Message);
        }
    }

    private void StopTicking()
    {
        _tick?.Dispose();
        _tick = null;
        _endsAt = null;
    }

    /// <summary>Sends the screen when what it shows has changed (ignored while the app isn't open).</summary>
    private void Update()
    {
        var screen = Describe();
        lock (_lock)
        {
            string shown = $"{screen.Text}|{screen.Value}|{screen.Label}";
            if (shown == _shown) return;
            _shown = shown;
        }
        Host.Update(Screen, screen);
    }

    private RingScreen Describe()
    {
        lock (_lock)
        {
            var left = _endsAt is { } end ? end - DateTime.UtcNow : _pausedLeft ?? SetTime;
            int seconds = Math.Max(0, (int)Math.Ceiling(left.TotalSeconds));  // 5:00 at the start, not 4:59
            return new RingScreen
            {
                Label = _endsAt != null ? "Press to pause" : _pausedLeft != null ? "Paused" : "Press to start",
                Text = seconds >= 3600 ? $"{seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}" : $"{seconds / 60}:{seconds % 60:00}",
                // The ring empties as the time runs out.
                Value = (int)Math.Ceiling(seconds * 100.0 / SetTime.TotalSeconds),
                Color = Accent,
            };
        }
    }
}
